#include "chuck_dl.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace {
struct Guide { t_CKINT axis; double position; bool relative; };
struct Geometry {
    std::vector<Guide> guides;
    double sx=1, sy=1, ox=0, oy=0;
};
t_CKUINT offset=0;
Geometry *state(Chuck_Object *self, CK_DL_API API) {
    return reinterpret_cast<Geometry *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,offset)));
}
double invalid() { return std::numeric_limits<double>::quiet_NaN(); }
bool geo_finite(double x) { return std::isfinite(x); }
double checked(double x) { return geo_finite(x)?x:invalid(); }
}
CK_DLL_CTOR(geo_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"native pointer must fit");
    OBJ_MEMBER_UINT(SELF,offset)=reinterpret_cast<uintptr_t>(new(std::nothrow) Geometry);
}
CK_DLL_DTOR(geo_dtor) {
    delete state(SELF,API);
    OBJ_MEMBER_UINT(SELF,offset)=0;
}
CK_DLL_MFUN(geo_add) {
    RETURN->v_int=0;
    auto *s=state(SELF,API);
    auto axis=GET_NEXT_INT(ARGS);
    auto p=GET_NEXT_FLOAT(ARGS);
    auto relative=GET_NEXT_INT(ARGS);
    if(!s || (axis!=0 && axis!=1) || !geo_finite(p) || (relative!=0 && relative!=1) ||
       (relative && (p<0 || p>100)) || s->guides.size()>=100000) return;
    try { s->guides.push_back({axis,p,relative!=0}); RETURN->v_int=1; } catch(...) {}
}
CK_DLL_MFUN(geo_count) {
    auto *s=state(SELF,API);
    RETURN->v_int=s?static_cast<t_CKINT>(s->guides.size()):-1;
}
CK_DLL_MFUN(geo_axis) {
    RETURN->v_int=-1;
    auto *s=state(SELF,API); auto i=GET_NEXT_INT(ARGS);
    if(s && i>=0 && static_cast<size_t>(i)<s->guides.size()) RETURN->v_int=s->guides[i].axis;
}
CK_DLL_MFUN(geo_position) {
    RETURN->v_float=invalid();
    auto *s=state(SELF,API); auto i=GET_NEXT_INT(ARGS);
    auto extent=GET_NEXT_FLOAT(ARGS);
    if(!s || i<0 || static_cast<size_t>(i)>=s->guides.size() || !geo_finite(extent) || extent<0) return;
    const auto &g=s->guides[i];
    RETURN->v_float=checked(g.relative?g.position/100.0*extent:g.position);
}
CK_DLL_MFUN(geo_remove) {
    RETURN->v_int=0;
    auto *s=state(SELF,API); auto i=GET_NEXT_INT(ARGS);
    if(s && i>=0 && static_cast<size_t>(i)<s->guides.size()) {
        s->guides.erase(s->guides.begin()+i); RETURN->v_int=1;
    }
}
CK_DLL_MFUN(geo_clear) { if(auto *s=state(SELF,API)) s->guides.clear(); }
CK_DLL_MFUN(geo_grid) {
    RETURN->v_int=0;
    auto *s=state(SELF,API);
    auto sx=GET_NEXT_FLOAT(ARGS),sy=GET_NEXT_FLOAT(ARGS),ox=GET_NEXT_FLOAT(ARGS),oy=GET_NEXT_FLOAT(ARGS);
    if(!s || !geo_finite(sx)||!geo_finite(sy)||!geo_finite(ox)||!geo_finite(oy)||sx<=0||sy<=0) return;
    s->sx=sx;s->sy=sy;s->ox=ox;s->oy=oy;RETURN->v_int=1;
}
CK_DLL_MFUN(geo_snap) {
    RETURN->v_float=invalid();
    auto *s=state(SELF,API);auto axis=GET_NEXT_INT(ARGS);auto p=GET_NEXT_FLOAT(ARGS);
    if(!s || (axis!=0 && axis!=1) || !geo_finite(p)) return;
    double step=axis==0?s->sx:s->sy, origin=axis==0?s->ox:s->oy;
    RETURN->v_float=checked(origin+std::round((p-origin)/step)*step);
}
CK_DLL_SFUN(geo_align) {
    RETURN->v_float=invalid();
    auto target=GET_NEXT_FLOAT(ARGS),size=GET_NEXT_FLOAT(ARGS),anchor=GET_NEXT_FLOAT(ARGS);
    if(geo_finite(target)&&geo_finite(size)&&geo_finite(anchor)&&size>=0&&anchor>=0&&anchor<=1)
        RETURN->v_float=checked(target-size*anchor);
}
CK_DLL_SFUN(geo_distribute) {
    RETURN->v_float=invalid();
    auto first=GET_NEXT_FLOAT(ARGS),last=GET_NEXT_FLOAT(ARGS),fraction=GET_NEXT_FLOAT(ARGS);
    if(geo_finite(first)&&geo_finite(last)&&geo_finite(fraction)&&fraction>=0&&fraction<=1)
        RETURN->v_float=checked(first*(1-fraction)+last*fraction);
}
CK_DLL_QUERY(MediaGeometry) {
    QUERY->setname(QUERY,"MediaGeometry");
    QUERY->begin_class(QUERY,"MediaGeometry","Object");
    offset=QUERY->add_mvar(QUERY,"int","@geometryNative",FALSE);
    if(offset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,geo_ctor); QUERY->add_dtor(QUERY,geo_dtor);
    QUERY->add_mfun(QUERY,geo_add,"int","addGuide");
    QUERY->add_arg(QUERY,"int","axis");QUERY->add_arg(QUERY,"float","position");QUERY->add_arg(QUERY,"int","relative");
    QUERY->add_mfun(QUERY,geo_count,"int","guideCount");
    QUERY->add_mfun(QUERY,geo_axis,"int","guideAxis");QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,geo_position,"float","guidePosition");QUERY->add_arg(QUERY,"int","index");QUERY->add_arg(QUERY,"float","extent");
    QUERY->add_mfun(QUERY,geo_remove,"int","removeGuide");QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,geo_clear,"void","clearGuides");
    QUERY->add_mfun(QUERY,geo_grid,"int","grid");
    QUERY->add_arg(QUERY,"float","spacingX");QUERY->add_arg(QUERY,"float","spacingY");
    QUERY->add_arg(QUERY,"float","offsetX");QUERY->add_arg(QUERY,"float","offsetY");
    QUERY->add_mfun(QUERY,geo_snap,"float","snap");QUERY->add_arg(QUERY,"int","axis");QUERY->add_arg(QUERY,"float","position");
    QUERY->add_sfun(QUERY,geo_align,"float","align");QUERY->add_arg(QUERY,"float","target");
    QUERY->add_arg(QUERY,"float","size");QUERY->add_arg(QUERY,"float","anchor");
    QUERY->add_sfun(QUERY,geo_distribute,"float","distribute");QUERY->add_arg(QUERY,"float","first");
    QUERY->add_arg(QUERY,"float","last");QUERY->add_arg(QUERY,"float","fraction");
    return QUERY->end_class(QUERY);
}
