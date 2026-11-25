#pragma once
#include "MediaFrame.h"
#include <string>

namespace mediawire {
// CKTX v1 carries nonempty plain UTF-8 text, no normalization or markup parsing.
constexpr size_t textHeader=12;
inline size_t textPacketSize(uint64_t n) {
    return n&&n<=maxPayload-textHeader?headerSize+textHeader+static_cast<size_t>(n):0;
}
inline bool textUtf8(const Bytes &s) {
    for(size_t i=0;i<s.size();) {
        uint32_t c=s[i++],v=c; unsigned more=0;
        if(c<128) { if(c==0) return false; continue; }
        if(c>=0xc2&&c<=0xdf) { more=1;v=c&31; }
        else if(c>=0xe0&&c<=0xef) { more=2;v=c&15; }
        else if(c>=0xf0&&c<=0xf4) { more=3;v=c&7; }
        else return false;
        if(more>s.size()-i) return false;
        unsigned n=more;
        while(more--) { c=s[i++];if((c&192)!=128)return false;v=(v<<6)|(c&63); }
        if((n==2&&v<0x800)||(n==3&&v<0x10000)||v>0x10ffff||(v>=0xd800&&v<=0xdfff)) return false;
    }
    return !s.empty();
}
inline bool encodeText(const Header &h,const Bytes &text,Bytes &out) {
    auto n=textPacketSize(text.size());
    if(h.type!=7||!valid(h)||h.stream==0xffffffffu||!n||!textUtf8(text)) return false;
    Bytes p(n-headerSize);
    p[0]='C';p[1]='K';p[2]='T';p[3]='X';p[4]=1;p[5]=1;
    put(p,8,text.size(),4);
    for(size_t i=0;i<text.size();++i)p[textHeader+i]=text[i];
    return encode(h,p,out);
}
inline bool decodeText(const Bytes &wire,Header &h,Bytes &text) {
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=7||candidate.stream==0xffffffffu||
       p.size()<textHeader||p[0]!='C'||p[1]!='K'||p[2]!='T'||p[3]!='X'||
       p[4]!=1||p[5]!=1||p[6]||p[7]||get(p,8,4)!=p.size()-textHeader) return false;
    Bytes data(p.begin()+textHeader,p.end());
    if(!textPacketSize(data.size())||!textUtf8(data))return false;
    h=candidate;text.swap(data);return true;
}
}
