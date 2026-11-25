#pragma once
#include "MediaVideoStream.h"

namespace mediawire {
// CKVD v1: XOR residual against the immediately previous decoded frame.
// Explicit reference slices keep the existing receiver memory budget unchanged.
constexpr size_t videoDeltaHeader=36;
struct VideoDelta {VideoFragment f;uint32_t reference=0;};
inline uint32_t videoReferenceCRC(const Bytes &b){
    uint32_t c=0xffffffffu;
    for(auto v:b){c^=v;for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}
    return c^0xffffffffu;
}
inline bool validVideoDelta(const VideoDelta &d){
    return validVideoFragment(d.f)&&d.f.frame>0&&d.reference==d.f.frame-1;
}
inline bool encodeVideoDelta(const Header &h,const VideoDelta &d,const Bytes &base,const Bytes &current,Bytes &out){
    if(h.type!=6||!valid(h)||h.stream==0xffffffffu||!validVideoDelta(d)||
       base.size()!=4ull*d.f.count||current.size()!=base.size())return false;
    Bytes residual(base.size());
    for(size_t i=0;i<base.size();++i)residual[i]=base[i]^current[i];
    Header raster=h;raster.type=1;Bytes inner;
    if(!encodeRasterCompressed(raster,{d.f.count,1},residual,inner)||
       inner.size()-headerSize>maxPayload-videoDeltaHeader)return false;
    Bytes p(videoDeltaHeader+inner.size()-headerSize);
    p[0]='C';p[1]='K';p[2]='V';p[3]='D';p[4]=1;p[5]=1;
    put(p,8,d.f.width,4);put(p,12,d.f.height,4);put(p,16,d.f.frame,4);
    put(p,20,d.f.offset,4);put(p,24,d.f.count,4);put(p,28,d.reference,4);
    put(p,32,videoReferenceCRC(base),4);
    std::copy(inner.begin()+headerSize,inner.end(),p.begin()+videoDeltaHeader);
    return encode(h,p,out);
}
inline bool decodeVideoDelta(const Bytes &wire,Header &h,VideoDelta &d,uint32_t &crc,Bytes &residual){
    Header c;Bytes p;
    if(!decode(wire,c,p)||c.type!=6||c.stream==0xffffffffu||p.size()<=videoDeltaHeader+16||
       p[0]!='C'||p[1]!='K'||p[2]!='V'||p[3]!='D'||p[4]!=1||p[5]!=1||p[6]||p[7])return false;
    VideoDelta v{{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4)),
        static_cast<uint32_t>(get(p,16,4)),static_cast<uint32_t>(get(p,20,4)),static_cast<uint32_t>(get(p,24,4))},
        static_cast<uint32_t>(get(p,28,4))};
    if(!validVideoDelta(v))return false;
    Bytes payload(p.begin()+videoDeltaHeader,p.end()),inner,data;Header raster=c;raster.type=1;RasterInfo info;
    if(get(payload,8,4)!=v.f.count||get(payload,12,4)!=1||!encode(raster,payload,inner)||
       !decodeRasterCompressed(inner,raster,info,data)||data.size()!=4ull*v.f.count)return false;
    h=c;d=v;crc=static_cast<uint32_t>(get(p,32,4));residual.swap(data);return true;
}
inline bool reconstructVideoDelta(const Bytes &wire,uint64_t reference,const Bytes &base,Bytes &out){
    Header h;VideoDelta d;uint32_t crc;Bytes data;
    if(!decodeVideoDelta(wire,h,d,crc,data)||reference!=d.reference||base.size()!=data.size()||videoReferenceCRC(base)!=crc)return false;
    for(size_t i=0;i<data.size();++i)data[i]^=base[i];
    return encodeVideoFragment(h,d.f,data,out);
}
}
