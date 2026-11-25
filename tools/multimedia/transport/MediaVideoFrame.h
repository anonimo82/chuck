#pragma once
#include "MediaFrame.h"

namespace mediawire {
// CKVF v1: one timestamped video frame, tightly packed top-down RGBA8, straight alpha, no compression.
constexpr size_t videoFrameHeader=16;
inline size_t videoFramePacketSize(uint64_t width,uint64_t height) {
    constexpr uint64_t pixels=(maxPayload-videoFrameHeader)/4;
    if(!width||!height||width>pixels||height>pixels/width) return 0;
    return headerSize+videoFrameHeader+static_cast<size_t>(width*height*4);
}
struct VideoFrameInfo { uint32_t width=0,height=0; };
inline bool encodeVideoFrame(const Header &h,const VideoFrameInfo &info,const Bytes &rgba,Bytes &out) {
    auto n=videoFramePacketSize(info.width,info.height);
    if(h.type!=6||!valid(h)||h.stream==0xffffffffu||!n||rgba.size()!=n-headerSize-videoFrameHeader) return false;
    Bytes payload(n-headerSize);
    payload[0]='C'; payload[1]='K'; payload[2]='V'; payload[3]='F';
    payload[4]=1; payload[5]=1; // RGBA8
    put(payload,8,info.width,4); put(payload,12,info.height,4);
    for(size_t i=0;i<rgba.size();++i) payload[videoFrameHeader+i]=rgba[i];
    return encode(h,payload,out);
}
inline bool decodeVideoFrame(const Bytes &wire,Header &h,VideoFrameInfo &info,Bytes &rgba) {
    Header candidate; Bytes payload;
    if(!decode(wire,candidate,payload)||candidate.type!=6||candidate.stream==0xffffffffu||
       payload.size()<videoFrameHeader||payload[0]!='C'||payload[1]!='K'||payload[2]!='V'||payload[3]!='F'||
       payload[4]!=1||payload[5]!=1||payload[6]||payload[7]) return false;
    VideoFrameInfo p{static_cast<uint32_t>(get(payload,8,4)),static_cast<uint32_t>(get(payload,12,4))};
    auto n=videoFramePacketSize(p.width,p.height);
    if(!n||n!=wire.size()) return false;
    Bytes data(payload.begin()+videoFrameHeader,payload.end());
    h=candidate; info=p; rgba.swap(data); return true;
}
}
