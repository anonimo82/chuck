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
    bool generated=false;
    int transpose=0;
    double velocity=1;
    void reset() { transpose=0; velocity=1; }
    bool read(const std::string &path) {
        auto next=std::make_unique<smf::MidiFile>();
        if(!load(*next,path)) return false;
        next->absoluteTicks();
        file=std::move(next);
        generated=false;
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
    if(auto *p=midiState(SELF,API)) { p->file.reset(); p->generated=false; p->reset(); }
}


namespace {
bool midiTrack(MidiState *p,t_CKINT t) {
    return p && p->file && p->generated && t>=0 && t<p->file->getTrackCount();
}
bool midiTick(t_CKINT t) { return t>=0 && t<=0x0fffffff; }
bool midiChannel(t_CKINT c) { return c>=0 && c<=15; }
bool midiByte(t_CKINT v) { return v>=0 && v<=127; }
// Transactional edits. Bound generated sequences to one million events.
template<class Edit>
bool midiEdit(MidiState *p,size_t added,Edit edit) {
    if(!p || !p->file || !p->generated) return false;
    size_t total=0;
    for(int t=0;t<p->file->getTrackCount();++t) total+=(*p->file)[t].size();
    if(total>1000000 || added>1000000-total) return false;
    auto copy=std::make_unique<smf::MidiFile>(*p->file);
    edit(*copy);
    copy->sortTracks();
    p->file=std::move(copy);
    return true;
}
}
CK_DLL_MFUN(midi_sequence_create) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto tpq=GET_NEXT_INT(ARGS); auto bpm=GET_NEXT_FLOAT(ARGS);
        if(!p || tpq<1 || tpq>32767 || !std::isfinite(bpm) || bpm<4 || bpm>1000) return;
        auto next=std::make_unique<smf::MidiFile>();
        next->setTicksPerQuarterNote(static_cast<int>(tpq));
        next->addTempo(0,0,bpm);
        next->sortTracks();
        p->file=std::move(next); p->generated=true; p->reset(); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_add_track) {
    RETURN->v_int=-1;
    try {
        auto *p=midiState(SELF,API);
        if(!p || !p->file || !p->generated || p->file->getTrackCount()>=256) return;
        int index=p->file->getTrackCount();
        if(midiEdit(p,0,[](smf::MidiFile &m){m.addTrack();})) RETURN->v_int=index;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_add_note) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto track=GET_NEXT_INT(ARGS),tick=GET_NEXT_INT(ARGS),length=GET_NEXT_INT(ARGS);
        auto channel=GET_NEXT_INT(ARGS),pitch=GET_NEXT_INT(ARGS),velocity=GET_NEXT_INT(ARGS);
        if(!midiTrack(p,track)||!midiTick(tick)||length<1||length>0x0fffffff-tick||
           !midiChannel(channel)||!midiByte(pitch)||velocity<1||velocity>127) return;
        RETURN->v_int=midiEdit(p,2,[&](smf::MidiFile &m){
            m.addNoteOn(track,tick,channel,pitch,velocity);
            m.addNoteOff(track,tick+length,channel,pitch,0);
        });
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_add_chord) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto track=GET_NEXT_INT(ARGS),tick=GET_NEXT_INT(ARGS),length=GET_NEXT_INT(ARGS),channel=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        auto velocity=GET_NEXT_INT(ARGS);
        if(!midiTrack(p,track)||!midiTick(tick)||length<1||length>0x0fffffff-tick||
           !midiChannel(channel)||!a||velocity<1||velocity>127) return;
        auto n=API->object->array_int_size(a);
        if(n<1||n>128) return;
        bool seen[128]={};
        std::vector<int> pitches;
        for(t_CKINT i=0;i<n;++i) {
            auto v=API->object->array_int_get_idx(a,i);
            if(!midiByte(v)||seen[v]) return;
            seen[v]=true; pitches.push_back(v);
        }
        RETURN->v_int=midiEdit(p,pitches.size()*2,[&](smf::MidiFile &m){
            for(int pitch:pitches) {
                m.addNoteOn(track,tick,channel,pitch,velocity);
                m.addNoteOff(track,tick+length,channel,pitch,0);
            }
        });
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_add_cc) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto track=GET_NEXT_INT(ARGS),tick=GET_NEXT_INT(ARGS),channel=GET_NEXT_INT(ARGS);
        auto controller=GET_NEXT_INT(ARGS),value=GET_NEXT_INT(ARGS);
        if(!midiTrack(p,track)||!midiTick(tick)||!midiChannel(channel)||!midiByte(controller)||!midiByte(value)) return;
        RETURN->v_int=midiEdit(p,1,[&](smf::MidiFile &m){m.addController(track,tick,channel,controller,value);});
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_add_program) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto track=GET_NEXT_INT(ARGS),tick=GET_NEXT_INT(ARGS),channel=GET_NEXT_INT(ARGS),program=GET_NEXT_INT(ARGS);
        if(!midiTrack(p,track)||!midiTick(tick)||!midiChannel(channel)||!midiByte(program)) return;
        RETURN->v_int=midiEdit(p,1,[&](smf::MidiFile &m){m.addPatchChange(track,tick,channel,program);});
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_add_tempo) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto tick=GET_NEXT_INT(ARGS); auto bpm=GET_NEXT_FLOAT(ARGS);
        if(!midiTrack(p,0)||!midiTick(tick)||!std::isfinite(bpm)||bpm<4||bpm>1000) return;
        // No ambiguous duplicate tempo changes at the same tick.
        for(int i=0;i<(*p->file)[0].size();++i) {
            auto &e=(*p->file)[0][i];
            if(e.tick==tick && e.size()>=2 && e[0]==0xff && e[1]==0x51) return;
        }
        RETURN->v_int=midiEdit(p,1,[&](smf::MidiFile &m){m.addTempo(0,tick,bpm);});
    } catch(...) {}
}


namespace {
struct NotePair { int on,off; };
// Pair only unambiguous, positive-duration notes on a single track.
bool notePairs(smf::MidiFile &m,int track,std::vector<NotePair> &pairs) {
    if(track<0 || track>=m.getTrackCount()) return false;
    int pending[2048]; std::fill_n(pending,2048,-1);
    auto &events=m[track];
    // At a shared tick, finish active notes before starting new ones.
    // This handles both explicit note-off and velocity-zero note-on,
    // independently of the library's ordering of simultaneous events.
    for(int first=0;first<events.size();) {
        int tick=events[first].tick;
        if(!midiTick(tick)||(first>0&&tick<events[first-1].tick)) return false;
        int last=first+1;
        while(last<events.size()&&events[last].tick==tick) ++last;
        for(int pass=0;pass<2;++pass) {
            for(int i=first;i<last;++i) {
                auto &e=events[i];
                if(e.size()!=3) continue;
                int status=e[0]&0xf0;
                if(status!=0x80&&status!=0x90) continue;
                if(e[1]>127||e[2]>127) return false;
                bool on=status==0x90&&e[2]>0;
                if(on!=(pass==1)) continue;
                int key=(e[0]&15)*128+e[1];
                if(on) {
                    if(pending[key]>=0) return false;
                    pending[key]=i;
                } else {
                    int begin=pending[key];
                    if(begin<0||tick<=events[begin].tick) return false;
                    pairs.push_back({begin,i}); pending[key]=-1;
                }
            }
        }
        first=last;
    }
    for(int i:pending) if(i>=0) return false;
    std::sort(pairs.begin(),pairs.end(),[](const NotePair &a,const NotePair &b){return a.on<b.on;});
    return true;
}
bool editableTrack(MidiState *p,t_CKINT t) {
    return p && p->file && t>=0 && t<p->file->getTrackCount() &&
           p->file->getTicksPerQuarterNote()>0 && p->file->getTicksPerQuarterNote()<=32767;
}
template<class Edit>
bool editNotes(MidiState *p,int track,Edit edit) {
    size_t total=0;
    for(int t=0;t<p->file->getTrackCount();++t) total+=(*p->file)[t].size();
    if(total>1000000) return false;
    auto copy=std::make_unique<smf::MidiFile>(*p->file);
    std::vector<NotePair> pairs;
    if(!notePairs(*copy,track,pairs) || !edit(*copy,pairs)) return false;
    // End-of-track must not precede a moved note. Keep trailing silence.
    int end=0;
    auto &events=(*copy)[track];
    for(int i=0;i<events.size();++i) end=std::max(end,events[i].tick);
    for(int i=0;i<events.size();++i) {
        auto &e=events[i];
        if(e.size()>=2 && e[0]==0xff && e[1]==0x2f) e.tick=end;
    }
    copy->sortTracks();
    pairs.clear();
    if(!notePairs(*copy,track,pairs)) return false;
    p->file=std::move(copy);
    return true;
}
}
CK_DLL_MFUN(midi_sequence_notes) {
    RETURN->v_int=-1;
    try {
        auto *p=midiState(SELF,API); auto track=GET_NEXT_INT(ARGS);
        if(!editableTrack(p,track)) return;
        std::vector<NotePair> pairs;
        if(notePairs(*p->file,track,pairs)) RETURN->v_int=pairs.size();
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_note_length) {
    RETURN->v_int=-1;
    try {
        auto *p=midiState(SELF,API);
        auto track=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS);
        if(!editableTrack(p,track)||index<0) return;
        std::vector<NotePair> pairs;
        if(!notePairs(*p->file,track,pairs)||static_cast<uint64_t>(index)>=pairs.size()) return;
        auto pair=pairs[index]; auto &events=(*p->file)[track];
        RETURN->v_int=events[pair.off].tick-events[pair.on].tick;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_set_note) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto track=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS),tick=GET_NEXT_INT(ARGS);
        auto length=GET_NEXT_INT(ARGS),pitch=GET_NEXT_INT(ARGS),velocity=GET_NEXT_INT(ARGS);
        if(!editableTrack(p,track)||index<0||!midiTick(tick)||length<1||
           length>0x0fffffff-tick||!midiByte(pitch)||velocity<1||velocity>127) return;
        RETURN->v_int=editNotes(p,track,[&](smf::MidiFile &m,const std::vector<NotePair> &pairs){
            if(static_cast<uint64_t>(index)>=pairs.size()) return false;
            auto pair=pairs[index]; auto &on=m[track][pair.on]; auto &off=m[track][pair.off];
            on.tick=tick; off.tick=tick+length;
            on[1]=off[1]=static_cast<unsigned char>(pitch); on[2]=static_cast<unsigned char>(velocity);
            return true;
        });
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_quantize) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API); auto track=GET_NEXT_INT(ARGS),grid=GET_NEXT_INT(ARGS);
        auto strength=GET_NEXT_FLOAT(ARGS);
        if(!editableTrack(p,track)||grid<1||grid>0x0fffffff||
           !std::isfinite(strength)||strength<0||strength>1) return;
        RETURN->v_int=editNotes(p,track,[&](smf::MidiFile &m,const std::vector<NotePair> &pairs){
            for(auto pair:pairs) {
                auto &on=m[track][pair.on]; auto &off=m[track][pair.off];
                int64_t snapped=((static_cast<int64_t>(on.tick)+grid/2)/grid)*grid;
                int64_t start=on.tick+std::llround((snapped-on.tick)*strength);
                int64_t stop=start+(off.tick-on.tick);
                if(!midiTick(start)||!midiTick(stop)) return false;
                on.tick=start; off.tick=stop;
            }
            return true;
        });
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_shift) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API); auto track=GET_NEXT_INT(ARGS),delta=GET_NEXT_INT(ARGS);
        if(!editableTrack(p,track)||delta< -0x0fffffff||delta>0x0fffffff) return;
        RETURN->v_int=editNotes(p,track,[&](smf::MidiFile &m,const std::vector<NotePair> &pairs){
            for(auto pair:pairs) {
                auto &on=m[track][pair.on]; auto &off=m[track][pair.off];
                int64_t start=static_cast<int64_t>(on.tick)+delta,stop=static_cast<int64_t>(off.tick)+delta;
                if(!midiTick(start)||!midiTick(stop)) return false;
                on.tick=start; off.tick=stop;
            }
            return true;
        });
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_scale_lengths) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API); auto track=GET_NEXT_INT(ARGS); auto factor=GET_NEXT_FLOAT(ARGS);
        if(!editableTrack(p,track)||!std::isfinite(factor)||factor<0.0625||factor>16) return;
        RETURN->v_int=editNotes(p,track,[&](smf::MidiFile &m,const std::vector<NotePair> &pairs){
            for(auto pair:pairs) {
                auto &on=m[track][pair.on]; auto &off=m[track][pair.off];
                int64_t stop=on.tick+std::max<int64_t>(1,std::llround((off.tick-on.tick)*factor));
                if(!midiTick(stop)) return false;
                off.tick=stop;
            }
            return true;
        });
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_remove_note) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API); auto track=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS);
        if(!editableTrack(p,track)||index<0) return;
        RETURN->v_int=editNotes(p,track,[&](smf::MidiFile &m,const std::vector<NotePair> &pairs){
            if(static_cast<uint64_t>(index)>=pairs.size()) return false;
            // Rebuild the selected track's event list without the two note messages.
            auto pair=pairs[index];
            smf::MidiEventList replacement;
            auto &events=m[track];
            for(int i=0;i<events.size();++i)
                if(i!=pair.on && i!=pair.off) replacement.push_back(events[i]);
            events=replacement;
            return true;
        });
    } catch(...) {}
}

namespace {
struct RenderNote { size_t begin,end; double hz,gain; int pitch; };
struct RenderTempo { int tick; double seconds,us; };
bool prepareRender(MidiState *p,t_CKINT rate,std::vector<RenderNote> &notes,size_t &frames,bool visual=false) {
    if(!p||!p->file||rate<8000||rate>96000) return false;
    auto &m=*p->file;
    int ppq=m.getTicksPerQuarterNote();
    if(ppq<1||ppq>32767||m.getTrackCount()>256) return false;
    std::vector<RenderTempo> tempos{{0,0,500000}},map;
    size_t total=0;
    int endTick=0;
    for(int t=0;t<m.getTrackCount();++t) {
        auto &events=m[t];
        total+=events.size();
        if(total>1000000) return false;
        for(int i=0;i<events.size();++i) {
            auto &e=events[i];
            if(!midiTick(e.tick)||(i&&e.tick<events[i-1].tick)) return false;
            endTick=std::max(endTick,e.tick);
            if(e.size()>=2&&e[0]==0xff&&e[1]==0x51) {
                if(e.size()!=6||e[2]!=3) return false;
                unsigned us=(unsigned(e[3])<<16)|(unsigned(e[4])<<8)|unsigned(e[5]);
                if(!us) return false;
                tempos.push_back({e.tick,0,double(us)});
            }
        }
    }
    std::stable_sort(tempos.begin(),tempos.end(),[](const RenderTempo &a,const RenderTempo &b){return a.tick<b.tick;});
    for(auto v:tempos) {
        if(!map.empty()&&map.back().tick==v.tick) map.back().us=v.us;
        else {
            if(!map.empty()) {
                auto &prev=map.back();
                v.seconds=prev.seconds+(v.tick-prev.tick)*prev.us/(1000000.0*ppq);
            }
            map.push_back(v);
        }
    }
    auto at=[&](int tick)->double {
        auto it=std::upper_bound(map.begin(),map.end(),tick,[](int v,const RenderTempo &t){return v<t.tick;});
        --it;
        return (it->seconds+(tick-it->tick)*it->us/(1000000.0*ppq))*rate;
    };
    double length=at(endTick);
    if(!std::isfinite(length)||length<0.5||length>33554432) return false;
    frames=static_cast<size_t>(std::llround(length));
    uint64_t work=0;
    for(int t=0;t<m.getTrackCount();++t) {
        std::vector<NotePair> pairs;
        if(!notePairs(m,t,pairs)) return false;
        for(const auto &pair:pairs) {
            auto &on=m[t][pair.on]; auto &off=m[t][pair.off];
            int pitch=int(on[1])+p->transpose;
            if(pitch<0||pitch>127||(!visual&&(on[0]&15)==9)) return false;
            double hz=440.0*std::pow(2.0,(pitch-69)/12.0);
            if(!visual&&hz>=rate/2.0) return false;
            auto begin=static_cast<size_t>(std::llround(at(on.tick)));
            auto end=static_cast<size_t>(std::llround(at(off.tick)));
            if(end<=begin||end>frames) return false;
            work+=end-begin;
            if(work>100000000) return false;
            double gain=std::max(1L,std::min(127L,std::lround(on[2]*p->velocity)))/127.0;
            notes.push_back({begin,end,hz,gain,pitch});
        }
    }
    return true;
}
}
CK_DLL_MFUN(midi_sequence_render_frames) {
    RETURN->v_int=-1;
    try {
        auto *p=midiState(SELF,API); auto rate=GET_NEXT_INT(ARGS);
        std::vector<RenderNote> notes; size_t frames=0;
        if(prepareRender(p,rate,notes,frames)) RETURN->v_int=frames;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_render_samples) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API); auto rate=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        std::vector<RenderNote> notes; size_t frames=0;
        if(!a||!prepareRender(p,rate,notes,frames)||
           API->object->array_float_size(a)!=static_cast<t_CKINT>(frames)) return;
        std::vector<double> pcm(frames,0);
        constexpr double tau=6.28318530717958647692;
        for(const auto &note:notes) {
            size_t count=note.end-note.begin;
            size_t ramp=std::min<size_t>(rate/200,count/2);
            double step=tau*note.hz/rate;
            for(size_t i=0;i<count;++i) {
                double env=1;
                if(ramp) env=std::min(1.0,double(std::min(i,count-1-i))/ramp);
                pcm[note.begin+i]+=std::sin(step*i)*env*note.gain*0.25;
            }
        }
        for(size_t i=0;i<frames;++i)
            API->object->array_float_set_idx(a,i,std::clamp(pcm[i],-1.0,1.0));
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_import_pitches) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto tpq=GET_NEXT_INT(ARGS);
        double bpm=GET_NEXT_FLOAT(ARGS),seconds=GET_NEXT_FLOAT(ARGS);
        auto channel=GET_NEXT_INT(ARGS),velocity=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||tpq<1||tpq>32767||!std::isfinite(bpm)||bpm<4||bpm>1000||
           !std::isfinite(seconds)||seconds<=0||seconds>60||channel<0||channel>15||
           velocity<1||velocity>127) return;
        auto n=API->object->array_int_size(a);
        double step=seconds*bpm*tpq/60.0;
        if(n<1||n>100000||step<1||step*n>0x0fffffff) return;
        std::vector<int> pitches(n);
        for(t_CKINT i=0;i<n;++i) {
            auto v=API->object->array_int_get_idx(a,i);
            if(v< -1||v>127) return;
            pitches[i]=static_cast<int>(v);
        }
        auto next=std::make_unique<smf::MidiFile>();
        next->setTicksPerQuarterNote(static_cast<int>(tpq));
        next->addTempo(0,0,bpm);
        for(t_CKINT i=0;i<n;) {
            t_CKINT end=i+1;
            while(end<n&&pitches[end]==pitches[i]) ++end;
            if(pitches[i]>=0) {
                next->addNoteOn(0,static_cast<int>(std::llround(i*step)),channel,pitches[i],velocity);
                next->addNoteOff(0,static_cast<int>(std::llround(end*step)),channel,pitches[i],0);
            }
            i=end;
        }
        next->sortTracks();
        p->file=std::move(next); p->generated=true; p->reset();
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_piano_roll) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto rate=GET_NEXT_INT(ARGS),cursor=GET_NEXT_INT(ARGS);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!a||cursor<0||w<1||w>4096||h<128||h>4096||w*h>4194304||
           API->object->array_int_size(a)!=w*h*4) return;
        std::vector<RenderNote> notes;
        size_t frames=0;
        if(!prepareRender(p,rate,notes,frames)||static_cast<uint64_t>(cursor)>frames) return;
        std::vector<unsigned char> rgba(w*h*4,0);
        for(size_t i=0;i<rgba.size()/4;++i) rgba[i*4+3]=255;
        uint64_t work=0;
        for(const auto &note:notes) {
            size_t x0=uint64_t(note.begin)*w/frames;
            size_t x1=(uint64_t(note.end)*w+frames-1)/frames;
            size_t y0=uint64_t(127-note.pitch)*h/128;
            size_t y1=uint64_t(128-note.pitch)*h/128;
            work+=(x1-x0)*(y1-y0);
            if(work>100000000) return;
            bool active=static_cast<size_t>(cursor)>=note.begin&&static_cast<size_t>(cursor)<note.end;
            for(size_t y=y0;y<y1;++y) for(size_t x=x0;x<x1;++x) {
                size_t i=(y*w+x)*4;
                // Active notes win over inactive notes in overlapping rectangles.
                if(!active&&rgba[i]==255) continue;
                rgba[i]=active?255:48;
                rgba[i+1]=active?192:112;
                rgba[i+2]=active?64:192;
            }
        }
        size_t x=std::min<size_t>(w-1,uint64_t(cursor)*w/frames);
        for(size_t y=0;y<static_cast<size_t>(h);++y) {
            size_t i=(y*w+x)*4;
            rgba[i]=rgba[i+1]=rgba[i+2]=255;
        }
        for(size_t i=0;i<rgba.size();++i) API->object->array_int_set_idx(a,i,rgba[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_static_roll) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto rgb=GET_NEXT_INT(ARGS),bg=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!a||w<1||w>4096||h<128||h>4096||w*h>4194304||
           rgb<0||rgb>0xffffff||bg<0||bg>0xffffff||
           API->object->array_int_size(a)!=w*h*4) return;
        std::vector<RenderNote> notes;
        size_t frames=0;
        if(!prepareRender(p,96000,notes,frames,true)) return;
        std::vector<unsigned char> levels(w*h,0);
        uint64_t work=0;
        for(const auto &note:notes) {
            size_t x0=uint64_t(note.begin)*w/frames;
            size_t x1=(uint64_t(note.end)*w+frames-1)/frames;
            size_t y0=uint64_t(127-note.pitch)*h/128;
            size_t y1=uint64_t(128-note.pitch)*h/128;
            work+=(x1-x0)*(y1-y0);
            if(work>100000000) return;
            auto velocity=static_cast<unsigned char>(std::lround(note.gain*127));
            for(size_t y=y0;y<y1;++y) for(size_t x=x0;x<x1;++x)
                levels[y*w+x]=std::max(levels[y*w+x],velocity);
        }
        for(size_t i=0;i<levels.size();++i) {
            for(int c=0;c<3;++c) {
                int from=(bg>>(16-8*c))&255,to=(rgb>>(16-8*c))&255;
                API->object->array_int_set_idx(a,i*4+c,std::lround(from+(to-from)*levels[i]/127.0));
            }
            API->object->array_int_set_idx(a,i*4+3,255);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_rect_count) {
    RETURN->v_int=-1;
    try {
        auto *p=midiState(SELF,API);
        std::vector<RenderNote> notes; size_t frames=0;
        if(prepareRender(p,96000,notes,frames,true)&&notes.size()<=4096)
            RETURN->v_int=notes.size();
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_rects) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),rgb=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!a||w<1||w>4096||h<128||h>4096||rgb<0||rgb>0xffffff) return;
        std::vector<RenderNote> notes; size_t frames=0;
        if(!prepareRender(p,96000,notes,frames,true)||notes.size()>4096||
           API->object->array_float_size(a)!=static_cast<t_CKINT>(notes.size()*6)) return;
        for(size_t i=0;i<notes.size();++i) {
            const auto &n=notes[i];
            double v[]={double(n.begin)*w/frames,double(127-n.pitch)*h/128,
                        double(n.end-n.begin)*w/frames,double(h)/128,double(rgb),n.gain};
            for(int k=0;k<6;++k) API->object->array_float_set_idx(a,i*6+k,v[k]);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(midi_sequence_boxes) {
    RETURN->v_int=0;
    try {
        auto *p=midiState(SELF,API);
        double timeSpan=GET_NEXT_FLOAT(ARGS),pitchStep=GET_NEXT_FLOAT(ARGS),heightScale=GET_NEXT_FLOAT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!a||!std::isfinite(timeSpan)||timeSpan<0.001||timeSpan>1000000||
           !std::isfinite(pitchStep)||pitchStep<0.001||pitchStep>1000000||
           !std::isfinite(heightScale)||heightScale<0.001||heightScale>1000000) return;
        std::vector<RenderNote> notes; size_t frames=0;
        if(!prepareRender(p,96000,notes,frames,true)||notes.empty()||notes.size()>4096||
           API->object->array_float_size(a)!=static_cast<t_CKINT>(notes.size()*6)) return;
        for(size_t i=0;i<notes.size();++i) {
            const auto &n=notes[i];
            double x0=double(n.begin)*timeSpan/frames-timeSpan*0.5;
            double x1=double(n.end)*timeSpan/frames-timeSpan*0.5;
            double y=(n.pitch-63.5)*pitchStep;
            double v[]={x0,y-pitchStep*0.4,0.0,x1,y+pitchStep*0.4,n.gain*heightScale};
            for(int k=0;k<6;++k) API->object->array_float_set_idx(a,i*6+k,v[k]);
        }
        RETURN->v_int=1;
    } catch(...) {}
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
    QUERY->add_mfun(QUERY,midi_sequence_import_pitches,"int","importPitches");
    QUERY->add_arg(QUERY,"int","ticksPerQuarter");
    QUERY->add_arg(QUERY,"float","bpm");
    QUERY->add_arg(QUERY,"float","blockSeconds");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","velocity");
    QUERY->add_arg(QUERY,"int[]","pitches");
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
    QUERY->add_mfun(QUERY,midi_sequence_create,"int","create");
    QUERY->add_arg(QUERY,"int","ticksPerQuarter");
    QUERY->add_arg(QUERY,"float","bpm");
    QUERY->add_mfun(QUERY,midi_sequence_add_track,"int","addTrack");
    QUERY->add_mfun(QUERY,midi_sequence_add_note,"int","addNote");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","tick");
    QUERY->add_arg(QUERY,"int","length");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","pitch");
    QUERY->add_arg(QUERY,"int","velocity");
    QUERY->add_mfun(QUERY,midi_sequence_add_chord,"int","addChord");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","tick");
    QUERY->add_arg(QUERY,"int","length");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int[]","pitches");
    QUERY->add_arg(QUERY,"int","velocity");
    QUERY->add_mfun(QUERY,midi_sequence_add_cc,"int","addCC");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","tick");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","controller");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,midi_sequence_add_program,"int","addProgram");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","tick");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","program");
    QUERY->add_mfun(QUERY,midi_sequence_add_tempo,"int","addTempo");
    QUERY->add_arg(QUERY,"int","tick");
    QUERY->add_arg(QUERY,"float","bpm");
    QUERY->add_mfun(QUERY,midi_sequence_notes,"int","notes");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_mfun(QUERY,midi_sequence_note_length,"int","noteLength");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,midi_sequence_set_note,"int","setNote");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","tick");
    QUERY->add_arg(QUERY,"int","length");
    QUERY->add_arg(QUERY,"int","pitch");
    QUERY->add_arg(QUERY,"int","velocity");
    QUERY->add_mfun(QUERY,midi_sequence_quantize,"int","quantize");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","grid");
    QUERY->add_arg(QUERY,"float","strength");
    QUERY->add_mfun(QUERY,midi_sequence_shift,"int","shiftNotes");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","delta");
    QUERY->add_mfun(QUERY,midi_sequence_scale_lengths,"int","scaleLengths");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"float","factor");
    QUERY->add_mfun(QUERY,midi_sequence_remove_note,"int","removeNote");
    QUERY->add_arg(QUERY,"int","track");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,midi_sequence_boxes,"int","exportNoteBoxes");
    QUERY->add_arg(QUERY,"float","timeSpan");
    QUERY->add_arg(QUERY,"float","pitchStep");
    QUERY->add_arg(QUERY,"float","heightScale");
    QUERY->add_arg(QUERY,"float[]","boxes");
    QUERY->add_mfun(QUERY,midi_sequence_rect_count,"int","pianoRollRectCount");
    QUERY->add_mfun(QUERY,midi_sequence_rects,"int","exportPianoRollRects");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float[]","rects");
    QUERY->add_mfun(QUERY,midi_sequence_static_roll,"int","exportPianoRoll");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"int","backgroundRGB");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,midi_sequence_piano_roll,"int","exportPianoRoll");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","cursorFrame");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,midi_sequence_render_frames,"int","renderFrames");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_mfun(QUERY,midi_sequence_render_samples,"int","renderSamples");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"float[]","samples");
    return QUERY->end_class(QUERY);
}
