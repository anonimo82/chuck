#include "chuck_dl.h"
#include "keyframe.h"
#include <limits>
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
    return QUERY->end_class(QUERY);
}
