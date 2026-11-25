#pragma once
#include "MediaTextFragment.h"
#include <deque>
#include <utility>

namespace mediawire {
// Caller supplies monotonic playback time in the same microsecond domain as CKMP.
class TextBuffer {
    struct Message { uint64_t time; Bytes bytes; };
    std::deque<Message> queue_;
    TextAssembler assembler_;
    uint64_t stream_=0,next_=0,last_=0,clock_=0,delay_=0;
    size_t capacity_=0,budget_=0,queued_=0,reserved_=0;
public:
    bool configure(uint64_t stream,uint64_t first,uint64_t delay,uint64_t capacity,uint64_t budget){
        if(!stream||stream>=0xffffffffu||first>0xffffffffu||delay>0x7fffffffffffffffull||
           !capacity||capacity>256||!budget||budget>64ull*1024*1024)return false;
        clear();stream_=stream;next_=first;delay_=delay;capacity_=capacity;budget_=budget;return true;
    }
    void clear(){queue_.clear();assembler_.reset();stream_=next_=last_=clock_=delay_=0;capacity_=budget_=queued_=reserved_=0;}
    size_t available()const{return queue_.size();}
    size_t bufferedBytes()const{return queued_;}
    size_t reservedBytes()const{return reserved_;}
    uint64_t nextSequence()const{return next_;}
    uint64_t timestamp()const{return queue_.empty()?0:queue_.front().time;}
    bool advance(uint64_t now){if(!stream_||now>0x7fffffffffffffffull||now<clock_)return false;clock_=now;return true;}
    bool ready()const{return !queue_.empty()&&clock_>=queue_.front().time&&clock_-queue_.front().time>=delay_;}
    const Bytes &front()const{return queue_.front().bytes;}
    bool pop(){if(!ready())return false;queued_-=queue_.front().bytes.size();queue_.pop_front();return true;}
    bool push(const Bytes &wire){
        if(!stream_)return false;
        Header h;Bytes d;uint32_t total=0,offset=0,first=0;
        bool plain=decodeText(wire,h,d);
        if(!plain&&!decodeTextFragment(wire,h,total,offset,first,d))return false;
        if(h.stream!=stream_||h.sequence!=next_||h.timestamp<last_||
           h.timestamp>0x7fffffffffffffffull-delay_)return false;
        if(plain){
            if(reserved_||queue_.size()>=capacity_||d.size()>budget_-queued_)return false;
            queue_.push_back({h.timestamp,std::move(d)});queued_+=queue_.back().bytes.size();
        }else{
            if(!reserved_&&(offset||queue_.size()>=capacity_||total>budget_-queued_))return false;
            // Allocate queue slot before mutating assembler on the final fragment.
            bool final=offset+d.size()==total;
            if(final)queue_.push_back({h.timestamp,{}});
            int status=0;
            try{status=assembler_.push(wire);}catch(...){if(final)queue_.pop_back();throw;}
            if(!status){if(final)queue_.pop_back();return false;}
            if(status==2){
                assembler_.take(queue_.back().bytes);
                queued_+=queue_.back().bytes.size();reserved_=0;
            }else reserved_=total;
        }
        last_=h.timestamp;++next_;return true;
    }
};
}
