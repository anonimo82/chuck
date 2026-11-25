#pragma once
#include "MediaRasterStream.h"
#include <png.h>
#include <cstring>

namespace mediawire {
// CKPN v1: complete PNG datastream, 8-bit RGBA, bounded decoded dimensions.
constexpr size_t rasterPngHeader=16;
struct PngImage {
    png_image image{};
    PngImage(){image.version=PNG_IMAGE_VERSION;}
    ~PngImage(){png_image_free(&image);}
    PngImage(const PngImage&)=delete;
    PngImage& operator=(const PngImage&)=delete;
};
inline bool encodeRasterPng(const Header &h,const RasterInfo &info,const Bytes &rgba,Bytes &out){
    auto n=imagePixels(info.width,info.height);
    if(h.type!=1||!valid(h)||h.stream==0xffffffffu||!n||rgba.size()!=4*n)return false;
    PngImage p;p.image.width=info.width;p.image.height=info.height;p.image.format=PNG_FORMAT_RGBA;
    png_alloc_size_t size=0;
    if(!png_image_write_to_memory(&p.image,nullptr,&size,0,rgba.data(),0,nullptr)||
       !size||size>maxPayload-rasterPngHeader)return false;
    Bytes payload(rasterPngHeader+static_cast<size_t>(size));
    if(!png_image_write_to_memory(&p.image,payload.data()+rasterPngHeader,&size,0,rgba.data(),0,nullptr))return false;
    if(!size||size>payload.size()-rasterPngHeader)return false;
    payload.resize(rasterPngHeader+static_cast<size_t>(size));
    payload[0]='C';payload[1]='K';payload[2]='P';payload[3]='N';payload[4]=1;payload[5]=1;
    put(payload,8,info.width,4);put(payload,12,info.height,4);
    return encode(h,payload,out);
}
// Restrict the wire profile to the encoder's RGBA8 PNGs, with complete chunk
// framing and no trailing bytes. libpng validates the compressed image data.
inline bool pngWireProfile(const Bytes &p,const RasterInfo &v){
    constexpr uint8_t signature[]={137,80,78,71,13,10,26,10};
    const size_t start=rasterPngHeader;
    if(p.size()<start+8+25+12||std::memcmp(p.data()+start,signature,8))return false;
    size_t pos=start+8;
    bool first=true,idat=false;
    while(pos<p.size()){
        if(p.size()-pos<12)return false;
        auto len=get(p,pos,4);
        if(len>p.size()-pos-12)return false;
        const auto *tag=p.data()+pos+4;
        if(first){
            if(std::memcmp(tag,"IHDR",4)||len!=13||get(p,pos+8,4)!=v.width||
               get(p,pos+12,4)!=v.height||p[pos+16]!=8||p[pos+17]!=6||
               p[pos+18]||p[pos+19]||p[pos+20])return false;
            first=false;
        }else if(!std::memcmp(tag,"IDAT",4))idat=true;
        else if(!std::memcmp(tag,"IEND",4))return idat&&!len&&pos+12==p.size();
        else if(std::memcmp(tag,"sRGB",4)&&std::memcmp(tag,"gAMA",4)&&std::memcmp(tag,"cHRM",4)&&std::memcmp(tag,"sBIT",4))return false;
        pos+=static_cast<size_t>(len)+12;
    }
    return false;
}
inline bool decodeRasterPng(const Bytes &wire,Header &h,RasterInfo &info,Bytes &rgba){
    Header c;Bytes p;
    if(!decode(wire,c,p)||c.type!=1||c.stream==0xffffffffu||p.size()<=rasterPngHeader||
       p[0]!='C'||p[1]!='K'||p[2]!='P'||p[3]!='N'||p[4]!=1||p[5]!=1||p[6]||p[7])return false;
    RasterInfo v{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4))};
    auto n=imagePixels(v.width,v.height);if(!n||!pngWireProfile(p,v))return false;
    PngImage image;
    if(!png_image_begin_read_from_memory(&image.image,p.data()+rasterPngHeader,p.size()-rasterPngHeader)||
       image.image.width!=v.width||image.image.height!=v.height)return false;
    image.image.format=PNG_FORMAT_RGBA;
    Bytes data(static_cast<size_t>(4*n));
    if(!png_image_finish_read(&image.image,nullptr,data.data(),0,nullptr)||
       image.image.warning_or_error)return false;
    h=c;info=v;rgba.swap(data);return true;
}
}
