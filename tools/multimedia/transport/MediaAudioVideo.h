#pragma once
#include "MediaPCMBuffer.h"
#include "MediaVideoBuffer.h"

namespace mediawire {
// Caller-paced synchronization: audio sample consumption is the master clock.
// Startup requires both prebuffers. Later video starvation does not stop audio.
class AudioVideo {
    PcmBuffer audio_;
    VideoBuffer video_;
    uint64_t origin_=0, rate_=0;
    bool started_=false;
    int error_=0;
    void arm() { if(audio_.ready()&&video_.ready()) started_=true; }
    uint64_t at(uint64_t frames) const {
        return origin_+(frames/rate_)*1000000+(frames%rate_)*1000000/rate_;
    }
public:
    bool reject(int e) { error_=e; return false; }
    bool configure(uint64_t audioStream,uint64_t videoStream,uint64_t rate,uint64_t channels,
                   uint64_t origin,uint64_t audioSeq,uint64_t videoSeq,uint64_t firstFrame,
                   uint64_t audioCapacity,uint64_t audioThreshold,uint64_t videoCapacity,
                   uint64_t videoThreshold,uint64_t videoBudget) {
        if(audioStream==videoStream) return false;
        PcmBuffer a; VideoBuffer v;
        if(!a.configure(audioStream,rate,channels,origin,audioSeq,audioCapacity,audioThreshold)||
           !v.configure(videoStream,videoSeq,firstFrame,origin,videoCapacity,videoThreshold,videoBudget)) return false;
        audio_=std::move(a); video_=std::move(v); origin_=origin; rate_=rate;
        started_=false; error_=0; return true;
    }
    void clear() { *this=AudioVideo{}; }
    bool configured() const { return audio_.configured(); }
    bool started() const { return started_; }
    bool ready() const { return started_&&audio_.ready(); }
    bool videoDue() const { return ready()&&video_.ready()&&video_.available()&&video_.timestamp()<=playTimestamp(); }
    uint64_t playTimestamp() const { return audio_.playTimestamp(); }
    uint64_t consumedFrames() const { return audio_.consumedFrames(); }
    uint32_t channels() const { return audio_.channels(); }
    size_t availableAudio() const { return audio_.available(); }
    size_t pendingVideo() const { return video_.available(); }
    size_t videoBytes() const { return video_.pixelBytes(); }
    uint32_t videoWidth() const { return video_.width(); }
    uint32_t videoHeight() const { return video_.height(); }
    uint64_t videoTimestamp() const { return video_.timestamp(); }
    uint64_t videoIndex() const { return video_.frameIndex(); }
    uint64_t videoLateness() const { return videoDue()?playTimestamp()-videoTimestamp():0; }
    int error() const { return error_; }
    int audioError() const { return audio_.error(); }
    int videoError() const { return video_.error(); }
    // Maximum buffered audio frames before the next video presentation boundary.
    // A non-sample-aligned timestamp is reached at the first sample boundary after it.
    size_t audioLimit() const {
        if(!ready()||videoDue()) return 0;
        size_t n=audio_.available();
        if(!n||!video_.available()||!video_.ready()) return n;
        auto consumed=audio_.consumedFrames(), target=video_.timestamp();
        if(at(consumed+n)<target) return n;
        size_t lo=1,hi=n;
        while(lo<hi) { size_t mid=lo+(hi-lo)/2; if(at(consumed+mid)>=target) hi=mid; else lo=mid+1; }
        return lo;
    }
    bool push(const Bytes &wire) {
        if(!configured()) return reject(1);
        Header h; Bytes data;
        if(!decode(wire,h,data)) return reject(2);
        if(h.type==4) { if(!audio_.push(wire)) return reject(3); }
        else if(h.type==6) { if(!video_.push(wire)) return reject(4); }
        else return reject(2);
        arm(); error_=0; return true;
    }
    bool pullAudio(size_t count,std::vector<double> &out) {
        if(!configured()) return reject(1);
        if(!count||count>audio_.capacity()) return reject(2);
        if(!started_) return reject(5);
        if(videoDue()) return reject(6);
        if(audio_.ready()&&video_.available()&&video_.ready()) {
            auto limit=audioLimit();
            if(count>limit && limit && at(audio_.consumedFrames()+limit)>=video_.timestamp())
                return reject(6);
        }
        if(!audio_.pull(count,out)) return reject(3);
        error_=0; return true;
    }
    bool pullVideo(size_t size,Bytes &out) {
        if(!configured()) return reject(1);
        if(!videoDue()) return reject(7);
        if(!video_.pull(size,out)) return reject(4);
        error_=0; return true;
    }
};
}
