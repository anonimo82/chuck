#pragma once
#include "MediaRasterCompressed.h"
#include "MediaRasterPng.h"
#include "MediaRasterPhoto.h"

namespace mediawire {
// CKCF v1: independent compressed rectangular image strips, each bounded by CKMP.
// Each strip contains a complete compressed raster payload, no nested CKMP on wire.
constexpr size_t compressedFragmentHeader=32;
struct CompressedFragment {uint32_t width=0,height=0,image=0,row=0,rows=0,codec=0;};
inline bool flattenRaster(uint64_t w,uint64_t h,uint64_t r,uint64_t g,uint64_t b,const Bytes &src,Bytes &out){
    auto n=imagePixels(w,h);if(!n||r>255||g>255||b>255||src.size()!=4*n)return false;
    Bytes data(src.size());const uint64_t bg[]={r,g,b};
    for(size_t i=0;i<n;++i){
        uint64_t a=src[4*i+3];
        for(size_t j=0;j<3;++j)data[4*i+j]=static_cast<uint8_t>((src[4*i+j]*a+bg[j]*(255-a)+127)/255);
        data[4*i+3]=255;
    }
    out.swap(data);return true;
}
inline bool compressedFragmentValid(const CompressedFragment &f){
    return imagePixels(f.width,f.height)&&f.row<f.height&&f.rows&&f.rows<=f.height-f.row&&f.codec>=1&&f.codec<=5;
}
inline bool encodeCompressedFragment(const Header &h,const CompressedFragment &f,const Bytes &pixels,int quality,Bytes &out){
    if(!valid(h)||h.type!=1||h.stream==0xffffffffu||!compressedFragmentValid(f)||pixels.size()!=4ull*f.width*f.rows)return false;
    RasterInfo v{f.width,f.rows};Bytes inner,p;
    bool ok=f.codec==1?encodeRasterCompressed(h,v,pixels,inner):f.codec==2?encodeRasterPng(h,v,pixels,inner):
        encodeRasterPhoto(h,v,pixels,quality,f.codec>=4,f.codec==5,inner);
    if(!ok||inner.size()-headerSize>maxPayload-compressedFragmentHeader)return false;
    p.resize(compressedFragmentHeader+inner.size()-headerSize);
    p[0]='C';p[1]='K';p[2]='C';p[3]='F';p[4]=1;p[5]=static_cast<uint8_t>(f.codec);
    put(p,8,f.width,4);put(p,12,f.height,4);put(p,16,f.image,4);put(p,20,f.row,4);put(p,24,f.rows,4);
    std::copy(inner.begin()+headerSize,inner.end(),p.begin()+compressedFragmentHeader);
    return encode(h,p,out);
}
inline bool decodeCompressedFragment(const Bytes &wire,Header &h,CompressedFragment &f,Bytes &pixels){
    Header c;Bytes p;
    if(!decode(wire,c,p)||c.type!=1||c.stream==0xffffffffu||p.size()<=compressedFragmentHeader||
       p[0]!='C'||p[1]!='K'||p[2]!='C'||p[3]!='F'||p[4]!=1||p[6]||p[7]||get(p,28,4))return false;
    CompressedFragment v{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4)),
        static_cast<uint32_t>(get(p,16,4)),static_cast<uint32_t>(get(p,20,4)),static_cast<uint32_t>(get(p,24,4)),p[5]};
    if(!compressedFragmentValid(v))return false;
    Bytes payload(p.begin()+compressedFragmentHeader,p.end()),inner,data;RasterInfo info;bool mode=false;
    // Check nested dimensions before codec allocation.
    if(payload.size()<16||get(payload,8,4)!=v.width||get(payload,12,4)!=v.rows||!encode(c,payload,inner))return false;
    bool ok=v.codec==1?decodeRasterCompressed(inner,c,info,data):v.codec==2?decodeRasterPng(inner,c,info,data):
        decodeRasterPhoto(inner,c,info,v.codec>=4,mode,data);
    if(!ok||info.width!=v.width||info.height!=v.rows||(v.codec>=4&&mode!=(v.codec==5)))return false;
    h=c;f=v;pixels.swap(data);return true;
}
class CompressedRasterAssembler {
    Header last_{};CompressedFragment f_{};uint64_t rows_=0;Bytes pixels_;
public:
    void reset(){last_=Header{};f_=CompressedFragment{};rows_=0;Bytes().swap(pixels_);}
    size_t rows()const{return rows_;}
    size_t size()const{return ready()?pixels_.size():0;}
    bool ready()const{return rows_&&rows_==f_.height;}
    const Bytes &pixels()const{return pixels_;}
    int push(const Bytes &wire){
        if(ready())return 0;
        Header h;CompressedFragment f;Bytes data;
        if(!decodeCompressedFragment(wire,h,f,data)||f.row!=rows_)return 0;
        if(rows_&&(h.stream!=last_.stream||h.timestamp!=last_.timestamp||last_.sequence==0xffffffffu||
            h.sequence!=last_.sequence+1||f.width!=f_.width||f.height!=f_.height||f.image!=f_.image||f.codec!=f_.codec))return 0;
        if(!rows_){Bytes full(static_cast<size_t>(4ull*f.width*f.height));pixels_.swap(full);}
        std::copy(data.begin(),data.end(),pixels_.begin()+4ull*f.width*f.row);
        rows_+=f.rows;f_=f;last_=h;return ready()?2:1;
    }
};
}
