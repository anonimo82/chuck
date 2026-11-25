#pragma once
#include "MediaVideoFrame.h"
#include "MediaRasterCompressedFragment.h"
#include <algorithm>

namespace mediawire {
// CKVS v1: contiguous pixel fragments of sequential RGBA8 video frames.
constexpr size_t videoFragmentHeader=32;
constexpr uint64_t maxVideoPixels=16777216; // 64 MiB RGBA; includes UHD and DCI 4K
inline uint64_t videoPixels(uint64_t w,uint64_t h) {
    return w&&h&&w<=maxVideoPixels&&h<=maxVideoPixels/w?w*h:0;
}
inline size_t videoFragmentPacketSize(uint64_t count) {
    return count&&count<=(maxPayload-videoFragmentHeader)/4?headerSize+videoFragmentHeader+4*count:0;
}
struct VideoFragment { uint32_t width=0,height=0,frame=0,offset=0,count=0,codec=0; };
inline bool validVideoFragment(const VideoFragment &f) {
    auto n=videoPixels(f.width,f.height);
    return n&&f.offset<n&&f.count<=n-f.offset&&videoFragmentPacketSize(f.count);
}
inline bool encodeVideoFragment(const Header &h,const VideoFragment &f,const Bytes &pixels,Bytes &out) {
    if(h.type!=6||!valid(h)||h.stream==0xffffffffu||!validVideoFragment(f)||pixels.size()!=4ull*f.count) return false;
    Bytes p(videoFragmentHeader+pixels.size());
    p[0]='C';p[1]='K';p[2]='V';p[3]='S';p[4]=1;p[5]=1;
    put(p,8,f.width,4);put(p,12,f.height,4);put(p,16,f.frame,4);
    put(p,20,f.offset,4);put(p,24,f.count,4);
    std::copy(pixels.begin(),pixels.end(),p.begin()+videoFragmentHeader);
    return encode(h,p,out);
}
// CKVC v1: independent compressed horizontal strips, with the same pixel
// offset/count semantics as CKVS. Keep decoded strip count within CKVS bounds
// so sequence exhaustion preflight remains valid for either representation.
inline bool validVideoCompressed(const VideoFragment &f){
    return validVideoFragment(f)&&f.codec>=1&&f.codec<=5&&
           f.offset%f.width==0&&f.count%f.width==0;
}
inline bool encodeVideoCompressed(const Header &h,const VideoFragment &f,const Bytes &pixels,int quality,Bytes &out){
    if(h.type!=6||!valid(h)||h.stream==0xffffffffu||!validVideoCompressed(f)||
       pixels.size()!=4ull*f.count||quality<1||quality>100)return false;
    Header raster=h;raster.type=1;
    RasterInfo v{f.width,f.count/f.width};Bytes inner,p;
    bool ok=f.codec==1?encodeRasterCompressed(raster,v,pixels,inner):
        f.codec==2?encodeRasterPng(raster,v,pixels,inner):
        encodeRasterPhoto(raster,v,pixels,quality,f.codec>=4,f.codec==5,inner);
    if(!ok||inner.size()-headerSize>maxPayload-videoFragmentHeader)return false;
    p.resize(videoFragmentHeader+inner.size()-headerSize);
    p[0]='C';p[1]='K';p[2]='V';p[3]='C';p[4]=1;p[5]=static_cast<uint8_t>(f.codec);
    put(p,8,f.width,4);put(p,12,f.height,4);put(p,16,f.frame,4);
    put(p,20,f.offset,4);put(p,24,f.count,4);
    std::copy(inner.begin()+headerSize,inner.end(),p.begin()+videoFragmentHeader);
    return encode(h,p,out);
}
inline bool decodeVideoCompressed(const Header &h,const Bytes &p,VideoFragment &f,Bytes &pixels){
    if(h.type!=6||h.stream==0xffffffffu||p.size()<videoFragmentHeader+16||
       p[0]!='C'||p[1]!='K'||p[2]!='V'||p[3]!='C'||p[4]!=1||p[6]||p[7]||get(p,28,4))return false;
    VideoFragment v{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4)),
       static_cast<uint32_t>(get(p,16,4)),static_cast<uint32_t>(get(p,20,4)),static_cast<uint32_t>(get(p,24,4)),p[5]};
    if(!validVideoCompressed(v))return false;
    Bytes payload(p.begin()+videoFragmentHeader,p.end()),inner,data;
    Header raster=h;raster.type=1;RasterInfo info;bool mode=false;
    if(get(payload,8,4)!=v.width||get(payload,12,4)!=v.count/v.width||!encode(raster,payload,inner))return false;
    bool ok=v.codec==1?decodeRasterCompressed(inner,raster,info,data):
        v.codec==2?decodeRasterPng(inner,raster,info,data):
        decodeRasterPhoto(inner,raster,info,v.codec>=4,mode,data);
    if(!ok||info.width!=v.width||info.height!=v.count/v.width||data.size()!=4ull*v.count||
       (v.codec>=4&&mode!=(v.codec==5)))return false;
    f=v;pixels.swap(data);return true;
}
inline bool decodeVideoFragment(const Bytes &wire,Header &h,VideoFragment &f,Bytes &pixels) {
    Header candidate; Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=6||candidate.stream==0xffffffffu)return false;
    if(p.size()>=4&&p[0]=='C'&&p[1]=='K'&&p[2]=='V'&&p[3]=='C'){
        if(!decodeVideoCompressed(candidate,p,f,pixels))return false;
        h=candidate;return true;
    }
    if(p.size()<videoFragmentHeader||p[0]!='C'||p[1]!='K'||p[2]!='V'||p[3]!='S'||
       p[4]!=1||p[5]!=1||p[6]||p[7]||get(p,28,4)) return false;
    VideoFragment v{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4)),
       static_cast<uint32_t>(get(p,16,4)),static_cast<uint32_t>(get(p,20,4)),static_cast<uint32_t>(get(p,24,4))};
    if(!validVideoFragment(v)||p.size()!=videoFragmentHeader+4ull*v.count) return false;
    Bytes data(p.begin()+videoFragmentHeader,p.end());
    h=candidate;f=v;pixels.swap(data);return true;
}
class VideoStream {
    uint32_t stream_=0,w_=0,h_=0;
    uint64_t seq_=0,frame_=0,offset_=0,timestamp_=0,completed_=0;
    bool ready_=false;
    Bytes pixels_;
    int error_=0;
public:
    bool reject(int e) { error_=e;return false; }
    bool configure(uint64_t stream,uint64_t sequence,uint64_t frame,uint64_t origin) {
        if(!stream||stream>=0xffffffffu||sequence>0xffffffffu||frame>0xffffffffu||origin>0x7fffffffffffffffull) return false;
        clear();stream_=stream;seq_=sequence;frame_=frame;timestamp_=origin;return true;
    }
    void clear() { *this=VideoStream{}; }
    bool configured() const { return stream_!=0; }
    bool ready() const { return ready_; }
    uint64_t nextSequence() const { return seq_; }
    uint64_t nextFrame() const { return frame_; }
    uint64_t receivedPixels() const { return offset_; }
    uint64_t completed() const { return completed_; }
    uint64_t timestamp() const { return timestamp_; }
    uint32_t width() const { return w_; }
    uint32_t height() const { return h_; }
    size_t pixelBytes() const { return ready_?pixels_.size():0; }
    int error() const { return error_; }
    bool push(const Bytes &wire) {
        if(!configured()) return reject(1);
        if(ready_) return reject(5);
        if(seq_>0xffffffffu||frame_>0xffffffffu) return reject(6);
        Header h;VideoFragment f;Bytes data;
        if(!decodeVideoFragment(wire,h,f,data)) return reject(2);
        if(h.stream!=stream_||h.sequence!=seq_||f.frame!=frame_||f.offset!=offset_) return reject(3);
        if(h.timestamp<timestamp_||(!offset_&&completed_&&h.timestamp<=timestamp_)||(offset_&&(h.timestamp!=timestamp_||f.width!=w_||f.height!=h_))) return reject(4);
        auto total=videoPixels(f.width,f.height);
        // Do not accept a fragment that cannot be completed before sequence exhaustion.
        uint64_t remaining=total-f.offset-f.count;
        constexpr uint64_t maxChunk=(maxPayload-videoFragmentHeader)/4;
        if((remaining+maxChunk-1)/maxChunk>0xffffffffull-seq_) return reject(6);
        if(!offset_) {
            Bytes full(static_cast<size_t>(total*4));
            pixels_.swap(full);w_=f.width;h_=f.height;timestamp_=h.timestamp;
        }
        std::copy(data.begin(),data.end(),pixels_.begin()+4*offset_);
        offset_+=f.count;++seq_;
        if(offset_==total) { ready_=true;++completed_; }
        error_=0;return true;
    }
    bool take(size_t size,Bytes &out) {
        if(!configured()) return reject(1);
        if(!ready_||size!=pixels_.size()) return reject(5);
        out.swap(pixels_);pixels_.clear();ready_=false;offset_=0;++frame_;error_=0;return true;
    }
};
}
