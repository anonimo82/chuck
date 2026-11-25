#include "MediaSession.h"
#include "MediaVideoSender.h"
#include "MediaRasterAuto.h"
#include "MediaPCMBuffer.h"
#include "MediaAudioMidi.h"
#include "MediaGraphics.h"
#include "MediaVideoFrame.h"
#include "MediaVideoStream.h"
#include "MediaVideoBuffer.h"
#include "MediaAudioVideo.h"
#include "MediaAVPacket.h"
#include "MediaText.h"
#include "MediaTextFragment.h"
#include "MediaTextBuffer.h"
#include "MediaRouter.h"
#include "MediaRasterCompressed.h"
#include "MediaRasterPng.h"
#include "MediaRasterPhoto.h"
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
namespace {
t_CKUINT avOffset=0;
mediawire::AudioMidi *avState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::AudioMidi*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,avOffset)));
}
}
CK_DLL_CTOR(av_ctor) {
    OBJ_MEMBER_UINT(SELF,avOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,avOffset)=reinterpret_cast<uintptr_t>(new mediawire::AudioMidi); } catch(...) {}
}
CK_DLL_DTOR(av_dtor) { delete avState(SELF,API); OBJ_MEMBER_UINT(SELF,avOffset)=0; }
CK_DLL_MFUN(av_configure) {
    RETURN->v_int=0;
    try {
        auto *p=avState(SELF,API);
        uint64_t a[10]; for(auto &v:a) v=GET_NEXT_INT(ARGS);
        if(p) RETURN->v_int=p->configure(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9]);
    } catch(...) {}
}
CK_DLL_MFUN(av_clear) { if(auto *p=avState(SELF,API)) p->clear(); }
CK_DLL_MFUN(av_push) {
    RETURN->v_int=0;
    auto *p=avState(SELF,API); if(!p) return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire;
        if(!p->configured()) { p->reject(1); return; }
        if(!read(a,API,mediawire::headerSize+mediawire::maxPayload,wire)) { p->reject(2); return; }
        RETURN->v_int=p->push(wire);
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(av_pull) {
    RETURN->v_int=0;
    auto *p=avState(SELF,API); if(!p) return;
    try {
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()) { p->reject(1); return; }
        if(!a) { p->reject(2); return; }
        auto n=API->object->array_float_size(a);
        if(n<=0||n%p->channels()) { p->reject(2); return; }
        std::vector<double> data;
        if(!p->pullAudio(n/p->channels(),data)) return;
        for(size_t i=0;i<data.size();++i) API->object->array_float_set_idx(a,i,data[i]);
        RETURN->v_int=1;
    } catch(...) { p->reject(2); }
}
CK_DLL_MFUN(av_pop) {
    RETURN->v_int=0;
    auto *p=avState(SELF,API); if(!p) return;
    auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
    if(!exact(a,API,6)) { p->reject(2); return; }
    std::array<uint64_t,6> event{};
    if(!p->popMidi(event)) return;
    for(size_t i=0;i<6;++i) API->object->array_int_set_idx(a,i,event[i]);
    RETURN->v_int=1;
}
CK_DLL_MFUN(av_configured) { auto *p=avState(SELF,API); RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(av_ready) { auto *p=avState(SELF,API); RETURN->v_int=p?p->ready():0; }
CK_DLL_MFUN(av_available) { auto *p=avState(SELF,API); RETURN->v_int=p?p->available():0; }
CK_DLL_MFUN(av_pendingMidi) { auto *p=avState(SELF,API); RETURN->v_int=p?p->pendingMidi():0; }
CK_DLL_MFUN(av_channels) { auto *p=avState(SELF,API); RETURN->v_int=p?p->channels():0; }
CK_DLL_MFUN(av_playTimestamp) { auto *p=avState(SELF,API); RETURN->v_int=p?p->playTimestamp():0; }
CK_DLL_MFUN(av_consumedFrames) { auto *p=avState(SELF,API); RETURN->v_int=p?p->consumedFrames():0; }
CK_DLL_MFUN(av_error) { auto *p=avState(SELF,API); RETURN->v_int=p?p->error():0; }
CK_DLL_MFUN(av_audioError) { auto *p=avState(SELF,API); RETURN->v_int=p?p->audioError():0; }
CK_DLL_MFUN(av_midiDue) { auto *p=avState(SELF,API); RETURN->v_int=p?p->midiDue():0; }
CK_DLL_SFUN(ra_size) {
    auto width=GET_NEXT_INT(ARGS),height=GET_NEXT_INT(ARGS);
    RETURN->v_int=mediawire::rasterPacketSize(width,height);
}
CK_DLL_SFUN(ra_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto width=GET_NEXT_INT(ARGS),height=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::rasterPacketSize(width,height);
        if(!n||stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0||!exact(dst,API,n)||
           !exact(src,API,n-mediawire::headerSize-mediawire::rasterHeader)) return;
        mediawire::Bytes rgba,wire;
        if(!read(src,API,mediawire::maxPayload,rgba)) return;
        if(mediawire::encodeRaster({1,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)},
           {static_cast<uint32_t>(width),static_cast<uint32_t>(height)},rgba,wire)) {
            write(dst,API,wire); RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_SFUN(ra_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire,rgba; mediawire::Header h; mediawire::RasterInfo info;
        if(!exact(dst,API,5)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeRaster(wire,h,info,rgba)) return;
        const uint64_t fields[]={h.stream,h.sequence,h.timestamp,info.width,info.height};
        for(size_t i=0;i<5;++i) API->object->array_int_set_idx(dst,i,fields[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(ra_decode) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire,rgba; mediawire::Header h; mediawire::RasterInfo info;
        if(!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeRaster(wire,h,info,rgba)||!exact(dst,API,rgba.size())) return;
        write(dst,API,rgba); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(rf_size) { auto n=GET_NEXT_INT(ARGS); RETURN->v_int=mediawire::fragmentPacketSize(n); }
CK_DLL_SFUN(rf_encode) {
    RETURN->v_int=0;
    try {
        uint64_t a[7]; for(auto &v:a) v=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!a[0]||a[0]>=0xffffffffu||a[1]>0xffffffffu||a[2]>0x7fffffffffffffffull||
           !mediawire::imagePixels(a[3],a[4])||a[5]>0xffffffffu||a[6]>=mediawire::maxImagePixels) return;
        if(!src) return;
        auto n=API->object->array_int_size(src);
        if(n<=0||n%4||!mediawire::fragmentPacketSize(n/4)||!exact(dst,API,mediawire::fragmentPacketSize(n/4))) return;
        mediawire::Bytes pixels,wire;
        if(!read(src,API,mediawire::maxPayload,pixels)) return;
        mediawire::Header h{1,static_cast<uint32_t>(a[0]),static_cast<uint32_t>(a[1]),a[2]};
        mediawire::Fragment f{static_cast<uint32_t>(a[3]),static_cast<uint32_t>(a[4]),static_cast<uint32_t>(a[5]),static_cast<uint32_t>(a[6]),static_cast<uint32_t>(n/4)};
        if(mediawire::encodeFragment(h,f,pixels,wire)) { write(dst,API,wire);RETURN->v_int=1; }
    } catch(...) {}
}
namespace {
t_CKUINT rsOffset=0;
mediawire::RasterStream *rsState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::RasterStream*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,rsOffset)));
}
}
CK_DLL_CTOR(rs_ctor) { OBJ_MEMBER_UINT(SELF,rsOffset)=0;try { OBJ_MEMBER_UINT(SELF,rsOffset)=reinterpret_cast<uintptr_t>(new mediawire::RasterStream); } catch(...) {} }
CK_DLL_DTOR(rs_dtor) { delete rsState(SELF,API);OBJ_MEMBER_UINT(SELF,rsOffset)=0; }
CK_DLL_MFUN(rs_configure) {
    auto *p=rsState(SELF,API);uint64_t a[4];for(auto &v:a)v=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&p->configure(a[0],a[1],a[2],a[3]);
}
CK_DLL_MFUN(rs_clear) { if(auto *p=rsState(SELF,API))p->clear(); }
CK_DLL_MFUN(rs_push) {
    RETURN->v_int=0;auto *p=rsState(SELF,API);if(!p)return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(!p->configured()){p->reject(1);return;}
        if(!read(a,API,mediawire::headerSize+mediawire::maxPayload,wire)){p->reject(2);return;}
        RETURN->v_int=p->push(wire);
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(rs_take) {
    RETURN->v_int=0;auto *p=rsState(SELF,API);if(!p)return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes pixels;
        if(!p->configured()){p->reject(1);return;}
        if(!p->ready()||!exact(a,API,p->pixelBytes())){p->reject(5);return;}
        if(p->take(p->pixelBytes(),pixels)){write(a,API,pixels);RETURN->v_int=1;}
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(rs_configured) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(rs_ready) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->ready():0; }
CK_DLL_MFUN(rs_nextSequence) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->nextSequence():0; }
CK_DLL_MFUN(rs_nextImage) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->nextImage():0; }
CK_DLL_MFUN(rs_receivedPixels) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->receivedPixels():0; }
CK_DLL_MFUN(rs_completed) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->completed():0; }
CK_DLL_MFUN(rs_timestamp) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->timestamp():0; }
CK_DLL_MFUN(rs_width) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->width():0; }
CK_DLL_MFUN(rs_height) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->height():0; }
CK_DLL_MFUN(rs_pixelBytes) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->pixelBytes():0; }
CK_DLL_MFUN(rs_error) { auto *p=rsState(SELF,API);RETURN->v_int=p?p->error():0; }
CK_DLL_SFUN(vg_size) {
    auto n=GET_NEXT_INT(ARGS); RETURN->v_int=mediawire::vectorPacketSize(n);
}
CK_DLL_SFUN(vg_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto svg=GET_NEXT_STRING_SAFE(ARGS);
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::vectorPacketSize(svg.size());
        if(!n||stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0||!exact(dst,API,n)) return;
        mediawire::Bytes data(svg.begin(),svg.end()),wire;
        if(mediawire::encodeVector({2,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)},data,wire)) {
            write(dst,API,wire);RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_SFUN(vg_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::Bytes wire,svg;
        if(!exact(dst,API,4)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeVector(wire,h,svg)) return;
        const uint64_t fields[]={h.stream,h.sequence,h.timestamp,svg.size()};
        for(size_t i=0;i<4;++i) API->object->array_int_set_idx(dst,i,fields[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(vg_decode) {
    std::string result;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::Bytes wire,svg;
        if(read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)&&mediawire::decodeVector(wire,h,svg))
            result.assign(svg.begin(),svg.end());
    } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}
CK_DLL_SFUN(md_size) {
    auto v=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS);RETURN->v_int=mediawire::modelPacketSize(v,t);
}
CK_DLL_SFUN(md_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto b=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!a||!b||stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0)return;
        auto n=API->object->array_float_size(a),m=API->object->array_int_size(b);
        if(n<9||m<3||n%3||m%3)return;
        auto bytes=mediawire::modelPacketSize(n/3,m/3);
        if(!bytes||!exact(dst,API,bytes))return;
        std::vector<double> xyz(n);std::vector<uint32_t> indices(m);
        for(t_CKINT i=0;i<n;++i)xyz[i]=API->object->array_float_get_idx(a,i);
        for(t_CKINT i=0;i<m;++i){auto v=API->object->array_int_get_idx(b,i);if(v<0||v>=n/3)return;indices[i]=static_cast<uint32_t>(v);}
        mediawire::Bytes wire;
        if(mediawire::encodeModel({3,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)},xyz,indices,wire)){
            write(dst,API,wire);RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_SFUN(md_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire;mediawire::Header h;std::vector<double> xyz;std::vector<uint32_t> indices;
        if(!exact(dst,API,5)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeModel(wire,h,xyz,indices))return;
        const uint64_t fields[]={h.stream,h.sequence,h.timestamp,xyz.size()/3,indices.size()/3};
        for(size_t i=0;i<5;++i)API->object->array_int_set_idx(dst,i,fields[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(md_decode) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto b=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire;mediawire::Header h;std::vector<double> xyz;std::vector<uint32_t> indices;
        if(!a||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeModel(wire,h,xyz,indices)||!exact(b,API,indices.size())||
           API->object->array_float_size(a)!=static_cast<t_CKINT>(xyz.size()))return;
        for(size_t i=0;i<xyz.size();++i)API->object->array_float_set_idx(a,i,xyz[i]);
        for(size_t i=0;i<indices.size();++i)API->object->array_int_set_idx(b,i,indices[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(gr_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire;mediawire::Header h;uint64_t a=0,b=0;
        if(!exact(dst,API,6)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire))return;
        int kind=mediawire::graphicsKind(wire,h,a,b);
        if(!kind)return;
        const uint64_t fields[]={static_cast<uint64_t>(kind),h.stream,h.sequence,h.timestamp,a,b};
        for(size_t i=0;i<6;++i)API->object->array_int_set_idx(dst,i,fields[i]);
        RETURN->v_int=kind;
    } catch(...) {}
}
CK_DLL_SFUN(vf_size) {
    auto width=GET_NEXT_INT(ARGS),height=GET_NEXT_INT(ARGS);
    RETURN->v_int=mediawire::videoFramePacketSize(width,height);
}
CK_DLL_SFUN(vf_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto width=GET_NEXT_INT(ARGS),height=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::videoFramePacketSize(width,height);
        if(!n||stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0||!exact(dst,API,n)||
           !exact(src,API,n-mediawire::headerSize-mediawire::videoFrameHeader)) return;
        mediawire::Bytes rgba,wire;
        if(!read(src,API,mediawire::maxPayload,rgba)) return;
        if(mediawire::encodeVideoFrame({6,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)},
           {static_cast<uint32_t>(width),static_cast<uint32_t>(height)},rgba,wire)) {
            write(dst,API,wire); RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_SFUN(vf_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire,rgba; mediawire::Header h; mediawire::VideoFrameInfo info;
        if(!exact(dst,API,5)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeVideoFrame(wire,h,info,rgba)) return;
        const uint64_t fields[]={h.stream,h.sequence,h.timestamp,info.width,info.height};
        for(size_t i=0;i<5;++i) API->object->array_int_set_idx(dst,i,fields[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(vf_decode) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire,rgba; mediawire::Header h; mediawire::VideoFrameInfo info;
        if(!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeVideoFrame(wire,h,info,rgba)||!exact(dst,API,rgba.size())) return;
        write(dst,API,rgba); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(vsf_size) { auto n=GET_NEXT_INT(ARGS); RETURN->v_int=mediawire::videoFragmentPacketSize(n); }
CK_DLL_SFUN(vsf_encode) {
    RETURN->v_int=0;
    try {
        uint64_t a[7]; for(auto &v:a) v=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(!a[0]||a[0]>=0xffffffffu||a[1]>0xffffffffu||a[2]>0x7fffffffffffffffull||
           !mediawire::videoPixels(a[3],a[4])||a[5]>0xffffffffu||a[6]>=mediawire::maxVideoPixels) return;
        if(!src) return;
        auto n=API->object->array_int_size(src);
        if(n<=0||n%4||!mediawire::videoFragmentPacketSize(n/4)||!exact(dst,API,mediawire::videoFragmentPacketSize(n/4))) return;
        mediawire::Bytes pixels,wire;
        if(!read(src,API,mediawire::maxPayload,pixels)) return;
        mediawire::Header h{6,static_cast<uint32_t>(a[0]),static_cast<uint32_t>(a[1]),a[2]};
        mediawire::VideoFragment f{static_cast<uint32_t>(a[3]),static_cast<uint32_t>(a[4]),static_cast<uint32_t>(a[5]),static_cast<uint32_t>(a[6]),static_cast<uint32_t>(n/4)};
        if(mediawire::encodeVideoFragment(h,f,pixels,wire)) { write(dst,API,wire);RETURN->v_int=1; }
    } catch(...) {}
}
namespace {
t_CKUINT vsOffset=0;
mediawire::VideoStream *vsState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::VideoStream*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,vsOffset)));
}
}
CK_DLL_CTOR(vs_ctor) { OBJ_MEMBER_UINT(SELF,vsOffset)=0;try { OBJ_MEMBER_UINT(SELF,vsOffset)=reinterpret_cast<uintptr_t>(new mediawire::VideoStream); } catch(...) {} }
CK_DLL_DTOR(vs_dtor) { delete vsState(SELF,API);OBJ_MEMBER_UINT(SELF,vsOffset)=0; }
CK_DLL_MFUN(vs_configure) {
    auto *p=vsState(SELF,API);uint64_t a[4];for(auto &v:a)v=GET_NEXT_INT(ARGS);
    RETURN->v_int=p&&p->configure(a[0],a[1],a[2],a[3]);
}
CK_DLL_MFUN(vs_clear) { if(auto *p=vsState(SELF,API))p->clear(); }
CK_DLL_MFUN(vs_push) {
    RETURN->v_int=0;auto *p=vsState(SELF,API);if(!p)return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(!p->configured()){p->reject(1);return;}
        if(!read(a,API,mediawire::headerSize+mediawire::maxPayload,wire)){p->reject(2);return;}
        RETURN->v_int=p->push(wire);
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(vs_take) {
    RETURN->v_int=0;auto *p=vsState(SELF,API);if(!p)return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes pixels;
        if(!p->configured()){p->reject(1);return;}
        if(!p->ready()||!exact(a,API,p->pixelBytes())){p->reject(5);return;}
        if(p->take(p->pixelBytes(),pixels)){write(a,API,pixels);RETURN->v_int=1;}
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(vs_configured) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(vs_ready) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->ready():0; }
CK_DLL_MFUN(vs_nextSequence) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->nextSequence():0; }
CK_DLL_MFUN(vs_nextFrame) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->nextFrame():0; }
CK_DLL_MFUN(vs_receivedPixels) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->receivedPixels():0; }
CK_DLL_MFUN(vs_completed) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->completed():0; }
CK_DLL_MFUN(vs_timestamp) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->timestamp():0; }
CK_DLL_MFUN(vs_width) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->width():0; }
CK_DLL_MFUN(vs_height) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->height():0; }
CK_DLL_MFUN(vs_pixelBytes) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->pixelBytes():0; }
CK_DLL_MFUN(vs_error) { auto *p=vsState(SELF,API);RETURN->v_int=p?p->error():0; }
namespace {
t_CKUINT vbOffset=0;
mediawire::VideoBuffer *vbState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::VideoBuffer*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,vbOffset)));
}
}
CK_DLL_CTOR(vb_ctor) { OBJ_MEMBER_UINT(SELF,vbOffset)=0;try { OBJ_MEMBER_UINT(SELF,vbOffset)=reinterpret_cast<uintptr_t>(new mediawire::VideoBuffer); } catch(...) {} }
CK_DLL_DTOR(vb_dtor) { delete vbState(SELF,API);OBJ_MEMBER_UINT(SELF,vbOffset)=0; }
CK_DLL_MFUN(vb_configure) {
    RETURN->v_int=0;
    try {
        auto *p=vbState(SELF,API);uint64_t a[7];for(auto &v:a)v=GET_NEXT_INT(ARGS);
        if(p)RETURN->v_int=p->configure(a[0],a[1],a[2],a[3],a[4],a[5],a[6]);
    } catch(...) {}
}
CK_DLL_MFUN(vb_clear) { if(auto *p=vbState(SELF,API))p->clear(); }
CK_DLL_MFUN(vb_push) {
    RETURN->v_int=0;auto *p=vbState(SELF,API);if(!p)return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(!p->configured()){p->reject(1);return;}
        if(!read(a,API,mediawire::headerSize+mediawire::maxPayload,wire)){p->reject(2);return;}
        RETURN->v_int=p->push(wire);
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(vb_pull) {
    RETURN->v_int=0;auto *p=vbState(SELF,API);if(!p)return;
    try {
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes pixels;
        if(!a){p->reject(2);return;}
        if(p->pull(API->object->array_int_size(a),pixels)){write(a,API,pixels);RETURN->v_int=1;}
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(vb_configured) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(vb_capacity) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->capacity():0; }
CK_DLL_MFUN(vb_threshold) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->threshold():0; }
CK_DLL_MFUN(vb_available) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->available():0; }
CK_DLL_MFUN(vb_freeFrames) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->freeFrames():0; }
CK_DLL_MFUN(vb_budget) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->budget():0; }
CK_DLL_MFUN(vb_bufferedBytes) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->bufferedBytes():0; }
CK_DLL_MFUN(vb_reservedBytes) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->reservedBytes():0; }
CK_DLL_MFUN(vb_ready) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->ready():0; }
CK_DLL_MFUN(vb_receivedFrames) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->receivedFrames():0; }
CK_DLL_MFUN(vb_consumedFrames) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->consumedFrames():0; }
CK_DLL_MFUN(vb_nextSequence) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->nextSequence():0; }
CK_DLL_MFUN(vb_nextFrame) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->nextFrame():0; }
CK_DLL_MFUN(vb_underruns) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->underruns():0; }
CK_DLL_MFUN(vb_overruns) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->overruns():0; }
CK_DLL_MFUN(vb_error) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->error():0; }
CK_DLL_MFUN(vb_pixelBytes) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->pixelBytes():0; }
CK_DLL_MFUN(vb_width) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->width():0; }
CK_DLL_MFUN(vb_height) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->height():0; }
CK_DLL_MFUN(vb_timestamp) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->timestamp():0; }
CK_DLL_MFUN(vb_frameIndex) { auto *p=vbState(SELF,API);RETURN->v_int=p?p->frameIndex():0; }
namespace {
t_CKUINT syncAVOffset=0;
mediawire::AudioVideo *syncAVState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::AudioVideo*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,syncAVOffset)));
}
}
CK_DLL_CTOR(sav_ctor) { OBJ_MEMBER_UINT(SELF,syncAVOffset)=0;try { OBJ_MEMBER_UINT(SELF,syncAVOffset)=reinterpret_cast<uintptr_t>(new mediawire::AudioVideo); } catch(...) {} }
CK_DLL_DTOR(sav_dtor) { delete syncAVState(SELF,API);OBJ_MEMBER_UINT(SELF,syncAVOffset)=0; }
CK_DLL_MFUN(sav_configure) {
    RETURN->v_int=0;
    try { auto *p=syncAVState(SELF,API);uint64_t a[13];for(auto &v:a)v=GET_NEXT_INT(ARGS);
        if(p)RETURN->v_int=p->configure(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12]);
    } catch(...) {}
}
CK_DLL_MFUN(sav_clear) { if(auto *p=syncAVState(SELF,API))p->clear(); }
CK_DLL_MFUN(sav_push) {
    RETURN->v_int=0;auto *p=syncAVState(SELF,API);if(!p)return;
    try { auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(!p->configured()){p->reject(1);return;}
        if(!read(a,API,mediawire::headerSize+mediawire::maxPayload,wire)){p->reject(2);return;}
        RETURN->v_int=p->push(wire);
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(sav_pullAudio) {
    RETURN->v_int=0;auto *p=syncAVState(SELF,API);if(!p)return;
    try {
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p->configured()){p->reject(1);return;}
        if(!a){p->reject(2);return;}
        auto n=API->object->array_float_size(a);
        if(n<=0||n%p->channels()){p->reject(2);return;}
        std::vector<double> data;
        if(!p->pullAudio(n/p->channels(),data))return;
        for(size_t i=0;i<data.size();++i)API->object->array_float_set_idx(a,i,data[i]);
        RETURN->v_int=1;
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(sav_pullVideo) {
    RETURN->v_int=0;auto *p=syncAVState(SELF,API);if(!p)return;
    try { auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes pixels;
        if(!a){p->reject(2);return;}
        if(p->pullVideo(API->object->array_int_size(a),pixels)){write(a,API,pixels);RETURN->v_int=1;}
    } catch(...) {p->reject(2);}
}
CK_DLL_MFUN(sav_configured) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->configured():0; }
CK_DLL_MFUN(sav_started) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->started():0; }
CK_DLL_MFUN(sav_ready) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->ready():0; }
CK_DLL_MFUN(sav_videoDue) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoDue():0; }
CK_DLL_MFUN(sav_playTimestamp) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->playTimestamp():0; }
CK_DLL_MFUN(sav_consumedFrames) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->consumedFrames():0; }
CK_DLL_MFUN(sav_channels) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->channels():0; }
CK_DLL_MFUN(sav_availableAudio) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->availableAudio():0; }
CK_DLL_MFUN(sav_pendingVideo) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->pendingVideo():0; }
CK_DLL_MFUN(sav_videoBytes) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoBytes():0; }
CK_DLL_MFUN(sav_videoWidth) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoWidth():0; }
CK_DLL_MFUN(sav_videoHeight) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoHeight():0; }
CK_DLL_MFUN(sav_videoTimestamp) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoTimestamp():0; }
CK_DLL_MFUN(sav_videoIndex) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoIndex():0; }
CK_DLL_MFUN(sav_videoLateness) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoLateness():0; }
CK_DLL_MFUN(sav_error) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->error():0; }
CK_DLL_MFUN(sav_audioError) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->audioError():0; }
CK_DLL_MFUN(sav_videoError) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->videoError():0; }
CK_DLL_MFUN(sav_audioLimit) { auto *p=syncAVState(SELF,API);RETURN->v_int=p?p->audioLimit():0; }
CK_DLL_SFUN(avp_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire;std::array<uint64_t,9> metadata{};
        if(!exact(dst,API,9)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire))return;
        int kind=mediawire::inspectAV(wire,metadata);
        if(!kind)return;
        for(size_t i=0;i<metadata.size();++i)API->object->array_int_set_idx(dst,i,metadata[i]);
        RETURN->v_int=kind;
    } catch(...) {}
}
CK_DLL_SFUN(tx_size) {
    auto n=GET_NEXT_INT(ARGS); RETURN->v_int=mediawire::textPacketSize(n);
}
CK_DLL_SFUN(tx_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto text=GET_NEXT_STRING_SAFE(ARGS);
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::textPacketSize(text.size());
        if(!n||stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0||!exact(dst,API,n)) return;
        mediawire::Bytes data(text.begin(),text.end()),wire;
        if(mediawire::encodeText({7,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)},data,wire)) {
            write(dst,API,wire);RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_SFUN(tx_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::Bytes wire,text;
        if(!exact(dst,API,4)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeText(wire,h,text)) return;
        const uint64_t fields[]={h.stream,h.sequence,h.timestamp,text.size()};
        for(size_t i=0;i<4;++i) API->object->array_int_set_idx(dst,i,fields[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(tx_decode) {
    std::string result;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::Bytes wire,text;
        if(read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)&&mediawire::decodeText(wire,h,text))
            result.assign(text.begin(),text.end());
    } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}
namespace {
t_CKUINT textAssemblerOffset=0;
mediawire::TextAssembler *textAssembler(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<mediawire::TextAssembler*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,textAssemblerOffset)));
}
}
CK_DLL_SFUN(tf_size) {
    RETURN->v_int=mediawire::textFragmentPacketSize(GET_NEXT_INT(ARGS));
}
CK_DLL_SFUN(tf_encode) {
    RETURN->v_int=0;
    try {
        auto stream=GET_NEXT_INT(ARGS),seq=GET_NEXT_INT(ARGS),ts=GET_NEXT_INT(ARGS);
        auto first=GET_NEXT_INT(ARGS),offset=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
        auto text=GET_NEXT_STRING_SAFE(ARGS);
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(stream<1||static_cast<uint64_t>(stream)>=0xffffffffu||seq<0||
           static_cast<uint64_t>(seq)>0xffffffffu||ts<0||first<0||first>seq||
           offset<0||count<=0||text.size()>mediawire::maxTextMessage||
           static_cast<uint64_t>(offset)>=text.size()||
           static_cast<uint64_t>(count)>text.size()-static_cast<size_t>(offset))return;
        auto n=mediawire::textFragmentPacketSize(count);
        if(!n||!exact(dst,API,n))return;
        mediawire::Bytes data(text.begin(),text.end()),wire;
        if(!mediawire::textUtf8(data))return;
        mediawire::Bytes part(data.begin()+offset,data.begin()+offset+count);
        if(mediawire::encodeTextFragment({7,static_cast<uint32_t>(stream),static_cast<uint32_t>(seq),static_cast<uint64_t>(ts)},
             static_cast<uint32_t>(data.size()),static_cast<uint32_t>(offset),static_cast<uint32_t>(first),part,wire)){
            write(dst,API,wire);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(tf_inspect) {
    RETURN->v_int=0;
    try {
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::Bytes wire,data;uint32_t t,o,f;
        if(!exact(dst,API,7)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeTextFragment(wire,h,t,o,f,data))return;
        const uint64_t fields[]={h.stream,h.sequence,h.timestamp,f,t,o,data.size()};
        for(size_t i=0;i<7;++i)API->object->array_int_set_idx(dst,i,fields[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_CTOR(ta_ctor) {
    OBJ_MEMBER_UINT(SELF,textAssemblerOffset)=0;
    try{OBJ_MEMBER_UINT(SELF,textAssemblerOffset)=reinterpret_cast<uintptr_t>(new mediawire::TextAssembler);}catch(...){}
}
CK_DLL_DTOR(ta_dtor) {
    delete textAssembler(SELF,API);OBJ_MEMBER_UINT(SELF,textAssemblerOffset)=0;
}
CK_DLL_MFUN(ta_reset) {if(auto *p=textAssembler(SELF,API))p->reset();}
CK_DLL_MFUN(ta_size) {auto *p=textAssembler(SELF,API);RETURN->v_int=p?p->size():0;}
CK_DLL_MFUN(ta_ready) {auto *p=textAssembler(SELF,API);RETURN->v_int=p&&p->ready();}
CK_DLL_MFUN(ta_push) {
    RETURN->v_int=0;
    try{
        auto *p=textAssembler(SELF,API);auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire;
        if(p&&read(src,API,mediawire::headerSize+mediawire::maxPayload,wire))RETURN->v_int=p->push(wire);
    }catch(...){}
}
CK_DLL_MFUN(ta_text) {
    std::string result;
    try{auto *p=textAssembler(SELF,API);if(p&&p->ready())result.assign(p->data().begin(),p->data().end());}catch(...){}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}
namespace {
t_CKUINT textBufferOffset=0;
mediawire::TextBuffer *textBuffer(Chuck_Object *self,CK_DL_API API){
    return reinterpret_cast<mediawire::TextBuffer*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,textBufferOffset)));
}
}
CK_DLL_CTOR(tb_ctor){
    OBJ_MEMBER_UINT(SELF,textBufferOffset)=0;
    try{OBJ_MEMBER_UINT(SELF,textBufferOffset)=reinterpret_cast<uintptr_t>(new mediawire::TextBuffer);}catch(...){}
}
CK_DLL_DTOR(tb_dtor){delete textBuffer(SELF,API);OBJ_MEMBER_UINT(SELF,textBufferOffset)=0;}
CK_DLL_MFUN(tb_clear){if(auto *p=textBuffer(SELF,API))p->clear();}
CK_DLL_MFUN(tb_configure){
    RETURN->v_int=0;
    try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),d=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS),b=GET_NEXT_INT(ARGS);
        auto *p=textBuffer(SELF,API);
        if(p&&s>0&&q>=0&&d>=0&&c>0&&b>0)RETURN->v_int=p->configure(s,q,d,c,b);
    }catch(...){}
}
CK_DLL_MFUN(tb_push){
    RETURN->v_int=0;
    try{auto *p=textBuffer(SELF,API);auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes w;
        if(p&&read(a,API,mediawire::headerSize+mediawire::maxPayload,w))RETURN->v_int=p->push(w);
    }catch(...){}
}
CK_DLL_MFUN(tb_advance){auto *p=textBuffer(SELF,API);auto t=GET_NEXT_INT(ARGS);RETURN->v_int=p&&t>=0&&p->advance(t);}
CK_DLL_MFUN(tb_ready){auto *p=textBuffer(SELF,API);RETURN->v_int=p&&p->ready();}
CK_DLL_MFUN(tb_available){auto *p=textBuffer(SELF,API);RETURN->v_int=p?p->available():0;}
CK_DLL_MFUN(tb_buffered){auto *p=textBuffer(SELF,API);RETURN->v_int=p?p->bufferedBytes():0;}
CK_DLL_MFUN(tb_reserved){auto *p=textBuffer(SELF,API);RETURN->v_int=p?p->reservedBytes():0;}
CK_DLL_MFUN(tb_sequence){auto *p=textBuffer(SELF,API);RETURN->v_int=p?p->nextSequence():0;}
CK_DLL_MFUN(tb_timestamp){auto *p=textBuffer(SELF,API);RETURN->v_int=p?p->timestamp():0;}
CK_DLL_MFUN(tb_pull){
    std::string result;auto *p=textBuffer(SELF,API);
    try{if(p&&p->ready())result.assign(p->front().begin(),p->front().end());}catch(...){}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
    if(p&&!result.empty()&&RETURN->v_string)p->pop();
}
namespace {
t_CKUINT routerOffset=0;
mediawire::MediaRouter *router(Chuck_Object *self,CK_DL_API API){
    return reinterpret_cast<mediawire::MediaRouter*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,routerOffset)));
}
}
CK_DLL_SFUN(media_inspect){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes wire;std::array<uint64_t,6> m{};
        if(!exact(dst,API,6)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire))return;
        int k=mediawire::inspectMedia(wire,m);if(!k)return;
        for(size_t i=0;i<m.size();++i)API->object->array_int_set_idx(dst,i,m[i]);
        RETURN->v_int=k;
    }catch(...){}
}
CK_DLL_CTOR(router_ctor){
    OBJ_MEMBER_UINT(SELF,routerOffset)=0;
    try{OBJ_MEMBER_UINT(SELF,routerOffset)=reinterpret_cast<uintptr_t>(new mediawire::MediaRouter);}catch(...){}
}
CK_DLL_DTOR(router_dtor){delete router(SELF,API);OBJ_MEMBER_UINT(SELF,routerOffset)=0;}
CK_DLL_MFUN(router_clear){if(auto *p=router(SELF,API))p->clear();}
CK_DLL_MFUN(router_configure){
    auto *p=router(SELF,API);auto m=GET_NEXT_INT(ARGS);RETURN->v_int=p&&m>0&&p->configure(m);
}
CK_DLL_MFUN(router_add){
    RETURN->v_int=0;
    try{auto *p=router(SELF,API);auto s=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),o=GET_NEXT_INT(ARGS);
        if(p&&s>0&&t>0&&q>=0&&o>=0)RETURN->v_int=p->add(s,t,q,o);
    }catch(...){}
}
CK_DLL_MFUN(router_streams){auto *p=router(SELF,API);RETURN->v_int=p?p->streams():0;}
CK_DLL_MFUN(router_next){auto *p=router(SELF,API);auto s=GET_NEXT_INT(ARGS);RETURN->v_int=p&&s>0?p->next(s):0;}
CK_DLL_MFUN(router_count){auto *p=router(SELF,API);auto s=GET_NEXT_INT(ARGS);RETURN->v_int=p&&s>0?p->count(s):0;}
CK_DLL_MFUN(router_route){
    RETURN->v_int=0;
    try{
        auto *p=router(SELF,API);auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;std::array<uint64_t,6> m{};
        if(!p||!exact(dst,API,6)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire))return;
        int k=p->route(wire,m);if(!k)return;
        for(size_t i=0;i<m.size();++i)API->object->array_int_set_idx(dst,i,m[i]);
        RETURN->v_int=k;
    }catch(...){}
}
CK_DLL_SFUN(rc_size){
    RETURN->v_int=0;
    try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes pixels,wire;
        auto n=mediawire::imagePixels(w,h);
        if(w>0&&h>0&&n&&exact(src,API,4*n)&&read(src,API,4*n,pixels)&&
           mediawire::encodeRasterCompressed({1,1,0,0},{static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,wire))
            RETURN->v_int=wire.size();
    }catch(...){}
}
CK_DLL_SFUN(rc_encode){
    RETURN->v_int=0;
    try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::imagePixels(w,h);mediawire::Bytes pixels,wire;
        if(s<=0||static_cast<uint64_t>(s)>=0xffffffffu||q<0||static_cast<uint64_t>(q)>0xffffffffu||t<0||w<=0||h<=0||
           !n||!exact(src,API,4*n)||!read(src,API,4*n,pixels))return;
        if(mediawire::encodeRasterCompressed({1,static_cast<uint32_t>(s),static_cast<uint32_t>(q),static_cast<uint64_t>(t)},
             {static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,wire)&&exact(dst,API,wire.size())){
            write(dst,API,wire);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rc_inspect){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;mediawire::Bytes wire,pixels;
        if(!exact(dst,API,5)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeRasterCompressed(wire,h,info,pixels))return;
        const uint64_t m[]={h.stream,h.sequence,h.timestamp,info.width,info.height};
        for(size_t i=0;i<5;++i)API->object->array_int_set_idx(dst,i,m[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(rc_decode){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;mediawire::Bytes wire,pixels;
        if(read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)&&
           mediawire::decodeRasterCompressed(wire,h,info,pixels)&&exact(dst,API,pixels.size())){
            write(dst,API,pixels);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rp_size){
    RETURN->v_int=0;
    try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes pixels,wire;
        auto n=mediawire::imagePixels(w,h);
        if(w>0&&h>0&&n&&exact(src,API,4*n)&&read(src,API,4*n,pixels)&&
           mediawire::encodeRasterPng({1,1,0,0},{static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,wire))
            RETURN->v_int=wire.size();
    }catch(...){}
}
CK_DLL_SFUN(rp_encode){
    RETURN->v_int=0;
    try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::imagePixels(w,h);mediawire::Bytes pixels,wire;
        if(s<=0||static_cast<uint64_t>(s)>=0xffffffffu||q<0||static_cast<uint64_t>(q)>0xffffffffu||t<0||w<=0||h<=0||
           !n||!exact(src,API,4*n)||!read(src,API,4*n,pixels))return;
        if(mediawire::encodeRasterPng({1,static_cast<uint32_t>(s),static_cast<uint32_t>(q),static_cast<uint64_t>(t)},
             {static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,wire)&&exact(dst,API,wire.size())){
            write(dst,API,wire);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rp_inspect){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;mediawire::Bytes wire,pixels;
        if(!exact(dst,API,5)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeRasterPng(wire,h,info,pixels))return;
        const uint64_t m[]={h.stream,h.sequence,h.timestamp,info.width,info.height};
        for(size_t i=0;i<5;++i)API->object->array_int_set_idx(dst,i,m[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(rp_decode){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;mediawire::Bytes wire,pixels;
        if(read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)&&
           mediawire::decodeRasterPng(wire,h,info,pixels)&&exact(dst,API,pixels.size())){
            write(dst,API,pixels);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rj_size){
    RETURN->v_int=0;
    try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto quality=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes pixels,wire;
        if(quality<1||quality>100)return;
        auto n=mediawire::imagePixels(w,h);
        if(w>0&&h>0&&n&&exact(src,API,4*n)&&read(src,API,4*n,pixels)&&
           mediawire::encodeRasterPhoto({1,1,0,0},{static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,static_cast<int>(quality),false,false,wire))
            RETURN->v_int=wire.size();
    }catch(...){}
}
CK_DLL_SFUN(rj_encode){
    RETURN->v_int=0;
    try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto quality=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(quality<1||quality>100)return;
        auto n=mediawire::imagePixels(w,h);mediawire::Bytes pixels,wire;
        if(s<=0||static_cast<uint64_t>(s)>=0xffffffffu||q<0||static_cast<uint64_t>(q)>0xffffffffu||t<0||w<=0||h<=0||
           !n||!exact(src,API,4*n)||!read(src,API,4*n,pixels))return;
        if(mediawire::encodeRasterPhoto({1,static_cast<uint32_t>(s),static_cast<uint32_t>(q),static_cast<uint64_t>(t)},
             {static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,static_cast<int>(quality),false,false,wire)&&exact(dst,API,wire.size())){
            write(dst,API,wire);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rj_inspect){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;bool lossless=false;mediawire::Bytes wire,pixels;
        if(!exact(dst,API,5)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeRasterPhoto(wire,h,info,false,lossless,pixels))return;
        const uint64_t m[]={h.stream,h.sequence,h.timestamp,info.width,info.height};
        for(size_t i=0;i<5;++i)API->object->array_int_set_idx(dst,i,m[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(rj_decode){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;bool lossless=false;mediawire::Bytes wire,pixels;
        if(read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)&&
           mediawire::decodeRasterPhoto(wire,h,info,false,lossless,pixels)&&exact(dst,API,pixels.size())){
            write(dst,API,pixels);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rw_size){
    RETURN->v_int=0;
    try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto quality=GET_NEXT_INT(ARGS); auto mode=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Bytes pixels,wire;
        if(quality<1||quality>100||mode<0||mode>1)return;
        auto n=mediawire::imagePixels(w,h);
        if(w>0&&h>0&&n&&exact(src,API,4*n)&&read(src,API,4*n,pixels)&&
           mediawire::encodeRasterPhoto({1,1,0,0},{static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,static_cast<int>(quality),true,mode==1,wire))
            RETURN->v_int=wire.size();
    }catch(...){}
}
CK_DLL_SFUN(rw_encode){
    RETURN->v_int=0;
    try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto quality=GET_NEXT_INT(ARGS); auto mode=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        if(quality<1||quality>100||mode<0||mode>1)return;
        auto n=mediawire::imagePixels(w,h);mediawire::Bytes pixels,wire;
        if(s<=0||static_cast<uint64_t>(s)>=0xffffffffu||q<0||static_cast<uint64_t>(q)>0xffffffffu||t<0||w<=0||h<=0||
           !n||!exact(src,API,4*n)||!read(src,API,4*n,pixels))return;
        if(mediawire::encodeRasterPhoto({1,static_cast<uint32_t>(s),static_cast<uint32_t>(q),static_cast<uint64_t>(t)},
             {static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,static_cast<int>(quality),true,mode==1,wire)&&exact(dst,API,wire.size())){
            write(dst,API,wire);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rw_inspect){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;bool lossless=false;mediawire::Bytes wire,pixels;
        if(!exact(dst,API,6)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||
           !mediawire::decodeRasterPhoto(wire,h,info,true,lossless,pixels))return;
        const uint64_t m[]={h.stream,h.sequence,h.timestamp,info.width,info.height,lossless?1ull:0ull};
        for(size_t i=0;i<6;++i)API->object->array_int_set_idx(dst,i,m[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(rw_decode){
    RETURN->v_int=0;
    try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        mediawire::Header h;mediawire::RasterInfo info;bool lossless=false;mediawire::Bytes wire,pixels;
        if(read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)&&
           mediawire::decodeRasterPhoto(wire,h,info,true,lossless,pixels)&&exact(dst,API,pixels.size())){
            write(dst,API,pixels);RETURN->v_int=1;
        }
    }catch(...){}
}
CK_DLL_SFUN(rj_flatten){
    RETURN->v_int=0;
    try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),r=GET_NEXT_INT(ARGS),g=GET_NEXT_INT(ARGS),b=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::imagePixels(w,h);mediawire::Bytes a,z;
        if(w>0&&h>0&&n&&r>=0&&g>=0&&b>=0&&exact(src,API,4*n)&&exact(dst,API,4*n)&&read(src,API,4*n,a)&&
           mediawire::flattenRaster(w,h,r,g,b,a,z)){write(dst,API,z);RETURN->v_int=1;}
    }catch(...){}
}
static bool cf_make(CK_DL_API API, t_CKINT s,t_CKINT q,t_CKINT t,t_CKINT w,t_CKINT h,t_CKINT image,t_CKINT row,t_CKINT rows,t_CKINT codec,t_CKINT quality,Arr src,mediawire::Bytes &wire){
    if(s<=0||s>=0xffffffffull||q<0||q>0xffffffffull||t<0||w<=0||h<=0||!mediawire::imagePixels(w,h)||
       image<0||image>0xffffffffull||row<0||row>=h||rows<=0||rows>h-row||codec<1||codec>5||quality<1||quality>100)return false;
    mediawire::Bytes pixels;auto n=4ull*w*rows;
    if(!exact(src,API,n)||!read(src,API,n,pixels))return false;
    return mediawire::encodeCompressedFragment({1,static_cast<uint32_t>(s),static_cast<uint32_t>(q),static_cast<uint64_t>(t)},
        {static_cast<uint32_t>(w),static_cast<uint32_t>(h),static_cast<uint32_t>(image),static_cast<uint32_t>(row),static_cast<uint32_t>(rows),static_cast<uint32_t>(codec)},pixels,static_cast<int>(quality),wire);
}
CK_DLL_SFUN(cf_size){
    RETURN->v_int=0;try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),row=GET_NEXT_INT(ARGS),rows=GET_NEXT_INT(ARGS),codec=GET_NEXT_INT(ARGS),quality=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(cf_make(API,1,0,0,w,h,0,row,rows,codec,quality,src,wire))RETURN->v_int=wire.size();
    }catch(...){}
}
CK_DLL_SFUN(cf_encode){
    RETURN->v_int=0;try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),image=GET_NEXT_INT(ARGS),row=GET_NEXT_INT(ARGS),rows=GET_NEXT_INT(ARGS),codec=GET_NEXT_INT(ARGS),quality=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(cf_make(API,s,q,t,w,h,image,row,rows,codec,quality,src,wire)&&exact(dst,API,wire.size())){write(dst,API,wire);RETURN->v_int=1;}
    }catch(...){}
}
CK_DLL_SFUN(cf_inspect){
    RETURN->v_int=0;try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire,pixels;mediawire::Header h;mediawire::CompressedFragment f;
        if(!exact(dst,API,9)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||!mediawire::decodeCompressedFragment(wire,h,f,pixels))return;
        const uint64_t m[]={h.stream,h.sequence,h.timestamp,f.width,f.height,f.image,f.row,f.rows,f.codec};
        for(size_t i=0;i<9;++i)API->object->array_int_set_idx(dst,i,m[i]);RETURN->v_int=1;
    }catch(...){}
}
bool vcf_make(CK_DL_API API, t_CKINT s,t_CKINT q,t_CKINT t,t_CKINT w,t_CKINT h,t_CKINT image,t_CKINT row,t_CKINT rows,t_CKINT codec,t_CKINT quality,Arr src,mediawire::Bytes &wire){
    if(s<=0||s>=0xffffffffull||q<0||q>0xffffffffull||t<0||w<=0||h<=0||!mediawire::videoPixels(w,h)||
       image<0||image>0xffffffffull||row<0||row>=h||rows<=0||rows>h-row||codec<1||codec>5||quality<1||quality>100)return false;
    mediawire::Bytes pixels;auto n=4ull*w*rows;if(!mediawire::videoFragmentPacketSize(n/4))return false;
    if(!exact(src,API,n)||!read(src,API,n,pixels))return false;
    return mediawire::encodeVideoCompressed({6,static_cast<uint32_t>(s),static_cast<uint32_t>(q),static_cast<uint64_t>(t)},
        {static_cast<uint32_t>(w),static_cast<uint32_t>(h),static_cast<uint32_t>(image),static_cast<uint32_t>(w*row),static_cast<uint32_t>(w*rows),static_cast<uint32_t>(codec)},pixels,static_cast<int>(quality),wire);
}
CK_DLL_SFUN(vcf_size){
    RETURN->v_int=0;try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),row=GET_NEXT_INT(ARGS),rows=GET_NEXT_INT(ARGS),codec=GET_NEXT_INT(ARGS),quality=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(vcf_make(API,1,0,0,w,h,0,row,rows,codec,quality,src,wire))RETURN->v_int=wire.size();
    }catch(...){}
}
CK_DLL_SFUN(vcf_encode){
    RETURN->v_int=0;try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),image=GET_NEXT_INT(ARGS),row=GET_NEXT_INT(ARGS),rows=GET_NEXT_INT(ARGS),codec=GET_NEXT_INT(ARGS),quality=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;
        if(vcf_make(API,s,q,t,w,h,image,row,rows,codec,quality,src,wire)&&exact(dst,API,wire.size())){write(dst,API,wire);RETURN->v_int=1;}
    }catch(...){}
}
CK_DLL_SFUN(vcf_inspect){
    RETURN->v_int=0;try{
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire,pixels;mediawire::Header h;mediawire::VideoFragment f;
        if(!exact(dst,API,9)||!read(src,API,mediawire::headerSize+mediawire::maxPayload,wire)||!mediawire::decodeVideoFragment(wire,h,f,pixels)||!f.codec)return;
        const uint64_t m[]={h.stream,h.sequence,h.timestamp,f.width,f.height,f.frame,f.offset/f.width,f.count/f.width,f.codec};
        for(size_t i=0;i<9;++i)API->object->array_int_set_idx(dst,i,m[i]);RETURN->v_int=1;
    }catch(...){}
}
namespace {t_CKUINT caOffset=0;
mediawire::CompressedRasterAssembler *ca(Chuck_Object *self,CK_DL_API API){return reinterpret_cast<mediawire::CompressedRasterAssembler*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,caOffset)));}}
CK_DLL_CTOR(ca_ctor){OBJ_MEMBER_UINT(SELF,caOffset)=0;try{OBJ_MEMBER_UINT(SELF,caOffset)=reinterpret_cast<uintptr_t>(new mediawire::CompressedRasterAssembler);}catch(...){}}
CK_DLL_DTOR(ca_dtor){delete ca(SELF,API);OBJ_MEMBER_UINT(SELF,caOffset)=0;}
CK_DLL_MFUN(ca_reset){if(auto *p=ca(SELF,API))p->reset();}
CK_DLL_MFUN(ca_rows){auto *p=ca(SELF,API);RETURN->v_int=p?p->rows():0;}
CK_DLL_MFUN(ca_size){auto *p=ca(SELF,API);RETURN->v_int=p?p->size():0;}
CK_DLL_MFUN(ca_ready){auto *p=ca(SELF,API);RETURN->v_int=p&&p->ready();}
CK_DLL_MFUN(ca_push){RETURN->v_int=0;try{auto *p=ca(SELF,API);auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));mediawire::Bytes wire;if(p&&read(a,API,mediawire::headerSize+mediawire::maxPayload,wire))RETURN->v_int=p->push(wire);}catch(...){}}
CK_DLL_MFUN(ca_pixels){RETURN->v_int=0;auto *p=ca(SELF,API);auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));if(p&&p->ready()&&exact(a,API,p->size())){write(a,API,p->pixels());RETURN->v_int=1;}}
CK_DLL_SFUN(raster_auto_select){
    RETURN->v_int=0;try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),limit=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS)),dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto n=mediawire::imagePixels(w,h);mediawire::Bytes pixels,wire;
        if(s<=0||s>=0xffffffffull||q<0||q>0xffffffffull||t<0||w<=0||h<=0||!n||limit<=0||
           !exact(src,API,4*n)||!read(src,API,4*n,pixels))return;
        int k=mediawire::chooseRasterPacket({1,static_cast<uint32_t>(s),static_cast<uint32_t>(q),static_cast<uint64_t>(t)},
            {static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,limit,wire);
        if(k&&exact(dst,API,wire.size())){write(dst,API,wire);RETURN->v_int=k;}
    }catch(...){}
}
CK_DLL_SFUN(raster_auto_size){
    RETURN->v_int=0;try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),limit=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));auto n=mediawire::imagePixels(w,h);mediawire::Bytes pixels,wire;
        if(w>0&&h>0&&n&&limit>0&&exact(src,API,4*n)&&read(src,API,4*n,pixels)&&
            mediawire::chooseRasterPacket({1,1,0,0},{static_cast<uint32_t>(w),static_cast<uint32_t>(h)},pixels,limit,wire))RETURN->v_int=wire.size();
    }catch(...){}
}
namespace {t_CKUINT rsSenderOffset=0;
mediawire::RasterStripSender *rsSender(Chuck_Object *self,CK_DL_API API){return reinterpret_cast<mediawire::RasterStripSender*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,rsSenderOffset)));}}
CK_DLL_CTOR(rss_ctor){OBJ_MEMBER_UINT(SELF,rsSenderOffset)=0;try{OBJ_MEMBER_UINT(SELF,rsSenderOffset)=reinterpret_cast<uintptr_t>(new mediawire::RasterStripSender);}catch(...){}}
CK_DLL_DTOR(rss_dtor){delete rsSender(SELF,API);OBJ_MEMBER_UINT(SELF,rsSenderOffset)=0;}
CK_DLL_MFUN(rss_clear){if(auto *p=rsSender(SELF,API))p->clear();}
CK_DLL_MFUN(rss_configure){
    RETURN->v_int=0;try{
        auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),image=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),codec=GET_NEXT_INT(ARGS),quality=GET_NEXT_INT(ARGS),limit=GET_NEXT_INT(ARGS);
        auto src=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));auto *p=rsSender(SELF,API);auto n=mediawire::imagePixels(w,h);mediawire::Bytes pixels;
        if(p&&s>0&&q>=0&&t>=0&&image>=0&&w>0&&h>0&&codec>0&&quality>0&&limit>0&&n&&exact(src,API,4*n)&&read(src,API,4*n,pixels))
            RETURN->v_int=p->configure(s,q,t,image,w,h,codec,quality,limit,pixels);
    }catch(...){}
}
CK_DLL_MFUN(rss_prepare){RETURN->v_int=0;try{if(auto *p=rsSender(SELF,API))RETURN->v_int=p->prepare();}catch(...){}}
CK_DLL_MFUN(rss_commit){auto *p=rsSender(SELF,API);RETURN->v_int=p&&p->commit();}
CK_DLL_MFUN(rss_done){auto *p=rsSender(SELF,API);RETURN->v_int=p&&p->done();}
CK_DLL_MFUN(rss_row){auto *p=rsSender(SELF,API);RETURN->v_int=p?p->row():0;}
CK_DLL_MFUN(rss_next){auto *p=rsSender(SELF,API);RETURN->v_int=p?p->nextSequence():0;}
CK_DLL_MFUN(rss_size){auto *p=rsSender(SELF,API);RETURN->v_int=p?p->packetSize():0;}
CK_DLL_MFUN(rss_peek){RETURN->v_int=0;auto *p=rsSender(SELF,API);auto dst=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));if(p&&p->packetSize()&&exact(dst,API,p->packetSize())){write(dst,API,p->packet());RETURN->v_int=1;}}
namespace {t_CKUINT videoSenderOffset=0;
mediawire::VideoFragmentSender *videoSender(Chuck_Object *self,CK_DL_API API){return reinterpret_cast<mediawire::VideoFragmentSender*>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,videoSenderOffset)));}}
CK_DLL_CTOR(vsend_ctor){OBJ_MEMBER_UINT(SELF,videoSenderOffset)=0;try{OBJ_MEMBER_UINT(SELF,videoSenderOffset)=reinterpret_cast<uintptr_t>(new mediawire::VideoFragmentSender);}catch(...){}}
CK_DLL_DTOR(vsend_dtor){delete videoSender(SELF,API);OBJ_MEMBER_UINT(SELF,videoSenderOffset)=0;}
CK_DLL_MFUN(vsend_clear){if(auto *p=videoSender(SELF,API))p->clear();}
CK_DLL_MFUN(vsend_configure){
    RETURN->v_int=0;
    auto s=GET_NEXT_INT(ARGS),q=GET_NEXT_INT(ARGS),f=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS),l=GET_NEXT_INT(ARGS);
    auto *p=videoSender(SELF,API);
    if(p&&s>0&&q>=0&&f>=0&&t>=0&&l>0)RETURN->v_int=p->configure(s,q,f,t,l);
}
CK_DLL_MFUN(vsend_begin){
    RETURN->v_int=0;try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),t=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));auto *p=videoSender(SELF,API);
        auto n=mediawire::videoPixels(w,h);mediawire::Bytes pixels;
        if(p&&w>0&&h>0&&t>=0&&n&&exact(a,API,4*n)&&read(a,API,4*n,pixels))RETURN->v_int=p->begin(w,h,t,pixels);
    }catch(...){}
}
CK_DLL_MFUN(vsend_prepare){RETURN->v_int=0;try{if(auto *p=videoSender(SELF,API))RETURN->v_int=p->prepare();}catch(...){}}
CK_DLL_MFUN(vsend_commit){auto *p=videoSender(SELF,API);RETURN->v_int=p&&p->commit();}
CK_DLL_MFUN(vsend_peek){RETURN->v_int=0;auto *p=videoSender(SELF,API);auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));if(p&&p->packetSize()&&exact(a,API,p->packetSize())){write(a,API,p->packet());RETURN->v_int=1;}}
CK_DLL_MFUN(vsend_configured){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->configured():0;}
CK_DLL_MFUN(vsend_busy){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->busy():0;}
CK_DLL_MFUN(vsend_nextSequence){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->nextSequence():0;}
CK_DLL_MFUN(vsend_nextFrame){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->nextFrame():0;}
CK_DLL_MFUN(vsend_sentPixels){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->sentPixels():0;}
CK_DLL_MFUN(vsend_completed){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->completed():0;}
CK_DLL_MFUN(vsend_maxFragmentPixels){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->maxFragmentPixels():0;}
CK_DLL_MFUN(vsend_packetSize){auto *p=videoSender(SELF,API);RETURN->v_int=p?p->packetSize():0;}
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
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaAudioMidi","Object");
    avOffset=QUERY->add_mvar(QUERY,"int","@audioMidiNative",FALSE);
    if(avOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,av_ctor);
    QUERY->add_dtor(QUERY,av_dtor);
    QUERY->add_mfun(QUERY,av_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","audioStream");
    QUERY->add_arg(QUERY,"int","midiStream");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_arg(QUERY,"int","audioSequence");
    QUERY->add_arg(QUERY,"int","midiSequence");
    QUERY->add_arg(QUERY,"int","capacityFrames");
    QUERY->add_arg(QUERY,"int","prebufferFrames");
    QUERY->add_arg(QUERY,"int","capacityEvents");
    QUERY->add_mfun(QUERY,av_clear,"void","clear");
    QUERY->add_mfun(QUERY,av_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,av_pull,"int","pullAudio");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,av_pop,"int","popMidi");
    QUERY->add_arg(QUERY,"int[]","event");
    QUERY->add_mfun(QUERY,av_configured,"int","configured");
    QUERY->add_mfun(QUERY,av_ready,"int","ready");
    QUERY->add_mfun(QUERY,av_available,"int","available");
    QUERY->add_mfun(QUERY,av_pendingMidi,"int","pendingMidi");
    QUERY->add_mfun(QUERY,av_channels,"int","channels");
    QUERY->add_mfun(QUERY,av_playTimestamp,"int","playTimestamp");
    QUERY->add_mfun(QUERY,av_consumedFrames,"int","consumedFrames");
    QUERY->add_mfun(QUERY,av_error,"int","error");
    QUERY->add_mfun(QUERY,av_audioError,"int","audioError");
    QUERY->add_mfun(QUERY,av_midiDue,"int","midiDue");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaRaster","Object");
    QUERY->add_sfun(QUERY,ra_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_sfun(QUERY,ra_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,ra_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,ra_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","pixels");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterFragment","Object");
    QUERY->add_sfun(QUERY,rf_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","pixelCount");
    QUERY->add_sfun(QUERY,rf_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","imageIndex");
    QUERY->add_arg(QUERY,"int","pixelOffset");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterStream","Object");
    rsOffset=QUERY->add_mvar(QUERY,"int","@rasterStreamNative",FALSE);
    if(rsOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,rs_ctor);
    QUERY->add_dtor(QUERY,rs_dtor);
    QUERY->add_mfun(QUERY,rs_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","firstImage");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_mfun(QUERY,rs_clear,"void","clear");
    QUERY->add_mfun(QUERY,rs_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,rs_take,"int","take");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,rs_configured,"int","configured");
    QUERY->add_mfun(QUERY,rs_ready,"int","ready");
    QUERY->add_mfun(QUERY,rs_nextSequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,rs_nextImage,"int","nextImage");
    QUERY->add_mfun(QUERY,rs_receivedPixels,"int","receivedPixels");
    QUERY->add_mfun(QUERY,rs_completed,"int","completed");
    QUERY->add_mfun(QUERY,rs_timestamp,"int","timestamp");
    QUERY->add_mfun(QUERY,rs_width,"int","width");
    QUERY->add_mfun(QUERY,rs_height,"int","height");
    QUERY->add_mfun(QUERY,rs_pixelBytes,"int","pixelBytes");
    QUERY->add_mfun(QUERY,rs_error,"int","error");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaVector","Object");
    QUERY->add_sfun(QUERY,vg_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","utf8Bytes");
    QUERY->add_sfun(QUERY,vg_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"string","svg");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,vg_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,vg_decode,"string","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaModel","Object");
    QUERY->add_sfun(QUERY,md_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","vertices");
    QUERY->add_arg(QUERY,"int","triangles");
    QUERY->add_sfun(QUERY,md_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_arg(QUERY,"int[]","indices");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,md_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,md_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"float[]","xyz");
    QUERY->add_arg(QUERY,"int[]","indices");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaVideoFrame","Object");
    QUERY->add_sfun(QUERY,vf_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_sfun(QUERY,vf_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,vf_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,vf_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","pixels");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaVideoFragment","Object");
    QUERY->add_sfun(QUERY,vsf_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","pixelCount");
    QUERY->add_sfun(QUERY,vsf_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","frameIndex");
    QUERY->add_arg(QUERY,"int","pixelOffset");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaVideoFragmentSender","Object");
    videoSenderOffset=QUERY->add_mvar(QUERY,"int","@videoFragmentSenderNative",FALSE);
    if(videoSenderOffset==CK_INVALID_OFFSET)return FALSE;
    QUERY->add_ctor(QUERY,vsend_ctor);
    QUERY->add_dtor(QUERY,vsend_dtor);
    QUERY->add_mfun(QUERY,vsend_clear,"void","clear");
    QUERY->add_mfun(QUERY,vsend_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_arg(QUERY,"int","payloadLimit");
    QUERY->add_mfun(QUERY,vsend_begin,"int","beginFrame");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,vsend_prepare,"int","prepare");
    QUERY->add_mfun(QUERY,vsend_commit,"int","commit");
    QUERY->add_mfun(QUERY,vsend_peek,"int","peek");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,vsend_configured,"int","configured");
    QUERY->add_mfun(QUERY,vsend_busy,"int","busy");
    QUERY->add_mfun(QUERY,vsend_nextSequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,vsend_nextFrame,"int","nextFrame");
    QUERY->add_mfun(QUERY,vsend_sentPixels,"int","sentPixels");
    QUERY->add_mfun(QUERY,vsend_completed,"int","completed");
    QUERY->add_mfun(QUERY,vsend_maxFragmentPixels,"int","maxFragmentPixels");
    QUERY->add_mfun(QUERY,vsend_packetSize,"int","packetSize");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaVideoStream","Object");
    vsOffset=QUERY->add_mvar(QUERY,"int","@videoStreamNative",FALSE);
    if(vsOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,vs_ctor);
    QUERY->add_dtor(QUERY,vs_dtor);
    QUERY->add_mfun(QUERY,vs_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_mfun(QUERY,vs_clear,"void","clear");
    QUERY->add_mfun(QUERY,vs_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,vs_take,"int","take");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,vs_configured,"int","configured");
    QUERY->add_mfun(QUERY,vs_ready,"int","ready");
    QUERY->add_mfun(QUERY,vs_nextSequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,vs_nextFrame,"int","nextFrame");
    QUERY->add_mfun(QUERY,vs_receivedPixels,"int","receivedPixels");
    QUERY->add_mfun(QUERY,vs_completed,"int","completed");
    QUERY->add_mfun(QUERY,vs_timestamp,"int","timestamp");
    QUERY->add_mfun(QUERY,vs_width,"int","width");
    QUERY->add_mfun(QUERY,vs_height,"int","height");
    QUERY->add_mfun(QUERY,vs_pixelBytes,"int","pixelBytes");
    QUERY->add_mfun(QUERY,vs_error,"int","error");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaVideoBuffer","Object");
    vbOffset=QUERY->add_mvar(QUERY,"int","@videoBufferNative",FALSE);
    if(vbOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,vb_ctor);
    QUERY->add_dtor(QUERY,vb_dtor);
    QUERY->add_mfun(QUERY,vb_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_arg(QUERY,"int","capacityFrames");
    QUERY->add_arg(QUERY,"int","prebufferFrames");
    QUERY->add_arg(QUERY,"int","byteBudget");
    QUERY->add_mfun(QUERY,vb_clear,"void","clear");
    QUERY->add_mfun(QUERY,vb_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,vb_pull,"int","pull");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,vb_configured,"int","configured");
    QUERY->add_mfun(QUERY,vb_capacity,"int","capacity");
    QUERY->add_mfun(QUERY,vb_threshold,"int","threshold");
    QUERY->add_mfun(QUERY,vb_available,"int","available");
    QUERY->add_mfun(QUERY,vb_freeFrames,"int","freeFrames");
    QUERY->add_mfun(QUERY,vb_budget,"int","budget");
    QUERY->add_mfun(QUERY,vb_bufferedBytes,"int","bufferedBytes");
    QUERY->add_mfun(QUERY,vb_reservedBytes,"int","reservedBytes");
    QUERY->add_mfun(QUERY,vb_ready,"int","ready");
    QUERY->add_mfun(QUERY,vb_receivedFrames,"int","receivedFrames");
    QUERY->add_mfun(QUERY,vb_consumedFrames,"int","consumedFrames");
    QUERY->add_mfun(QUERY,vb_nextSequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,vb_nextFrame,"int","nextFrame");
    QUERY->add_mfun(QUERY,vb_underruns,"int","underruns");
    QUERY->add_mfun(QUERY,vb_overruns,"int","overruns");
    QUERY->add_mfun(QUERY,vb_error,"int","error");
    QUERY->add_mfun(QUERY,vb_pixelBytes,"int","pixelBytes");
    QUERY->add_mfun(QUERY,vb_width,"int","width");
    QUERY->add_mfun(QUERY,vb_height,"int","height");
    QUERY->add_mfun(QUERY,vb_timestamp,"int","timestamp");
    QUERY->add_mfun(QUERY,vb_frameIndex,"int","frameIndex");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaAudioVideo","Object");
    syncAVOffset=QUERY->add_mvar(QUERY,"int","@audioVideoNative",FALSE);
    if(syncAVOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,sav_ctor);
    QUERY->add_dtor(QUERY,sav_dtor);
    QUERY->add_mfun(QUERY,sav_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","audioStream");
    QUERY->add_arg(QUERY,"int","videoStream");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_arg(QUERY,"int","audioSequence");
    QUERY->add_arg(QUERY,"int","videoSequence");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","audioCapacity");
    QUERY->add_arg(QUERY,"int","audioThreshold");
    QUERY->add_arg(QUERY,"int","videoCapacity");
    QUERY->add_arg(QUERY,"int","videoThreshold");
    QUERY->add_arg(QUERY,"int","videoBudget");
    QUERY->add_mfun(QUERY,sav_clear,"void","clear");
    QUERY->add_mfun(QUERY,sav_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,sav_pullAudio,"int","pullAudio");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,sav_pullVideo,"int","pullVideo");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,sav_configured,"int","configured");
    QUERY->add_mfun(QUERY,sav_started,"int","started");
    QUERY->add_mfun(QUERY,sav_ready,"int","ready");
    QUERY->add_mfun(QUERY,sav_videoDue,"int","videoDue");
    QUERY->add_mfun(QUERY,sav_playTimestamp,"int","playTimestamp");
    QUERY->add_mfun(QUERY,sav_consumedFrames,"int","consumedFrames");
    QUERY->add_mfun(QUERY,sav_channels,"int","channels");
    QUERY->add_mfun(QUERY,sav_availableAudio,"int","availableAudio");
    QUERY->add_mfun(QUERY,sav_pendingVideo,"int","pendingVideo");
    QUERY->add_mfun(QUERY,sav_videoBytes,"int","videoBytes");
    QUERY->add_mfun(QUERY,sav_videoWidth,"int","videoWidth");
    QUERY->add_mfun(QUERY,sav_videoHeight,"int","videoHeight");
    QUERY->add_mfun(QUERY,sav_videoTimestamp,"int","videoTimestamp");
    QUERY->add_mfun(QUERY,sav_videoIndex,"int","videoIndex");
    QUERY->add_mfun(QUERY,sav_videoLateness,"int","videoLateness");
    QUERY->add_mfun(QUERY,sav_error,"int","error");
    QUERY->add_mfun(QUERY,sav_audioError,"int","audioError");
    QUERY->add_mfun(QUERY,sav_videoError,"int","videoError");
    QUERY->add_mfun(QUERY,sav_audioLimit,"int","audioLimit");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaAVPacket","Object");
    QUERY->add_sfun(QUERY,avp_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaText","Object");
    QUERY->add_sfun(QUERY,tx_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","utf8Bytes");
    QUERY->add_sfun(QUERY,tx_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,tx_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,tx_decode,"string","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaTextFragment","Object");
    QUERY->add_sfun(QUERY,tf_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","bytes");
    QUERY->add_sfun(QUERY,tf_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","offset");
    QUERY->add_arg(QUERY,"int","count");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,tf_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaTextAssembler","Object");
    textAssemblerOffset=QUERY->add_mvar(QUERY,"int","@textAssemblerNative",FALSE);
    if(textAssemblerOffset==CK_INVALID_OFFSET)return FALSE;
    QUERY->add_ctor(QUERY,ta_ctor);
    QUERY->add_dtor(QUERY,ta_dtor);
    QUERY->add_mfun(QUERY,ta_reset,"void","reset");
    QUERY->add_mfun(QUERY,ta_size,"int","size");
    QUERY->add_mfun(QUERY,ta_ready,"int","ready");
    QUERY->add_mfun(QUERY,ta_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,ta_text,"string","text");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaTextBuffer","Object");
    textBufferOffset=QUERY->add_mvar(QUERY,"int","@textBufferNative",FALSE);
    if(textBufferOffset==CK_INVALID_OFFSET)return FALSE;
    QUERY->add_ctor(QUERY,tb_ctor);
    QUERY->add_dtor(QUERY,tb_dtor);
    QUERY->add_mfun(QUERY,tb_clear,"void","clear");
    QUERY->add_mfun(QUERY,tb_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","delayUs");
    QUERY->add_arg(QUERY,"int","capacityMessages");
    QUERY->add_arg(QUERY,"int","byteBudget");
    QUERY->add_mfun(QUERY,tb_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,tb_advance,"int","advance");
    QUERY->add_arg(QUERY,"int","nowUs");
    QUERY->add_mfun(QUERY,tb_ready,"int","ready");
    QUERY->add_mfun(QUERY,tb_available,"int","available");
    QUERY->add_mfun(QUERY,tb_buffered,"int","bufferedBytes");
    QUERY->add_mfun(QUERY,tb_reserved,"int","reservedBytes");
    QUERY->add_mfun(QUERY,tb_sequence,"int","nextSequence");
    QUERY->add_mfun(QUERY,tb_timestamp,"int","timestamp");
    QUERY->add_mfun(QUERY,tb_pull,"string","pull");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaPacket","Object");
    QUERY->add_sfun(QUERY,media_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRouter","Object");
    routerOffset=QUERY->add_mvar(QUERY,"int","@routerNative",FALSE);
    if(routerOffset==CK_INVALID_OFFSET)return FALSE;
    QUERY->add_ctor(QUERY,router_ctor);
    QUERY->add_dtor(QUERY,router_dtor);
    QUERY->add_mfun(QUERY,router_clear,"void","clear");
    QUERY->add_mfun(QUERY,router_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","mask");
    QUERY->add_mfun(QUERY,router_add,"int","addStream");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","type");
    QUERY->add_arg(QUERY,"int","firstSequence");
    QUERY->add_arg(QUERY,"int","originUs");
    QUERY->add_mfun(QUERY,router_streams,"int","streams");
    QUERY->add_mfun(QUERY,router_next,"int","nextSequence");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_mfun(QUERY,router_count,"int","packets");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_mfun(QUERY,router_route,"int","route");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterCompressed","Object");
    QUERY->add_sfun(QUERY,rc_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_sfun(QUERY,rc_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,rc_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,rc_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","pixels");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterPng","Object");
    QUERY->add_sfun(QUERY,rp_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_sfun(QUERY,rp_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,rp_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,rp_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","pixels");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterJpeg","Object");
    QUERY->add_sfun(QUERY,rj_flatten,"int","flatten");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","red");
    QUERY->add_arg(QUERY,"int","green");
    QUERY->add_arg(QUERY,"int","blue");
    QUERY->add_arg(QUERY,"int[]","source");
    QUERY->add_arg(QUERY,"int[]","destination");

    QUERY->add_sfun(QUERY,rj_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_sfun(QUERY,rj_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,rj_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,rj_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","pixels");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterWebp","Object");
    QUERY->add_sfun(QUERY,rw_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int","lossless");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_sfun(QUERY,rw_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int","lossless");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,rw_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    QUERY->add_sfun(QUERY,rw_decode,"int","decode");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","pixels");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterCompressedFragment","Object");
    QUERY->add_sfun(QUERY,cf_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","rows");
    QUERY->add_arg(QUERY,"int","codec");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_sfun(QUERY,cf_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","image");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","rows");
    QUERY->add_arg(QUERY,"int","codec");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,cf_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaVideoCompressedFragment","Object");
    QUERY->add_sfun(QUERY,vcf_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","rows");
    QUERY->add_arg(QUERY,"int","codec");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_sfun(QUERY,vcf_encode,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","frameIndex");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","rows");
    QUERY->add_arg(QUERY,"int","codec");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_sfun(QUERY,vcf_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterCompressedAssembler","Object");
    caOffset=QUERY->add_mvar(QUERY,"int","@compressedAssemblerNative",FALSE);
    if(caOffset==CK_INVALID_OFFSET)return FALSE;
    QUERY->add_ctor(QUERY,ca_ctor);
    QUERY->add_dtor(QUERY,ca_dtor);
    QUERY->add_mfun(QUERY,ca_reset,"void","reset");
    QUERY->add_mfun(QUERY,ca_rows,"int","rows");
    QUERY->add_mfun(QUERY,ca_size,"int","size");
    QUERY->add_mfun(QUERY,ca_ready,"int","ready");
    QUERY->add_mfun(QUERY,ca_push,"int","push");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_mfun(QUERY,ca_pixels,"int","pixels");
    QUERY->add_arg(QUERY,"int[]","pixels");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterAuto","Object");
    QUERY->add_sfun(QUERY,raster_auto_size,"int","packetSize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","payloadLimit");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_sfun(QUERY,raster_auto_select,"int","encode");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","payloadLimit");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"int[]","packet");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaRasterStripSender","Object");
    rsSenderOffset=QUERY->add_mvar(QUERY,"int","@rasterStripSenderNative",FALSE);
    if(rsSenderOffset==CK_INVALID_OFFSET)return FALSE;
    QUERY->add_ctor(QUERY,rss_ctor);
    QUERY->add_dtor(QUERY,rss_dtor);
    QUERY->add_mfun(QUERY,rss_clear,"void","clear");
    QUERY->add_mfun(QUERY,rss_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","stream");
    QUERY->add_arg(QUERY,"int","sequence");
    QUERY->add_arg(QUERY,"int","timestampUs");
    QUERY->add_arg(QUERY,"int","image");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","codec");
    QUERY->add_arg(QUERY,"int","quality");
    QUERY->add_arg(QUERY,"int","payloadLimit");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,rss_prepare,"int","prepare");
    QUERY->add_mfun(QUERY,rss_commit,"int","commit");
    QUERY->add_mfun(QUERY,rss_done,"int","done");
    QUERY->add_mfun(QUERY,rss_row,"int","row");
    QUERY->add_mfun(QUERY,rss_next,"int","nextSequence");
    QUERY->add_mfun(QUERY,rss_size,"int","packetSize");
    QUERY->add_mfun(QUERY,rss_peek,"int","peek");
    QUERY->add_arg(QUERY,"int[]","packet");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaGraphics","Object");
    QUERY->add_sfun(QUERY,gr_inspect,"int","inspect");
    QUERY->add_arg(QUERY,"int[]","packet");
    QUERY->add_arg(QUERY,"int[]","metadata");
    return QUERY->end_class(QUERY);
}
