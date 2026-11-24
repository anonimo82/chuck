#include "chuck_dl.h"
#include "subtitle_io.h"
#include <cmath>
#include <limits>
#include <new>
#include <memory>
CK_DLL_SFUN(sub_count) {
    RETURN->v_int=-1;
    try {
        std::vector<subtitles::Cue> cues;
        if(subtitles::load(GET_NEXT_STRING_SAFE(ARGS),cues)) RETURN->v_int=cues.size();
    } catch(...) {}
}
CK_DLL_SFUN(sub_start) {
    RETURN->v_float=-1;
    try {
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto index=GET_NEXT_INT(ARGS);
        std::vector<subtitles::Cue> cues;
        if(index>=0 && subtitles::load(path,cues) && static_cast<size_t>(index)<cues.size())
            RETURN->v_float=cues[index].start/1000.0;
    } catch(...) {}
}
CK_DLL_SFUN(sub_end) {
    RETURN->v_float=-1;
    try {
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto index=GET_NEXT_INT(ARGS);
        std::vector<subtitles::Cue> cues;
        if(index>=0 && subtitles::load(path,cues) && static_cast<size_t>(index)<cues.size())
            RETURN->v_float=cues[index].end/1000.0;
    } catch(...) {}
}
CK_DLL_SFUN(sub_text) {
    std::string result;
    try {
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto index=GET_NEXT_INT(ARGS);
        std::vector<subtitles::Cue> cues;
        if(index>=0 && subtitles::load(path,cues) && static_cast<size_t>(index)<cues.size())
            result=cues[index].text;
    } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}
CK_DLL_SFUN(sub_convert) {
    RETURN->v_int=0;
    try {
        auto source=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        std::vector<subtitles::Cue> cues;
        if(subtitles::load(source,cues) && subtitles::save(dest,cues)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(sub_write) {
    RETURN->v_int=0;
    try {
        auto dest=GET_NEXT_STRING_SAFE(ARGS), content=GET_NEXT_STRING_SAFE(ARGS);
        auto begin=GET_NEXT_INT(ARGS), finish=GET_NEXT_INT(ARGS);
        if(begin<0 || finish<=begin || finish>=360000000 || content.empty() ||
           content.size()>16384 || content.front()=='\n' || content.back()=='\n' ||
           content.find("\n\n")!=std::string::npos || content.find_first_of("\r<>&")!=std::string::npos ||
           !g_utf8_validate(content.data(),static_cast<gssize>(content.size()),nullptr)) return;
        if(subtitles::save(dest,{{begin,finish,content}})) RETURN->v_int=1;
    } catch(...) {}
}

namespace {
constexpr int64_t maxSubtitleMs=359999999;
bool cueValid(int64_t begin,int64_t end,const std::string &text) {
    return begin>=0 && end>begin && end<=maxSubtitleMs && !text.empty() &&
        text.size()<=16384 && text.front()!='\n' && text.back()!='\n' &&
        text.find("\n\n")==std::string::npos && text.find_first_of("\r<>&")==std::string::npos &&
        g_utf8_validate(text.data(),static_cast<gssize>(text.size()),nullptr);
}
struct SubtitleState {
    std::vector<subtitles::Cue> cues;
    double offset=0,scale=1;
    void reset() { offset=0; scale=1; }
    bool load(const std::string &path) {
        std::vector<subtitles::Cue> next;
        if(!subtitles::load(path,next)) return false;
        cues=std::move(next); reset(); return true;
    }
    bool transform(const subtitles::Cue &c,int64_t &a,int64_t &b) const {
        double x=c.start*scale+offset*1000,y=c.end*scale+offset*1000;
        if(x<0 || y>maxSubtitleMs) return false;
        a=std::llround(x); b=std::llround(y);
        return b>a;
    }
    bool save(const std::string &path) const {
        std::vector<subtitles::Cue> next;
        next.reserve(cues.size());
        size_t bytes=0;
        for(const auto &c:cues) {
            int64_t a,b;
            if(!transform(c,a,b)) return false;
            bytes+=c.text.size()+64;
            if(bytes>16*1024*1024) return false;
            next.push_back({a,b,c.text});
        }
        return subtitles::save(path,next);
    }
    bool index(t_CKINT i) const { return i>=0 && static_cast<uint64_t>(i)<cues.size(); }
    bool edit(t_CKINT i,int64_t a,int64_t b,const std::string &text,bool append) {
        if(!cueValid(a,b,text)) return false;
        if(append) {
            if(cues.size()>=100000 || (!cues.empty() && a<cues.back().start)) return false;
            cues.push_back({a,b,text});
        } else {
            if(!index(i)) return false;
            size_t n=static_cast<size_t>(i);
            if((n>0 && a<cues[n-1].start) || (n+1<cues.size() && a>cues[n+1].start)) return false;
            cues[n]={a,b,text};
        }
        return true;
    }
};
t_CKUINT subtitleOffset=0;
SubtitleState *state(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<SubtitleState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,subtitleOffset)));
}
double nanValue() { return std::numeric_limits<double>::quiet_NaN(); }
}
CK_DLL_CTOR(track_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold pointer");
    OBJ_MEMBER_UINT(SELF,subtitleOffset)=0;
    OBJ_MEMBER_UINT(SELF,subtitleOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) SubtitleState);
}
CK_DLL_DTOR(track_dtor) {
    delete state(SELF,API);
    OBJ_MEMBER_UINT(SELF,subtitleOffset)=0;
}
CK_DLL_MFUN(track_load) {
    RETURN->v_int=0;
    try { auto *p=state(SELF,API); auto path=GET_NEXT_STRING_SAFE(ARGS); if(p && p->load(path)) RETURN->v_int=1; } catch(...) {}
}
CK_DLL_MFUN(track_save) {
    RETURN->v_int=0;
    try { auto *p=state(SELF,API); auto path=GET_NEXT_STRING_SAFE(ARGS); if(p && p->save(path)) RETURN->v_int=1; } catch(...) {}
}
CK_DLL_MFUN(track_count) {
    auto *p=state(SELF,API); RETURN->v_int=p?static_cast<t_CKINT>(p->cues.size()):0;
}
CK_DLL_MFUN(track_start) {
    auto *p=state(SELF,API); auto i=GET_NEXT_INT(ARGS);
    RETURN->v_float=p && p->index(i)?p->cues[i].start/1000.0:-1;
}
CK_DLL_MFUN(track_end) {
    auto *p=state(SELF,API); auto i=GET_NEXT_INT(ARGS);
    RETURN->v_float=p && p->index(i)?p->cues[i].end/1000.0:-1;
}
CK_DLL_MFUN(track_text) {
    auto *p=state(SELF,API); auto i=GET_NEXT_INT(ARGS);
    RETURN->v_string=API->object->create_string(VM,p && p->index(i)?p->cues[i].text.c_str():"",FALSE);
}
CK_DLL_MFUN(track_add) {
    RETURN->v_int=0;
    try {
        auto *p=state(SELF,API);
        auto a=GET_NEXT_INT(ARGS),b=GET_NEXT_INT(ARGS);
        auto text=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->edit(0,a,b,text,true)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(track_set) {
    RETURN->v_int=0;
    try {
        auto *p=state(SELF,API);
        auto i=GET_NEXT_INT(ARGS),a=GET_NEXT_INT(ARGS),b=GET_NEXT_INT(ARGS);
        auto text=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->edit(i,a,b,text,false)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(track_remove) {
    auto *p=state(SELF,API); auto i=GET_NEXT_INT(ARGS);
    RETURN->v_int=0;
    if(p && p->index(i)) { p->cues.erase(p->cues.begin()+i); RETURN->v_int=1; }
}
CK_DLL_MFUN(track_reset) { if(auto *p=state(SELF,API)) p->reset(); }
CK_DLL_MFUN(track_clear) {
    if(auto *p=state(SELF,API)) { std::vector<subtitles::Cue>().swap(p->cues); p->reset(); }
}
CK_DLL_MFUN(track_get_offset) { auto *p=state(SELF,API); RETURN->v_float=p?p->offset:nanValue(); }
CK_DLL_MFUN(track_set_offset) {
    auto *p=state(SELF,API); auto v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=nanValue();
    if(p && std::isfinite(v) && std::abs(v)<=360000) RETURN->v_float=p->offset=v;
}
CK_DLL_MFUN(track_get_scale) { auto *p=state(SELF,API); RETURN->v_float=p?p->scale:nanValue(); }
CK_DLL_MFUN(track_set_scale) {
    auto *p=state(SELF,API); auto v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=nanValue();
    if(p && std::isfinite(v) && v>=0.001 && v<=1000) RETURN->v_float=p->scale=v;
}
CK_DLL_MFUN(track_active) {
    auto *p=state(SELF,API); auto t=GET_NEXT_FLOAT(ARGS);
    std::string result;
    try {
        if(p && std::isfinite(t) && t>=0 && t<=360000) {
            for(const auto &c:p->cues) {
                int64_t a,b;
                if(!p->transform(c,a,b)) { result.clear(); break; }
                if(t>=a/1000.0 && t<b/1000.0) {
                    if(result.size()+c.text.size()+1>1048576) { result.clear(); break; }
                    if(!result.empty()) result+='\n';
                    result+=c.text;
                }
            }
        }
    } catch(...) { result.clear(); }
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}

CK_DLL_QUERY(SubtitleIO) {
    QUERY->setname(QUERY,"SubtitleIO");
    QUERY->begin_class(QUERY,"SubtitleIO","Object");
    QUERY->add_sfun(QUERY,sub_count,"int","count");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_sfun(QUERY,sub_start,"float","startTime");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_sfun(QUERY,sub_end,"float","endTime");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_sfun(QUERY,sub_text,"string","text");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_sfun(QUERY,sub_convert,"int","convert");
    QUERY->add_arg(QUERY,"string","source");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_sfun(QUERY,sub_write,"int","writeCue");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"string","content");
    QUERY->add_arg(QUERY,"int","beginMs");
    QUERY->add_arg(QUERY,"int","endMs");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"SubtitleTrack","Object");
    subtitleOffset=QUERY->add_mvar(QUERY,"int","@subtitleTrackNative",FALSE);
    if(subtitleOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,track_ctor);
    QUERY->add_dtor(QUERY,track_dtor);
    QUERY->add_mfun(QUERY,track_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,track_save,"int","save");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,track_count,"int","count");
    QUERY->add_mfun(QUERY,track_start,"float","startTime");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,track_end,"float","endTime");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,track_text,"string","text");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,track_add,"int","add");
    QUERY->add_arg(QUERY,"int","beginMs");
    QUERY->add_arg(QUERY,"int","endMs");
    QUERY->add_arg(QUERY,"string","content");
    QUERY->add_mfun(QUERY,track_set,"int","set");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","beginMs");
    QUERY->add_arg(QUERY,"int","endMs");
    QUERY->add_arg(QUERY,"string","content");
    QUERY->add_mfun(QUERY,track_remove,"int","remove");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,track_reset,"void","reset");
    QUERY->add_mfun(QUERY,track_clear,"void","clear");
    QUERY->add_mfun(QUERY,track_get_offset,"float","offset");
    QUERY->add_mfun(QUERY,track_set_offset,"float","offset");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,track_get_scale,"float","timeScale");
    QUERY->add_mfun(QUERY,track_set_scale,"float","timeScale");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,track_active,"string","textAt");
    QUERY->add_arg(QUERY,"float","timeSeconds");
    return QUERY->end_class(QUERY);
}
