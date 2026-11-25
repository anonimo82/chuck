#pragma once
#include "MediaLoopback.h"
#include <chrono>
#include <utility>

namespace mediawire {
// The control plane reserves stream UINT32_MAX within MediaSession only.
constexpr uint32_t controlStream=0xffffffffu;
class Session {
    Loopback link_;
    int state_=0, error_=0; // 0 idle, 1 negotiating, 2 ready, 3 failed
    unsigned mask_=0, limit_=0, common_=0, negotiated_=0;
    bool hello_=false;
    std::chrono::steady_clock::time_point deadline_;
    void reset() { state_=error_=0; common_=negotiated_=0; hello_=false; }
    int fail(int e) { link_.disconnect(); common_=negotiated_=0; state_=3; error_=e; return -1; }
    bool begin() {
        try {
            Bytes data(12), wire;
            data[0]='C'; data[1]='K'; data[2]='H'; data[3]='S';
            data[4]=1; data[5]=1; data[6]=static_cast<uint8_t>(mask_);
            put(data,8,limit_,4);
            if(!encode({7,controlStream,0,0},data,wire)||!link_.enqueue(std::move(wire))) {
                fail(1); return false;
            }
            common_=negotiated_=0; hello_=false; error_=0; state_=1;
            deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            return true;
        } catch(...) { fail(1); return false; }
    }
    bool allowed(const Header &h,size_t n) const {
        return h.stream!=controlStream && (common_ & (1u<<(h.type-1))) && n<=negotiated_;
    }
public:
    Session() : mask_(127),limit_(maxPayload) {}
    bool configure(unsigned mask,unsigned limit) {
        if(link_.connected()||!mask||mask>127||!limit||limit>maxPayload) return false;
        mask_=mask; limit_=limit; return true;
    }
    int state() const { return state_; }
    int error() const { return error_; }
    unsigned mask() const { return common_; }
    unsigned limit() const { return negotiated_; }
    unsigned port() const { return link_.port(); }
    bool busy() const { return state_==2&&link_.busy(); }
    size_t packetSize() const { return state_==2?link_.packetSize():0; }
    bool listenOn(unsigned port) { if(!link_.listenOn(port)) return false; reset(); return true; }
    bool connectTo(unsigned port) { return link_.connectTo(port)&&begin(); }
    int acceptOne() { int r=link_.acceptOne(); return r==1?(begin()?1:-1):r; }
    void disconnect() { link_.disconnect(); reset(); }
    void stop() { link_.stop(); reset(); }
    bool enqueue(Bytes packet) {
        Header h; Bytes data;
        return state_==2&&!link_.busy()&&decode(packet,h,data)&&allowed(h,data.size())&&link_.enqueue(std::move(packet));
    }
    bool take(Bytes &packet) { return state_==2&&link_.take(packet); }
    int poll() {
        if(state_!=1&&state_!=2) return -1;
        try {
            if(state_==1&&std::chrono::steady_clock::now()>=deadline_) return fail(4);
            int r=link_.poll();
            if(r<0) return fail(1);
            if(state_==1) {
                if(!hello_&&link_.packetSize()) {
                    Bytes wire,data; Header h;
                    if(!link_.take(wire)||!decode(wire,h,data)||h.type!=7||h.stream!=controlStream||
                       h.sequence||h.timestamp||data.size()!=12||
                       data[0]!='C'||data[1]!='K'||data[2]!='H'||data[3]!='S'||
                       data[4]!=1||data[5]!=1||!data[6]||data[6]>127||data[7]||
                       !get(data,8,4)||get(data,8,4)>maxPayload) return fail(2);
                    common_=mask_&data[6]; negotiated_=std::min<unsigned>(limit_,static_cast<unsigned>(get(data,8,4)));
                    if(!common_) return fail(3);
                    hello_=true;
                }
                if(hello_&&!link_.busy()) { state_=2; return 1; }
            }
            if(state_==2&&link_.packetSize()) {
                // Validate before exposing the queued packet; no extra packet buffer.
                Header h; Bytes data;
                if(!decode(link_.front(),h,data)||!allowed(h,data.size())) return fail(5);
            }
            return r;
        } catch(...) { return fail(1); }
    }
};
}
