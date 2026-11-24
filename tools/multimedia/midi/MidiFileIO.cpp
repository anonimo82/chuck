// Optional offline Standard MIDI File I/O using Craig Sapp's midifile library.
#include "chuck_dl.h"
#include "../file_compat.h"
#include "MidiFile.h"
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

namespace {
bool load(smf::MidiFile &m, const std::string &path) {
    return m.read(path) && m.status();
}
bool saveNew(smf::MidiFile &m, const std::string &path) {
    std::ostringstream stream(std::ios::out | std::ios::binary);
    if(!m.write(stream)) return false;
    auto bytes = stream.str();
    FILE *out = mediaio::openNew(path.c_str());
    if(!out) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size();
    if(std::fclose(out) != 0) ok = false;
    if(!ok) std::remove(path.c_str());
    return ok;
}
}
CK_DLL_SFUN(midi_tracks) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        smf::MidiFile m;
        if(load(m, path)) RETURN->v_int = m.getTrackCount();
    } catch(...) {}
}
CK_DLL_SFUN(midi_events) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        auto track = GET_NEXT_INT(ARGS);
        smf::MidiFile m;
        if(load(m, path) && track >= 0 && track < m.getTrackCount())
            RETURN->v_int = m[static_cast<int>(track)].size();
    } catch(...) {}
}
CK_DLL_SFUN(midi_copy) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto destination = GET_NEXT_STRING_SAFE(ARGS);
        smf::MidiFile m;
        if(load(m, source) && saveNew(m, destination)) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(midi_note) {
    RETURN->v_int = 0;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        auto pitch = GET_NEXT_INT(ARGS);
        auto velocity = GET_NEXT_INT(ARGS);
        auto ticks = GET_NEXT_INT(ARGS);
        if(pitch < 0 || pitch > 127 || velocity < 1 || velocity > 127 ||
           ticks < 1 || ticks > 0x0fffffff) return;
        smf::MidiFile m;
        m.setTicksPerQuarterNote(480);
        m.addTempo(0, 0, 120.0);
        m.addNoteOn(0, 0, 0, static_cast<int>(pitch), static_cast<int>(velocity));
        m.addNoteOff(0, static_cast<int>(ticks), 0, static_cast<int>(pitch), 0);
        m.sortTracks();
        if(saveNew(m, path)) RETURN->v_int = 1;
    } catch(...) {}
}

namespace {
struct MidiState {
    std::unique_ptr<smf::MidiFile> file;
    int transpose=0;
    double velocity=1;
    void reset() { transpose=0; velocity=1; }
    bool read(const std::string &path) {
        auto next=std::make_unique<smf::MidiFile>();
        if(!load(*next,path)) return false;
        next->absoluteTicks();
        file=std::move(next);
        reset();
        return true;
    }
    smf::MidiEvent *event(t_CKINT track,t_CKINT index) {
        if(!file || track<0 || track>=file->getTrackCount() ||
           index<0 || index>=(*file)[static_cast<int>(track)].size()) return nullptr;
        return &(*file)[static_cast<int>(track)][static_cast<int>(index)];
    }
    bool save(const std::string &path) const {
        if(!file) return false;
        smf::MidiFile copy(*file);
        for(int t=0;t<copy.getTrackCount();++t) {
            for(int i=0;i<copy[t].size();++i) {
                auto &e=copy[t][i];
                if(e.size()!=3) continue;
                unsigned status=e[0]&0xf0;
                if(status!=0x80 && status!=0x90 && status!=0xa0) continue;
                int pitch=static_cast<int>(e[1])+transpose;
                // Reject out-of-range notes rather than merge distinct pitches by clipping.
                if(pitch<0 || pitch>127) return false;
                e[1]=static_cast<unsigned char>(pitch);
                if(status==0x90 && e[2]>0)
                    e[2]=static_cast<unsigned char>(std::max(1L,std::min(127L,std::lround(e[2]*velocity))));
            }
        }
        return saveNew(copy,path);
    }
};
t_CKUINT midiSequenceOffset=0;
MidiState *midiState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<MidiState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,midiSequenceOffset)));
}
}
CK_DLL_CTOR(midi_sequence_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold native pointer");
    OBJ_MEMBER_UINT(SELF,midiSequenceOffset)=0;
    OBJ_MEMBER_UINT(SELF,midiSequenceOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) MidiState);
}
CK_DLL_DTOR(midi_sequence_dtor) {
    delete midiState(SELF,API);
    OBJ_MEMBER_UINT(SELF,midiSequenceOffset)=0;
}
CK_DLL_MFUN(midi_sequence_load) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->read(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_save) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->save(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_loaded) {
    auto *p=midiState(SELF,API);
    RETURN->v_int=p && p->file;
}
CK_DLL_MFUN(midi_sequence_tracks) {
    auto *p=midiState(SELF,API);
    RETURN->v_int=p && p->file?p->file->getTrackCount():-1;
}
CK_DLL_MFUN(midi_sequence_events) {
    auto *p=midiState(SELF,API);
    auto track=GET_NEXT_INT(ARGS);
    RETURN->v_int=p && p->file && track>=0 && track<p->file->getTrackCount()
        ?(*p->file)[static_cast<int>(track)].size():-1;
}
CK_DLL_MFUN(midi_sequence_tpq) {
    auto *p=midiState(SELF,API);
    RETURN->v_int=p && p->file?p->file->getTicksPerQuarterNote():-1;
}
CK_DLL_MFUN(midi_sequence_tick) {
    auto *p=midiState(SELF,API);
    auto track=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS);
    auto *e=p?p->event(track,index):nullptr;
    RETURN->v_int=e?e->tick:-1;
}
CK_DLL_MFUN(midi_sequence_size) {
    auto *p=midiState(SELF,API);
    auto track=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS);
    auto *e=p?p->event(track,index):nullptr;
    RETURN->v_int=e?static_cast<t_CKINT>(e->size()):-1;
}
CK_DLL_MFUN(midi_sequence_byte) {
    auto *p=midiState(SELF,API);
    auto track=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS),byte=GET_NEXT_INT(ARGS);
    auto *e=p?p->event(track,index):nullptr;
    RETURN->v_int=e && byte>=0 && static_cast<uint64_t>(byte)<e->size()?(*e)[static_cast<size_t>(byte)]:-1;
}
CK_DLL_MFUN(midi_sequence_set_velocity) {
    auto *p=midiState(SELF,API);
    auto track=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS),value=GET_NEXT_INT(ARGS);
    auto *e=p?p->event(track,index):nullptr;
    RETURN->v_int=0;
    if(e && e->size()==3 && ((*e)[0]&0xf0)==0x90 && (*e)[2]>0 && value>=1 && value<=127) {
        (*e)[2]=static_cast<unsigned char>(value);
        RETURN->v_int=1;
    }
}
CK_DLL_MFUN(midi_sequence_get_transpose) {
    auto *p=midiState(SELF,API);
    RETURN->v_int=p?p->transpose:0;
}
CK_DLL_MFUN(midi_sequence_set_transpose) {
    auto *p=midiState(SELF,API);
    auto v=GET_NEXT_INT(ARGS);
    if(p && v>=-127 && v<=127) p->transpose=static_cast<int>(v);
    RETURN->v_int=p?p->transpose:0;
}
CK_DLL_MFUN(midi_sequence_get_velocity) {
    auto *p=midiState(SELF,API);
    RETURN->v_float=p?p->velocity:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(midi_sequence_set_gain) {
    auto *p=midiState(SELF,API);
    auto v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p && std::isfinite(v) && v>0 && v<=16) RETURN->v_float=p->velocity=v;
}
CK_DLL_MFUN(midi_sequence_reset) {
    if(auto *p=midiState(SELF,API)) p->reset();
}
CK_DLL_MFUN(midi_sequence_clear) {
    if(auto *p=midiState(SELF,API)) { p->file.reset(); p->reset(); }
}

CK_DLL_QUERY(MidiFileIO) {
    QUERY->setname(QUERY, "MidiFileIO");
    QUERY->begin_class(QUERY, "MidiFileIO", "Object");
    QUERY->add_sfun(QUERY, midi_tracks, "int", "tracks");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, midi_events, "int", "events");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_arg(QUERY, "int", "track");
    QUERY->add_sfun(QUERY, midi_copy, "int", "copy");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_sfun(QUERY, midi_note, "int", "writeNote");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_arg(QUERY, "int", "pitch");
    QUERY->add_arg(QUERY, "int", "velocity");
    QUERY->add_arg(QUERY, "int", "ticks");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MidiSequence","Object");
    midiSequenceOffset=QUERY->add_mvar(QUERY,"int","@midiSequenceNative",FALSE);
    if(midiSequenceOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,midi_sequence_ctor);
    QUERY->add_dtor(QUERY,midi_sequence_dtor);
    QUERY->add_mfun(QUERY,midi_sequence_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,midi_sequence_save,"int","save");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,midi_sequence_loaded,"int","loaded");
    QUERY->add_mfun(QUERY,midi_sequence_tracks,"int","tracks");
    QUERY->add_mfun(QUERY,midi_sequence_events,"int","events");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_mfun(QUERY,midi_sequence_tpq,"int","ticksPerQuarter");
    QUERY->add_mfun(QUERY,midi_sequence_tick,"int","tickAt");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,midi_sequence_size,"int","sizeAt");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,midi_sequence_byte,"int","byteAt");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","byteIndex");
    QUERY->add_mfun(QUERY,midi_sequence_set_velocity,"int","setVelocity");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,midi_sequence_get_transpose,"int","transpose");
    QUERY->add_mfun(QUERY,midi_sequence_set_transpose,"int","transpose");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,midi_sequence_get_velocity,"float","velocityGain");
    QUERY->add_mfun(QUERY,midi_sequence_set_gain,"float","velocityGain");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,midi_sequence_reset,"void","reset");
    QUERY->add_mfun(QUERY,midi_sequence_clear,"void","clear");
    return QUERY->end_class(QUERY);
}
