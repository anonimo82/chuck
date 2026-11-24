#include "chuck_dl.h"
#include "keyframe.h"
#include <limits>
#include <cstdint>
#include <new>
CK_DLL_SFUN(kf_valid) {
    RETURN->v_int=0;
    try { keyframes::Curve c; RETURN->v_int=keyframes::load(GET_NEXT_STRING_SAFE(ARGS),c); } catch(...) {}
}
CK_DLL_SFUN(kf_count) {
    RETURN->v_int=-1;
    try { keyframes::Curve c; if(keyframes::load(GET_NEXT_STRING_SAFE(ARGS),c)) RETURN->v_int=c.keys.size(); } catch(...) {}
}
CK_DLL_SFUN(kf_value) {
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    try {
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto t=GET_NEXT_FLOAT(ARGS);
        keyframes::Curve c;
        double v;
        if(keyframes::load(path,c)&&c.sample(t,v)) RETURN->v_float=v;
    } catch(...) {}
}
CK_DLL_SFUN(kf_interpolate) {
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    try {
        auto a=GET_NEXT_FLOAT(ARGS), b=GET_NEXT_FLOAT(ARGS), u=GET_NEXT_FLOAT(ARGS);
        auto name=GET_NEXT_STRING_SAFE(ARGS);
        keyframes::Mode m;
        double v;
        if(keyframes::mode(name,m)&&keyframes::interpolate(a,b,u,m,v)) RETURN->v_float=v;
    } catch(...) {}
}

namespace {
t_CKUINT curveOffset=0;
keyframes::Curve *curve(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<keyframes::Curve *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,curveOffset)));
}
}
CK_DLL_CTOR(curve_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold native pointer");
    OBJ_MEMBER_UINT(SELF,curveOffset)=0;
    auto *p=new(std::nothrow) keyframes::Curve;
    OBJ_MEMBER_UINT(SELF,curveOffset)=reinterpret_cast<uintptr_t>(p);
}
CK_DLL_DTOR(curve_dtor) {
    delete curve(SELF,API);
    OBJ_MEMBER_UINT(SELF,curveOffset)=0;
}
CK_DLL_MFUN(curve_load) {
    RETURN->v_int=0;
    try {
        auto *c=curve(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(c && keyframes::load(path,*c)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(curve_add) {
    RETURN->v_int=0;
    try {
        auto *c=curve(SELF,API);
        auto time=GET_NEXT_FLOAT(ARGS),value=GET_NEXT_FLOAT(ARGS);
        auto name=GET_NEXT_STRING_SAFE(ARGS);
        keyframes::Mode m;
        if(c && keyframes::mode(name,m) && c->append(time,value,m)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(curve_clear) {
    if(auto *c=curve(SELF,API)) c->keys.clear();
}
CK_DLL_MFUN(curve_count) {
    auto *c=curve(SELF,API);
    RETURN->v_int=c?static_cast<t_CKINT>(c->keys.size()):-1;
}
CK_DLL_MFUN(curve_value) {
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    auto *c=curve(SELF,API);
    double v;
    if(c && c->sample(GET_NEXT_FLOAT(ARGS),v)) RETURN->v_float=v;
}
CK_DLL_MFUN(curve_set) {
    RETURN->v_int=0;
    try {
        auto *c=curve(SELF,API);
        auto index=GET_NEXT_INT(ARGS);
        auto time=GET_NEXT_FLOAT(ARGS),value=GET_NEXT_FLOAT(ARGS);
        auto name=GET_NEXT_STRING_SAFE(ARGS);
        keyframes::Mode m;
        if(!c || index<0 || static_cast<size_t>(index)>=c->keys.size() ||
           !std::isfinite(time) || time<0 || time>1e12 || !std::isfinite(value) ||
           !keyframes::mode(name,m)) return;
        size_t i=static_cast<size_t>(index);
        if((i>0 && time<=c->keys[i-1].time) ||
           (i+1<c->keys.size() && time>=c->keys[i+1].time)) return;
        c->keys[i]={time,value,m};
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(curve_remove) {
    RETURN->v_int=0;
    auto *c=curve(SELF,API);
    auto i=GET_NEXT_INT(ARGS);
    if(c && i>=0 && static_cast<size_t>(i)<c->keys.size()) {
        c->keys.erase(c->keys.begin()+i);
        RETURN->v_int=1;
    }
}

CK_DLL_QUERY(KeyframeIO) {
    QUERY->setname(QUERY,"KeyframeIO");
    QUERY->begin_class(QUERY,"KeyframeIO","Object");
    QUERY->add_sfun(QUERY,kf_valid,"int","valid");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_sfun(QUERY,kf_count,"int","count");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_sfun(QUERY,kf_value,"float","valueAt");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"float","timeSeconds");
    QUERY->add_sfun(QUERY,kf_interpolate,"float","interpolate");
    QUERY->add_arg(QUERY,"float","fromValue");
    QUERY->add_arg(QUERY,"float","toValue");
    QUERY->add_arg(QUERY,"float","fraction");
    QUERY->add_arg(QUERY,"string","mode");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"KeyframeCurve","Object");
    curveOffset=QUERY->add_mvar(QUERY,"int","@curveNative",FALSE);
    if(curveOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,curve_ctor);
    QUERY->add_dtor(QUERY,curve_dtor);
    QUERY->add_mfun(QUERY,curve_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,curve_add,"int","add");
    QUERY->add_arg(QUERY,"float","timeSeconds");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_arg(QUERY,"string","mode");
    QUERY->add_mfun(QUERY,curve_clear,"void","clear");
    QUERY->add_mfun(QUERY,curve_count,"int","count");
    QUERY->add_mfun(QUERY,curve_value,"float","valueAt");
    QUERY->add_arg(QUERY,"float","timeSeconds");
    QUERY->add_mfun(QUERY,curve_set,"int","set");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","timeSeconds");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_arg(QUERY,"string","mode");
    QUERY->add_mfun(QUERY,curve_remove,"int","remove");
    QUERY->add_arg(QUERY,"int","index");
    return QUERY->end_class(QUERY);
}
