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
namespace {
// Row-major homogeneous 2D matrix, applied to a column vector.
struct Transform { double m[9]={1,0,0,0,1,0,0,0,1}; };
t_CKUINT transformOffset=0;
Transform *transform(Chuck_Object *self, CK_DL_API API) {
    return reinterpret_cast<Transform *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,transformOffset)));
}
bool multiply(const Transform &a,const Transform &b,Transform &out) {
    Transform next;
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) {
        double v=0;
        for(int k=0;k<3;k++) v+=a.m[i*3+k]*b.m[k*3+j];
        if(!geo_finite(v)) return false;
        next.m[i*3+j]=v;
    }
    out=next;return true;
}
bool append(Transform *t,const Transform &op) {
    return t && multiply(op,*t,*t);
}
bool mapPoint(const Transform &t,double x,double y,double &px,double &py) {
    if(!geo_finite(x)||!geo_finite(y)) return false;
    double w=t.m[6]*x+t.m[7]*y+t.m[8];
    if(!geo_finite(w)||w==0) return false;
    px=(t.m[0]*x+t.m[1]*y+t.m[2])/w;
    py=(t.m[3]*x+t.m[4]*y+t.m[5])/w;
    return geo_finite(px)&&geo_finite(py);
}
}
CK_DLL_CTOR(transform_ctor) {
    OBJ_MEMBER_UINT(SELF,transformOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) Transform);
}
CK_DLL_DTOR(transform_dtor) {
    delete transform(SELF,API);
    OBJ_MEMBER_UINT(SELF,transformOffset)=0;
}
CK_DLL_MFUN(transform_reset) { if(auto *t=transform(SELF,API)) *t=Transform{}; }
CK_DLL_MFUN(transform_set) {
    RETURN->v_int=0; auto *t=transform(SELF,API);Transform next;
    for(double &v:next.m) { v=GET_NEXT_FLOAT(ARGS);if(!geo_finite(v)) return; }
    if(!t || (next.m[6]==0 && next.m[7]==0 && next.m[8]==0)) return;
    *t=next;RETURN->v_int=1;
}
CK_DLL_MFUN(transform_at) {
    RETURN->v_float=invalid(); auto *t=transform(SELF,API);auto i=GET_NEXT_INT(ARGS);
    if(t && i>=0 && i<9) RETURN->v_float=t->m[i];
}
CK_DLL_MFUN(transform_translate) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);
    Transform op{{1,0,x,0,1,y,0,0,1}};
    RETURN->v_int=append(transform(SELF,API),op);
}
CK_DLL_MFUN(transform_scale) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);
    Transform op{{x,0,0,0,y,0,0,0,1}};
    RETURN->v_int=append(transform(SELF,API),op);
}
CK_DLL_MFUN(transform_rotate) {
    auto a=GET_NEXT_FLOAT(ARGS);
    double c=std::cos(a),s=std::sin(a);
    Transform op{{c,-s,0,s,c,0,0,0,1}};
    RETURN->v_int=append(transform(SELF,API),op);
}
CK_DLL_MFUN(transform_shear) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);
    Transform op{{1,x,0,y,1,0,0,0,1}};
    RETURN->v_int=append(transform(SELF,API),op);
}
CK_DLL_MFUN(transform_x) {
    RETURN->v_float=invalid();auto *t=transform(SELF,API);
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);double px,py;
    if(t&&mapPoint(*t,x,y,px,py)) RETURN->v_float=px;
}
CK_DLL_MFUN(transform_y) {
    RETURN->v_float=invalid();auto *t=transform(SELF,API);
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);double px,py;
    if(t&&mapPoint(*t,x,y,px,py)) RETURN->v_float=py;
}
CK_DLL_MFUN(transform_invert) {
    RETURN->v_int=0;auto *t=transform(SELF,API);if(!t)return;
    const auto &m=t->m;
    Transform adj{{m[4]*m[8]-m[5]*m[7],m[2]*m[7]-m[1]*m[8],m[1]*m[5]-m[2]*m[4],
                   m[5]*m[6]-m[3]*m[8],m[0]*m[8]-m[2]*m[6],m[2]*m[3]-m[0]*m[5],
                   m[3]*m[7]-m[4]*m[6],m[1]*m[6]-m[0]*m[7],m[0]*m[4]-m[1]*m[3]}};
    double det=m[0]*adj.m[0]+m[1]*adj.m[3]+m[2]*adj.m[6];
    if(!geo_finite(det)||det==0)return;
    for(double &v:adj.m){v/=det;if(!geo_finite(v))return;}
    *t=adj;RETURN->v_int=1;
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
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaTransform2D","Object");
    transformOffset=QUERY->add_mvar(QUERY,"int","@transformNative",FALSE);
    if(transformOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,transform_ctor);QUERY->add_dtor(QUERY,transform_dtor);
    QUERY->add_mfun(QUERY,transform_reset,"void","reset");
    QUERY->add_mfun(QUERY,transform_set,"int","setMatrix");
    QUERY->add_arg(QUERY,"float","m0");
    QUERY->add_arg(QUERY,"float","m1");
    QUERY->add_arg(QUERY,"float","m2");
    QUERY->add_arg(QUERY,"float","m3");
    QUERY->add_arg(QUERY,"float","m4");
    QUERY->add_arg(QUERY,"float","m5");
    QUERY->add_arg(QUERY,"float","m6");
    QUERY->add_arg(QUERY,"float","m7");
    QUERY->add_arg(QUERY,"float","m8");
    QUERY->add_mfun(QUERY,transform_at,"float","matrixAt");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,transform_translate,"int","translate");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,transform_scale,"int","scale");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,transform_rotate,"int","rotate");
    QUERY->add_arg(QUERY,"float","radians");
    QUERY->add_mfun(QUERY,transform_shear,"int","shear");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,transform_x,"float","mapX");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,transform_y,"float","mapY");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,transform_invert,"int","invert");
    return QUERY->end_class(QUERY);
}
