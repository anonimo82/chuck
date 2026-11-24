// Optional offline audio file I/O. Does not modify the ChucK VM.
#include "chuck_dl.h"
#include "../file_compat.h"
#include <sndfile.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {
using Sound = std::unique_ptr<SNDFILE, decltype(&sf_close)>;
Sound openInput(const std::string &path, SF_INFO &info) {
    return Sound(sf_open(path.c_str(), SFM_READ, &info), sf_close);
}
int outputFormat(const std::string &path) {
    auto pos = path.find_last_of('.');
    if(pos == std::string::npos) return 0;
    auto ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c){ return std::tolower(c); });
    if(ext == "wav") return SF_FORMAT_WAV | SF_FORMAT_PCM_24;
    if(ext == "aif" || ext == "aiff") return SF_FORMAT_AIFF | SF_FORMAT_PCM_24;
    if(ext == "au" || ext == "snd") return SF_FORMAT_AU | SF_FORMAT_PCM_24;
    if(ext == "flac") return SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
    if(ext == "ogg") return SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    if(ext == "opus") return SF_FORMAT_OGG | SF_FORMAT_OPUS;
    if(ext == "mp3") return SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    return 0;
}
}
CK_DLL_SFUN(audio_rate) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        SF_INFO info{};
        auto f = openInput(path, info);
        if(f) RETURN->v_int = info.samplerate;
    } catch(...) {}
}
CK_DLL_SFUN(audio_channels) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        SF_INFO info{};
        auto f = openInput(path, info);
        if(f) RETURN->v_int = info.channels;
    } catch(...) {}
}
CK_DLL_SFUN(audio_seconds) {
    RETURN->v_float = -1.0;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        SF_INFO info{};
        auto f = openInput(path, info);
        if(f && info.samplerate > 0)
            RETURN->v_float = static_cast<t_CKFLOAT>(info.frames) / info.samplerate;
    } catch(...) {}
}
CK_DLL_SFUN(audio_convert) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        SF_INFO info{};
        auto input = openInput(source, info);
        if(!input || info.channels < 1 || info.channels > 256) return;
        SF_INFO output{};
        output.channels = info.channels;
        output.samplerate = info.samplerate;
        output.format = outputFormat(dest);
        if(!output.format || !sf_format_check(&output)) return;
        const sf_count_t block = 4096;
        std::vector<double> samples(static_cast<size_t>(block) * info.channels);
        // Exclusive file creation, including when source and destination coincide.
        FILE *file = mediaio::openNew(dest.c_str());
        if(!file) return;
#if defined(_WIN32)
        int fd = _fileno(file);
#else
        int fd = fileno(file);
#endif
        Sound out(sf_open_fd(fd, SFM_WRITE, &output, SF_FALSE), sf_close);
        bool ok = static_cast<bool>(out);
        if(ok) {
            sf_count_t n;
            while((n = sf_readf_double(input.get(), samples.data(), block)) > 0) {
                if(sf_writef_double(out.get(), samples.data(), n) != n) {
                    ok = false;
                    break;
                }
            }
            if(sf_error(input.get()) != SF_ERR_NO_ERROR ||
               sf_error(out.get()) != SF_ERR_NO_ERROR) ok = false;
            if(sf_close(out.release()) != 0) ok = false;
        }
        if(std::fclose(file) != 0) ok = false;
        if(!ok) { std::remove(dest.c_str()); return; }
        RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_QUERY(AudioIO) {
    QUERY->setname(QUERY, "AudioIO");
    QUERY->begin_class(QUERY, "AudioIO", "Object");
    QUERY->add_sfun(QUERY, audio_rate, "int", "sampleRate");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, audio_channels, "int", "channels");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, audio_seconds, "float", "seconds");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, audio_convert, "int", "convert");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    return QUERY->end_class(QUERY);
}
