#pragma once
#include "MediaFrame.h"
#include <string>

namespace mediawire {
// CKVG v1 carries UTF-8 SVG source, not a parsed or sanitized document.
constexpr size_t vectorHeader=12;
inline size_t vectorPacketSize(uint64_t n) {
    return n&&n<=maxPayload-vectorHeader?headerSize+vectorHeader+static_cast<size_t>(n):0;
}
inline bool vectorUtf8(const Bytes &s) {
    for(size_t i=0;i<s.size();) {
        uint32_t c=s[i++],v=c; unsigned more=0;
        if(c<128) { if(c<32&&c!=9&&c!=10&&c!=13) return false; continue; }
        if(c>=0xc2&&c<=0xdf) { more=1;v=c&31; }
        else if(c>=0xe0&&c<=0xef) { more=2;v=c&15; }
        else if(c>=0xf0&&c<=0xf4) { more=3;v=c&7; }
        else return false;
        if(more>s.size()-i) return false;
        unsigned n=more;
        while(more--) { c=s[i++];if((c&192)!=128)return false;v=(v<<6)|(c&63); }
        if((n==2&&v<0x800)||(n==3&&v<0x10000)||v>0x10ffff||(v>=0xd800&&v<=0xdfff)||v==0xfffe||v==0xffff) return false;
    }
    return !s.empty();
}
inline bool encodeVector(const Header &h,const Bytes &svg,Bytes &out) {
    auto n=vectorPacketSize(svg.size());
    if(h.type!=2||!valid(h)||h.stream==0xffffffffu||!n||!vectorUtf8(svg)) return false;
    Bytes p(n-headerSize);
    p[0]='C';p[1]='K';p[2]='V';p[3]='G';p[4]=1;p[5]=1;
    put(p,8,svg.size(),4);
    for(size_t i=0;i<svg.size();++i)p[vectorHeader+i]=svg[i];
    return encode(h,p,out);
}
inline bool decodeVector(const Bytes &wire,Header &h,Bytes &svg) {
    Header candidate;Bytes p;
    if(!decode(wire,candidate,p)||candidate.type!=2||candidate.stream==0xffffffffu||
       p.size()<vectorHeader||p[0]!='C'||p[1]!='K'||p[2]!='V'||p[3]!='G'||
       p[4]!=1||p[5]!=1||p[6]||p[7]||get(p,8,4)!=p.size()-vectorHeader) return false;
    Bytes data(p.begin()+vectorHeader,p.end());
    if(!vectorPacketSize(data.size())||!vectorUtf8(data))return false;
    h=candidate;svg.swap(data);return true;
}
}
