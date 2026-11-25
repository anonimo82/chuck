#pragma once
#include "MediaRasterCompressedFragment.h"

namespace mediawire {
// Lossless automatic single-packet selection. Ties favor the earlier codec.
inline int chooseRasterPacket(const Header &h,const RasterInfo &v,const Bytes &pixels,uint64_t limit,Bytes &out){
    if(!limit||limit>maxPayload)return 0;
    Bytes best;int kind=0;
    for(int codec=0;codec<=3;++codec){
        Bytes candidate;
        bool ok=codec==0?encodeRaster(h,v,pixels,candidate):
            codec==1?encodeRasterCompressed(h,v,pixels,candidate):
            codec==2?encodeRasterPng(h,v,pixels,candidate):
            encodeRasterPhoto(h,v,pixels,75,true,true,candidate);
        if(ok&&candidate.size()-headerSize<=limit&&(best.empty()||candidate.size()<best.size())){
            best.swap(candidate);kind=codec+1;
        }
    }
    if(kind)out.swap(best);
    return kind;
}
// One image at a time. Explicit codec selection, caller supplies session limit.
// peek/commit separates packet generation from acceptance by the transport.
class RasterStripSender {
    Bytes pixels_,packet_;Header h_{};CompressedFragment f_{};
    size_t limit_=0;uint32_t quality_=0,pendingRows_=0;uint64_t next_=0;
public:
    void clear(){*this=RasterStripSender{};}
    bool configure(uint64_t stream,uint64_t sequence,uint64_t timestamp,uint64_t image,
                   uint64_t width,uint64_t height,uint64_t codec,uint64_t quality,uint64_t limit,const Bytes &pixels){
        auto n=imagePixels(width,height);
        if(!stream||stream>=0xffffffffu||sequence>0xffffffffu||timestamp>0x7fffffffffffffffull||
           image>0xffffffffu||!n||pixels.size()!=4*n||codec<1||codec>5||quality<1||quality>100||
           limit<=compressedFragmentHeader+16||limit>maxPayload)return false;
        if(codec==3)for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]!=255)return false;
        RasterStripSender candidate;
        candidate.pixels_=pixels;candidate.h_={1,static_cast<uint32_t>(stream),static_cast<uint32_t>(sequence),timestamp};
        candidate.f_={static_cast<uint32_t>(width),static_cast<uint32_t>(height),static_cast<uint32_t>(image),0,0,static_cast<uint32_t>(codec)};
        candidate.limit_=limit;candidate.quality_=quality;candidate.next_=sequence;
        if(!candidate.prepare())return false;
        *this=std::move(candidate);return true;
    }
    bool done()const{return limit_&&f_.row==f_.height;}
    uint32_t row()const{return f_.row;}
    uint64_t nextSequence()const{return next_;}
    size_t packetSize()const{return packet_.size();}
    const Bytes &packet()const{return packet_;}
    bool prepare(){
        if(!limit_||done()||next_>0xffffffffu)return false;
        if(!packet_.empty())return true;
        // Try a large strip, then halve its height until it fits.
        // No monotonic size assumption and no claim of optimal packing.
        uint32_t rows=f_.height-f_.row;
        while(rows){
            if(next_==0xffffffffu&&rows!=f_.height-f_.row)return false;
            CompressedFragment f=f_;f.rows=rows;
            Header h=h_;h.sequence=static_cast<uint32_t>(next_);
            auto first=pixels_.begin()+4ull*f.width*f.row;
            Bytes strip(first,first+4ull*f.width*rows),wire;
            if(encodeCompressedFragment(h,f,strip,quality_,wire)&&wire.size()-headerSize<=limit_){
                packet_.swap(wire);pendingRows_=rows;return true;
            }
            rows/=2;
        }
        return false;
    }
    bool commit(){
        if(packet_.empty())return false;
        f_.row+=pendingRows_;++next_;pendingRows_=0;packet_.clear();
        if(done())Bytes().swap(pixels_);
        return true;
    }
};
}
