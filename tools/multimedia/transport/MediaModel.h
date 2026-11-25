#pragma once
#include "MediaFrame.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace mediawire {
// CKMD v1: one indexed triangle mesh, IEEE binary32 XYZ, uint32 indices.
constexpr size_t modelHeader=16;
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,"CKMD requires IEEE binary32");
inline size_t modelPacketSize(uint64_t vertices,uint64_t triangles) {
    constexpr uint64_t budget=(maxPayload-modelHeader)/12;
    if(vertices<3||vertices>budget||!triangles||triangles>budget-vertices) return 0;
    return headerSize+modelHeader+12*(vertices+triangles);
}
inline bool encodeModel(const Header &h,const std::vector<double> &xyz,const std::vector<uint32_t> &indices,Bytes &out) {
    if(h.type!=3||!valid(h)||h.stream==0xffffffffu||xyz.size()%3||indices.size()%3) return false;
    auto v=xyz.size()/3,t=indices.size()/3,n=modelPacketSize(v,t);
    if(!n)return false;
    Bytes p(n-headerSize);
    p[0]='C';p[1]='K';p[2]='M';p[3]='D';p[4]=1;p[5]=1;
    put(p,8,v,4);put(p,12,t,4);
    for(size_t i=0;i<xyz.size();++i) {
        if(!std::isfinite(xyz[i])||std::abs(xyz[i])>std::numeric_limits<float>::max()) return false;
        float f=static_cast<float>(xyz[i]);uint32_t bits;std::memcpy(&bits,&f,4);
        put(p,modelHeader+i*4,bits,4);
    }
    for(size_t i=0;i<indices.size();++i) {
        if(indices[i]>=v)return false;
        put(p,modelHeader+4*(xyz.size()+i),indices[i],4);
    }
    return encode(h,p,out);
}
inline bool decodeModel(const Bytes &wire,Header &h,std::vector<double> &xyz,std::vector<uint32_t> &indices) {
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=3||candidate.stream==0xffffffffu||
       p.size()<modelHeader||p[0]!='C'||p[1]!='K'||p[2]!='M'||p[3]!='D'||p[4]!=1||p[5]!=1||p[6]||p[7])return false;
    auto v=get(p,8,4),t=get(p,12,4),n=modelPacketSize(v,t);
    if(!n||n!=wire.size())return false;
    std::vector<double> coords(v*3);std::vector<uint32_t> tris(t*3);
    for(size_t i=0;i<coords.size();++i) {
        uint32_t bits=static_cast<uint32_t>(get(p,modelHeader+i*4,4));float f;
        std::memcpy(&f,&bits,4);if(!std::isfinite(f))return false;coords[i]=f;
    }
    for(size_t i=0;i<tris.size();++i) {
        auto index=get(p,modelHeader+4*(coords.size()+i),4);
        if(index>=v)return false;tris[i]=static_cast<uint32_t>(index);
    }
    h=candidate;xyz.swap(coords);indices.swap(tris);return true;
}
}
