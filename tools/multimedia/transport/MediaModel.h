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

// CKSN v1: scene/node identifiers, parent (-1 root), row-major local affine
// matrix, then mesh/material ID pairs. Parent must precede child by node ID.
constexpr size_t sceneNodeHeader=88;
constexpr uint64_t noModelId=0xffffffffull;
struct SceneNodeInfo{uint64_t scene=0,node=0,parent=noModelId;};
inline size_t sceneNodePacketSize(uint64_t bindings){
    if(bindings>(maxPayload-sceneNodeHeader)/8)return 0;
    return headerSize+sceneNodeHeader+8*bindings;
}
inline bool sceneNodeInfoValid(const SceneNodeInfo& n){
    return n.scene<noModelId&&n.node<noModelId&&(n.parent==noModelId||n.parent<n.node);
}
inline bool sceneMatrixValid(const std::vector<double>& m){
    if(m.size()!=16)return false;
    for(double x:m)if(!std::isfinite(x)||std::abs(x)>std::numeric_limits<float>::max())return false;
    return m[12]==0&&m[13]==0&&m[14]==0&&m[15]==1;
}
inline bool encodeSceneNode(const Header& h,const SceneNodeInfo& n,const std::vector<double>& m,
                            const std::vector<uint32_t>& bindings,Bytes& out){
    if(h.type!=3||!valid(h)||h.stream==noModelId||!sceneNodeInfoValid(n)||!sceneMatrixValid(m)||bindings.size()%2)return false;
    auto bytes=sceneNodePacketSize(bindings.size()/2);if(!bytes)return false;
    for(size_t i=0;i<bindings.size();i+=2)if(bindings[i]==noModelId)return false;
    Bytes p(bytes-headerSize);p[0]='C';p[1]='K';p[2]='S';p[3]='N';p[4]=1;
    put(p,8,n.scene,4);put(p,12,n.node,4);put(p,16,n.parent,4);put(p,20,bindings.size()/2,4);
    for(size_t i=0;i<16;++i){float x=static_cast<float>(m[i]);uint32_t bits;std::memcpy(&bits,&x,4);put(p,24+4*i,bits,4);}
    for(size_t i=0;i<bindings.size();++i)put(p,sceneNodeHeader+4*i,bindings[i],4);
    return encode(h,p,out);
}
inline bool decodeSceneNode(const Bytes& wire,Header& h,SceneNodeInfo& n,std::vector<double>& m,
                            std::vector<uint32_t>& bindings){
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=3||candidate.stream==noModelId||
       p.size()<sceneNodeHeader||p[0]!='C'||p[1]!='K'||p[2]!='S'||p[3]!='N'||p[4]!=1||p[5]||p[6]||p[7])return false;
    SceneNodeInfo info{get(p,8,4),get(p,12,4),get(p,16,4)};auto count=get(p,20,4);
    if(!sceneNodeInfoValid(info)||sceneNodePacketSize(count)!=wire.size())return false;
    std::vector<double> matrix(16);std::vector<uint32_t> refs(2*count);
    for(size_t i=0;i<16;++i){uint32_t bits=static_cast<uint32_t>(get(p,24+4*i,4));float x;std::memcpy(&x,&bits,4);matrix[i]=x;}
    if(!sceneMatrixValid(matrix))return false;
    for(size_t i=0;i<refs.size();++i){auto id=get(p,sceneNodeHeader+4*i,4);if(i%2==0&&id==noModelId)return false;refs[i]=static_cast<uint32_t>(id);}
    h=candidate;n=info;m.swap(matrix);bindings.swap(refs);return true;
}

// CKAN v1: explicit skeletal/morph resource records with finite binary32 values.
// Kinds: 1 bone definition (parent + inverse bind), 2 vertex weights,
// 3 bone TRS key, 4 morph position delta, 5 morph weight key.
constexpr size_t animationRecordHeader=40;
struct AnimationRecord{uint64_t kind=0,model=0,target=0,index=0,aux=0;};
inline size_t animationRecordValues(uint64_t kind){
    switch(kind){case 1:return 16;case 2:return 1;case 3:return 11;case 4:return 3;case 5:return 2;default:return 0;}
}
inline size_t animationRecordPacketSize(uint64_t kind){
    auto n=animationRecordValues(kind);return n?headerSize+animationRecordHeader+4*n:0;
}
inline bool animationRecordValid(const AnimationRecord& a,const std::vector<double>& v){
    auto n=animationRecordValues(a.kind);
    if(!n||v.size()!=n||a.model>=noModelId||a.target>=noModelId||a.index>=noModelId||a.aux>noModelId)return false;
    for(double x:v)if(!std::isfinite(x)||std::abs(x)>std::numeric_limits<float>::max())return false;
    switch(a.kind){
        case 1:return (a.aux==noModelId||a.aux<a.index)&&sceneMatrixValid(v);
        case 2:return a.aux<noModelId&&v[0]>0&&v[0]<=1;
        case 3:{
            if(a.aux>=noModelId||v[0]<0||v[0]>1e6)return false;
            double q=0;for(size_t i=4;i<8;++i)q+=v[i]*v[i];
            return std::abs(q-1.0)<=1e-5;
        }
        case 4:return a.aux==0;
        case 5:return a.aux==0&&v[0]>=0&&v[0]<=1e6&&v[1]>=0&&v[1]<=1;
        default:return false;
    }
}
inline bool encodeAnimationRecord(const Header& h,const AnimationRecord& a,const std::vector<double>& v,Bytes& out){
    if(h.type!=3||!valid(h)||h.stream==noModelId||!animationRecordValid(a,v))return false;
    std::vector<double> quantized;quantized.reserve(v.size());
    for(double x:v)quantized.push_back(static_cast<float>(x));
    if(!animationRecordValid(a,quantized))return false;
    Bytes p(animationRecordHeader+4*v.size());p[0]='C';p[1]='K';p[2]='A';p[3]='N';p[4]=1;p[5]=static_cast<uint8_t>(a.kind);
    put(p,8,a.model,4);put(p,12,a.target,4);put(p,16,a.index,4);put(p,20,a.aux,4);
    for(size_t i=0;i<v.size();++i){float x=static_cast<float>(v[i]);uint32_t bits;std::memcpy(&bits,&x,4);put(p,animationRecordHeader+4*i,bits,4);}
    return encode(h,p,out);
}
inline bool decodeAnimationRecord(const Bytes& wire,Header& h,AnimationRecord& a,std::vector<double>& v){
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=3||candidate.stream==noModelId||p.size()<animationRecordHeader||
       p[0]!='C'||p[1]!='K'||p[2]!='A'||p[3]!='N'||p[4]!=1||p[6]||p[7]||
       get(p,24,4)||get(p,28,4)||get(p,32,4)||get(p,36,4))return false;
    AnimationRecord info{p[5],get(p,8,4),get(p,12,4),get(p,16,4),get(p,20,4)};
    auto n=animationRecordValues(info.kind);
    if(!n||animationRecordPacketSize(info.kind)!=wire.size())return false;
    std::vector<double> data(n);
    for(size_t i=0;i<n;++i){uint32_t bits=static_cast<uint32_t>(get(p,animationRecordHeader+4*i,4));float x;std::memcpy(&x,&bits,4);data[i]=x;}
    if(!animationRecordValid(info,data))return false;
    h=candidate;a=info;v.swap(data);return true;
}

// CKMF v1: bounded large model resource, opaque byte slices with resource ID.
// Resource format 1 = RGBA8 (width/height in resource header), 2 = application-defined binary.
// All fragments share stream, timestamp, resource ID, format, total and first sequence.
constexpr size_t modelFragmentHeader=32,maxModelResource=16*1024*1024;
struct ModelFragmentInfo{uint64_t resource=0,format=0,total=0,offset=0,first=0;};
inline size_t modelFragmentPacketSize(uint64_t n){
    return n&&n<=maxPayload-modelFragmentHeader?headerSize+modelFragmentHeader+n:0;
}
inline bool validModelFragment(const Header& h,const ModelFragmentInfo& f,size_t n){
    return valid(h)&&h.type==3&&h.stream!=noModelId&&f.resource<noModelId&&
        (f.format==1||f.format==2)&&f.total<=maxModelResource&&f.offset<f.total&&n<=f.total-f.offset&&
        modelFragmentPacketSize(n)&&f.first<=h.sequence&&((f.offset==0)==(f.first==h.sequence));
}
inline bool encodeModelFragment(const Header& h,const ModelFragmentInfo& f,const Bytes& data,Bytes& out){
    if(!validModelFragment(h,f,data.size()))return false;
    Bytes p(modelFragmentHeader+data.size());p[0]='C';p[1]='K';p[2]='M';p[3]='F';p[4]=1;p[5]=static_cast<uint8_t>(f.format);
    put(p,8,f.resource,4);put(p,12,f.total,4);put(p,16,f.offset,4);put(p,20,f.first,4);
    std::copy(data.begin(),data.end(),p.begin()+modelFragmentHeader);return encode(h,p,out);
}
inline bool decodeModelFragment(const Bytes& wire,Header& h,ModelFragmentInfo& f,Bytes& data){
    Header c;Bytes p;
    if(!decode(wire,c,p)||p.size()<=modelFragmentHeader||p[0]!='C'||p[1]!='K'||p[2]!='M'||p[3]!='F'||
       p[4]!=1||p[6]||p[7]||get(p,24,4)||get(p,28,4))return false;
    ModelFragmentInfo info{get(p,8,4),p[5],get(p,12,4),get(p,16,4),get(p,20,4)};
    if(!validModelFragment(c,info,p.size()-modelFragmentHeader))return false;
    Bytes bytes(p.begin()+modelFragmentHeader,p.end());h=c;f=info;data.swap(bytes);return true;
}
// Format 1 canonical texture resource: CKTX v1 payload (without CKMP envelope),
// with the same metadata and RGBA8 layout as the single-packet texture.
inline bool validLargeTexture(const Bytes& p){
    if(p.size()<textureHeader||p[0]!='C'||p[1]!='K'||p[2]!='T'||p[3]!='X'||p[4]!=1||
       p[6]||p[7]||get(p,24,4)||get(p,28,4))return false;
    auto w=get(p,16,4),h=get(p,20,4);
    constexpr uint64_t limit=(maxModelResource-textureHeader)/4;
    return get(p,8,4)<noModelId&&get(p,12,4)<noModelId&&p[5]>=1&&p[5]<=6&&
        w&&h&&w<=limit&&h<=limit/w&&p.size()==textureHeader+4*w*h;
}
class ModelAssembler {
    Header h_{};ModelFragmentInfo f_{};Bytes data_;
public:
    void reset(){h_={};f_={};Bytes().swap(data_);}
    size_t size()const{return data_.size();}
    bool ready()const{return f_.total&&data_.size()==f_.total;}
    const Bytes& data()const{return data_;}
    int push(const Bytes& wire){
        if(ready())return 0;
        Header h;ModelFragmentInfo f;Bytes d;
        if(!decodeModelFragment(wire,h,f,d)||f.offset!=data_.size())return 0;
        if(f.offset&&(h.stream!=h_.stream||h.timestamp!=h_.timestamp||f.resource!=f_.resource||
           f.format!=f_.format||f.total!=f_.total||f.first!=f_.first||h_.sequence==0xffffffffu||h.sequence!=h_.sequence+1))return 0;
        if(f.offset+d.size()==f.total&&f.format==1){
            Bytes complete=data_;complete.insert(complete.end(),d.begin(),d.end());
            if(!validLargeTexture(complete)||get(complete,8,4)!=f.resource)return 0;
            data_.swap(complete);
        }else data_.insert(data_.end(),d.begin(),d.end());
        h_=h;f_=f;return ready()?2:1;
    }
};
}
