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
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

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

namespace {
struct AudioBufferState {
    std::vector<double> samples;
    int rate=0,channels=0;
    double gain=1,balance=0;
    void reset() { gain=1; balance=0; }
    bool loaded() const { return rate>0 && channels>0; }
    size_t frames() const { return loaded()?samples.size()/channels:0; }
    bool read(const std::string &path) {
        SF_INFO info{};
        auto in=openInput(path,info);
        constexpr size_t maxSamples=32*1024*1024;
        if(!in || info.channels<1 || info.channels>8 || info.samplerate<1 ||
           info.samplerate>384000 || info.frames<1 ||
           static_cast<uint64_t>(info.frames)>maxSamples/info.channels) return false;
        std::vector<double> data(static_cast<size_t>(info.frames)*info.channels);
        if(sf_readf_double(in.get(),data.data(),info.frames)!=info.frames ||
           sf_error(in.get())!=SF_ERR_NO_ERROR) return false;
        for(double v:data) if(!std::isfinite(v)) return false;
        samples=std::move(data);
        rate=info.samplerate; channels=info.channels;
        reset();
        return true;
    }
    double value(size_t frame,int channel) const {
        double factor=gain;
        if(channels==2) {
            if(channel==0 && balance>0) factor*=1-balance;
            if(channel==1 && balance<0) factor*=1+balance;
        }
        return std::max(-1.0,std::min(1.0,samples[frame*channels+channel]*factor));
    }
    bool save(const std::string &path) const {
        if(!loaded()) return false;
        SF_INFO info{};
        info.samplerate=rate; info.channels=channels;
        info.format=outputFormat(path);
        if(!info.format || !sf_format_check(&info)) return false;
        std::vector<double> block(4096*channels);
        FILE *file=mediaio::openNew(path.c_str());
        if(!file) return false;
#if defined(_WIN32)
        int fd=_fileno(file);
#else
        int fd=fileno(file);
#endif
        Sound out(sf_open_fd(fd,SFM_WRITE,&info,SF_FALSE),sf_close);
        bool ok=static_cast<bool>(out);
        if(ok) {
            for(size_t pos=0;pos<frames();) {
                size_t n=std::min<size_t>(4096,frames()-pos);
                for(size_t i=0;i<n;++i)
                    for(int c=0;c<channels;++c) block[i*channels+c]=value(pos+i,c);
                if(sf_writef_double(out.get(),block.data(),n)!=static_cast<sf_count_t>(n)) {
                    ok=false; break;
                }
                pos+=n;
            }
            if(sf_error(out.get())!=SF_ERR_NO_ERROR) ok=false;
            if(sf_close(out.release())!=0) ok=false;
        }
        if(std::fclose(file)!=0) ok=false;
        if(!ok) std::remove(path.c_str());
        return ok;
    }
};
t_CKUINT audioBufferOffset=0;
AudioBufferState *audioState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<AudioBufferState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,audioBufferOffset)));
}
double audioNaN() { return std::numeric_limits<double>::quiet_NaN(); }
bool sampleIndex(AudioBufferState *p,t_CKINT frame,t_CKINT channel) {
    return p && p->loaded() && frame>=0 && channel>=0 &&
           static_cast<uint64_t>(frame)<p->frames() && channel<p->channels;
}
}
CK_DLL_CTOR(audio_buffer_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold native pointer");
    OBJ_MEMBER_UINT(SELF,audioBufferOffset)=0;
    OBJ_MEMBER_UINT(SELF,audioBufferOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) AudioBufferState);
}
CK_DLL_DTOR(audio_buffer_dtor) {
    delete audioState(SELF,API);
    OBJ_MEMBER_UINT(SELF,audioBufferOffset)=0;
}
CK_DLL_MFUN(audio_buffer_load) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->read(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_save) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->save(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_loaded) {
    auto *p=audioState(SELF,API);
    RETURN->v_int=p && p->loaded();
}
CK_DLL_MFUN(audio_buffer_frames) {
    auto *p=audioState(SELF,API);
    RETURN->v_int=p?static_cast<t_CKINT>(p->frames()):0;
}
CK_DLL_MFUN(audio_buffer_rate) {
    auto *p=audioState(SELF,API);
    RETURN->v_int=p?p->rate:0;
}
CK_DLL_MFUN(audio_buffer_channels) {
    auto *p=audioState(SELF,API);
    RETURN->v_int=p?p->channels:0;
}
CK_DLL_MFUN(audio_buffer_seconds) {
    auto *p=audioState(SELF,API);
    RETURN->v_float=p && p->loaded()?static_cast<double>(p->frames())/p->rate:0;
}
CK_DLL_MFUN(audio_buffer_clear) {
    if(auto *p=audioState(SELF,API)) {
        std::vector<double>().swap(p->samples);
        p->rate=p->channels=0; p->reset();
    }
}
CK_DLL_MFUN(audio_buffer_reset) {
    if(auto *p=audioState(SELF,API)) p->reset();
}
CK_DLL_MFUN(audio_buffer_get_gain) {
    auto *p=audioState(SELF,API);
    RETURN->v_float=p?p->gain:audioNaN();
}
CK_DLL_MFUN(audio_buffer_set_gain) {
    auto *p=audioState(SELF,API);
    auto v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=audioNaN();
    if(p && std::isfinite(v) && v>=0 && v<=16) RETURN->v_float=p->gain=v;
}
CK_DLL_MFUN(audio_buffer_get_balance) {
    auto *p=audioState(SELF,API);
    RETURN->v_float=p?p->balance:audioNaN();
}
CK_DLL_MFUN(audio_buffer_set_balance) {
    auto *p=audioState(SELF,API);
    auto v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=audioNaN();
    if(p && std::isfinite(v) && v>=-1 && v<=1) RETURN->v_float=p->balance=v;
}
CK_DLL_MFUN(audio_buffer_sample) {
    auto *p=audioState(SELF,API);
    auto frame=GET_NEXT_INT(ARGS),channel=GET_NEXT_INT(ARGS);
    RETURN->v_float=sampleIndex(p,frame,channel)?p->samples[static_cast<size_t>(frame)*p->channels+channel]:audioNaN();
}
CK_DLL_MFUN(audio_buffer_value) {
    auto *p=audioState(SELF,API);
    auto frame=GET_NEXT_INT(ARGS),channel=GET_NEXT_INT(ARGS);
    RETURN->v_float=sampleIndex(p,frame,channel)?p->value(frame,channel):audioNaN();
}
CK_DLL_MFUN(audio_buffer_set_sample) {
    auto *p=audioState(SELF,API);
    auto frame=GET_NEXT_INT(ARGS),channel=GET_NEXT_INT(ARGS);
    auto v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_int=0;
    if(sampleIndex(p,frame,channel) && std::isfinite(v) && v>=-1 && v<=1) {
        p->samples[static_cast<size_t>(frame)*p->channels+channel]=v;
        RETURN->v_int=1;
    }
}


namespace {
bool bufferDimensions(t_CKINT frames,t_CKINT rate,t_CKINT channels) {
    return frames>0 && rate>0 && rate<=384000 && channels>0 && channels<=8 &&
        frames<=32*1024*1024/channels;
}
}
CK_DLL_MFUN(audio_buffer_create) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto frames=GET_NEXT_INT(ARGS),rate=GET_NEXT_INT(ARGS),channels=GET_NEXT_INT(ARGS);
        if(!p || !bufferDimensions(frames,rate,channels)) return;
        std::vector<double> data(static_cast<size_t>(frames)*channels,0.0);
        p->samples=std::move(data); p->rate=rate; p->channels=channels; p->reset();
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_import_samples) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto rate=GET_NEXT_INT(ARGS),channels=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p || !a || channels<1 || channels>8) return;
        auto n=API->object->array_float_size(a);
        if(n<=0 || n%channels || !bufferDimensions(n/channels,rate,channels)) return;
        std::vector<double> data(static_cast<size_t>(n));
        for(t_CKINT i=0;i<n;++i) {
            double v=API->object->array_float_get_idx(a,i);
            if(!std::isfinite(v) || v< -1 || v>1) return;
            data[i]=v;
        }
        p->samples=std::move(data); p->rate=rate; p->channels=channels; p->reset();
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_export_samples) {
    RETURN->v_int=0;
    auto *p=audioState(SELF,API);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
    if(!p || !a || !p->loaded() ||
       API->object->array_float_size(a)!=static_cast<t_CKINT>(p->samples.size())) return;
    for(size_t f=0;f<p->frames();++f)
        for(int c=0;c<p->channels;++c)
            API->object->array_float_set_idx(a,f*p->channels+c,p->value(f,c));
    RETURN->v_int=1;
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
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"AudioBuffer","Object");
    audioBufferOffset=QUERY->add_mvar(QUERY,"int","@audioBufferNative",FALSE);
    if(audioBufferOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,audio_buffer_ctor);
    QUERY->add_dtor(QUERY,audio_buffer_dtor);
    QUERY->add_mfun(QUERY,audio_buffer_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,audio_buffer_save,"int","save");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,audio_buffer_loaded,"int","loaded");
    QUERY->add_mfun(QUERY,audio_buffer_frames,"int","frames");
    QUERY->add_mfun(QUERY,audio_buffer_rate,"int","sampleRate");
    QUERY->add_mfun(QUERY,audio_buffer_channels,"int","channels");
    QUERY->add_mfun(QUERY,audio_buffer_seconds,"float","seconds");
    QUERY->add_mfun(QUERY,audio_buffer_clear,"void","clear");
    QUERY->add_mfun(QUERY,audio_buffer_reset,"void","reset");
    QUERY->add_mfun(QUERY,audio_buffer_get_gain,"float","gain");
    QUERY->add_mfun(QUERY,audio_buffer_set_gain,"float","gain");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,audio_buffer_get_balance,"float","balance");
    QUERY->add_mfun(QUERY,audio_buffer_set_balance,"float","balance");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,audio_buffer_sample,"float","sampleAt");
    QUERY->add_arg(QUERY,"int","frameIndex");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_mfun(QUERY,audio_buffer_value,"float","valueAt");
    QUERY->add_arg(QUERY,"int","frameIndex");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_mfun(QUERY,audio_buffer_set_sample,"int","setSample");
    QUERY->add_arg(QUERY,"int","frameIndex");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,audio_buffer_create,"int","create");
    QUERY->add_arg(QUERY,"int","frameCount");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_mfun(QUERY,audio_buffer_import_samples,"int","importSamples");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,audio_buffer_export_samples,"int","exportSamples");
    QUERY->add_arg(QUERY,"float[]","samples");
    return QUERY->end_class(QUERY);
}
