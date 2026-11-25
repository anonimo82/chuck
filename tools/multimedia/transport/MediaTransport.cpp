#include "MediaSession.h"
#include "MediaPCMBuffer.h"
#include "MediaMIDI.h"
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
namespace {
t_CKUINT sessionOffset=0;
mediawire::Session *session(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::Session*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,sessionOffset)));
}
}
CK_DLL_CTOR(session_ctor) {
    OBJ_MEMBER_UINT(SELF,sessionOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,sessionOffset)=reinterpret_cast<uintptr_t>(new mediawire::Session); } catch(...) {}
}
CK_DLL_DTOR(session_dtor) {
    delete session(SELF,API); OBJ_MEMBER_UINT(SELF,sessionOffset)=0;
}
CK_DLL_MFUN(session_listen) {
    auto *p=session(SELF,API); auto port=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&port>=0&&port<=65535&&p->listenOn(static_cast<unsigned>(port));
}
CK_DLL_MFUN(session_connect) {
    auto *p=session(SELF,API); auto port=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&port>0&&port<=65535&&p->connectTo(static_cast<unsigned>(port));
}
CK_DLL_MFUN(session_accept) { auto *p=session(SELF,API); RETURN->v_int=p?p->acceptOne():-1; }
CK_DLL_MFUN(session_port) { auto *p=session(SELF,API); RETURN->v_int=p?p->port():0; }
CK_DLL_MFUN(session_connected) { auto *p=session(SELF,API); RETURN->v_int=p&&p->state()==2; }
CK_DLL_MFUN(session_busy) { auto *p=session(SELF,API); RETURN->v_int=p&&p->busy(); }
CK_DLL_MFUN(session_size) { auto *p=session(SELF,API); RETURN->v_int=p?p->packetSize():0; }
CK_DLL_MFUN(session_poll) { auto *p=session(SELF,API); RETURN->v_int=p?p->poll():-1; }
CK_DLL_MFUN(session_disconnect) { if(auto *p=session(SELF,API)) p->disconnect(); }
CK_DLL_MFUN(session_stop) { if(auto *p=session(SELF,API)) p->stop(); }
CK_DLL_MFUN(session_send) {
    RETURN->v_int=0;
    try {
        auto *p=session(SELF,API); auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes b;
        if(p&&p->state()==2&&!p->busy()&&read(a,API,mediawire::headerSize+mediawire::maxPayload,b))
            RETURN->v_int=p->enqueue(std::move(b));
    } catch(...) {}
}
CK_DLL_MFUN(session_receive) {
    RETURN->v_int=0;
    try {
        auto *p=session(SELF,API); auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->packetSize()||!exact(a,API,p->packetSize())) return;
        mediawire::Bytes b;
        if(p->take(b)) { write(a,API,b); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(session_configure) {
    auto *p=session(SELF,API); auto mask=GET_NEXT_INT(ARGS),limit=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&mask>0&&mask<=127&&limit>0&&limit<=mediawire::maxPayload&&p->configure(static_cast<unsigned>(mask),static_cast<unsigned>(limit));
}
CK_DLL_MFUN(session_state) { auto *p=session(SELF,API); RETURN->v_int=p?p->state():0; }
CK_DLL_MFUN(session_error) { auto *p=session(SELF,API); RETURN->v_int=p?p->error():0; }
CK_DLL_MFUN(session_mask) { auto *p=session(SELF,API); RETURN->v_int=p?p->mask():0; }
CK_DLL_MFUN(session_limit) { auto *p=session(SELF,API); RETURN->v_int=p?p->limit():0; }
CK_DLL_MFUN(loop_allow_remote) {
    auto *p=loop(SELF,API); auto value=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&(value==0||value==1)&&p->allowRemote(value==1);
}
CK_DLL_MFUN(loop_remote_enabled) { auto *p=loop(SELF,API); RETURN->v_int=p&&p->remoteEnabled(); }
CK_DLL_MFUN(loop_listen_at) {
    RETURN->v_int=0;
    try {
        auto *p=loop(SELF,API);
        auto ip=GET_NEXT_STRING_SAFE(ARGS); auto port=GET_NEXT_INT(ARGS);
        auto peer=GET_NEXT_STRING_SAFE(ARGS);
        if(p&&port>=0&&port<=65535) RETURN->v_int=p->listenAt(ip,static_cast<unsigned>(port),peer);
    } catch(...) {}
}
CK_DLL_MFUN(loop_connect_at) {
    RETURN->v_int=0;
    try {
        auto *p=loop(SELF,API);
        auto ip=GET_NEXT_STRING_SAFE(ARGS); auto port=GET_NEXT_INT(ARGS),timeout=GET_NEXT_INT(ARGS);
        if(p&&port>0&&port<=65535&&timeout>0&&timeout<=5000)
            RETURN->v_int=p->connectAt(ip,static_cast<unsigned>(port),static_cast<unsigned>(timeout));
    } catch(...) {}
}
CK_DLL_MFUN(loop_peer_address) {
    auto *p=loop(SELF,API);
    RETURN->v_string=API->object->create_string(VM,p?p->peerAddress().c_str():"",FALSE);
}
CK_DLL_MFUN(session_allow_remote) {
    auto *p=session(SELF,API); auto value=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&(value==0||value==1)&&p->allowRemote(value==1);
}
CK_DLL_MFUN(session_remote_enabled) { auto *p=session(SELF,API); RETURN->v_int=p&&p->remoteEnabled(); }
CK_DLL_MFUN(session_listen_at) {
    RETURN->v_int=0;
    try {
        auto *p=session(SELF,API);
        auto ip=GET_NEXT_STRING_SAFE(ARGS); auto port=GET_NEXT_INT(ARGS);
        auto peer=GET_NEXT_STRING_SAFE(ARGS);
        if(p&&port>=0&&port<=65535) RETURN->v_int=p->listenAt(ip,static_cast<unsigned>(port),peer);
    } catch(...) {}
}
CK_DLL_MFUN(session_connect_at) {
    RETURN->v_int=0;
    try {
        auto *p=session(SELF,API);
        auto ip=GET_NEXT_STRING_SAFE(ARGS); auto port=GET_NEXT_INT(ARGS),timeout=GET_NEXT_INT(ARGS);
        if(p&&port>0&&port<=65535&&timeout>0&&timeout<=5000)
            RETURN->v_int=p->connectAt(ip,static_cast<unsigned>(port),static_cast<unsigned>(timeout));
    } catch(...) {}
}
CK_DLL_MFUN(session_peer_address) {
    auto *p=session(SELF,API);
    RETURN->v_string=API->object->create_string(VM,p?p->peerAddress().c_str():"",FALSE);
}
CK_DLL_MFUN(loop_sentPackets) { auto *p=loop(SELF,API); RETURN->v_int=p?p->sentPackets():0; }
CK_DLL_MFUN(loop_receivedPackets) { auto *p=loop(SELF,API); RETURN->v_int=p?p->receivedPackets():0; }
CK_DLL_MFUN(loop_sentBytes) { auto *p=loop(SELF,API); RETURN->v_int=p?p->sentBytes():0; }
CK_DLL_MFUN(loop_receivedBytes) { auto *p=loop(SELF,API); RETURN->v_int=p?p->receivedBytes():0; }
CK_DLL_MFUN(loop_transportError) { auto *p=loop(SELF,API); RETURN->v_int=p?p->transportError():0; }
CK_DLL_MFUN(session_sentPackets) { auto *p=session(SELF,API); RETURN->v_int=p?p->sentPackets():0; }
CK_DLL_MFUN(session_receivedPackets) { auto *p=session(SELF,API); RETURN->v_int=p?p->receivedPackets():0; }
CK_DLL_MFUN(session_sentBytes) { auto *p=session(SELF,API); RETURN->v_int=p?p->sentBytes():0; }
CK_DLL_MFUN(session_receivedBytes) { auto *p=session(SELF,API); RETURN->v_int=p?p->receivedBytes():0; }
CK_DLL_MFUN(session_transportError) { auto *p=session(SELF,API); RETURN->v_int=p?p->transportError():0; }
CK_DLL_SFUN(pcm_size) {
    auto rate=GET_NEXT_INT(ARGS),channels=GET_NEXT_INT(ARGS),frames=GET_NEXT_INT(ARGS);
    RETURN->v_int=mediawire::pcmPacketSize(rate,channels,frames);
}
CK_DLL_SFUN(pcm_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto rate=GET_NEXT_INT(ARGS),channels=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!a||stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0||rate<8000||rate>192000||channels<1||channels>8) return;
        auto n=API->object->array_float_size(a);
        if(n<=0||n%channels) return;
        auto frames=n/channels;
        auto bytes=mediawire::pcmPacketSize(rate,channels,frames);
        if(!bytes||!exact(dst,API,bytes)) return;
        std::vector<double> samples(static_cast<size_t>(n));
        for(t_CKINT i=0;i<n;++i) samples[i]=API->object->array_float_get_idx(a,i);
        mediawire::Header h{4,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)};
        mediawire::PcmInfo info{static_cast<uint32_t>(rate),static_cast<uint32_t>(channels),static_cast<uint32_t>(frames)};
        mediawire::Bytes wire;
        if(mediawire::encodePcm(h,info,samples,wire)) { write(dst,API,wire); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_SFUN(pcm_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto meta=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h; mediawire::PcmInfo info; mediawire::Bytes wire; std::vector<double> samples;
        if(!exact(meta,API,6)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodePcm(wire,h,info,samples)) return;
        const uint64_t fields[]={h.stream,h.sequence,h.timestamp,info.rate,info.channels,info.frames};
        for(size_t i=0;i<6;++i) API->object->array_int_set_idx(meta,i,fields[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(pcm_decode) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h; mediawire::PcmInfo info; mediawire::Bytes wire; std::vector<double> samples;
        if(!dst||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodePcm(wire,h,info,samples)||
           API->object->array_float_size(dst)!=static_cast<t_CKINT>(samples.size())) return;
        for(size_t i=0;i<samples.size();++i) API->object->array_float_set_idx(dst,i,samples[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
namespace {
t_CKUINT pcmStreamOffset=0;
mediawire::PcmStream *pcmStream(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::PcmStream*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,pcmStreamOffset)));
}
}
CK_DLL_CTOR(ps_ctor) {
    OBJ_MEMBER_UINT(SELF,pcmStreamOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,pcmStreamOffset)=reinterpret_cast<uintptr_t>(new mediawire::PcmStream); } catch(...) {}
}
CK_DLL_DTOR(ps_dtor) { delete pcmStream(SELF,API); OBJ_MEMBER_UINT(SELF,pcmStreamOffset)=0; }
CK_DLL_MFUN(ps_configure) {
    auto *p=pcmStream(SELF,API);
    auto stream=GET_NEXT_INT(ARGS),rate=GET_NEXT_INT(ARGS),channels=GET_NEXT_INT(ARGS),origin=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&p->configure(stream,rate,channels,origin,seq);
}
CK_DLL_MFUN(ps_clear) { if(auto *p=pcmStream(SELF,API)) p->clear(); }
CK_DLL_MFUN(ps_size) { auto *p=pcmStream(SELF,API); auto n=GET_NEXT_INT(ARGS); RETURN->v_int=p?p->packetSize(n):0; }
CK_DLL_MFUN(ps_encode) {
    RETURN->v_int=0;
    auto *p=pcmStream(SELF,API);
    if(!p) return;
    try {
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()) { p->reject(1); return; }
        if(!a) { p->reject(2); return; }
        auto n=API->object->array_float_size(a);
        if(n<=0||n%p->channels()||!p->packetSize(n/p->channels())||!exact(dst,API,p->packetSize(n/p->channels()))) { p->reject(2); return; }
        std::vector<double> data(static_cast<size_t>(n));
        for(t_CKINT i=0;i<n;++i) data[i]=API->object->array_float_get_idx(a,i);
        mediawire::Bytes wire;
        if(p->encodeBlock(data,wire)) { write(dst,API,wire); RETURN->v_int=1; }
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(ps_decode) {
    RETURN->v_int=0;
    auto *p=pcmStream(SELF,API);
    if(!p) return;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()) { p->reject(1); return; }
        mediawire::Bytes wire;
        if(!dst||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)) { p->reject(2); return; }
        std::vector<double> data;
        if(!p->decodeBlock(wire,API->object->array_float_size(dst),data)) return;
        for(size_t i=0;i<data.size();++i) API->object->array_float_set_idx(dst,i,data[i]);
        RETURN->v_int=1;
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(ps_configured) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(ps_frames) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->frames():0; }
CK_DLL_MFUN(ps_blocks) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->blocks():0; }
CK_DLL_MFUN(ps_nextSequence) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->nextSequence():0; }
CK_DLL_MFUN(ps_nextTimestamp) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->nextTimestamp():0; }
CK_DLL_MFUN(ps_error) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->error():0; }
CK_DLL_MFUN(ps_rate) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->rate():0; }
CK_DLL_MFUN(ps_channels) { auto *p=pcmStream(SELF,API); RETURN->v_int=p?p->channels():0; }
namespace {
t_CKUINT pcmBufferOffset=0;
mediawire::PcmBuffer *pcmBuffer(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::PcmBuffer*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,pcmBufferOffset)));
}
}
CK_DLL_CTOR(pb_ctor) {
    OBJ_MEMBER_UINT(SELF,pcmBufferOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,pcmBufferOffset)=reinterpret_cast<uintptr_t>(new mediawire::PcmBuffer); } catch(...) {}
}
CK_DLL_DTOR(pb_dtor) { delete pcmBuffer(SELF,API); OBJ_MEMBER_UINT(SELF,pcmBufferOffset)=0; }
CK_DLL_MFUN(pb_configure) {
    RETURN->v_int=0;
    try {
        auto *p=pcmBuffer(SELF,API);
        auto stream=GET_NEXT_INT(ARGS),rate=GET_NEXT_INT(ARGS),channels=GET_NEXT_INT(ARGS),origin=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),capacity=GET_NEXT_INT(ARGS),threshold=GET_NEXT_INT(ARGS);
        if(p) RETURN->v_int=p->configure(stream,rate,channels,origin,seq,capacity,threshold);
    } catch(...) {}
}
CK_DLL_MFUN(pb_clear) { if(auto *p=pcmBuffer(SELF,API)) p->clear(); }
CK_DLL_MFUN(pb_push) {
    RETURN->v_int=0;
    auto *p=pcmBuffer(SELF,API); if(!p) return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()) { p->reject(1); return; }
        mediawire::Bytes wire;
        if(!read(a,API,mediawire::headerSize+mediawire::maxPayload,wire)) { p->reject(2); return; }
        RETURN->v_int=p->push(wire);
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(pb_pull) {
    RETURN->v_int=0;
    auto *p=pcmBuffer(SELF,API); if(!p) return;
    try {
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()) { p->reject(1); return; }
        if(!a) { p->reject(2); return; }
        auto n=API->object->array_float_size(a);
        if(n<=0||n%p->channels()) { p->reject(2); return; }
        std::vector<double> data;
        if(!p->pull(n/p->channels(),data)) return;
        for(size_t i=0;i<data.size();++i) API->object->array_float_set_idx(a,i,data[i]);
        RETURN->v_int=1;
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(pb_configured) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(pb_capacity) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->capacity():0; }
CK_DLL_MFUN(pb_threshold) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->threshold():0; }
CK_DLL_MFUN(pb_available) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->available():0; }
CK_DLL_MFUN(pb_freeFrames) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->freeFrames():0; }
CK_DLL_MFUN(pb_ready) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->ready():0; }
CK_DLL_MFUN(pb_receivedFrames) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->receivedFrames():0; }
CK_DLL_MFUN(pb_consumedFrames) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->consumedFrames():0; }
CK_DLL_MFUN(pb_nextSequence) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->nextSequence():0; }
CK_DLL_MFUN(pb_nextTimestamp) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->nextTimestamp():0; }
CK_DLL_MFUN(pb_playTimestamp) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->playTimestamp():0; }
CK_DLL_MFUN(pb_bufferedUs) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->bufferedUs():0; }
CK_DLL_MFUN(pb_underruns) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->underruns():0; }
CK_DLL_MFUN(pb_overruns) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->overruns():0; }
CK_DLL_MFUN(pb_error) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->error():0; }
CK_DLL_MFUN(pb_channels) { auto *p=pcmBuffer(SELF,API); RETURN->v_int=p?p->channels():0; }
CK_DLL_SFUN(mi_size) { RETURN->v_int=mediawire::headerSize+mediawire::midiPayloadSize; }
CK_DLL_SFUN(mi_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto status=GET_NEXT_INT(ARGS),d1=GET_NEXT_INT(ARGS),d2=GET_NEXT_INT(ARGS);
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0||!exact(dst,API,40)) return;
        mediawire::Bytes wire;
        if(mediawire::encodeMidi({5,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)},status,d1,d2,wire)) {
            write(dst,API,wire); RETURN->v_int=1;
        }
    } catch(...) {}
}
namespace {
void writeMidi(Arr a,CK_DL_API API,const mediawire::Header &h,const mediawire::Bytes &msg) {
    const uint64_t fields[]={h.stream,h.sequence,h.timestamp,msg[0],msg[1],msg[2]};
    for(size_t i=0;i<6;++i) API->object->array_int_set_idx(a,i,fields[i]);
}
t_CKUINT midiStreamOffset=0;
mediawire::MidiStream *midiStream(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::MidiStream*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,midiStreamOffset)));
}
}
CK_DLL_SFUN(mi_decode) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire,msg; mediawire::Header h;
        if(!exact(dst,API,6)||!read(src,API,40,wire)||!mediawire::decodeMidi(wire,h,msg)) return;
        writeMidi(dst,API,h,msg); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_CTOR(ms_ctor) {
    OBJ_MEMBER_UINT(SELF,midiStreamOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,midiStreamOffset)=reinterpret_cast<uintptr_t>(new mediawire::MidiStream); } catch(...) {}
}
CK_DLL_DTOR(ms_dtor) { delete midiStream(SELF,API); OBJ_MEMBER_UINT(SELF,midiStreamOffset)=0; }
CK_DLL_MFUN(ms_configure) {
    auto *p=midiStream(SELF,API);
    auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),origin=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&p->configure(stream,seq,origin);
}
CK_DLL_MFUN(ms_clear) { if(auto *p=midiStream(SELF,API)) p->clear(); }
CK_DLL_MFUN(ms_encode) {
    RETURN->v_int=0;
    auto *p=midiStream(SELF,API); if(!p) return;
    try {
        auto ts=GET_NEXT_INT(ARGS),status=GET_NEXT_INT(ARGS),d1=GET_NEXT_INT(ARGS),d2=GET_NEXT_INT(ARGS);
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()) { p->reject(1); return; }
        if(!exact(dst,API,40)) { p->reject(2); return; }
        mediawire::Bytes wire;
        if(p->encodeEvent(ts,status,d1,d2,wire)) { write(dst,API,wire); RETURN->v_int=1; }
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(ms_decode) {
    RETURN->v_int=0;
    auto *p=midiStream(SELF,API); if(!p) return;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()) { p->reject(1); return; }
        mediawire::Bytes wire,msg; mediawire::Header h;
        if(!exact(dst,API,6)||!read(src,API,40,wire)) { p->reject(2); return; }
        if(p->decodeEvent(wire,h,msg)) { writeMidi(dst,API,h,msg); RETURN->v_int=1; }
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(ms_configured) { auto *p=midiStream(SELF,API); RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(ms_nextSequence) { auto *p=midiStream(SELF,API); RETURN->v_int=p?p->nextSequence():0; }
CK_DLL_MFUN(ms_lastTimestamp) { auto *p=midiStream(SELF,API); RETURN->v_int=p?p->lastTimestamp():0; }
CK_DLL_MFUN(ms_events) { auto *p=midiStream(SELF,API); RETURN->v_int=p?p->events():0; }
CK_DLL_MFUN(ms_error) { auto *p=midiStream(SELF,API); RETURN->v_int=p?p->error():0; }
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
    QUERY->add_mfun(QUERY,loop_allow_remote,"int","allowRemote");
    QUERY->add_arg(QUERY,"int","enabled");
    QUERY->add_mfun(QUERY,loop_remote_enabled,"int","remoteEnabled");
    QUERY->add_mfun(QUERY,loop_listen_at,"int","listenAt");
    QUERY->add_arg(QUERY,"string","address");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_arg(QUERY,"string","allowedPeer");
    QUERY->add_mfun(QUERY,loop_connect_at,"int","connectAt");
    QUERY->add_arg(QUERY,"string","address");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_arg(QUERY,"int","timeoutMs");
    QUERY->add_mfun(QUERY,loop_peer_address,"string","peerAddress");
    QUERY->add_mfun(QUERY,loop_sentPackets,"int","sentPackets");
    QUERY->add_mfun(QUERY,loop_receivedPackets,"int","receivedPackets");
    QUERY->add_mfun(QUERY,loop_sentBytes,"int","sentBytes");
    QUERY->add_mfun(QUERY,loop_receivedBytes,"int","receivedBytes");
    QUERY->add_mfun(QUERY,loop_transportError,"int","transportError");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaSession","Object");
    sessionOffset=QUERY->add_mvar(QUERY,"int","@sessionNative",FALSE);
    if(sessionOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,session_ctor);
    QUERY->add_dtor(QUERY,session_dtor);
    QUERY->add_mfun(QUERY,session_listen,"int","listen");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_mfun(QUERY,session_connect,"int","connect");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_mfun(QUERY,session_accept,"int","accept");
    QUERY->add_mfun(QUERY,session_port,"int","port");
    QUERY->add_mfun(QUERY,session_connected,"int","connected");
    QUERY->add_mfun(QUERY,session_busy,"int","sendPending");
    QUERY->add_mfun(QUERY,session_size,"int","packetSize");
    QUERY->add_mfun(QUERY,session_poll,"int","poll");
    QUERY->add_mfun(QUERY,session_disconnect,"void","disconnect");
    QUERY->add_mfun(QUERY,session_stop,"void","close");
    QUERY->add_mfun(QUERY,session_send,"int","sendPacket");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,session_receive,"int","receivePacket");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,session_allow_remote,"int","allowRemote");
    QUERY->add_arg(QUERY,"int","enabled");
    QUERY->add_mfun(QUERY,session_remote_enabled,"int","remoteEnabled");
    QUERY->add_mfun(QUERY,session_listen_at,"int","listenAt");
    QUERY->add_arg(QUERY,"string","address");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_arg(QUERY,"string","allowedPeer");
    QUERY->add_mfun(QUERY,session_connect_at,"int","connectAt");
    QUERY->add_arg(QUERY,"string","address");
    QUERY->add_arg(QUERY,"int","port");
    QUERY->add_arg(QUERY,"int","timeoutMs");
    QUERY->add_mfun(QUERY,session_peer_address,"string","peerAddress");
    QUERY->add_mfun(QUERY,session_sentPackets,"int","sentPackets");
    QUERY->add_mfun(QUERY,session_receivedPackets,"int","receivedPackets");
    QUERY->add_mfun(QUERY,session_sentBytes,"int","sentBytes");
    QUERY->add_mfun(QUERY,session_receivedBytes,"int","receivedBytes");
    QUERY->add_mfun(QUERY,session_transportError,"int","transportError");
    QUERY->add_mfun(QUERY,session_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","mediaMask");
    QUERY->add_arg(QUERY,"int","maxPayload");
    QUERY->add_mfun(QUERY,session_state,"int","state");
    QUERY->add_mfun(QUERY,session_error,"int","error");
    QUERY->add_mfun(QUERY,session_mask,"int","mediaMask");
    QUERY->add_mfun(QUERY,session_limit,"int","payloadLimit");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaPCM","Object");
    QUERY->add_sfun(QUERY,pcm_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"int","frames");
    QUERY->add_sfun(QUERY,pcm_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,pcm_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,pcm_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"float[]","samples");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaPCMStream","Object");
    pcmStreamOffset=QUERY->add_mvar(QUERY,"int","@pcmStreamNative",FALSE);
    if(pcmStreamOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,ps_ctor);
    QUERY->add_dtor(QUERY,ps_dtor);
    QUERY->add_mfun(QUERY,ps_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_mfun(QUERY,ps_clear,"void","clear");
    QUERY->add_mfun(QUERY,ps_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","frames");
    QUERY->add_mfun(QUERY,ps_encode,"int","encodeBlock");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,ps_decode,"int","decodeBlock");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,ps_configured,"int","configured");
    QUERY->add_mfun(QUERY,ps_frames,"int","frames");
    QUERY->add_mfun(QUERY,ps_blocks,"int","blocks");
    QUERY->add_mfun(QUERY,ps_nextSequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,ps_nextTimestamp,"int","nextTimestamp");
    QUERY->add_mfun(QUERY,ps_error,"int","error");
    QUERY->add_mfun(QUERY,ps_rate,"int","rate");
    QUERY->add_mfun(QUERY,ps_channels,"int","channels");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaPCMBuffer","Object");
    pcmBufferOffset=QUERY->add_mvar(QUERY,"int","@pcmBufferNative",FALSE);
    if(pcmBufferOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,pb_ctor);
    QUERY->add_dtor(QUERY,pb_dtor);
    QUERY->add_mfun(QUERY,pb_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","capacityFrames");
    QUERY->add_arg(QUERY,"int","prebufferFrames");
    QUERY->add_mfun(QUERY,pb_clear,"void","clear");
    QUERY->add_mfun(QUERY,pb_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,pb_pull,"int","pull");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,pb_configured,"int","configured");
    QUERY->add_mfun(QUERY,pb_capacity,"int","capacity");
    QUERY->add_mfun(QUERY,pb_threshold,"int","threshold");
    QUERY->add_mfun(QUERY,pb_available,"int","available");
    QUERY->add_mfun(QUERY,pb_freeFrames,"int","freeFrames");
    QUERY->add_mfun(QUERY,pb_ready,"int","ready");
    QUERY->add_mfun(QUERY,pb_receivedFrames,"int","receivedFrames");
    QUERY->add_mfun(QUERY,pb_consumedFrames,"int","consumedFrames");
    QUERY->add_mfun(QUERY,pb_nextSequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,pb_nextTimestamp,"int","nextTimestamp");
    QUERY->add_mfun(QUERY,pb_playTimestamp,"int","playTimestamp");
    QUERY->add_mfun(QUERY,pb_bufferedUs,"int","bufferedUs");
    QUERY->add_mfun(QUERY,pb_underruns,"int","underruns");
    QUERY->add_mfun(QUERY,pb_overruns,"int","overruns");
    QUERY->add_mfun(QUERY,pb_error,"int","error");
    QUERY->add_mfun(QUERY,pb_channels,"int","channels");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaMIDI","Object");
    QUERY->add_sfun(QUERY,mi_size,"int","packetSize");
    QUERY->add_sfun(QUERY,mi_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","status");
    QUERY->add_arg(QUERY,"int","data1");
    QUERY->add_arg(QUERY,"int","data2");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,mi_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","event");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaMIDIStream","Object");
    midiStreamOffset=QUERY->add_mvar(QUERY,"int","@midiStreamNative",FALSE);
    if(midiStreamOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,ms_ctor);
    QUERY->add_dtor(QUERY,ms_dtor);
    QUERY->add_mfun(QUERY,ms_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_mfun(QUERY,ms_clear,"void","clear");
    QUERY->add_mfun(QUERY,ms_encode,"int","encodeEvent");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","status");
    QUERY->add_arg(QUERY,"int","data1");
    QUERY->add_arg(QUERY,"int","data2");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,ms_decode,"int","decodeEvent");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","event");
    QUERY->add_mfun(QUERY,ms_configured,"int","configured");
    QUERY->add_mfun(QUERY,ms_nextSequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,ms_lastTimestamp,"int","lastTimestamp");
    QUERY->add_mfun(QUERY,ms_events,"int","events");
    QUERY->add_mfun(QUERY,ms_error,"int","error");
    return QUERY->end_class(QUERY);
}
