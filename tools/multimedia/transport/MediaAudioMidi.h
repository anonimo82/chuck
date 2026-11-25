#pragma once
#include "MediaPCMBuffer.h"
#include "MediaMIDI.h"
#include <array>
#include <deque>

namespace mediawire {
// Caller-paced common media clock. MIDI events are released at audio boundaries.
class AudioMidi {
    PcmBuffer audio_;
    MidiStream midi_;
    std::deque<std::array<uint64_t,6>> queue_;
    size_t capacity_=0;
    int error_=0;
public:
    bool reject(int e) { error_=e; return false; }
    bool configure(uint64_t audioStream,uint64_t midiStream,uint64_t rate,uint64_t channels,
                   uint64_t origin,uint64_t audioSeq,uint64_t midiSeq,uint64_t frames,
                   uint64_t threshold,uint64_t events) {
        if(audioStream==midiStream||!events||events>65536) return false;
        PcmBuffer a; MidiStream m;
        if(!a.configure(audioStream,rate,channels,origin,audioSeq,frames,threshold)||
           !m.configure(midiStream,midiSeq,origin)) return false;
        audio_=std::move(a); midi_=m; queue_.clear(); capacity_=events; error_=0; return true;
    }
    void clear() { *this=AudioMidi{}; }
    bool configured() const { return audio_.configured(); }
    bool ready() const { return audio_.ready(); }
    size_t available() const { return audio_.available(); }
    size_t pendingMidi() const { return queue_.size(); }
    uint32_t channels() const { return audio_.channels(); }
    uint64_t playTimestamp() const { return audio_.playTimestamp(); }
    uint64_t consumedFrames() const { return audio_.consumedFrames(); }
    int error() const { return error_; }
    int audioError() const { return audio_.error(); }
    bool midiDue() const { return ready()&&!queue_.empty()&&queue_.front()[2]<=playTimestamp(); }
    bool push(const Bytes &wire) {
        if(!configured()) return reject(1);
        Header h; Bytes payload;
        if(!decode(wire,h,payload)) return reject(2);
        if(h.type==4) {
            if(!audio_.push(wire)) return reject(3);
        } else if(h.type==5) {
            MidiStream candidate=midi_; Bytes msg;
            if(!candidate.decodeEvent(wire,h,msg)) return reject(4);
            if(h.timestamp<playTimestamp()) return reject(5);
            if(queue_.size()>=capacity_) return reject(6);
            queue_.push_back({h.stream,h.sequence,h.timestamp,msg[0],msg[1],msg[2]});
            midi_=candidate;
        } else return reject(2);
        error_=0; return true;
    }
    bool pullAudio(size_t frames,std::vector<double> &out) {
        if(!configured()) return reject(1);
        if(midiDue()) return reject(7);
        if(!audio_.pull(frames,out)) return reject(3);
        error_=0; return true;
    }
    bool popMidi(std::array<uint64_t,6> &event) {
        if(!configured()) return reject(1);
        if(!midiDue()) return reject(8);
        event=queue_.front(); queue_.pop_front(); error_=0; return true;
    }
};
}
