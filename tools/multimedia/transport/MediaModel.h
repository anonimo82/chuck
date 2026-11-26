#pragma once
#include "MediaFrame.h"
#include <algorithm>
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

// CKMA v1: contiguous XYZ block, optional attribute blocks, then triangle indices.
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

// CKMT v1: material ID, six PBR factors and eight lighting factors.
// CKTX v1: texture ID, material ID, semantic slot, UV channel zero, RGBA8.
constexpr size_t materialHeader=72, textureHeader=32;
inline bool materialFactorsValid(const std::vector<double>& v){
    if(v.size()!=14)return false;
    for(size_t i=0;i<v.size();++i){
        if(!std::isfinite(v[i])||v[i]<0||v[i]>(i==12?10000.0:1.0))return false;
        if(i==13&&v[i]!=0&&v[i]!=1)return false;
    }
    return true;
}
inline bool encodeMaterial(const Header& h,uint64_t id,const std::vector<double>& v,Bytes& out){
    if(h.type!=3||!valid(h)||h.stream==0xffffffffu||id>=0xffffffffull||!materialFactorsValid(v))return false;
    Bytes p(materialHeader);p[0]='C';p[1]='K';p[2]='M';p[3]='T';p[4]=1;
    put(p,8,id,4);
    for(size_t i=0;i<v.size();++i){float x=static_cast<float>(v[i]);uint32_t bits;std::memcpy(&bits,&x,4);put(p,16+4*i,bits,4);}
    return encode(h,p,out);
}
inline bool decodeMaterial(const Bytes& wire,Header& h,uint64_t& id,std::vector<double>& v){
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=3||candidate.stream==0xffffffffu||
       p.size()!=materialHeader||p[0]!='C'||p[1]!='K'||p[2]!='M'||p[3]!='T'||p[4]!=1||
       p[5]||p[6]||p[7]||get(p,12,4)||get(p,8,4)==0xffffffffull)return false;
    std::vector<double> data(14);
    for(size_t i=0;i<data.size();++i){uint32_t bits=static_cast<uint32_t>(get(p,16+4*i,4));float x;std::memcpy(&x,&bits,4);data[i]=x;}
    if(!materialFactorsValid(data))return false;
    h=candidate;id=get(p,8,4);v.swap(data);return true;
}
struct ModelTexture{uint64_t id=0,material=0,slot=0,width=0,height=0;};
inline size_t modelTexturePacketSize(uint64_t w,uint64_t h){
    constexpr uint64_t limit=(maxPayload-textureHeader)/4;
    if(!w||!h||w>limit||h>limit/w)return 0;
    return headerSize+textureHeader+4*w*h;
}
inline bool validModelTexture(const ModelTexture& t){
    return t.id<0xffffffffull&&t.material<0xffffffffull&&t.slot>=1&&t.slot<=6&&modelTexturePacketSize(t.width,t.height);
}
inline bool encodeModelTexture(const Header& h,const ModelTexture& t,const Bytes& pixels,Bytes& out){
    if(h.type!=3||!valid(h)||h.stream==0xffffffffu||!validModelTexture(t)||pixels.size()!=4*t.width*t.height)return false;
    Bytes p(textureHeader+pixels.size());p[0]='C';p[1]='K';p[2]='T';p[3]='X';p[4]=1;p[5]=static_cast<uint8_t>(t.slot);
    put(p,8,t.id,4);put(p,12,t.material,4);put(p,16,t.width,4);put(p,20,t.height,4);
    std::copy(pixels.begin(),pixels.end(),p.begin()+textureHeader);
    return encode(h,p,out);
}
inline bool decodeModelTexture(const Bytes& wire,Header& h,ModelTexture& t,Bytes& pixels){
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=3||candidate.stream==0xffffffffu||
       p.size()<textureHeader||p[0]!='C'||p[1]!='K'||p[2]!='T'||p[3]!='X'||p[4]!=1||
       p[6]||p[7]||get(p,24,4)||get(p,28,4))return false;
    ModelTexture info{get(p,8,4),get(p,12,4),p[5],get(p,16,4),get(p,20,4)};
    if(!validModelTexture(info)||modelTexturePacketSize(info.width,info.height)!=wire.size())return false;
    Bytes data(p.begin()+textureHeader,p.end());h=candidate;t=info;pixels.swap(data);return true;
}
}
