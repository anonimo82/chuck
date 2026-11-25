#pragma once
#include "MediaVideoStream.h"
#include <utility>

namespace mediawire {
// Sequential CKVS sender, one immutable pending frame and one prepared packet.
// Commit only after the caller's transport accepts the packet.
class VideoFragmentSender {
    uint32_t stream_=0,w_=0,h_=0;
    uint64_t seq_=0,frame_=0,time_=0,offset_=0,completed_=0,chunk_=0,pending_=0;
    Bytes pixels_,packet_,reference_;
    size_t referenceBudget_=0;
    uint32_t referenceW_=0,referenceH_=0;
    bool adaptive_=false;
public:
    void clear(){*this=VideoFragmentSender{};}
    bool configure(uint64_t stream,uint64_t sequence,uint64_t frame,uint64_t origin,uint64_t limit){
        if(!stream||stream>=0xffffffffu||sequence>0xffffffffu||frame>0xffffffffu||
           origin>0x7fffffffffffffffull||limit<videoFragmentHeader+4||limit>maxPayload)return false;
        clear();stream_=static_cast<uint32_t>(stream);seq_=sequence;frame_=frame;time_=origin;
        chunk_=(limit-videoFragmentHeader)/4;return true;
    }
    bool adaptive(uint64_t budget){
        if(!configured()||busy()||completed_||budget>maxVideoPixels*4)return false;
        adaptive_=true;referenceBudget_=static_cast<size_t>(budget);return true;
    }
    bool configured()const{return stream_!=0;}
    bool busy()const{return !pixels_.empty();}
    uint64_t nextSequence()const{return seq_;}
    uint64_t nextFrame()const{return frame_;}
    uint64_t sentPixels()const{return offset_;}
    uint64_t completed()const{return completed_;}
    uint64_t maxFragmentPixels()const{return chunk_;}
    size_t packetSize()const{return packet_.size();}
    const Bytes &packet()const{return packet_;}
    bool begin(uint64_t w,uint64_t h,uint64_t timestamp,const Bytes &pixels){
        auto total=videoPixels(w,h);
        if(!configured()||busy()||!total||pixels.size()!=4*total||
           timestamp>0x7fffffffffffffffull||timestamp<time_||(completed_&&timestamp==time_)||
           seq_>0xffffffffu||frame_>0xffffffffu)return false;
        auto count=(total+chunk_-1)/chunk_;
        if(count>0x100000000ull-seq_)return false;
        Bytes copy=pixels;
        pixels_.swap(copy);w_=static_cast<uint32_t>(w);h_=static_cast<uint32_t>(h);
        time_=timestamp;offset_=0;return true;
    }
    bool prepare(){
        if(!busy())return false;
        if(!packet_.empty())return true;
        auto total=videoPixels(w_,h_);
        auto n=std::min(chunk_,total-offset_);
        auto start=pixels_.begin()+4*offset_;
        Bytes data(start,start+4*n),wire;
        Header h{6,stream_,static_cast<uint32_t>(seq_),time_};
        VideoFragment f{w_,h_,static_cast<uint32_t>(frame_),static_cast<uint32_t>(offset_),static_cast<uint32_t>(n)};
        if(!encodeVideoFragment(h,f,data,wire))return false;
        if(adaptive_){
            Bytes candidate;
            if(f.offset%f.width==0&&f.count%f.width==0){
                auto compressed=f;compressed.codec=1;
                if(encodeVideoCompressed(h,compressed,data,100,candidate)&&candidate.size()<wire.size())wire.swap(candidate);
            }
            if(frame_&&referenceW_==w_&&referenceH_==h_&&reference_.size()==pixels_.size()){
                Bytes base(reference_.begin()+4*offset_,reference_.begin()+4*(offset_+n));
                if(encodeVideoDelta(h,{f,static_cast<uint32_t>(frame_-1)},base,data,candidate)&&candidate.size()<wire.size())wire.swap(candidate);
            }
        }
        packet_.swap(wire);pending_=n;return true;
    }
    bool commit(){
        if(packet_.empty())return false;
        offset_+=pending_;pending_=0;++seq_;packet_.clear();
        if(offset_==videoPixels(w_,h_)){
            if(adaptive_&&pixels_.size()<=referenceBudget_){
                reference_.swap(pixels_);referenceW_=w_;referenceH_=h_;
            }else{Bytes().swap(reference_);referenceW_=referenceH_=0;}
            Bytes().swap(pixels_);++frame_;++completed_;
        }
        return true;
    }
};
}
