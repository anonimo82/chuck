#include "chuck_dl.h"
#include <cstdint>
#include <vector>
#include <algorithm>
#include <limits>

namespace {
constexpr size_t headerSize=24, maxPayload=1048576;
constexpr unsigned char magic[4]={'C','K','M','1'};
using Bytes=std::vector<unsigned char>;
using Array=Chuck_DL_Api::ArrayInt;
void put(Bytes &out,uint64_t value,size_t count) {
    for(size_t i=count;i>0;--i) out.push_back(static_cast<unsigned char>(value>>((i-1)*8)));
}
uint64_t get(const Bytes &in,size_t start,size_t count) {
    uint64_t result=0;
    for(size_t i=0;i<count;++i) result=(result<<8)|in[start+i];
    return result;
}
bool readBytes(Array a,CK_DL_API API,size_t limit,Bytes &out) {
    if(!a) return false;
    auto n=API->object->array_int_size(a);
    if(n<0||static_cast<uint64_t>(n)>limit) return false;
    out.resize(static_cast<size_t>(n));
    for(size_t i=0;i<out.size();++i) {
        auto v=API->object->array_int_get_idx(a,i);
        if(v<0||v>255) return false;
        out[i]=static_cast<unsigned char>(v);
    }
    return true;
}
bool outputBytes(Array a,CK_DL_API API,const Bytes &in) {
    if(!a||API->object->array_int_size(a)!=static_cast<t_CKINT>(in.size())) return false;
    for(size_t i=0;i<in.size();++i) API->object->array_int_set_idx(a,i,in[i]);
    return true;
}
bool validPacket(const Bytes &b) {
    return b.size()>=headerSize&&std::equal(magic,magic+4,b.begin())&&
        b[4]>=1&&b[4]<=7&&b[5]==0&&b[6]==0&&b[7]==0&&
        get(b,8,8)<=INT64_MAX&&get(b,16,4)<=maxPayload&&
        get(b,16,4)==b.size()-headerSize&&get(b,20,4)==0;
}
}
CK_DLL_SFUN(packet_size) {
    auto n=GET_NEXT_INT(ARGS);
    RETURN->v_int=n>=0&&static_cast<uint64_t>(n)<=maxPayload?static_cast<t_CKINT>(headerSize+n):-1;
}
CK_DLL_SFUN(packet_encode) {
    RETURN->v_int=0;
    try {
        auto kind=GET_NEXT_INT(ARGS),stamp=GET_NEXT_INT(ARGS);
        auto payload=reinterpret_cast<Array>(GET_NEXT_OBJECT(ARGS));
        auto output=reinterpret_cast<Array>(GET_NEXT_OBJECT(ARGS));
        Bytes data;
        if(kind<1||kind>7||stamp<0||!readBytes(payload,API,maxPayload,data)) return;
        Bytes packet; packet.reserve(headerSize+data.size());
        packet.insert(packet.end(),magic,magic+4);
        packet.push_back(static_cast<unsigned char>(kind));
        put(packet,0,3); put(packet,static_cast<uint64_t>(stamp),8);
        put(packet,data.size(),4); put(packet,0,4);
        packet.insert(packet.end(),data.begin(),data.end());
        RETURN->v_int=outputBytes(output,API,packet);
    } catch(...) {}
}
CK_DLL_SFUN(packet_kind) {
    RETURN->v_int=-1;
    try { Bytes b; auto a=reinterpret_cast<Array>(GET_NEXT_OBJECT(ARGS));
        if(readBytes(a,API,headerSize+maxPayload,b)&&validPacket(b)) RETURN->v_int=b[4];
    } catch(...) {}
}
CK_DLL_SFUN(packet_timestamp) {
    RETURN->v_int=-1;
    try { Bytes b; auto a=reinterpret_cast<Array>(GET_NEXT_OBJECT(ARGS));
        if(readBytes(a,API,headerSize+maxPayload,b)&&validPacket(b)&&get(b,8,8)<=static_cast<uint64_t>(std::numeric_limits<t_CKINT>::max()))
            RETURN->v_int=static_cast<t_CKINT>(get(b,8,8));
    } catch(...) {}
}
CK_DLL_SFUN(packet_payload_size) {
    RETURN->v_int=-1;
    try { Bytes b; auto a=reinterpret_cast<Array>(GET_NEXT_OBJECT(ARGS));
        if(readBytes(a,API,headerSize+maxPayload,b)&&validPacket(b)) RETURN->v_int=static_cast<t_CKINT>(b.size()-headerSize);
    } catch(...) {}
}
CK_DLL_SFUN(packet_decode) {
    RETURN->v_int=0;
    try { Bytes b; auto a=reinterpret_cast<Array>(GET_NEXT_OBJECT(ARGS));
        auto out=reinterpret_cast<Array>(GET_NEXT_OBJECT(ARGS));
        if(!readBytes(a,API,headerSize+maxPayload,b)||!validPacket(b)) return;
        Bytes payload(b.begin()+headerSize,b.end());
        RETURN->v_int=outputBytes(out,API,payload);
    } catch(...) {}
}
CK_DLL_QUERY(MediaTransport) {
    QUERY->setname(QUERY,"MediaTransport");
    QUERY->begin_class(QUERY,"MediaPacket","Object");
    QUERY->add_sfun(QUERY,packet_size,"int","encodedSize");
    QUERY->add_arg(QUERY,"int","payloadSize");
    QUERY->add_sfun(QUERY,packet_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","kind");
    QUERY->add_arg(QUERY,"int","timestampMicros");
    QUERY->add_arg(QUERY,"int[]","payload");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,packet_kind,"int","kind");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,packet_timestamp,"int","timestamp");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,packet_payload_size,"int","payloadSize");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,packet_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","payload");
    return QUERY->end_class(QUERY);
}
