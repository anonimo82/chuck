#pragma once
#include "MediaFrame.h"
#include <cmath>

namespace mediawire {
// CKAP v1, signed PCM16, interleaved, big endian.
constexpr size_t pcmHeader=16;
struct PcmInfo { uint32_t rate=0, channels=0, frames=0; };
inline size_t pcmPacketSize(uint64_t rate,uint64_t channels,uint64_t frames) {
    if(rate<8000||rate>192000||channels<1||channels>8||frames<1||
       frames>(maxPayload-pcmHeader)/(2*channels)) return 0;
    return headerSize+pcmHeader+static_cast<size_t>(2*channels*frames);
}
inline bool encodePcm(const Header &h,const PcmInfo &info,const std::vector<double> &samples,Bytes &out) {
    auto n=pcmPacketSize(info.rate,info.channels,info.frames);
    if(h.type!=4||!valid(h)||h.stream==0xffffffffu||!n||
       samples.size()!=static_cast<size_t>(info.channels)*info.frames) return false;
    Bytes data(n-headerSize);
    data[0]='C'; data[1]='K'; data[2]='A'; data[3]='P';
    data[4]=1; data[5]=1; data[6]=static_cast<uint8_t>(info.channels);
    put(data,8,info.rate,4); put(data,12,info.frames,4);
    for(size_t i=0;i<samples.size();++i) {
        double v=samples[i];
        if(!std::isfinite(v)||v<-1.0||v>1.0) return false;
        int32_t q=static_cast<int32_t>(std::lround(v*(v<0?32768.0:32767.0)));
        put(data,pcmHeader+2*i,static_cast<uint16_t>(q),2);
    }
    return encode(h,data,out);
}
inline bool decodePcm(const Bytes &wire,Header &h,PcmInfo &info,std::vector<double> &samples) {
    Header candidate; Bytes data;
    if(!decode(wire,candidate,data)||candidate.type!=4||candidate.stream==0xffffffffu||
       data.size()<pcmHeader||data[0]!='C'||data[1]!='K'||data[2]!='A'||data[3]!='P'||
       data[4]!=1||data[5]!=1||data[7]!=0) return false;
    PcmInfo p{static_cast<uint32_t>(get(data,8,4)),data[6],static_cast<uint32_t>(get(data,12,4))};
    auto n=pcmPacketSize(p.rate,p.channels,p.frames);
    if(!n||n!=wire.size()) return false;
    std::vector<double> decoded(static_cast<size_t>(p.frames)*p.channels);
    for(size_t i=0;i<decoded.size();++i) {
        auto q=static_cast<int32_t>(get(data,pcmHeader+2*i,2));
        if(q>=32768) q-=65536;
        decoded[i]=q/(q<0?32768.0:32767.0);
    }
    h=candidate; info=p; samples.swap(decoded); return true;
}
}
