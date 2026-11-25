#pragma once
#include "MediaVideoStream.h"
#include <utility>

namespace mediawire {
// Caller-paced FIFO of completed CKVS frames. One slot is reserved for
// an in-progress frame; the byte budget includes that entire frame.
class VideoBuffer {
    struct Frame { Bytes pixels; uint64_t index=0, timestamp=0; uint32_t width=0,height=0; };
    VideoStream stream_;
    std::vector<Frame> ring_;
    size_t head_=0, used_=0, threshold_=0, budget_=0, queued_=0, reserved_=0;
    uint64_t consumed_=0,underruns_=0,overruns_=0;
    bool ready_=false;
    int error_=0;
    static void increment(uint64_t &n) { if(n<0x7fffffffffffffffull) ++n; }
public:
    bool reject(int e) { error_=e; return false; }
    bool configure(uint64_t stream,uint64_t seq,uint64_t frame,uint64_t origin,
                   uint64_t capacity,uint64_t threshold,uint64_t budget) {
        VideoStream candidate;
        if(!candidate.configure(stream,seq,frame,origin)||!capacity||capacity>256||
           !threshold||threshold>capacity||budget<4||budget>64ull*1024*1024||
           threshold>budget/4) return false;
        std::vector<Frame> slots(static_cast<size_t>(capacity));
        stream_=std::move(candidate); ring_.swap(slots);
        head_=used_=queued_=reserved_=0; threshold_=threshold; budget_=budget;
        consumed_=underruns_=overruns_=0; ready_=false; error_=0; return true;
    }
    void clear() { *this=VideoBuffer{}; }
    bool configured() const { return stream_.configured(); }
    size_t capacity() const { return ring_.size(); }
    size_t threshold() const { return threshold_; }
    size_t available() const { return used_; }
    size_t freeFrames() const { return capacity()-used_-(reserved_?1:0); }
    size_t budget() const { return budget_; }
    size_t bufferedBytes() const { return queued_; }
    size_t reservedBytes() const { return reserved_; }
    bool ready() const { return ready_; }
    uint64_t receivedFrames() const { return stream_.completed(); }
    uint64_t consumedFrames() const { return consumed_; }
    uint64_t nextSequence() const { return stream_.nextSequence(); }
    uint64_t nextFrame() const { return stream_.nextFrame(); }
    uint64_t underruns() const { return underruns_; }
    uint64_t overruns() const { return overruns_; }
    int error() const { return error_; }
    size_t pixelBytes() const { return used_?ring_[head_].pixels.size():0; }
    uint32_t width() const { return used_?ring_[head_].width:0; }
    uint32_t height() const { return used_?ring_[head_].height:0; }
    uint64_t timestamp() const { return used_?ring_[head_].timestamp:0; }
    uint64_t frameIndex() const { return used_?ring_[head_].index:0; }
    bool adaptive(uint64_t budget){return stream_.adaptive(budget);}
    bool push(const Bytes &wire) {
        if(!configured()) return reject(1);
        Header h; VideoFragment f; Bytes data;
        Bytes raw;
        if(!decodeVideoFragment(wire,h,f,data)){
            if(!stream_.normalize(wire,raw)||!decodeVideoFragment(raw,h,f,data))return reject(2);
        }
        size_t bytes=static_cast<size_t>(videoPixels(f.width,f.height)*4);
        if(!reserved_) {
            if(bytes>budget_) return reject(2);
            if(!freeFrames()||bytes>budget_-queued_) { increment(overruns_); return reject(7); }
        }
        if(!stream_.push(raw.empty()?wire:raw)) return reject(stream_.error());
        if(!reserved_) reserved_=bytes;
        if(stream_.ready()) {
            auto &slot=ring_[(head_+used_)%capacity()];
            slot.index=stream_.nextFrame(); slot.timestamp=stream_.timestamp();
            slot.width=stream_.width(); slot.height=stream_.height();
            // take is guaranteed by the ready flag and exact reserved size.
            stream_.take(reserved_,slot.pixels);
            queued_+=reserved_; reserved_=0; ++used_;
            if(used_>=threshold_) ready_=true;
        }
        error_=0; return true;
    }
    bool pull(size_t size,Bytes &out) {
        if(!configured()) return reject(1);
        if(!ready_) return reject(8);
        if(!used_) { ready_=false; increment(underruns_); return reject(9); }
        if(size!=pixelBytes()) return reject(2);
        auto &slot=ring_[head_];
        queued_-=slot.pixels.size(); out.swap(slot.pixels); slot=Frame{};
        head_=(head_+1)%capacity(); --used_; increment(consumed_);
        error_=0; return true;
    }
};
}
