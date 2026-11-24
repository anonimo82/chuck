// Optional offline Standard MIDI File I/O using Craig Sapp's midifile library.
#include "chuck_dl.h"
#include "MidiFile.h"
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace {
bool load(smf::MidiFile &m, const std::string &path) {
    return m.read(path) && m.status();
}
bool saveNew(smf::MidiFile &m, const std::string &path) {
    std::ostringstream stream(std::ios::out | std::ios::binary);
    if(!m.write(stream)) return false;
    auto bytes = stream.str();
    FILE *out = std::fopen(path.c_str(), "wbx");
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
    return QUERY->end_class(QUERY);
}
