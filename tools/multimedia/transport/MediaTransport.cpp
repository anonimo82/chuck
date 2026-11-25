#include "MediaLoopback.h"
#include "chuck_dl.h"
#include <utility>
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
namespace {
t_CKUINT loopOffset=0;
mediawire::Loopback *loop(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::Loopback*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,loopOffset)));
}
}
CK_DLL_CTOR(loop_ctor) {
    OBJ_MEMBER_UINT(SELF,loopOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,loopOffset)=reinterpret_cast<uintptr_t>(new mediawire::Loopback); } catch(...) {}
}
CK_DLL_DTOR(loop_dtor) {
    delete loop(SELF,API); OBJ_MEMBER_UINT(SELF,loopOffset)=0;
}
CK_DLL_MFUN(loop_listen) {
    auto *p=loop(SELF,API); auto port=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&port>=0&&port<=65535&&p->listenOn(static_cast<unsigned>(port));
}
CK_DLL_MFUN(loop_connect) {
    auto *p=loop(SELF,API); auto port=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&port>0&&port<=65535&&p->connectTo(static_cast<unsigned>(port));
}
CK_DLL_MFUN(loop_accept) { auto *p=loop(SELF,API); RETURN->v_int=p?p->acceptOne():-1; }
CK_DLL_MFUN(loop_port) { auto *p=loop(SELF,API); RETURN->v_int=p?p->port():0; }
CK_DLL_MFUN(loop_connected) { auto *p=loop(SELF,API); RETURN->v_int=p&&p->connected(); }
CK_DLL_MFUN(loop_busy) { auto *p=loop(SELF,API); RETURN->v_int=p&&p->busy(); }
CK_DLL_MFUN(loop_size) { auto *p=loop(SELF,API); RETURN->v_int=p?p->packetSize():0; }
CK_DLL_MFUN(loop_poll) { auto *p=loop(SELF,API); RETURN->v_int=p?p->poll():-1; }
CK_DLL_MFUN(loop_disconnect) { if(auto *p=loop(SELF,API)) p->disconnect(); }
CK_DLL_MFUN(loop_stop) { if(auto *p=loop(SELF,API)) p->stop(); }
CK_DLL_MFUN(loop_send) {
    RETURN->v_int=0;
    try {
        auto *p=loop(SELF,API); auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes b;
        if(p&&p->connected()&&!p->busy()&&read(a,API,mediawire::headerSize+mediawire::maxPayload,b))
            RETURN->v_int=p->enqueue(std::move(b));
    } catch(...) {}
}
CK_DLL_MFUN(loop_receive) {
    RETURN->v_int=0;
    try {
        auto *p=loop(SELF,API); auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->packetSize()||!exact(a,API,p->packetSize())) return;
        mediawire::Bytes b;
        if(p->take(b)) { write(a,API,b); RETURN->v_int=1; }
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
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaLoopback","Object");
    loopOffset=QUERY->add_mvar(QUERY,"int","@loopNative",FALSE);
    if(loopOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,loop_ctor);
    QUERY->add_dtor(QUERY,loop_dtor);
    QUERY->add_mfun(QUERY,loop_listen,"int","listen");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_mfun(QUERY,loop_connect,"int","connect");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_mfun(QUERY,loop_accept,"int","accept");
    QUERY->add_mfun(QUERY,loop_port,"int","port");
    QUERY->add_mfun(QUERY,loop_connected,"int","connected");
    QUERY->add_mfun(QUERY,loop_busy,"int","sendPending");
    QUERY->add_mfun(QUERY,loop_size,"int","packetSize");
    QUERY->add_mfun(QUERY,loop_poll,"int","poll");
    QUERY->add_mfun(QUERY,loop_disconnect,"void","disconnect");
    QUERY->add_mfun(QUERY,loop_stop,"void","close");
    QUERY->add_mfun(QUERY,loop_send,"int","sendPacket");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,loop_receive,"int","receivePacket");
    QUERY->add_arg(QUERY,"int[]","packet");
    return QUERY->end_class(QUERY);
}
