#pragma once
#include "MediaPCMStream.h"
#include <algorithm>
#include <utility>

namespace mediawire {
// Caller-clocked receiver. All operations belong to one control thread.
class PcmBuffer {
    PcmStream timeline_;
    std::vector<double> ring_;
    size_t capacity_=0, threshold_=0, head_=0, used_=0;
    uint64_t consumed_=0, underruns_=0, overruns_=0, origin_=0;
    bool ready_=false;
    int error_=0;
    static constexpr uint64_t cap=0x7fffffffffffffffull;
    static void increment(uint64_t &n) { if(n<cap) ++n; }
public:
    bool reject(int e) { error_=e; return false; }
    bool configure(uint64_t stream,uint64_t rate,uint64_t channels,uint64_t origin,
                   uint64_t seq,uint64_t capacity,uint64_t threshold) {
        PcmStream candidate;
        if(!candidate.configure(stream,rate,channels,origin,seq)||capacity<1||
           capacity>1048576/channels||threshold<1||threshold>capacity) return false;
        std::vector<double> data(static_cast<size_t>(capacity*channels));
        timeline_=candidate; ring_.swap(data); capacity_=capacity; threshold_=threshold;
        head_=used_=0; consumed_=underruns_=overruns_=0; origin_=origin; ready_=false; error_=0;
        return true;
    }
    void clear() { *this=PcmBuffer{}; }
    bool configured() const { return timeline_.configured(); }
    size_t capacity() const { return capacity_; }
    size_t threshold() const { return threshold_; }
    size_t available() const { return used_; }
    size_t freeFrames() const { return capacity_-used_; }
    bool ready() const { return ready_; }
    uint64_t receivedFrames() const { return timeline_.frames(); }
    uint64_t consumedFrames() const { return consumed_; }
    uint64_t nextSequence() const { return timeline_.nextSequence(); }
    uint64_t nextTimestamp() const { return timeline_.nextTimestamp(); }
    uint64_t playTimestamp() const {
        if(!configured()) return 0;
        return origin_+(consumed_/timeline_.rate())*1000000+
               (consumed_%timeline_.rate())*1000000/timeline_.rate();
    }
    uint64_t bufferedUs() const { return configured()?used_*1000000ull/timeline_.rate():0; }
    uint64_t underruns() const { return underruns_; }
    uint64_t overruns() const { return overruns_; }
    int error() const { return error_; }
    uint32_t channels() const { return timeline_.channels(); }
    bool push(const Bytes &wire) {
        if(!configured()) return reject(1);
        Header h; PcmInfo p; std::vector<double> data;
        if(!decodePcm(wire,h,p,data)) return reject(2);
        // Validate a copy, so overflow or invalid data never advances the receiver.
        PcmStream candidate=timeline_;
        if(!candidate.decodeBlock(wire,data.size(),data)) return reject(candidate.error());
        if(p.frames>freeFrames()) { increment(overruns_); return reject(7); }
        size_t tail=(head_+used_)%capacity_, ch=channels();
        for(size_t f=0;f<p.frames;++f)
            for(size_t c=0;c<ch;++c) ring_[((tail+f)%capacity_)*ch+c]=data[f*ch+c];
        timeline_=candidate; used_+=p.frames;
        if(used_>=threshold_) ready_=true;
        error_=0; return true;
    }
    bool pull(size_t count,std::vector<double> &out) {
        if(!configured()) return reject(1);
        if(!count||count>capacity_) return reject(2);
        if(!ready_) return reject(8);
        if(count>used_) { ready_=false; increment(underruns_); return reject(9); }
        size_t ch=channels();
        std::vector<double> data(count*ch);
        for(size_t f=0;f<count;++f)
            for(size_t c=0;c<ch;++c) data[f*ch+c]=ring_[((head_+f)%capacity_)*ch+c];
        head_=(head_+count)%capacity_; used_-=count; consumed_+=count;
        out.swap(data); error_=0; return true;
    }
};
}
