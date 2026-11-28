#include "chuck_dl.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>
#include <utility>

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

namespace {
struct Delta { double x=0,y=0; };
struct Deformation {
    size_t width=0,height=0;
    double ox=0,oy=0,sx=1,sy=1;
    std::vector<Delta> points;
};
t_CKUINT fieldOffset=0;
Deformation *field(Chuck_Object *self, CK_DL_API API) {
    return reinterpret_cast<Deformation *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,fieldOffset)));
}
bool pointIndex(const Deformation *f,t_CKINT x,t_CKINT y,size_t &i) {
    if(!f||x<0||y<0||static_cast<size_t>(x)>=f->width||static_cast<size_t>(y)>=f->height)return false;
    i=static_cast<size_t>(y)*f->width+static_cast<size_t>(x);return true;
}
Delta mixDelta(Delta a,Delta b,double u) {
    return {a.x*(1-u)+b.x*u,a.y*(1-u)+b.y*u};
}
bool sampleField(const Deformation *f,double x,double y,Delta &d) {
    if(!f||f->points.empty()||!geo_finite(x)||!geo_finite(y))return false;
    double u=(x-f->ox)/f->sx,v=(y-f->oy)/f->sy;
    if(!geo_finite(u)||!geo_finite(v))return false;
    if(u<0||v<0||u>f->width-1||v>f->height-1){d={};return true;}
    size_t a=static_cast<size_t>(std::floor(u)),b=static_cast<size_t>(std::floor(v));
    size_t c=a+1<f->width?a+1:a,e=b+1<f->height?b+1:b;
    d=mixDelta(mixDelta(f->points[b*f->width+a],f->points[b*f->width+c],u-a),
               mixDelta(f->points[e*f->width+a],f->points[e*f->width+c],u-a),v-b);
    return geo_finite(d.x)&&geo_finite(d.y);
}
// Operations act on the displacement grid, not directly on media.
bool brush(Deformation *f,int mode,double cx,double cy,double radius,double a,double b) {
    if(!f||f->points.empty()||!geo_finite(cx)||!geo_finite(cy)||!geo_finite(radius)||radius<=0||
       !geo_finite(a)||!geo_finite(b)||(mode>=3&&(a<0||a>1)))return false;
    try {
        auto next=f->points;
        for(size_t y=0;y<f->height;y++)for(size_t x=0;x<f->width;x++){
            size_t i=y*f->width+x;
            double dx=f->ox+x*f->sx-cx,dy=f->oy+y*f->sy-cy;
            double distance=std::hypot(dx,dy);
            if(distance>=radius)continue;
            double w=1-distance/radius;
            Delta d=next[i];
            if(mode==0){d.x+=a*w;d.y+=b*w;}
            else if(mode==1){d.x+=dx*a*w;d.y+=dy*a*w;}
            else if(mode==2){
                double c=std::cos(a*w),s=std::sin(a*w);
                d.x+=(c-1)*dx-s*dy;d.y+=s*dx+(c-1)*dy;
            }else if(mode==3){
                // Smooth using the original grid, including the center.
                Delta mean{};int n=0;
                for(int oy=-1;oy<=1;oy++)for(int ox=-1;ox<=1;ox++){
                    size_t j;
                    if(pointIndex(f,static_cast<t_CKINT>(x)+ox,static_cast<t_CKINT>(y)+oy,j)) {
                        mean.x+=f->points[j].x/9;mean.y+=f->points[j].y/9;n++;
                    }
                }
                mean.x*=9.0/n;mean.y*=9.0/n;
                d=mixDelta(d,mean,a*w);
            }else{d.x*=1-a*w;d.y*=1-a*w;}
            if(!geo_finite(d.x)||!geo_finite(d.y))return false;
            next[i]=d;
        }
        f->points.swap(next);return true;
    }catch(...){return false;}
}
}
CK_DLL_CTOR(field_ctor) {
    OBJ_MEMBER_UINT(SELF,fieldOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) Deformation);
}
CK_DLL_DTOR(field_dtor) {
    delete field(SELF,API);OBJ_MEMBER_UINT(SELF,fieldOffset)=0;
}
CK_DLL_MFUN(field_configure) {
    RETURN->v_int=0;auto *f=field(SELF,API);
    auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
    auto ox=GET_NEXT_FLOAT(ARGS),oy=GET_NEXT_FLOAT(ARGS),sx=GET_NEXT_FLOAT(ARGS),sy=GET_NEXT_FLOAT(ARGS);
    if(!f||w<2||h<2||w>1024||h>1024||!geo_finite(ox)||!geo_finite(oy)||
       !geo_finite(sx)||!geo_finite(sy)||sx<=0||sy<=0||
       !geo_finite(ox+(w-1)*sx)||!geo_finite(oy+(h-1)*sy))return;
    try {
        Deformation next;
        next.width=w;next.height=h;next.ox=ox;next.oy=oy;next.sx=sx;next.sy=sy;
        next.points.resize(static_cast<size_t>(w)*static_cast<size_t>(h));
        *f=std::move(next);RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_MFUN(field_width) {auto *f=field(SELF,API);RETURN->v_int=f?f->width:0;}
CK_DLL_MFUN(field_height) {auto *f=field(SELF,API);RETURN->v_int=f?f->height:0;}
CK_DLL_MFUN(field_set) {
    RETURN->v_int=0;auto *f=field(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS);
    auto dx=GET_NEXT_FLOAT(ARGS),dy=GET_NEXT_FLOAT(ARGS);size_t i;
    if(pointIndex(f,x,y,i)&&geo_finite(dx)&&geo_finite(dy)){f->points[i]={dx,dy};RETURN->v_int=1;}
}
CK_DLL_MFUN(field_point_x) {
    RETURN->v_float=invalid();auto *f=field(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS);size_t i;
    if(pointIndex(f,x,y,i))RETURN->v_float=f->points[i].x;
}
CK_DLL_MFUN(field_point_y) {
    RETURN->v_float=invalid();auto *f=field(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS);size_t i;
    if(pointIndex(f,x,y,i))RETURN->v_float=f->points[i].y;
}
CK_DLL_MFUN(field_sample_x) {
    RETURN->v_float=invalid();auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);Delta d;
    if(sampleField(field(SELF,API),x,y,d))RETURN->v_float=d.x;
}
CK_DLL_MFUN(field_sample_y) {
    RETURN->v_float=invalid();auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);Delta d;
    if(sampleField(field(SELF,API),x,y,d))RETURN->v_float=d.y;
}
CK_DLL_MFUN(field_clear) {
    if(auto *f=field(SELF,API))for(auto &p:f->points)p={};
}
CK_DLL_MFUN(field_move) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),r=GET_NEXT_FLOAT(ARGS);
    auto dx=GET_NEXT_FLOAT(ARGS),dy=GET_NEXT_FLOAT(ARGS);
    RETURN->v_int=brush(field(SELF,API),0,x,y,r,dx,dy);
}
CK_DLL_MFUN(field_radial) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),r=GET_NEXT_FLOAT(ARGS),s=GET_NEXT_FLOAT(ARGS);
    RETURN->v_int=brush(field(SELF,API),1,x,y,r,s,0);
}
CK_DLL_MFUN(field_twist) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),r=GET_NEXT_FLOAT(ARGS),s=GET_NEXT_FLOAT(ARGS);
    RETURN->v_int=brush(field(SELF,API),2,x,y,r,s,0);
}
CK_DLL_MFUN(field_smooth) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),r=GET_NEXT_FLOAT(ARGS),s=GET_NEXT_FLOAT(ARGS);
    RETURN->v_int=brush(field(SELF,API),3,x,y,r,s,0);
}
CK_DLL_MFUN(field_erase) {
    auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),r=GET_NEXT_FLOAT(ARGS),s=GET_NEXT_FLOAT(ARGS);
    RETURN->v_int=brush(field(SELF,API),4,x,y,r,s,0);
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
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"MediaDeformation2D","Object");
    fieldOffset=QUERY->add_mvar(QUERY,"int","@deformationNative",FALSE);
    if(fieldOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,field_ctor);QUERY->add_dtor(QUERY,field_dtor);
    QUERY->add_mfun(QUERY,field_configure,"int","configure");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float","originX");
    QUERY->add_arg(QUERY,"float","originY");
    QUERY->add_arg(QUERY,"float","spacingX");
    QUERY->add_arg(QUERY,"float","spacingY");
    QUERY->add_mfun(QUERY,field_width,"int","width");
    QUERY->add_mfun(QUERY,field_height,"int","height");
    QUERY->add_mfun(QUERY,field_set,"int","setPoint");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"float","dx");
    QUERY->add_arg(QUERY,"float","dy");
    QUERY->add_mfun(QUERY,field_point_x,"float","pointX");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_mfun(QUERY,field_point_y,"float","pointY");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_mfun(QUERY,field_sample_x,"float","sampleX");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,field_sample_y,"float","sampleY");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,field_clear,"void","clear");
    QUERY->add_mfun(QUERY,field_move,"int","move");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","radius");
    QUERY->add_arg(QUERY,"float","dx");
    QUERY->add_arg(QUERY,"float","dy");
    QUERY->add_mfun(QUERY,field_radial,"int","radial");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","radius");
    QUERY->add_arg(QUERY,"float","strength");
    QUERY->add_mfun(QUERY,field_twist,"int","twist");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","radius");
    QUERY->add_arg(QUERY,"float","radians");
    QUERY->add_mfun(QUERY,field_smooth,"int","smooth");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","radius");
    QUERY->add_arg(QUERY,"float","strength");
    QUERY->add_mfun(QUERY,field_erase,"int","erase");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","radius");
    QUERY->add_arg(QUERY,"float","strength");
    return QUERY->end_class(QUERY);
}
