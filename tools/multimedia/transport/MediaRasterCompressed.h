#pragma once
#include "MediaRasterStream.h"
#include <algorithm>

namespace mediawire {
// CKRC v1: deterministic RGBA8 pixel RLE. Token low 7 bits encode count-1;
// high bit selects repeated pixel (4 bytes) rather than literal pixels (4*count).
constexpr size_t rasterCompressedHeader=16;
inline bool samePixel(const Bytes &p,size_t a,size_t b){
    return std::equal(p.begin()+4*a,p.begin()+4*a+4,p.begin()+4*b);
}
inline size_t pixelRun(const Bytes &p,size_t i,size_t n){
    size_t count=1;while(count<128&&i+count<n&&samePixel(p,i,i+count))++count;return count;
}
inline bool encodeRasterCompressed(const Header &h,const RasterInfo &info,const Bytes &rgba,Bytes &out){
    auto n=imagePixels(info.width,info.height);
    if(h.type!=1||!valid(h)||h.stream==0xffffffffu||!n||rgba.size()!=4*n)return false;
    Bytes p(rasterCompressedHeader);
    p[0]='C';p[1]='K';p[2]='R';p[3]='C';p[4]=1;p[5]=1;
    put(p,8,info.width,4);put(p,12,info.height,4);
    for(size_t i=0;i<n;){
        size_t count=pixelRun(rgba,i,n);bool repeat=count>=2;
        if(!repeat){
            while(count<128&&i+count<n&&pixelRun(rgba,i+count,n)==1)++count;
        }
        size_t bytes=4*(repeat?1:count);
        if(p.size()+1+bytes>maxPayload)return false;
        p.push_back(static_cast<uint8_t>((repeat?128:0)|(count-1)));
        p.insert(p.end(),rgba.begin()+4*i,rgba.begin()+4*i+bytes);
        i+=count;
    }
    return encode(h,p,out);
}
inline bool decodeRasterCompressed(const Bytes &wire,Header &h,RasterInfo &info,Bytes &rgba){
    Header c;Bytes p;
    if(!decode(wire,c,p)||c.type!=1||c.stream==0xffffffffu||p.size()<=rasterCompressedHeader||
       p[0]!='C'||p[1]!='K'||p[2]!='R'||p[3]!='C'||p[4]!=1||p[5]!=1||p[6]||p[7])return false;
    RasterInfo v{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4))};
    auto n=imagePixels(v.width,v.height);if(!n)return false;
    Bytes data;data.reserve(static_cast<size_t>(4*n));
    size_t pos=rasterCompressedHeader,done=0;
    while(pos<p.size()){
        auto token=p[pos++];size_t count=(token&127)+1;bool repeat=(token&128)!=0;
        size_t bytes=4*(repeat?1:count);
        if(count>n-done||bytes>p.size()-pos)return false;
        if(repeat){for(size_t j=0;j<count;++j)data.insert(data.end(),p.begin()+pos,p.begin()+pos+4);}
        else data.insert(data.end(),p.begin()+pos,p.begin()+pos+bytes);
        pos+=bytes;done+=count;
    }
    if(done!=n)return false;
    h=c;info=v;rgba.swap(data);return true;
}
}
