#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace mediawire {
using Bytes = std::vector<uint8_t>;
constexpr size_t headerSize=32, maxPayload=1024*1024;
struct Header { uint32_t type=0, stream=0, sequence=0; uint64_t timestamp=0; };
inline void put(Bytes &b,size_t at,uint64_t v,size_t n) {
    for(size_t i=0;i<n;++i) { b[at+n-1-i]=static_cast<uint8_t>(v); v>>=8; }
}
inline uint64_t get(const Bytes &b,size_t at,size_t n) {
    uint64_t v=0; for(size_t i=0;i<n;++i) v=(v<<8)|b[at+i]; return v;
}
inline uint32_t checksum(const Bytes &b) {
    uint32_t c=0xffffffffu;
    for(size_t i=0;i<b.size();++i) {
        if(i>=28 && i<32) continue;
        c^=b[i];
        for(int j=0;j<8;++j) c=(c>>1) ^ (0xedb88320u & (0u-(c&1u)));
    }
    return c^0xffffffffu;
}
inline bool valid(const Header &h) {
    return h.type>=1 && h.type<=7 && h.stream!=0 && h.timestamp<=0x7fffffffffffffffull;
}
inline bool encode(const Header &h,const Bytes &payload,Bytes &out) {
    if(!valid(h)||payload.size()>maxPayload) return false;
    Bytes b(headerSize+payload.size());
    b[0]='C'; b[1]='K'; b[2]='M'; b[3]='P'; b[4]=1; b[5]=static_cast<uint8_t>(h.type);
    put(b,8,h.stream,4); put(b,12,h.sequence,4); put(b,16,h.timestamp,8);
    put(b,24,payload.size(),4);
    for(size_t i=0;i<payload.size();++i) b[headerSize+i]=payload[i];
    put(b,28,checksum(b),4);
    out.swap(b); return true;
}
inline bool decode(const Bytes &b,Header &h,Bytes &payload) {
    if(b.size()<headerSize || b.size()>headerSize+maxPayload ||
       b[0]!='C'||b[1]!='K'||b[2]!='M'||b[3]!='P'||b[4]!=1||b[6]!=0||b[7]!=0 ||
       get(b,24,4)!=b.size()-headerSize) return false;
    Header candidate{b[5],static_cast<uint32_t>(get(b,8,4)),static_cast<uint32_t>(get(b,12,4)),get(b,16,8)};
    if(!valid(candidate)||get(b,28,4)!=checksum(b)) return false;
    Bytes data(b.begin()+headerSize,b.end());
    h=candidate; payload.swap(data); return true;
}
}
