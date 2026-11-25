#pragma once
#include "MediaGraphics.h"
#include "MediaAVPacket.h"
#include "MediaMIDI.h"
#include "MediaTextFragment.h"
#include <array>
#include <map>

namespace mediawire {
// Stable kind identifiers: raster, raster fragment, vector, model, PCM, MIDI,
// video frame, video fragment, text, text fragment, compressed raster, PNG raster, JPEG raster, WebP raster.
inline int inspectMedia(const Bytes &wire,std::array<uint64_t,6> &out) {
    Header h;Bytes payload;
    if(!decode(wire,h,payload)||h.stream==0xffffffffu)return 0;
    int kind=0;
    if(h.type<=3){uint64_t a=0,b=0;kind=graphicsKind(wire,h,a,b);if(kind==5)kind=11;else if(kind==6)kind=12;else if(kind==7)kind=13;else if(kind==8)kind=14;}
    else if(h.type==4||h.type==6){
        std::array<uint64_t,9> m{};
        int av=inspectAV(wire,m);
        if(av==1)kind=5;else if(av==2)kind=7;else if(av==3)kind=8;
    }else if(h.type==5){Bytes msg;if(decodeMidi(wire,h,msg))kind=6;}
    else if(h.type==7){
        Bytes text;uint32_t total,offset,first;
        if(decodeText(wire,h,text))kind=9;
        else if(decodeTextFragment(wire,h,total,offset,first,text))kind=10;
    }
    if(kind)out={static_cast<uint64_t>(kind),h.type,h.stream,h.sequence,h.timestamp,payload.size()};
    return kind;
}
// Per-stream sequence and timestamp validation; independent streams may interleave.
class MediaRouter {
    struct Track {uint64_t type,next,last,count;};
    std::map<uint32_t,Track> tracks_;
    unsigned mask_=0;
public:
    bool configure(uint64_t mask){if(!mask||mask>127)return false;clear();mask_=static_cast<unsigned>(mask);return true;}
    void clear(){tracks_.clear();mask_=0;}
    bool add(uint64_t stream,uint64_t type,uint64_t seq,uint64_t origin){
        if(!mask_||!stream||stream>=0xffffffffu||type<1||type>7||
           !(mask_&(1u<<(type-1)))||seq>0xffffffffu||origin>0x7fffffffffffffffull||tracks_.size()>=256)return false;
        return tracks_.emplace(static_cast<uint32_t>(stream),Track{type,seq,origin,0}).second;
    }
    size_t streams()const{return tracks_.size();}
    uint64_t next(uint64_t stream)const{if(stream>0xffffffffu)return 0;auto i=tracks_.find(static_cast<uint32_t>(stream));return i==tracks_.end()?0:i->second.next;}
    uint64_t count(uint64_t stream)const{if(stream>0xffffffffu)return 0;auto i=tracks_.find(static_cast<uint32_t>(stream));return i==tracks_.end()?0:i->second.count;}
    int route(const Bytes &wire,std::array<uint64_t,6> &out){
        std::array<uint64_t,6> m{};
        int kind=inspectMedia(wire,m);if(!kind)return 0;
        auto i=tracks_.find(static_cast<uint32_t>(m[2]));
        if(i==tracks_.end())return 0;
        auto &t=i->second;
        if(m[1]!=t.type||m[3]!=t.next||m[4]<t.last)return 0;
        ++t.next;t.last=m[4];++t.count;out=m;return kind;
    }
};
}
