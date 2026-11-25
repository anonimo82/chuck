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
    auto v=get(p,8,4),t=get(p,12,4);
    auto n=modelPacketSize(v,t);
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

// CKMA v1: XYZ and triangle indices followed by optional per-vertex arrays.
// Mask bits: normals(3), UV(2), RGBA(4), tangents(3), bitangents(3).
constexpr size_t meshAttributeHeader=20;
inline size_t meshAttributeStride(uint64_t mask){
    if(mask>31)return 0;
    return 3+((mask&1)?3:0)+((mask&2)?2:0)+((mask&4)?4:0)+((mask&8)?3:0)+((mask&16)?3:0);
}
inline size_t meshAttributePacketSize(uint64_t v,uint64_t t,uint64_t mask){
    auto stride=meshAttributeStride(mask);
    constexpr uint64_t words=(maxPayload-meshAttributeHeader)/4;
    if(!stride||v<3||!t||v>words/stride||t>(words-v*stride)/3)return 0;
    return headerSize+meshAttributeHeader+4*(v*stride+3*t);
}
inline bool encodeMeshAttributes(const Header &h,uint64_t v,uint64_t mask,const std::vector<double>& values,
                                 const std::vector<uint32_t>& indices,Bytes& out){
    if(h.type!=3||!valid(h)||h.stream==0xffffffffu||indices.size()%3)return false;
    auto n=meshAttributePacketSize(v,indices.size()/3,mask);
    if(!n||values.size()!=v*meshAttributeStride(mask))return false;
    Bytes p(n-headerSize);
    p[0]='C';p[1]='K';p[2]='M';p[3]='A';p[4]=1;p[5]=static_cast<uint8_t>(mask);
    put(p,8,v,4);put(p,12,indices.size()/3,4);
    for(size_t i=0;i<values.size();++i){
        if(!std::isfinite(values[i])||std::abs(values[i])>std::numeric_limits<float>::max())return false;
        float value=static_cast<float>(values[i]);uint32_t bits;std::memcpy(&bits,&value,4);
        put(p,meshAttributeHeader+4*i,bits,4);
    }
    for(size_t i=0;i<indices.size();++i){
        if(indices[i]>=v)return false;
        put(p,meshAttributeHeader+4*(values.size()+i),indices[i],4);
    }
    return encode(h,p,out);
}
inline bool decodeMeshAttributes(const Bytes& wire,Header& h,uint64_t& v,uint64_t& mask,
                                 std::vector<double>& values,std::vector<uint32_t>& indices){
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=3||candidate.stream==0xffffffffu||
       p.size()<meshAttributeHeader||p[0]!='C'||p[1]!='K'||p[2]!='M'||p[3]!='A'||p[4]!=1||
       p[6]||p[7]||get(p,16,4))return false;
    auto vertices=get(p,8,4),triangles=get(p,12,4);uint64_t m=p[5];
    auto n=meshAttributePacketSize(vertices,triangles,m);
    if(!n||n!=wire.size())return false;
    std::vector<double> data(vertices*meshAttributeStride(m));
    std::vector<uint32_t> tris(3*triangles);
    for(size_t i=0;i<data.size();++i){
        uint32_t bits=static_cast<uint32_t>(get(p,meshAttributeHeader+4*i,4));float value;
        std::memcpy(&value,&bits,4);if(!std::isfinite(value))return false;data[i]=value;
    }
    for(size_t i=0;i<tris.size();++i){
        auto index=get(p,meshAttributeHeader+4*(data.size()+i),4);
        if(index>=vertices)return false;tris[i]=static_cast<uint32_t>(index);
    }
    h=candidate;v=vertices;mask=m;values.swap(data);indices.swap(tris);return true;
}
}
