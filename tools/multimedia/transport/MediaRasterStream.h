#pragma once
#include "MediaRaster.h"
#include <algorithm>

namespace mediawire {
// CKRF v1: contiguous pixel fragments of sequential RGBA8 images.
constexpr size_t fragmentHeader=32;
constexpr uint64_t maxImagePixels=4194304; // 16 MiB RGBA
inline uint64_t imagePixels(uint64_t w,uint64_t h) {
    return w&&h&&w<=maxImagePixels&&h<=maxImagePixels/w?w*h:0;
}
inline size_t fragmentPacketSize(uint64_t count) {
    return count&&count<=(maxPayload-fragmentHeader)/4?headerSize+fragmentHeader+4*count:0;
}
struct Fragment { uint32_t width=0,height=0,image=0,offset=0,count=0; };
inline bool validFragment(const Fragment &f) {
    auto n=imagePixels(f.width,f.height);
    return n&&f.offset<n&&f.count<=n-f.offset&&fragmentPacketSize(f.count);
}
inline bool encodeFragment(const Header &h,const Fragment &f,const Bytes &pixels,Bytes &out) {
    if(h.type!=1||!valid(h)||h.stream==0xffffffffu||!validFragment(f)||pixels.size()!=4ull*f.count) return false;
    Bytes p(fragmentHeader+pixels.size());
    p[0]='C';p[1]='K';p[2]='R';p[3]='F';p[4]=1;p[5]=1;
    put(p,8,f.width,4);put(p,12,f.height,4);put(p,16,f.image,4);
    put(p,20,f.offset,4);put(p,24,f.count,4);
    std::copy(pixels.begin(),pixels.end(),p.begin()+fragmentHeader);
    return encode(h,p,out);
}
inline bool decodeFragment(const Bytes &wire,Header &h,Fragment &f,Bytes &pixels) {
    Header candidate; Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=1||candidate.stream==0xffffffffu||
       p.size()<fragmentHeader||p[0]!='C'||p[1]!='K'||p[2]!='R'||p[3]!='F'||
       p[4]!=1||p[5]!=1||p[6]||p[7]||get(p,28,4)) return false;
    Fragment v{static_cast<uint32_t>(get(p,8,4)),static_cast<uint32_t>(get(p,12,4)),
       static_cast<uint32_t>(get(p,16,4)),static_cast<uint32_t>(get(p,20,4)),static_cast<uint32_t>(get(p,24,4))};
    if(!validFragment(v)||p.size()!=fragmentHeader+4ull*v.count) return false;
    Bytes data(p.begin()+fragmentHeader,p.end());
    h=candidate;f=v;pixels.swap(data);return true;
}
class RasterStream {
    uint32_t stream_=0,w_=0,h_=0;
    uint64_t seq_=0,image_=0,offset_=0,timestamp_=0,completed_=0;
    bool ready_=false;
    Bytes pixels_;
    int error_=0;
public:
    bool reject(int e) { error_=e;return false; }
    bool configure(uint64_t stream,uint64_t sequence,uint64_t image,uint64_t origin) {
        if(!stream||stream>=0xffffffffu||sequence>0xffffffffu||image>0xffffffffu||origin>0x7fffffffffffffffull) return false;
        clear();stream_=stream;seq_=sequence;image_=image;timestamp_=origin;return true;
    }
    void clear() { *this=RasterStream{}; }
    bool configured() const { return stream_!=0; }
    bool ready() const { return ready_; }
    uint64_t nextSequence() const { return seq_; }
    uint64_t nextImage() const { return image_; }
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
        if(seq_>0xffffffffu||image_>0xffffffffu) return reject(6);
        Header h;Fragment f;Bytes data;
        if(!decodeFragment(wire,h,f,data)) return reject(2);
        if(h.stream!=stream_||h.sequence!=seq_||f.image!=image_||f.offset!=offset_) return reject(3);
        if(h.timestamp<timestamp_||(offset_&&(h.timestamp!=timestamp_||f.width!=w_||f.height!=h_))) return reject(4);
        auto total=imagePixels(f.width,f.height);
        // Do not accept a fragment that cannot be completed before sequence exhaustion.
        uint64_t remaining=total-f.offset-f.count;
        constexpr uint64_t maxChunk=(maxPayload-fragmentHeader)/4;
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
        out.swap(pixels_);pixels_.clear();ready_=false;offset_=0;++image_;error_=0;return true;
    }
};
}
