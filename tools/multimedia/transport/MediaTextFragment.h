#pragma once
#include "MediaText.h"

namespace mediawire {
// CKTX v2: total message bytes, byte offset, first sequence; same stream/time throughout.
constexpr size_t textFragmentHeader=20, maxTextMessage=16*1024*1024;
inline size_t textFragmentPacketSize(uint64_t n) {
    return n && n<=maxPayload-textFragmentHeader ? headerSize+textFragmentHeader+static_cast<size_t>(n):0;
}
inline bool fragmentFields(const Header &h,uint64_t total,uint64_t offset,uint64_t first,size_t n) {
    return valid(h)&&h.type==7&&h.stream!=0xffffffffu&&textFragmentPacketSize(n)&&
        total<=maxTextMessage&&offset<total&&n<=total-offset&&first<=h.sequence&&
        ((offset==0)==(first==h.sequence));
}
inline bool encodeTextFragment(const Header &h,uint32_t total,uint32_t offset,uint32_t first,const Bytes &data,Bytes &out) {
    if(!fragmentFields(h,total,offset,first,data.size()))return false;
    for(auto c:data)if(!c)return false;
    Bytes p(textFragmentHeader+data.size());
    p[0]='C';p[1]='K';p[2]='T';p[3]='X';p[4]=2;p[5]=1;
    put(p,8,total,4);put(p,12,offset,4);put(p,16,first,4);
    for(size_t i=0;i<data.size();++i)p[textFragmentHeader+i]=data[i];
    return encode(h,p,out);
}
inline bool decodeTextFragment(const Bytes &wire,Header &h,uint32_t &total,uint32_t &offset,uint32_t &first,Bytes &data) {
    Header c;Bytes p;
    if(!decode(wire,c,p)||p.size()<=textFragmentHeader||p[0]!='C'||p[1]!='K'||p[2]!='T'||p[3]!='X'||
       p[4]!=2||p[5]!=1||p[6]||p[7])return false;
    auto t=get(p,8,4),o=get(p,12,4),f=get(p,16,4);
    if(!fragmentFields(c,t,o,f,p.size()-textFragmentHeader))return false;
    Bytes d(p.begin()+textFragmentHeader,p.end());
    for(auto v:d)if(!v)return false;
    h=c;total=t;offset=o;first=f;data.swap(d);return true;
}
class TextAssembler {
    Header h_{}; uint32_t total_=0,first_=0; Bytes data_;
public:
    void reset(){h_=Header{};total_=first_=0;Bytes().swap(data_);}
    size_t size()const{return data_.size();}
    bool ready()const{return total_&&data_.size()==total_;}
    const Bytes &data()const{return data_;}
    bool take(Bytes &out){if(!ready())return false;out.swap(data_);reset();return true;}
    // 0 rejected (state unchanged), 1 accepted, 2 complete. Explicit reset between messages.
    int push(const Bytes &wire){
        if(ready())return 0;
        Header h;uint32_t t,o,f;Bytes d;
        if(!decodeTextFragment(wire,h,t,o,f,d)||o!=data_.size())return 0;
        if(o&&(h.stream!=h_.stream||h.timestamp!=h_.timestamp||t!=total_||f!=first_||
               h_.sequence==0xffffffffu||h.sequence!=h_.sequence+1))return 0;
        if(o+d.size()==t){
            Bytes complete=data_;complete.insert(complete.end(),d.begin(),d.end());
            if(!textUtf8(complete))return 0;
            data_.swap(complete);
        }else data_.insert(data_.end(),d.begin(),d.end());
        h_=h;total_=t;first_=f;return ready()?2:1;
    }
};
}
