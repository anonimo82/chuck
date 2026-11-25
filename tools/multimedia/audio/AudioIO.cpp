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
#include <complex>
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


namespace {
bool audioRange(AudioBufferState *p,t_CKINT first,t_CKINT count) {
    return p && p->loaded() && first>=0 && count>0 &&
        static_cast<uint64_t>(first)<p->frames() &&
        static_cast<uint64_t>(count)<=p->frames()-static_cast<size_t>(first);
}
double clipSample(double v) { return std::max(-1.0,std::min(1.0,v)); }
}
CK_DLL_MFUN(audio_buffer_crop) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto first=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
        if(!audioRange(p,first,count)) return;
        auto begin=p->samples.begin()+static_cast<size_t>(first)*p->channels;
        std::vector<double> next(begin,begin+static_cast<size_t>(count)*p->channels);
        p->samples.swap(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_remove) {
    RETURN->v_int=0;
    auto *p=audioState(SELF,API);
    auto first=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
    if(!audioRange(p,first,count) || static_cast<size_t>(count)==p->frames()) return;
    auto begin=p->samples.begin()+static_cast<size_t>(first)*p->channels;
    p->samples.erase(begin,begin+static_cast<size_t>(count)*p->channels);
    RETURN->v_int=1;
}
CK_DLL_MFUN(audio_buffer_append) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p || !p->loaded() || !a) return;
        auto n=API->object->array_float_size(a);
        constexpr size_t maxSamples=32*1024*1024;
        if(n<=0 || n%p->channels || static_cast<uint64_t>(n)>maxSamples-p->samples.size()) return;
        std::vector<double> next;
        next.reserve(static_cast<size_t>(n));
        for(t_CKINT i=0;i<n;++i) {
            double v=API->object->array_float_get_idx(a,i);
            if(!std::isfinite(v)||v< -1||v>1) return;
            next.push_back(v);
        }
        p->samples.insert(p->samples.end(),next.begin(),next.end());
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_reverse) {
    RETURN->v_int=0;
    auto *p=audioState(SELF,API);
    if(!p || !p->loaded()) return;
    size_t n=p->frames();
    for(size_t i=0;i<n/2;++i) for(int c=0;c<p->channels;++c)
        std::swap(p->samples[i*p->channels+c],p->samples[(n-1-i)*p->channels+c]);
    RETURN->v_int=1;
}
CK_DLL_MFUN(audio_buffer_peak) {
    RETURN->v_float=audioNaN();
    auto *p=audioState(SELF,API);
    if(!p || !p->loaded()) return;
    double peak=0;
    for(double v:p->samples) peak=std::max(peak,std::abs(v));
    RETURN->v_float=peak;
}
CK_DLL_MFUN(audio_buffer_normalize) {
    RETURN->v_int=0;
    auto *p=audioState(SELF,API); auto target=GET_NEXT_FLOAT(ARGS);
    if(!p || !p->loaded() || !std::isfinite(target)||target<0||target>1) return;
    double peak=0;
    for(double v:p->samples) peak=std::max(peak,std::abs(v));
    if(peak>0) for(auto &v:p->samples) v=(v/peak)*target;
    RETURN->v_int=1;
}
CK_DLL_MFUN(audio_buffer_fade) {
    RETURN->v_int=0;
    auto *p=audioState(SELF,API);
    auto first=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
    auto from=GET_NEXT_FLOAT(ARGS),to=GET_NEXT_FLOAT(ARGS);
    if(!audioRange(p,first,count)||count<2||!std::isfinite(from)||!std::isfinite(to)||
       from<0||from>1||to<0||to>1) return;
    for(size_t i=0;i<static_cast<size_t>(count);++i) {
        double t=static_cast<double>(i)/(count-1);
        double factor=from+(to-from)*t;
        for(int c=0;c<p->channels;++c) p->samples[(static_cast<size_t>(first)+i)*p->channels+c]*=factor;
    }
    RETURN->v_int=1;
}
CK_DLL_MFUN(audio_buffer_mix) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API); auto first=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto gain=GET_NEXT_FLOAT(ARGS);
        if(!p||!p->loaded()||!a||!std::isfinite(gain)||gain<0||gain>16) return;
        auto n=API->object->array_float_size(a);
        if(n<=0||n%p->channels||!audioRange(p,first,n/p->channels)) return;
        for(t_CKINT i=0;i<n;++i) {
            double v=API->object->array_float_get_idx(a,i);
            if(!std::isfinite(v)||v< -1||v>1) return;
        }
        size_t start=static_cast<size_t>(first)*p->channels;
        for(t_CKINT i=0;i<n;++i)
            p->samples[start+i]=clipSample(p->samples[start+i]+gain*API->object->array_float_get_idx(a,i));
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_read_samples) {
    RETURN->v_int=0;
    auto *p=audioState(SELF,API); auto first=GET_NEXT_INT(ARGS);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
    if(!p||!p->loaded()||!a) return;
    auto n=API->object->array_float_size(a);
    if(n<=0||n%p->channels||!audioRange(p,first,n/p->channels)) return;
    for(t_CKINT i=0;i<n;++i)
        API->object->array_float_set_idx(a,i,p->value(static_cast<size_t>(first)+i/p->channels,i%p->channels));
    RETURN->v_int=1;
}


namespace {
// Bounded windowed-sinc interpolation. The cutoff follows the output
// Nyquist frequency when decimating; edge taps extend the endpoint samples.
bool resampleBuffer(AudioBufferState *p,double ratio,int newRate) {
    if(!p || !p->loaded() || !std::isfinite(ratio) || ratio<0.0625 || ratio>16) return false;
    const size_t old=p->frames();
    const double exact=old*ratio;
    if(exact<0.5 || exact>32.0*1024*1024/p->channels+0.499999) return false;
    const auto n=static_cast<size_t>(std::llround(exact));
    if(!bufferDimensions(n,newRate,p->channels)) return false;
    if(ratio==1.0) { p->rate=newRate; return true; }
    // Limit the total convolution cost as well as allocation size.
    constexpr double pi=3.14159265358979323846;
    const double cutoff=std::min(1.0,ratio);
    const int radius=static_cast<int>(std::ceil(16.0/cutoff));
    if(static_cast<double>(n)*(2*radius+1)*p->channels>1000000000.0) return false;
    std::vector<double> next(n*p->channels);
    for(size_t i=0;i<n;++i) {
        double pos=static_cast<double>(i)/ratio;
        int64_t center=static_cast<int64_t>(std::floor(pos));
        double sums[8]={}, weights=0;
        for(int j=-radius;j<=radius;++j) {
            int64_t k=center+j;
            double d=pos-k;
            if(std::abs(d)>=radius) continue;
            double z=pi*d*cutoff;
            double sinc=std::abs(z)<1e-12?1.0:std::sin(z)/z;
            double weight=sinc*(0.5+0.5*std::cos(pi*d/radius))*cutoff;
            size_t src=static_cast<size_t>(std::clamp<int64_t>(k,0,static_cast<int64_t>(old)-1));
            for(int c=0;c<p->channels;++c) sums[c]+=p->samples[src*p->channels+c]*weight;
            weights+=weight;
        }
        if(!std::isfinite(weights)||std::abs(weights)<1e-12) return false;
        for(int c=0;c<p->channels;++c) {
            double v=sums[c]/weights;
            if(!std::isfinite(v)) return false;
            next[i*p->channels+c]=clipSample(v);
        }
    }
    p->samples.swap(next); p->rate=newRate; return true;
}
bool filterBuffer(AudioBufferState *p,double hz,bool high) {
    if(!p || !p->loaded() || !std::isfinite(hz) || hz<=0 || hz>=p->rate/2.0) return false;
    // One-pole low-pass with exponential coefficient; high-pass is its
    // complementary residual. Independent state per channel, zero initial state.
    const double a=-std::expm1(-2.0*3.14159265358979323846*hz/p->rate);
    double state[8]={};
    std::vector<double> next(p->samples.size());
    for(size_t i=0;i<p->frames();++i) for(int c=0;c<p->channels;++c) {
        size_t j=i*p->channels+c;
        double x=p->samples[j];
        state[c]=(1-a)*state[c]+a*x;
        double v=high?x-state[c]:state[c];
        if(!std::isfinite(v)) return false;
        next[j]=clipSample(v);
    }
    p->samples.swap(next); return true;
}
}
CK_DLL_MFUN(audio_buffer_resample) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API); auto rate=GET_NEXT_INT(ARGS);
        if(p && p->loaded() && rate>=1 && rate<=384000)
            RETURN->v_int=resampleBuffer(p,static_cast<double>(rate)/p->rate,static_cast<int>(rate));
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_speed) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API); auto factor=GET_NEXT_FLOAT(ARGS);
        if(p && p->loaded() && std::isfinite(factor) && factor>=0.0625 && factor<=16)
            RETURN->v_int=resampleBuffer(p,1.0/factor,p->rate);
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_channels_convert) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API); auto channels=GET_NEXT_INT(ARGS);
        if(!p || !p->loaded() || channels<1 || channels>2 ||
           p->channels>2 || !bufferDimensions(p->frames(),p->rate,channels)) return;
        if(channels==p->channels) { RETURN->v_int=1; return; }
        size_t n=p->frames();
        std::vector<double> next(n*channels);
        for(size_t i=0;i<n;++i) {
            if(channels==1) next[i]=0.5*p->samples[i*2]+0.5*p->samples[i*2+1];
            else next[i*2]=next[i*2+1]=p->samples[i];
        }
        p->samples.swap(next); p->channels=static_cast<int>(channels); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_lowpass) {
    RETURN->v_int=0;
    try { auto *p=audioState(SELF,API); auto hz=GET_NEXT_FLOAT(ARGS); RETURN->v_int=filterBuffer(p,hz,false); } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_highpass) {
    RETURN->v_int=0;
    try { auto *p=audioState(SELF,API); auto hz=GET_NEXT_FLOAT(ARGS); RETURN->v_int=filterBuffer(p,hz,true); } catch(...) {}
}

namespace {
bool imageOutput(AudioBufferState *p,t_CKINT channel,t_CKINT w,t_CKINT h,
                 Chuck_DL_Api::ArrayInt a,CK_DL_API API) {
    return p&&p->loaded()&&channel>=0&&channel<p->channels&&w>=1&&w<=4096&&
           h>=1&&h<=4096&&w*h<=4194304&&a&&API->object->array_int_size(a)==w*h*4;
}
void grayPixel(std::vector<unsigned char> &rgba,size_t pixel,unsigned char level) {
    rgba[4*pixel]=rgba[4*pixel+1]=rgba[4*pixel+2]=level;
    rgba[4*pixel+3]=255;
}
void publishImage(const std::vector<unsigned char> &rgba,Chuck_DL_Api::ArrayInt a,CK_DL_API API) {
    for(size_t i=0;i<rgba.size();++i) API->object->array_int_set_idx(a,i,rgba[i]);
}
void fft(std::vector<std::complex<double>> &v) {
    size_t n=v.size();
    for(size_t i=1,j=0;i<n;++i) {
        size_t bit=n>>1;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if(i<j) std::swap(v[i],v[j]);
    }
    for(size_t len=2;len<=n;len*=2) {
        double angle=-6.28318530717958647692/len;
        std::complex<double> step(std::cos(angle),std::sin(angle));
        for(size_t i=0;i<n;i+=len) {
            std::complex<double> phase(1,0);
            for(size_t j=0;j<len/2;++j) {
                auto a=v[i+j],b=v[i+j+len/2]*phase;
                v[i+j]=a+b; v[i+j+len/2]=a-b;
                phase*=step;
            }
        }
    }
}
}
CK_DLL_MFUN(audio_buffer_waveform) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto channel=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!imageOutput(p,channel,w,h,a,API)) return;
        std::vector<unsigned char> rgba(w*h*4,0);
        for(size_t i=0;i<rgba.size()/4;++i) rgba[4*i+3]=255;
        auto row=[&](double v){return static_cast<size_t>(std::llround((1-v)*0.5*(h-1)));};
        for(size_t x=0;x<static_cast<size_t>(w);++x) {
            size_t begin=x*uint64_t(p->frames())/w;
            size_t end=std::max(begin+1,(x+1)*uint64_t(p->frames())/w);
            double lo=1,hi=-1;
            for(size_t i=begin;i<end;++i) {
                double v=p->value(i,channel);
                lo=std::min(lo,v); hi=std::max(hi,v);
            }
            for(size_t y=row(hi);y<=row(lo);++y) grayPixel(rgba,y*w+x,255);
        }
        publishImage(rgba,a,API); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_spectrogram) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto channel=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),n=GET_NEXT_INT(ARGS);
        double floorDb=GET_NEXT_FLOAT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(n<32||n>4096||(n&(n-1))||!std::isfinite(floorDb)||floorDb< -160||floorDb> -10) return;
        auto h=n/2+1;
        if(!imageOutput(p,channel,w,h,a,API)||uint64_t(w)*n>8388608) return;
        std::vector<unsigned char> rgba(w*h*4,0);
        std::vector<double> window(n);
        std::vector<std::complex<double>> bins(n);
        double sum=0;
        for(size_t i=0;i<window.size();++i) {
            window[i]=0.5-0.5*std::cos(6.28318530717958647692*i/(n-1));
            sum+=window[i];
        }
        for(size_t x=0;x<static_cast<size_t>(w);++x) {
            // Uniform frame centers over the source; zero-pad outside it.
            int64_t center=static_cast<int64_t>((2*x+1)*uint64_t(p->frames())/(2*w));
            for(int64_t i=0;i<n;++i) {
                int64_t src=center+i-n/2;
                bins[i]=(src>=0&&static_cast<uint64_t>(src)<p->frames()?p->value(src,channel):0.0)*window[i];
            }
            fft(bins);
            for(size_t k=0;k<static_cast<size_t>(h);++k) {
                double magnitude=std::abs(bins[k])/sum;
                if(k>0&&k<static_cast<size_t>(n/2)) magnitude*=2;
                double db=20*std::log10(std::max(magnitude,1e-12));
                auto level=static_cast<unsigned char>(std::lround(255*std::clamp((db-floorDb)/(-floorDb),0.0,1.0)));
                grayPixel(rgba,(h-1-k)*w+x,level);
            }
        }
        publishImage(rgba,a,API); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_envelope) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto channel=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->loaded()||!p->frames()||channel<0||channel>=p->channels||
           count<1||count>4096||!a||API->object->array_float_size(a)!=count*2) return;
        std::vector<double> out(count*2);
        for(size_t x=0;x<static_cast<size_t>(count);++x) {
            size_t begin=x*uint64_t(p->frames())/count;
            size_t end=std::max(begin+1,(x+1)*uint64_t(p->frames())/count);
            double lo=1,hi=-1;
            for(size_t i=begin;i<end;++i) {
                double v=p->value(i,channel);
                lo=std::min(lo,v); hi=std::max(hi,v);
            }
            out[2*x]=lo; out[2*x+1]=hi;
        }
        for(size_t i=0;i<out.size();++i) API->object->array_float_set_idx(a,i,out[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_detect_pitches) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto channel=GET_NEXT_INT(ARGS),block=GET_NEXT_INT(ARGS);
        double gate=GET_NEXT_FLOAT(ARGS),confidence=GET_NEXT_FLOAT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->loaded()||!a||channel<0||channel>=p->channels||
           block<256||block>16384||p->rate<8000||p->rate>96000||
           !std::isfinite(gate)||gate<=0||gate>1||
           !std::isfinite(confidence)||confidence<0.5||confidence>1) return;
        size_t count=p->frames()/block;
        if(count<1||count>100000||API->object->array_int_size(a)!=static_cast<t_CKINT>(count)) return;
        int minLag=std::max(2,static_cast<int>(std::floor(p->rate/2000.0)));
        int maxLag=std::min<int>(block/3,static_cast<int>(std::ceil(p->rate/65.0)));
        if(maxLag<=minLag||uint64_t(count)*block*(maxLag-minLag+1)>200000000) return;
        std::vector<t_CKINT> pitches(count,-1);
        std::vector<double> signal(block),scores(maxLag+1);
        for(size_t b=0;b<count;++b) {
            double mean=0;
            for(int i=0;i<block;++i) { signal[i]=p->value(b*block+i,channel); mean+=signal[i]; }
            mean/=block;
            double power=0;
            for(auto &v:signal) { v-=mean; power+=v*v; }
            if(std::sqrt(power/block)<gate) continue;
            for(int lag=minLag-1;lag<=maxLag;++lag) {
                double xy=0,xx=0,yy=0;
                for(int i=0;i<block-lag;++i) {
                    double x=signal[i],y=signal[i+lag];
                    xy+=x*y; xx+=x*x; yy+=y*y;
                }
                scores[lag]=(xx>0&&yy>0)?xy/std::sqrt(xx*yy):0.0;
            }
            int best=-1;
            for(int lag=minLag;lag<maxLag;++lag)
                if(scores[lag]>=confidence&&scores[lag]>=scores[lag-1]&&scores[lag]>=scores[lag+1]) {
                    best=lag; break;
                }
            if(best<0) continue;
            double l=scores[best-1],c=scores[best],r=scores[best+1],den=l-2*c+r;
            double delta=den<0?std::clamp(0.5*(l-r)/den,-0.5,0.5):0.0;
            double hz=p->rate/(best+delta);
            if(hz<65||hz>2000) continue;
            long note=std::lround(69+12*std::log2(hz/440.0));
            if(note>=0&&note<=127) pitches[b]=note;
        }
        for(size_t i=0;i<count;++i) API->object->array_int_set_idx(a,i,pitches[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(audio_buffer_waveform_range) {
    RETURN->v_int=0;
    try {
        auto *p=audioState(SELF,API);
        auto channel=GET_NEXT_INT(ARGS),first=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!imageOutput(p,channel,w,h,a,API)||!audioRange(p,first,count)) return;
        std::vector<unsigned char> rgba(w*h*4,0);
        for(size_t i=0;i<rgba.size()/4;++i) rgba[4*i+3]=255;
        auto row=[&](double v){return static_cast<size_t>(std::llround((1-v)*0.5*(h-1)));};
        for(size_t x=0;x<static_cast<size_t>(w);++x) {
            size_t begin=x*uint64_t(count)/w;
            size_t end=std::max(begin+1,(x+1)*uint64_t(count)/w);
            double lo=1,hi=-1;
            for(size_t i=begin;i<end;++i) {
                double v=p->value(static_cast<size_t>(first)+i,channel);
                lo=std::min(lo,v); hi=std::max(hi,v);
            }
            for(size_t y=row(hi);y<=row(lo);++y) grayPixel(rgba,y*w+x,255);
        }
        publishImage(rgba,a,API); RETURN->v_int=1;
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
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"AudioBuffer","Object");
    audioBufferOffset=QUERY->add_mvar(QUERY,"int","@audioBufferNative",FALSE);
    if(audioBufferOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,audio_buffer_ctor);
    QUERY->add_dtor(QUERY,audio_buffer_dtor);
    QUERY->add_mfun(QUERY,audio_buffer_detect_pitches,"int","detectPitches");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","blockFrames");
    QUERY->add_arg(QUERY,"float","rmsGate");
    QUERY->add_arg(QUERY,"float","confidence");
    QUERY->add_arg(QUERY,"int[]","pitches");
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
    QUERY->add_mfun(QUERY,audio_buffer_crop,"int","crop");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","frameCount");
    QUERY->add_mfun(QUERY,audio_buffer_remove,"int","remove");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","frameCount");
    QUERY->add_mfun(QUERY,audio_buffer_append,"int","appendSamples");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,audio_buffer_reverse,"int","reverse");
    QUERY->add_mfun(QUERY,audio_buffer_peak,"float","peak");
    QUERY->add_mfun(QUERY,audio_buffer_normalize,"int","normalize");
    QUERY->add_arg(QUERY,"float","target");
    QUERY->add_mfun(QUERY,audio_buffer_fade,"int","fade");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","frameCount");
    QUERY->add_arg(QUERY,"float","fromGain");
    QUERY->add_arg(QUERY,"float","toGain");
    QUERY->add_mfun(QUERY,audio_buffer_mix,"int","mixSamples");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_arg(QUERY,"float","mixGain");
    QUERY->add_mfun(QUERY,audio_buffer_read_samples,"int","readSamples");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,audio_buffer_resample,"int","resample");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_mfun(QUERY,audio_buffer_speed,"int","changeSpeed");
    QUERY->add_arg(QUERY,"float","factor");
    QUERY->add_mfun(QUERY,audio_buffer_channels_convert,"int","convertChannels");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_mfun(QUERY,audio_buffer_lowpass,"int","lowPass");
    QUERY->add_arg(QUERY,"float","frequency");
    QUERY->add_mfun(QUERY,audio_buffer_highpass,"int","highPass");
    QUERY->add_arg(QUERY,"float","frequency");
    QUERY->add_mfun(QUERY,audio_buffer_envelope,"int","exportEnvelope");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","count");
    QUERY->add_arg(QUERY,"float[]","bounds");
    QUERY->add_mfun(QUERY,audio_buffer_waveform_range,"int","exportWaveform");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","firstFrame");
    QUERY->add_arg(QUERY,"int","frameCount");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,audio_buffer_waveform,"int","exportWaveform");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,audio_buffer_spectrogram,"int","exportSpectrogram");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","fftSize");
    QUERY->add_arg(QUERY,"float","floorDb");
    QUERY->add_arg(QUERY,"int[]","pixels");
    return QUERY->end_class(QUERY);
}
