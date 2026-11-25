#pragma once
#include "MediaPCM.h"

namespace mediawire {
// Strict contiguous sample timeline; one object per direction and stream.
class PcmStream {
    uint32_t stream_=0, rate_=0, channels_=0;
    uint64_t origin_=0, frames_=0, seq_=0, blocks_=0;
    int error_=0;
    static constexpr uint64_t cap=0x7fffffffffffffffull;
    bool timestampAt(uint64_t frames,uint64_t &ts) const {
        if(!rate_) return false;
        uint64_t seconds=frames/rate_, remainder=frames%rate_;
        if(seconds>(cap-origin_)/1000000) return false;
        uint64_t whole=seconds*1000000, fraction=remainder*1000000/rate_;
        if(fraction>cap-origin_-whole) return false;
        ts=origin_+whole+fraction; return true;
    }
    bool canAdvance(uint64_t count) const {
        uint64_t ts;
        return seq_<=0xffffffffu && count<=cap-frames_ && timestampAt(frames_+count,ts);
    }
    void advance(uint64_t count) { frames_+=count; ++seq_; ++blocks_; error_=0; }
public:
    bool configure(uint64_t stream,uint64_t rate,uint64_t channels,uint64_t origin,uint64_t seq) {
        if(!stream||stream>=0xffffffffu||!pcmPacketSize(rate,channels,1)||origin>cap||seq>0xffffffffu) return false;
        stream_=static_cast<uint32_t>(stream); rate_=static_cast<uint32_t>(rate);
        channels_=static_cast<uint32_t>(channels); origin_=origin; seq_=seq;
        frames_=blocks_=0; error_=0; return true;
    }
    void clear() { *this=PcmStream{}; }
    bool configured() const { return stream_!=0; }
    uint64_t frames() const { return frames_; }
    uint64_t blocks() const { return blocks_; }
    uint64_t nextSequence() const { return seq_; }
    uint64_t nextTimestamp() const { uint64_t ts=0; timestampAt(frames_,ts); return ts; }
    int error() const { return error_; }
    uint32_t rate() const { return rate_; }
    uint32_t channels() const { return channels_; }
    size_t packetSize(uint64_t frames) const { return pcmPacketSize(rate_,channels_,frames); }
    bool reject(int code) { error_=code; return false; }
    bool encodeBlock(const std::vector<double> &samples,Bytes &wire) {
        if(!configured()) return reject(1);
        if(samples.empty()||samples.size()%channels_) return reject(2);
        uint64_t count=samples.size()/channels_;
        if(!packetSize(count)) return reject(2);
        if(!canAdvance(count)) return reject(6);
        Header h{4,stream_,static_cast<uint32_t>(seq_),nextTimestamp()};
        PcmInfo p{rate_,channels_,static_cast<uint32_t>(count)};
        if(!encodePcm(h,p,samples,wire)) return reject(2);
        advance(count); return true;
    }
    bool decodeBlock(const Bytes &wire,size_t expectedSamples,std::vector<double> &out) {
        if(!configured()) return reject(1);
        Header h; PcmInfo p; std::vector<double> data;
        if(!decodePcm(wire,h,p,data)) return reject(2);
        if(h.stream!=stream_||p.rate!=rate_||p.channels!=channels_) return reject(3);
        if(h.sequence!=seq_) return reject(4);
        if(h.timestamp!=nextTimestamp()) return reject(5);
        if(data.size()!=expectedSamples) return reject(2);
        if(!canAdvance(p.frames)) return reject(6);
        out.swap(data); advance(p.frames); return true;
    }
};
}
