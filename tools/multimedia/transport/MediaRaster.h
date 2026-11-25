#pragma once
#include "MediaFrame.h"

namespace mediawire {
// CKRA v1: tightly packed top-down RGBA8, straight alpha, no compression.
constexpr size_t rasterHeader=16;
inline size_t rasterPacketSize(uint64_t width,uint64_t height) {
    constexpr uint64_t pixels=(maxPayload-rasterHeader)/4;
    if(!width||!height||width>pixels||height>pixels/width) return 0;
    return headerSize+rasterHeader+static_cast<size_t>(width*height*4);
}
struct RasterInfo { uint32_t width=0,height=0; };
inline bool encodeRaster(const Header &h,const RasterInfo &info,const Bytes &rgba,Bytes &out) {
    auto n=rasterPacketSize(info.width,info.height);
    if(h.type!=1||!valid(h)||h.stream==0xffffffffu||!n||rgba.size()!=n-headerSize-rasterHeader) return false;
    Bytes payload(n-headerSize);
    payload[0]='C'; payload[1]='K'; payload[2]='R'; payload[3]='A';
    payload[4]=1; payload[5]=1; // RGBA8
    put(payload,8,info.width,4); put(payload,12,info.height,4);
    for(size_t i=0;i<rgba.size();++i) payload[rasterHeader+i]=rgba[i];
    return encode(h,payload,out);
}
inline bool decodeRaster(const Bytes &wire,Header &h,RasterInfo &info,Bytes &rgba) {
    Header candidate; Bytes payload;
    if(!decode(wire,candidate,payload)||candidate.type!=1||candidate.stream==0xffffffffu||
       payload.size()<rasterHeader||payload[0]!='C'||payload[1]!='K'||payload[2]!='R'||payload[3]!='A'||
       payload[4]!=1||payload[5]!=1||payload[6]||payload[7]) return false;
    RasterInfo p{static_cast<uint32_t>(get(payload,8,4)),static_cast<uint32_t>(get(payload,12,4))};
    auto n=rasterPacketSize(p.width,p.height);
    if(!n||n!=wire.size()) return false;
    Bytes data(payload.begin()+rasterHeader,payload.end());
    h=candidate; info=p; rgba.swap(data); return true;
}
}
