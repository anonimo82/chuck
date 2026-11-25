#pragma once
#include "MediaFrame.h"

namespace mediawire {
// CKMI v1: one complete MIDI 1.0 channel voice message per packet.
constexpr size_t midiPayloadSize=8;
inline bool midiMessage(uint64_t status,uint64_t d1,uint64_t d2) {
    if(status<0x80||status>0xef||d1>127||d2>127) return false;
    auto kind=status&0xf0;
    return (kind!=0xc0&&kind!=0xd0)||d2==0;
}
inline bool encodeMidi(const Header &h,uint64_t status,uint64_t d1,uint64_t d2,Bytes &out) {
    if(h.type!=5||!valid(h)||h.stream==0xffffffffu||!midiMessage(status,d1,d2)) return false;
    Bytes data(midiPayloadSize);
    data[0]='C'; data[1]='K'; data[2]='M'; data[3]='I'; data[4]=1;
    data[5]=static_cast<uint8_t>(status); data[6]=static_cast<uint8_t>(d1); data[7]=static_cast<uint8_t>(d2);
    return encode(h,data,out);
}
inline bool decodeMidi(const Bytes &wire,Header &h,Bytes &message) {
    Header candidate; Bytes data;
    if(!decode(wire,candidate,data)||candidate.type!=5||candidate.stream==0xffffffffu||
       data.size()!=midiPayloadSize||data[0]!='C'||data[1]!='K'||data[2]!='M'||data[3]!='I'||
       data[4]!=1||!midiMessage(data[5],data[6],data[7])) return false;
    Bytes result(data.begin()+5,data.end());
    h=candidate; message.swap(result); return true;
}
class MidiStream {
    uint32_t stream_=0;
    uint64_t sequence_=0, timestamp_=0, events_=0;
    int error_=0;
    bool advance(uint64_t timestamp) {
        if(sequence_>0xffffffffu) return reject(5);
        if(timestamp<timestamp_||timestamp>0x7fffffffffffffffull) return reject(4);
        ++sequence_; ++events_; timestamp_=timestamp; error_=0; return true;
    }
public:
    bool reject(int e) { error_=e; return false; }
    bool configure(uint64_t stream,uint64_t sequence,uint64_t origin) {
        if(!stream||stream>=0xffffffffu||sequence>0xffffffffu||origin>0x7fffffffffffffffull) return false;
        stream_=static_cast<uint32_t>(stream); sequence_=sequence; timestamp_=origin; events_=0; error_=0; return true;
    }
    void clear() { *this=MidiStream{}; }
    bool configured() const { return stream_!=0; }
    uint64_t nextSequence() const { return sequence_; }
    uint64_t lastTimestamp() const { return timestamp_; }
    uint64_t events() const { return events_; }
    int error() const { return error_; }
    bool encodeEvent(uint64_t timestamp,uint64_t status,uint64_t d1,uint64_t d2,Bytes &out) {
        if(!configured()) return reject(1);
        if(sequence_>0xffffffffu) return reject(5);
        if(timestamp<timestamp_||timestamp>0x7fffffffffffffffull) return reject(4);
        Bytes wire;
        if(!encodeMidi({5,stream_,static_cast<uint32_t>(sequence_),timestamp},status,d1,d2,wire)) return reject(2);
        if(!advance(timestamp)) return false;
        out.swap(wire); return true;
    }
    bool decodeEvent(const Bytes &wire,Header &h,Bytes &message) {
        if(!configured()) return reject(1);
        Header candidate; Bytes data;
        if(!decodeMidi(wire,candidate,data)) return reject(2);
        if(sequence_>0xffffffffu) return reject(5);
        if(candidate.stream!=stream_||candidate.sequence!=sequence_) return reject(3);
        if(!advance(candidate.timestamp)) return false;
        h=candidate; message.swap(data); return true;
    }
};
}
