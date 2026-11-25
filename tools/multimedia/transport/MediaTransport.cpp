#include "chuck_dl.h"
#include "MediaFrame.h"

namespace {
using Arr=Chuck_DL_Api::ArrayInt;
bool read(Arr a,CK_DL_API API,size_t limit,mediawire::Bytes &b) {
    if(!a) return false;
    auto n=API->object->array_int_size(a);
    if(n<0 || static_cast<uint64_t>(n)>limit) return false;
    b.resize(static_cast<size_t>(n));
    for(t_CKINT i=0;i<n;++i) {
        auto v=API->object->array_int_get_idx(a,i);
        if(v<0||v>255) return false;
        b[i]=static_cast<uint8_t>(v);
    }
    return true;
}
bool exact(Arr a,CK_DL_API API,size_t n) {
    return a && API->object->array_int_size(a)==static_cast<t_CKINT>(n);
}
void write(Arr a,CK_DL_API API,const mediawire::Bytes &b) {
    for(size_t i=0;i<b.size();++i) API->object->array_int_set_idx(a,i,b[i]);
}
}
CK_DLL_SFUN(frame_header) { RETURN->v_int=mediawire::headerSize; }
CK_DLL_SFUN(frame_max) { RETURN->v_int=mediawire::maxPayload; }
CK_DLL_SFUN(frame_encode) {
    RETURN->v_int=0;
    try {
        auto type=GET_NEXT_INT(ARGS),stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(type<1||type>7||stream<1||static_cast<uint64_t>(stream)>0xffffffffu||
           seq<0||static_cast<uint64_t>(seq)>0xffffffffu||ts<0) return;
        mediawire::Bytes data,wire;
        if(!read(src,API,mediawire::maxPayload,data)||!exact(dst,API,mediawire::headerSize+data.size())) return;
        mediawire::Header h{static_cast<uint32_t>(type),static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)};
        if(mediawire::encode(h,data,wire)) { write(dst,API,wire); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_SFUN(frame_decode) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto meta=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(meta==dst||!exact(meta,API,4)) return;
        mediawire::Bytes wire,data; mediawire::Header h;
        if(!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decode(wire,h,data)||!exact(dst,API,data.size())) return;
        API->object->array_int_set_idx(meta,0,h.type);
        API->object->array_int_set_idx(meta,1,h.stream);
        API->object->array_int_set_idx(meta,2,h.sequence);
        API->object->array_int_set_idx(meta,3,h.timestamp);
        write(dst,API,data); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_QUERY(MediaTransport) {
    QUERY->setname(QUERY,"MediaTransport");
    QUERY->begin_class(QUERY,"MediaTransport","Object");
    QUERY->add_sfun(QUERY,frame_header,"int","headerSize");
    QUERY->add_sfun(QUERY,frame_max,"int","maxPayload");
    QUERY->add_sfun(QUERY,frame_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","mediaType");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int[]","payload");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,frame_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_arg(QUERY,"int[]","payload");
    return QUERY->end_class(QUERY);
}
