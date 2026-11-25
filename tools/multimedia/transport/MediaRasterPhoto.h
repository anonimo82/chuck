#pragma once
#include "MediaRasterStream.h"
#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <memory>
#include <jpeglib.h>
#include <webp/encode.h>
#include <webp/decode.h>

namespace mediawire {
constexpr size_t rasterPhotoHeader=16;
struct JpegFailure { jpeg_error_mgr base; std::jmp_buf jump; };
inline void jpegFail(j_common_ptr c){std::longjmp(reinterpret_cast<JpegFailure*>(c->err)->jump,1);}
inline void jpegMessage(j_common_ptr c,int level){if(level<0)jpegFail(c);}
struct JpegEncoder {
    jpeg_compress_struct c{}; JpegFailure error{}; unsigned char *buffer=nullptr; unsigned long size=0;
    ~JpegEncoder(){jpeg_destroy_compress(&c);std::free(buffer);}
};
struct JpegDecoder {
    jpeg_decompress_struct c{}; JpegFailure error{};
    ~JpegDecoder(){jpeg_destroy_decompress(&c);}
};
// Keep all nontrivial C++ objects outside the region crossed by longjmp.
inline bool jpegEncodePixels(const RasterInfo &v,const Bytes &rgba,int quality,Bytes &out){
    auto n=imagePixels(v.width,v.height);
    if(!n||v.width>JPEG_MAX_DIMENSION||v.height>JPEG_MAX_DIMENSION||rgba.size()!=4*n||quality<1||quality>100)return false;
    Bytes rgb(static_cast<size_t>(3*n));
    for(size_t i=0;i<n;++i){
        if(rgba[4*i+3]!=255)return false;
        for(size_t j=0;j<3;++j)rgb[3*i+j]=rgba[4*i+j];
    }
    auto e=std::make_unique<JpegEncoder>();
    e->c.err=jpeg_std_error(&e->error.base);e->error.base.error_exit=jpegFail;e->error.base.emit_message=jpegMessage;
    if(setjmp(e->error.jump))return false;
    jpeg_create_compress(&e->c);jpeg_mem_dest(&e->c,&e->buffer,&e->size);
    e->c.image_width=v.width;e->c.image_height=v.height;e->c.input_components=3;e->c.in_color_space=JCS_RGB;
    jpeg_set_defaults(&e->c);jpeg_set_quality(&e->c,quality,TRUE);
    jpeg_start_compress(&e->c,TRUE);
    while(e->c.next_scanline<e->c.image_height){
        JSAMPROW row=rgb.data()+static_cast<size_t>(e->c.next_scanline)*v.width*3;
        if(jpeg_write_scanlines(&e->c,&row,1)!=1)return false;
    }
    jpeg_finish_compress(&e->c);
    if(!e->size||e->size>maxPayload-rasterPhotoHeader)return false;
    Bytes data(e->buffer,e->buffer+e->size);out.swap(data);return true;
}
inline bool jpegDecodePixels(const RasterInfo &v,const uint8_t *src,size_t size,Bytes &out){
    auto n=imagePixels(v.width,v.height);
    if(!n||size<4||src[0]!=255||src[1]!=216||src[size-2]!=255||src[size-1]!=217)return false;
    Bytes rgb(static_cast<size_t>(3*n)),rgba(static_cast<size_t>(4*n));
    auto d=std::make_unique<JpegDecoder>();
    d->c.err=jpeg_std_error(&d->error.base);d->error.base.error_exit=jpegFail;d->error.base.emit_message=jpegMessage;
    if(setjmp(d->error.jump))return false;
    jpeg_create_decompress(&d->c);jpeg_mem_src(&d->c,src,static_cast<unsigned long>(size));
    if(jpeg_read_header(&d->c,TRUE)!=JPEG_HEADER_OK||d->c.image_width!=v.width||d->c.image_height!=v.height||
       d->c.data_precision!=8||d->c.num_components!=3||d->c.progressive_mode||
       (d->c.jpeg_color_space!=JCS_YCbCr&&d->c.jpeg_color_space!=JCS_RGB))return false;
    d->c.out_color_space=JCS_RGB;
    if(!jpeg_start_decompress(&d->c)||d->c.output_width!=v.width||d->c.output_height!=v.height||d->c.output_components!=3)return false;
    while(d->c.output_scanline<d->c.output_height){
        JSAMPROW row=rgb.data()+static_cast<size_t>(d->c.output_scanline)*v.width*3;
        if(jpeg_read_scanlines(&d->c,&row,1)!=1)return false;
    }
    if(!jpeg_finish_decompress(&d->c)||d->c.src->bytes_in_buffer)return false;
    for(size_t i=0;i<n;++i){for(size_t j=0;j<3;++j)rgba[4*i+j]=rgb[3*i+j];rgba[4*i+3]=255;}
    out.swap(rgba);return true;
}
struct WebpPicture {
    WebPPicture p{}; bool initialized=false;
    WebpPicture(){initialized=WebPPictureInit(&p)!=0;}
    ~WebpPicture(){if(initialized)WebPPictureFree(&p);}
};
inline int webpBoundedWrite(const uint8_t *data,size_t size,const WebPPicture *pic){
    auto *out=static_cast<Bytes*>(pic->custom_ptr);
    if(!out||size>maxPayload-rasterPhotoHeader-out->size())return 0;
    try{out->insert(out->end(),data,data+size);return 1;}catch(...){return 0;}
}
inline bool webpEncodePixels(const RasterInfo &v,const Bytes &rgba,int quality,bool lossless,Bytes &out){
    auto n=imagePixels(v.width,v.height);
    if(!n||v.width>WEBP_MAX_DIMENSION||v.height>WEBP_MAX_DIMENSION||rgba.size()!=4*n||quality<1||quality>100)return false;
    WebPConfig cfg;
    if(!WebPConfigInit(&cfg))return false;
    cfg.lossless=lossless;cfg.quality=static_cast<float>(quality);cfg.exact=1;cfg.alpha_quality=100;
    if(!WebPValidateConfig(&cfg))return false;
    WebpPicture pic;if(!pic.initialized)return false;
    pic.p.width=v.width;pic.p.height=v.height;pic.p.use_argb=1;
    Bytes data;pic.p.writer=webpBoundedWrite;pic.p.custom_ptr=&data;
    if(!WebPPictureImportRGBA(&pic.p,rgba.data(),static_cast<int>(v.width*4))||!WebPEncode(&cfg,&pic.p)||data.empty())return false;
    out.swap(data);return true;
}
inline bool webpDecodePixels(const RasterInfo &v,const uint8_t *src,size_t size,bool lossless,Bytes &out){
    auto n=imagePixels(v.width,v.height);
    if(!n||size<12||src[0]!='R'||src[1]!='I'||src[2]!='F'||src[3]!='F'||
       src[8]!='W'||src[9]!='E'||src[10]!='B'||src[11]!='P')return false;
    uint64_t riff=0;for(unsigned i=0;i<4;++i)riff|=static_cast<uint64_t>(src[4+i])<<(8*i);
    if(riff+8!=size)return false;
    WebPBitstreamFeatures f{};
    if(WebPGetFeatures(src,size,&f)!=VP8_STATUS_OK||f.width!=static_cast<int>(v.width)||
       f.height!=static_cast<int>(v.height)||f.has_animation||f.format!=(lossless?2:1))return false;
    Bytes data(static_cast<size_t>(4*n));
    if(!WebPDecodeRGBAInto(src,size,data.data(),data.size(),static_cast<int>(v.width*4)))return false;
    out.swap(data);return true;
}
// CKJP and CKWP use the same envelope layout; mode 0=lossy, 1=WebP lossless.
inline bool encodeRasterPhoto(const Header &h,const RasterInfo &v,const Bytes &rgba,int quality,bool webp,bool lossless,Bytes &out){
    if(h.type!=1||!valid(h)||h.stream==0xffffffffu||(!webp&&lossless))return false;
    Bytes blob;
    if(!(webp?webpEncodePixels(v,rgba,quality,lossless,blob):jpegEncodePixels(v,rgba,quality,blob)))return false;
    Bytes p(rasterPhotoHeader+blob.size());p[0]='C';p[1]='K';p[2]=webp?'W':'J';p[3]='P';
    p[4]=1;p[5]=1;p[6]=lossless?1:0;
    put(p,8,v.width,4);put(p,12,v.height,4);
    std::copy(blob.begin(),blob.end(),p.begin()+rasterPhotoHeader);return encode(h,p,out);
}
inline bool decodeRasterPhoto(const Bytes &wire,Header &h,RasterInfo &v,bool webp,bool &lossless,Bytes &rgba){
    Header c;Bytes p;
    if(!decode(wire,c,p)||c.type!=1||c.stream==0xffffffffu||p.size()<=rasterPhotoHeader||
       p[0]!='C'||p[1]!='K'||p[2]!=(webp?'W':'J')||p[3]!='P'||p[4]!=1||p[5]!=1||p[6]>(webp?1:0)||p[7])return false;
    RasterInfo info{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4))};
    if(!imagePixels(info.width,info.height))return false;
    bool mode=p[6]!=0;Bytes pixels;
    const auto *src=p.data()+rasterPhotoHeader;auto size=p.size()-rasterPhotoHeader;
    if(!(webp?webpDecodePixels(info,src,size,mode,pixels):jpegDecodePixels(info,src,size,pixels)))return false;
    h=c;v=info;lossless=mode;rgba.swap(pixels);return true;
}
}
