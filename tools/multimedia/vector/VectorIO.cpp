// Optional SVG I/O using librsvg and Cairo; synchronous, offline operations.
#include "chuck_dl.h"
#include "../file_compat.h"
#include <librsvg/rsvg.h>
#include <cairo.h>
#include <cstdio>
#include <memory>
#include <string>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <fstream>
#include <sstream>
#include <locale>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cstring>

namespace {
using Handle = std::unique_ptr<RsvgHandle, decltype(&g_object_unref)>;
Handle load(const std::string &path) {
    // Load local SVG bytes without a base URI for external relative resources.
    gchar *data = nullptr;
    gsize size = 0;
    if(!g_file_get_contents(path.c_str(), &data, &size, nullptr))
        return Handle(nullptr, g_object_unref);
    std::unique_ptr<gchar, decltype(&g_free)> bytes(data, g_free);
    return Handle(rsvg_handle_new_from_data(
        reinterpret_cast<const guint8 *>(data), size, nullptr), g_object_unref);
}
bool writeNew(const std::string &path, const unsigned char *bytes, size_t size) {
    FILE *f = mediaio::openNew(path.c_str());
    if(!f) return false;
    bool ok = std::fwrite(bytes, 1, size, f) == size;
    if(std::fclose(f) != 0) ok = false;
    if(!ok) std::remove(path.c_str());
    return ok;
}
cairo_status_t writePng(void *closure, const unsigned char *data, unsigned int size) {
    return std::fwrite(data, 1, size, static_cast<FILE *>(closure)) == size
        ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}
}
CK_DLL_SFUN(vector_valid) {
    RETURN->v_int = 0;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        RETURN->v_int = load(path) ? 1 : 0;
    } catch(...) {}
}
CK_DLL_SFUN(vector_save) {
    RETURN->v_int = 0;
    try {
        auto svg = GET_NEXT_STRING_SAFE(ARGS);
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        Handle h(rsvg_handle_new_from_data(
            reinterpret_cast<const guint8 *>(svg.data()), svg.size(), nullptr), g_object_unref);
        if(h && writeNew(path, reinterpret_cast<const unsigned char *>(svg.data()), svg.size()))
            RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(vector_render) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto width = GET_NEXT_INT(ARGS);
        auto height = GET_NEXT_INT(ARGS);
        if(width <= 0 || height <= 0 || width > 4096 || height > 4096) return;
        auto h = load(source);
        if(!h) return;
        std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface(
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, static_cast<int>(width),
            static_cast<int>(height)), cairo_surface_destroy);
        if(cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS) return;
        std::unique_ptr<cairo_t, decltype(&cairo_destroy)> cr(
            cairo_create(surface.get()), cairo_destroy);
        RsvgRectangle viewport = {0, 0, static_cast<double>(width), static_cast<double>(height)};
        if(!rsvg_handle_render_document(h.get(), cr.get(), &viewport, nullptr)
           || cairo_status(cr.get()) != CAIRO_STATUS_SUCCESS) return;
        FILE *f = mediaio::openNew(dest.c_str());
        if(!f) return;
        bool ok = cairo_surface_write_to_png_stream(surface.get(), writePng, f)
                  == CAIRO_STATUS_SUCCESS;
        if(std::fclose(f) != 0) ok = false;
        if(!ok) { std::remove(dest.c_str()); return; }
        RETURN->v_int = 1;
    } catch(...) {}
}

namespace {
struct VectorImageState {
    Handle handle{nullptr,g_object_unref};
    std::string markup;
    int canvasW=0,canvasH=0;
    std::vector<std::string> shapes;
    std::array<double,6> controls{{0,0,1,1,0,1}};
    void reset() { controls={{0,0,1,1,0,1}}; }
    bool setSvg(const std::string &svg) {
        if(svg.empty() || svg.size()>16*1024*1024) return false;
        Handle candidate(rsvg_handle_new_from_data(
            reinterpret_cast<const guint8 *>(svg.data()),svg.size(),nullptr),g_object_unref);
        if(!candidate) return false;
        std::string copy=svg;
        handle=std::move(candidate);
        markup=std::move(copy);
        canvasW=canvasH=0; shapes.clear();
        reset();
        return true;
    }
    bool loadFile(const std::string &path) {
        std::ifstream in(path,std::ios::binary);
        if(!in) return false;
        in.seekg(0,std::ios::end);
        auto n=in.tellg();
        if(n<=0 || n>16*1024*1024) return false;
        in.seekg(0,std::ios::beg);
        std::string svg(static_cast<size_t>(n),'\0');
        if(!in.read(svg.data(),static_cast<std::streamsize>(n))) return false;
        return setSvg(svg);
    }
    static std::string document(int w,int h,const std::vector<std::string> &items) {
        std::string svg="<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""+std::to_string(w)+
            "\" height=\""+std::to_string(h)+"\" viewBox=\"0 0 "+std::to_string(w)+" "+std::to_string(h)+"\">\n";
        for(const auto &item:items) {
            if(item.size()>16*1024*1024-svg.size()-16) return {};
            svg+=item; svg+='\n';
        }
        svg+="</svg>\n"; return svg;
    }
    bool replaceShapes(int w,int h,std::vector<std::string> next) {
        size_t nodes=0;
        for(const auto &item:next) {
            int depth=0;
            for(size_t i=0;i<item.size();++i) {
                if(item[i]=='<') {
                    if(i+1<item.size()&&item[i+1]=='/') --depth;
                    else {
                        if(++nodes>4096) return false;
                        if(item.compare(i,3,"<g>")==0&&++depth>16) return false;
                    }
                }
            }
            if(depth!=0) return false;
        }
        auto svg=document(w,h,next);
        if(svg.empty()) return false;
        Handle candidate(rsvg_handle_new_from_data(reinterpret_cast<const guint8 *>(svg.data()),svg.size(),nullptr),g_object_unref);
        if(!candidate) return false;
        handle=std::move(candidate); markup=std::move(svg);
        canvasW=w; canvasH=h; shapes=std::move(next);
        return true;
    }
    bool addShape(std::string shape) {
        if(!canvasW || shapes.size()>=4096) return false;
        auto next=shapes; next.push_back(std::move(shape));
        return replaceShapes(canvasW,canvasH,std::move(next));
    }
    bool removeShape(t_CKINT index) {
        if(!canvasW || index<0 || static_cast<uint64_t>(index)>=shapes.size()) return false;
        auto next=shapes; next.erase(next.begin()+index);
        return replaceShapes(canvasW,canvasH,std::move(next));
    }
    using Surface=std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)>;
    Surface rasterize(int w,int h) {
        if(!handle || w<1 || h<1 || w>4096 || h>4096) return Surface(nullptr,cairo_surface_destroy);
        std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)> surface(
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h),cairo_surface_destroy);
        if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return Surface(nullptr,cairo_surface_destroy);
        std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
        if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return Surface(nullptr,cairo_surface_destroy);
        cairo_translate(cr.get(),controls[0]+w/2.0,controls[1]+h/2.0);
        cairo_rotate(cr.get(),std::remainder(controls[4],360.0)*3.14159265358979323846/180.0);
        cairo_scale(cr.get(),controls[2],controls[3]);
        cairo_translate(cr.get(),-w/2.0,-h/2.0);
        cairo_push_group(cr.get());
        RsvgRectangle viewport={0,0,static_cast<double>(w),static_cast<double>(h)};
        bool rendered=rsvg_handle_render_document(handle.get(),cr.get(),&viewport,nullptr);
        cairo_pop_group_to_source(cr.get());
        cairo_paint_with_alpha(cr.get(),controls[5]);
        if(!rendered || cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return Surface(nullptr,cairo_surface_destroy);
        cairo_surface_flush(surface.get());
        return surface;
    }
    bool render(const std::string &path,int w,int h) {
        auto surface=rasterize(w,h);
        if(!surface) return false;
        FILE *out=mediaio::openNew(path.c_str());
        if(!out) return false;
        bool ok=cairo_surface_write_to_png_stream(surface.get(),writePng,out)==CAIRO_STATUS_SUCCESS;
        if(std::fclose(out)!=0) ok=false;
        if(!ok) std::remove(path.c_str());
        return ok;
    }
};
t_CKUINT vectorImageOffset=0;
VectorImageState *vectorState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<VectorImageState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,vectorImageOffset)));
}
double vectorGet(VectorImageState *p,size_t i) {
    return p?p->controls[i]:std::numeric_limits<double>::quiet_NaN();
}
double vectorSet(VectorImageState *p,size_t i,double v) {
    if(!p || !std::isfinite(v) ||
       (i<2 && std::abs(v)>32768) ||
       ((i==2 || i==3) && (v<0.001 || v>64)) ||
       (i==4 && std::abs(v)>1e9) ||
       (i==5 && (v<0 || v>1))) return std::numeric_limits<double>::quiet_NaN();
    return p->controls[i]=v;
}
}
CK_DLL_CTOR(vector_image_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold native pointer");
    OBJ_MEMBER_UINT(SELF,vectorImageOffset)=0;
    auto *p=new(std::nothrow) VectorImageState;
    OBJ_MEMBER_UINT(SELF,vectorImageOffset)=reinterpret_cast<uintptr_t>(p);
}
CK_DLL_DTOR(vector_image_dtor) {
    delete vectorState(SELF,API);
    OBJ_MEMBER_UINT(SELF,vectorImageOffset)=0;
}
CK_DLL_MFUN(vector_image_load) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->loadFile(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_set_svg) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto svg=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->setSvg(svg)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_loaded) {
    auto *p=vectorState(SELF,API);
    RETURN->v_int=p && p->handle;
}
CK_DLL_MFUN(vector_image_render) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        if(p && w>=1 && w<=4096 && h>=1 && h<=4096 &&
           p->render(path,static_cast<int>(w),static_cast<int>(h))) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_reset) {
    if(auto *p=vectorState(SELF,API)) p->reset();
}
CK_DLL_MFUN(vector_image_clear) {
    if(auto *p=vectorState(SELF,API)) { p->handle.reset(); p->markup.clear(); p->shapes.clear(); p->canvasW=p->canvasH=0; p->reset(); }
}
CK_DLL_MFUN(vector_image_get_x) {
    RETURN->v_float=vectorGet(vectorState(SELF,API),0);
}
CK_DLL_MFUN(vector_image_set_x) {
    RETURN->v_float=vectorSet(vectorState(SELF,API),0,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(vector_image_get_y) {
    RETURN->v_float=vectorGet(vectorState(SELF,API),1);
}
CK_DLL_MFUN(vector_image_set_y) {
    RETURN->v_float=vectorSet(vectorState(SELF,API),1,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(vector_image_get_scaleX) {
    RETURN->v_float=vectorGet(vectorState(SELF,API),2);
}
CK_DLL_MFUN(vector_image_set_scaleX) {
    RETURN->v_float=vectorSet(vectorState(SELF,API),2,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(vector_image_get_scaleY) {
    RETURN->v_float=vectorGet(vectorState(SELF,API),3);
}
CK_DLL_MFUN(vector_image_set_scaleY) {
    RETURN->v_float=vectorSet(vectorState(SELF,API),3,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(vector_image_get_rotation) {
    RETURN->v_float=vectorGet(vectorState(SELF,API),4);
}
CK_DLL_MFUN(vector_image_set_rotation) {
    RETURN->v_float=vectorSet(vectorState(SELF,API),4,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(vector_image_get_opacity) {
    RETURN->v_float=vectorGet(vectorState(SELF,API),5);
}
CK_DLL_MFUN(vector_image_set_opacity) {
    RETURN->v_float=vectorSet(vectorState(SELF,API),5,GET_NEXT_FLOAT(ARGS));
}


namespace {
bool coord(double v) { return std::isfinite(v) && std::abs(v)<=1000000; }
bool color(t_CKINT rgb,double alpha) {
    return rgb>=0 && rgb<=0xffffff && std::isfinite(alpha) && alpha>=0 && alpha<=1;
}
std::ostringstream svgStream() {
    std::ostringstream o; o.imbue(std::locale::classic()); o<<std::setprecision(17); return o;
}
std::string style(t_CKINT rgb,double alpha,bool stroke) {
    auto o=svgStream();
    o<<(stroke?" stroke=\"#":" fill=\"#")<<std::hex<<std::setw(6)<<std::setfill('0')<<rgb
     <<"\""<<(stroke?" stroke-opacity=\"":" fill-opacity=\"")<<std::dec<<alpha<<"\"";
    return o.str();
}
}
CK_DLL_MFUN(vector_image_create) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        if(p && w>=1 && w<=4096 && h>=1 && h<=4096 && p->replaceShapes(w,h,{})) {
            p->reset(); RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_rect) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),w=GET_NEXT_FLOAT(ARGS),h=GET_NEXT_FLOAT(ARGS);
        auto rgb=GET_NEXT_INT(ARGS); auto alpha=GET_NEXT_FLOAT(ARGS);
        if(!p || !coord(x)||!coord(y)||!coord(w)||!coord(h)||w<=0||h<=0||!color(rgb,alpha)) return;
        auto o=svgStream();
        o<<"<rect x=\""<<x<<"\" y=\""<<y<<"\" width=\""<<w<<"\" height=\""<<h<<"\""<<style(rgb,alpha,false)<<"/>";
        RETURN->v_int=p->addShape(o.str());
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_circle) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),r=GET_NEXT_FLOAT(ARGS);
        auto rgb=GET_NEXT_INT(ARGS); auto alpha=GET_NEXT_FLOAT(ARGS);
        if(!p || !coord(x)||!coord(y)||!coord(r)||r<=0||!color(rgb,alpha)) return;
        auto o=svgStream();
        o<<"<circle cx=\""<<x<<"\" cy=\""<<y<<"\" r=\""<<r<<"\""<<style(rgb,alpha,false)<<"/>";
        RETURN->v_int=p->addShape(o.str());
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_line) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS),x2=GET_NEXT_FLOAT(ARGS),y2=GET_NEXT_FLOAT(ARGS),w=GET_NEXT_FLOAT(ARGS);
        auto rgb=GET_NEXT_INT(ARGS); auto alpha=GET_NEXT_FLOAT(ARGS);
        if(!p || !coord(x)||!coord(y)||!coord(x2)||!coord(y2)||!coord(w)||w<=0||!color(rgb,alpha)) return;
        auto o=svgStream();
        o<<"<line x1=\""<<x<<"\" y1=\""<<y<<"\" x2=\""<<x2<<"\" y2=\""<<y2<<"\" stroke-width=\""<<w<<"\""<<style(rgb,alpha,true)<<"/>";
        RETURN->v_int=p->addShape(o.str());
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_polygon) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto rgb=GET_NEXT_INT(ARGS); auto alpha=GET_NEXT_FLOAT(ARGS);
        if(!p || !a || !color(rgb,alpha)) return;
        auto n=API->object->array_float_size(a);
        if(n<6 || n>8192 || n%2) return;
        auto o=svgStream(); o<<"<polygon points=\"";
        for(t_CKINT i=0;i<n;i+=2) {
            double x=API->object->array_float_get_idx(a,i),y=API->object->array_float_get_idx(a,i+1);
            if(!coord(x)||!coord(y)) return;
            o<<x<<','<<y<<' ';
        }
        o<<"\""<<style(rgb,alpha,false)<<"/>";
        RETURN->v_int=p->addShape(o.str());
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_count) {
    auto *p=vectorState(SELF,API);
    RETURN->v_int=p && p->canvasW?static_cast<t_CKINT>(p->shapes.size()):-1;
}
CK_DLL_MFUN(vector_image_remove) {
    RETURN->v_int=0;
    try { auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS); if(p) RETURN->v_int=p->removeShape(i); } catch(...) {}
}
namespace {
// These helpers operate only on markup generated by VectorImage itself.
const std::string *shapeAt(VectorImageState *p,t_CKINT i) {
    return p&&p->canvasW&&i>=0&&static_cast<uint64_t>(i)<p->shapes.size()?&p->shapes[i]:nullptr;
}
std::string shapeTag(const std::string &s) {
    auto end=s.find_first_of(" >");
    return end==std::string::npos?"":s.substr(1,end-1);
}
bool shapeAttribute(const std::string &s,const std::string &name,std::string &value) {
    auto key=" "+name+"=\"";
    auto start=s.find(key);
    if(start==std::string::npos||start>=s.find('>')) return false;
    start+=key.size();
    auto end=s.find('"',start);
    if(end==std::string::npos) return false;
    value=s.substr(start,end-start);
    return true;
}
bool parseGeometry(const std::string &s,std::vector<double> &values) {
    auto tag=shapeTag(s);
    std::vector<std::string> names;
    if(tag=="rect") names={"x","y","width","height"};
    else if(tag=="circle") names={"cx","cy","r"};
    else if(tag=="line") names={"x1","y1","x2","y2","stroke-width"};
    else if(tag=="polygon") {
        std::string points;
        if(!shapeAttribute(s,"points",points)) return false;
        std::replace(points.begin(),points.end(),',',' ');
        std::istringstream in(points); in.imbue(std::locale::classic());
        double v;
        while(in>>v) { if(!coord(v)||values.size()>=8192) return false; values.push_back(v); }
        return in.eof()&&values.size()>=6&&values.size()%2==0;
    } else return false;
    for(const auto &name:names) {
        std::string value;
        if(!shapeAttribute(s,name,value)) return false;
        std::istringstream in(value); in.imbue(std::locale::classic());
        double v;
        if(!(in>>v)||!coord(v)) return false;
        in>>std::ws;
        if(!in.eof()) return false;
        values.push_back(v);
    }
    return true;
}
bool replaceAttribute(std::string &s,const std::string &name,const std::string &value) {
    auto key=" "+name+"=\"";
    auto start=s.find(key);
    if(start==std::string::npos) return false;
    start+=key.size();
    auto end=s.find('"',start);
    if(end==std::string::npos) return false;
    s.replace(start,end-start,value);
    return true;
}
bool updateGeometry(std::string &s,const std::vector<double> &v) {
    auto tag=shapeTag(s);
    std::vector<std::string> names;
    if(tag=="rect") { if(v.size()!=4||v[2]<=0||v[3]<=0) return false; names={"x","y","width","height"}; }
    else if(tag=="circle") { if(v.size()!=3||v[2]<=0) return false; names={"cx","cy","r"}; }
    else if(tag=="line") { if(v.size()!=5||v[4]<=0) return false; names={"x1","y1","x2","y2","stroke-width"}; }
    else if(tag=="polygon") {
        if(v.size()<6||v.size()>8192||v.size()%2) return false;
        auto o=svgStream();
        for(size_t i=0;i<v.size();i+=2) o<<v[i]<<','<<v[i+1]<<' ';
        return replaceAttribute(s,"points",o.str());
    } else return false;
    for(size_t i=0;i<names.size();++i) {
        auto o=svgStream(); o<<v[i];
        if(!replaceAttribute(s,names[i],o.str())) return false;
    }
    return true;
}
}
// Canonical editable path subset: explicit absolute M,L,Q,C,Z commands.
// Existing H/V paths remain accepted by the legacy editable importer.
namespace {
bool canonicalPath(const std::string &input,std::string &output) {
    if(input.empty()||input.size()>1024*1024)return false;
    std::istringstream in(input);in.imbue(std::locale::classic());
    auto o=svgStream();bool active=false,drawn=false;size_t count=0;
    char cmd;
    while(in>>cmd){
        if(++count>4096)return false;
        int n=0;
        if(cmd=='M'){n=2;active=true;}
        else if(cmd=='Z'){if(!active||!drawn)return false;active=false;}
        else if(active&&(cmd=='L'||cmd=='Q'||cmd=='C')){n=cmd=='L'?2:cmd=='Q'?4:6;drawn=true;}
        else return false;
        if(count>1)o<<' ';
        o<<cmd;
        for(int i=0;i<n;i++){
            double v;if(!(in>>v)||!coord(v))return false;
            o<<' '<<v;
        }
        if(cmd=='M')drawn=false;
    }
    if(!in.eof()||count==0)return false;
    output=o.str();return true;
}
}
CK_DLL_MFUN(vector_image_add_path) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto raw=GET_NEXT_STRING_SAFE(ARGS);
        auto rgb=GET_NEXT_INT(ARGS);auto alpha=GET_NEXT_FLOAT(ARGS);
        std::string d;
        if(!p||!color(rgb,alpha)||!canonicalPath(raw,d))return;
        RETURN->v_int=p->addShape("<path d=\""+d+"\""+style(rgb,alpha,false)+"/>");
    }catch(...){}
}
CK_DLL_MFUN(vector_image_get_path) {
    std::string d;
    try {
        auto *s=shapeAt(vectorState(SELF,API),GET_NEXT_INT(ARGS));
        if(s&&shapeTag(*s)=="path")shapeAttribute(*s,"d",d);
    }catch(...){}
    RETURN->v_string=API->object->create_string(VM,d.c_str(),FALSE);
}
CK_DLL_MFUN(vector_image_set_path) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);auto i=GET_NEXT_INT(ARGS);
        auto raw=GET_NEXT_STRING_SAFE(ARGS);
        auto *s=shapeAt(p,i);std::string d;
        if(!s||shapeTag(*s)!="path"||!canonicalPath(raw,d))return;
        auto next=p->shapes;
        if(!replaceAttribute(next[i],"d",d))return;
        RETURN->v_int=p->replaceShapes(p->canvasW,p->canvasH,std::move(next));
    }catch(...){}
}

namespace {
struct PathSegment { char cmd; double startX,startY; std::array<double,6> v{}; };
struct PathSub { double x=0,y=0; bool closed=false; std::vector<PathSegment> segments; };
bool reversePath(const std::string &raw,std::string &out) {
    std::string canonical;
    if(!canonicalPath(raw,canonical))return false;
    std::istringstream in(canonical);in.imbue(std::locale::classic());
    std::vector<PathSub> subs;double x=0,y=0;char cmd;
    while(in>>cmd){
        if(cmd=='M'){
            in>>x>>y;subs.push_back(PathSub{});subs.back().x=x;subs.back().y=y;
        }else if(cmd=='Z'){subs.back().closed=true;}
        else{
            PathSegment s{cmd,x,y,{}};
            int n=cmd=='L'?2:cmd=='Q'?4:6;
            for(int i=0;i<n;i++)in>>s.v[i];
            x=s.v[n-2];y=s.v[n-1];subs.back().segments.push_back(s);
        }
    }
    auto o=svgStream();
    for(const auto &sub:subs){
        if(o.tellp()>0)o<<' ';
        double endX=sub.x,endY=sub.y;
        if(!sub.segments.empty()){
            const auto &s=sub.segments.back();int n=s.cmd=='L'?2:s.cmd=='Q'?4:6;
            endX=s.v[n-2];endY=s.v[n-1];
        }
        o<<"M "<<endX<<' '<<endY;
        for(auto it=sub.segments.rbegin();it!=sub.segments.rend();++it){
            const auto &s=*it;o<<' '<<s.cmd;
            if(s.cmd=='Q')o<<' '<<s.v[0]<<' '<<s.v[1];
            if(s.cmd=='C')o<<' '<<s.v[2]<<' '<<s.v[3]<<' '<<s.v[0]<<' '<<s.v[1];
            o<<' '<<s.startX<<' '<<s.startY;
        }
        if(sub.closed)o<<" Z";
    }
    return canonicalPath(o.str(),out);
}
}
CK_DLL_MFUN(vector_image_reverse_path) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);auto i=GET_NEXT_INT(ARGS);
        auto *s=shapeAt(p,i);std::string raw,d;
        if(!s||shapeTag(*s)!="path"||!shapeAttribute(*s,"d",raw)||!reversePath(raw,d))return;
        auto next=p->shapes;
        if(!replaceAttribute(next[i],"d",d))return;
        RETURN->v_int=p->replaceShapes(p->canvasW,p->canvasH,std::move(next));
    }catch(...){}
}
// Append a second canonical path as distinct subpaths in the first shape.
// Both shapes must have identical non-geometry attributes (including transform).
// This is a compound path operation, not a boolean union.
CK_DLL_MFUN(vector_image_join_paths) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);auto a=GET_NEXT_INT(ARGS),b=GET_NEXT_INT(ARGS);
        auto *s=shapeAt(p,a);auto *t=shapeAt(p,b);
        if(!s||!t||a==b||shapeTag(*s)!="path"||shapeTag(*t)!="path")return;
        std::string da,db,ca,cb,d,sa=*s,sb=*t;
        if(!shapeAttribute(*s,"d",da)||!shapeAttribute(*t,"d",db)||
           !canonicalPath(da,ca)||!canonicalPath(db,cb)||
           !replaceAttribute(sa,"d","")||!replaceAttribute(sb,"d","")||sa!=sb||
           !canonicalPath(ca+" "+cb,d))return;
        auto next=p->shapes;
        if(!replaceAttribute(next[a],"d",d))return;
        next.erase(next.begin()+b);
        RETURN->v_int=p->replaceShapes(p->canvasW,p->canvasH,std::move(next));
    }catch(...){}
}

CK_DLL_MFUN(vector_image_shape_type) {
    std::string tag;
    try { if(auto *s=shapeAt(vectorState(SELF,API),GET_NEXT_INT(ARGS))) tag=shapeTag(*s); } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,tag.c_str(),FALSE);
}
CK_DLL_MFUN(vector_image_geometry_size) {
    RETURN->v_int=-1;
    try {
        auto *s=shapeAt(vectorState(SELF,API),GET_NEXT_INT(ARGS));
        std::vector<double> values;
        if(s&&parseGeometry(*s,values)) RETURN->v_int=static_cast<t_CKINT>(values.size());
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_get_geometry) {
    RETURN->v_int=0;
    try {
        auto *s=shapeAt(vectorState(SELF,API),GET_NEXT_INT(ARGS));
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        std::vector<double> values;
        if(!s||!a||!parseGeometry(*s,values)||API->object->array_float_size(a)!=static_cast<t_CKINT>(values.size())) return;
        for(size_t i=0;i<values.size();++i) API->object->array_float_set_idx(a,i,values[i]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_set_geometry) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto *s=shapeAt(p,i);
        if(!s||!a) return;
        auto n=API->object->array_float_size(a);
        if(n<3||n>8192) return;
        std::vector<double> values;
        for(t_CKINT k=0;k<n;++k) {
            double v=API->object->array_float_get_idx(a,k);
            if(!coord(v)) return;
            values.push_back(v);
        }
        auto next=p->shapes;
        if(updateGeometry(next[i],values)) RETURN->v_int=p->replaceShapes(p->canvasW,p->canvasH,std::move(next));
    } catch(...) {}
}
namespace {
using ShapeMatrix=std::array<double,6>;
bool readTransform(const std::string &s,ShapeMatrix &m) {
    if(shapeTag(s)=="g") return false;
    m={{1,0,0,1,0,0}};
    std::string value;
    if(!shapeAttribute(s,"transform",value)) return true;
    if(value.size()<8||value.substr(0,7)!="matrix("||value.back()!=')') return false;
    std::istringstream in(value.substr(7,value.size()-8));
    in.imbue(std::locale::classic());
    for(auto &v:m) if(!(in>>v)||!std::isfinite(v)) return false;
    in>>std::ws;
    return in.eof();
}
bool validTransform(const ShapeMatrix &m) {
    for(double v:m) if(!coord(v)) return false;
    double det=m[0]*m[3]-m[1]*m[2];
    return std::isfinite(det)&&std::abs(det)>=1e-12;
}
bool writeTransform(std::string &s,const ShapeMatrix &m) {
    if(shapeTag(s)=="g") return false;
    if(!validTransform(m)) return false;
    auto o=svgStream(); o<<"matrix(";
    for(size_t k=0;k<m.size();++k) { if(k) o<<' '; o<<m[k]; }
    o<<')';
    std::string previous;
    if(shapeAttribute(s,"transform",previous)) return replaceAttribute(s,"transform",o.str());
    if(s.size()<2||s.substr(s.size()-2)!="/>") return false;
    s.insert(s.size()-2," transform=\""+o.str()+"\"");
    return true;
}
bool setTransform(VectorImageState *p,t_CKINT index,const ShapeMatrix &m) {
    if(!shapeAt(p,index)||!validTransform(m)) return false;
    auto next=p->shapes;
    return writeTransform(next[index],m)&&p->replaceShapes(p->canvasW,p->canvasH,std::move(next));
}
// Compose in SVG canvas coordinates: new operation times the existing matrix.
bool composeTransform(VectorImageState *p,t_CKINT index,const ShapeMatrix &op) {
    auto *s=shapeAt(p,index);
    ShapeMatrix m;
    if(!s||!validTransform(op)||!readTransform(*s,m)) return false;
    ShapeMatrix next{{op[0]*m[0]+op[2]*m[1],op[1]*m[0]+op[3]*m[1],
        op[0]*m[2]+op[2]*m[3],op[1]*m[2]+op[3]*m[3],
        op[0]*m[4]+op[2]*m[5]+op[4],op[1]*m[4]+op[3]*m[5]+op[5]}};
    return setTransform(p,index,next);
}
}
CK_DLL_MFUN(vector_image_get_transform) {
    RETURN->v_int=0;
    try {
        auto *s=shapeAt(vectorState(SELF,API),GET_NEXT_INT(ARGS));
        auto out=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        ShapeMatrix m;
        if(!s||!out||API->object->array_float_size(out)!=6||!readTransform(*s,m)) return;
        for(size_t k=0;k<6;++k) API->object->array_float_set_idx(out,k,m[k]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_set_transform) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        auto in=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!in||API->object->array_float_size(in)!=6) return;
        ShapeMatrix m;
        for(size_t k=0;k<6;++k) m[k]=API->object->array_float_get_idx(in,k);
        RETURN->v_int=setTransform(p,i,m);
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_reset_transform) {
    RETURN->v_int=0;
    try { auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        RETURN->v_int=setTransform(p,i,{{1,0,0,1,0,0}});
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_translate_shape) {
    RETURN->v_int=0;
    try { auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        double x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);
        RETURN->v_int=composeTransform(p,i,{{1,0,0,1,x,y}});
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_rotate_shape) {
    RETURN->v_int=0;
    try { auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        double angle=GET_NEXT_FLOAT(ARGS),x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);
        if(!std::isfinite(angle)||std::abs(angle)>1e9||!coord(x)||!coord(y)) return;
        double rad=std::remainder(angle,360.0)*3.14159265358979323846/180.0;
        double c=std::cos(rad),s=std::sin(rad);
        RETURN->v_int=composeTransform(p,i,{{c,s,-s,c,x-c*x+s*y,y-s*x-c*y}});
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_scale_shape) {
    RETURN->v_int=0;
    try { auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        double sx=GET_NEXT_FLOAT(ARGS),sy=GET_NEXT_FLOAT(ARGS),x=GET_NEXT_FLOAT(ARGS),y=GET_NEXT_FLOAT(ARGS);
        if(!std::isfinite(sx)||!std::isfinite(sy)||std::abs(sx)<0.001||std::abs(sy)<0.001||
            std::abs(sx)>64||std::abs(sy)>64||!coord(x)||!coord(y)) return;
        RETURN->v_int=composeTransform(p,i,{{sx,0,0,sy,x-sx*x,y-sy*y}});
    } catch(...) {}
}
namespace {
bool putAttribute(std::string &s,const std::string &name,const std::string &value) {
    std::string old;
    if(shapeAttribute(s,name,old)) return replaceAttribute(s,name,value);
    if(s.size()<2||s.substr(s.size()-2)!="/>") return false;
    s.insert(s.size()-2," "+name+"=\""+value+"\"");
    return true;
}
std::string numberAttribute(double v) { auto o=svgStream(); o<<v; return o.str(); }
std::string paintAttribute(t_CKINT rgb) {
    if(rgb==-1) return "none";
    auto o=svgStream(); o<<'#'<<std::hex<<std::setw(6)<<std::setfill('0')<<rgb; return o.str();
}
bool readNumber(const std::string &s,const std::string &name,double fallback,double &v) {
    std::string value;
    if(!shapeAttribute(s,name,value)) { v=fallback; return true; }
    std::istringstream in(value); in.imbue(std::locale::classic());
    if(!(in>>v)||!std::isfinite(v)) return false;
    in>>std::ws; return in.eof();
}
bool readPaint(const std::string &s,const std::string &name,t_CKINT fallback,double &v) {
    std::string value;
    if(!shapeAttribute(s,name,value)) { v=fallback; return true; }
    if(value=="none") { v=-1; return true; }
    if(value.size()!=7||value[0]!='#') return false;
    unsigned rgb=0;
    for(size_t k=1;k<7;++k) {
        char ch=value[k];
        unsigned n=ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:16;
        if(n>15) return false;
        rgb=rgb*16+n;
    }
    v=rgb; return true;
}
using ShapePaint=std::array<double,6>;
bool readStyle(const std::string &s,ShapePaint &v) {
    if(shapeTag(s)=="g") return false;
    return readPaint(s,"fill",shapeTag(s)=="line"?-1:0,v[0])&&
        readNumber(s,"fill-opacity",1,v[1])&&readPaint(s,"stroke",-1,v[2])&&
        readNumber(s,"stroke-opacity",1,v[3])&&readNumber(s,"stroke-width",1,v[4])&&
        readNumber(s,"opacity",1,v[5]);
}
bool validPaint(double v) { return std::isfinite(v)&&v>=-1&&v<=0xffffff&&std::floor(v)==v; }
bool validAlpha(double v) { return std::isfinite(v)&&v>=0&&v<=1; }
bool setStyle(VectorImageState *p,t_CKINT i,const ShapePaint &v) {
    if(!shapeAt(p,i)||!validPaint(v[0])||!validPaint(v[2])||
       !validAlpha(v[1])||!validAlpha(v[3])||!validAlpha(v[5])||
       !coord(v[4])||v[4]<=0) return false;
    auto next=p->shapes; auto &s=next[i];
    if(shapeTag(s)=="g"||(shapeTag(s)=="line"&&v[0]!=-1)) return false;
    if(!putAttribute(s,"fill",paintAttribute(static_cast<t_CKINT>(v[0])))||
       !putAttribute(s,"fill-opacity",numberAttribute(v[1]))||
       !putAttribute(s,"stroke",paintAttribute(static_cast<t_CKINT>(v[2])))||
       !putAttribute(s,"stroke-opacity",numberAttribute(v[3]))||
       !putAttribute(s,"stroke-width",numberAttribute(v[4]))||
       !putAttribute(s,"opacity",numberAttribute(v[5]))) return false;
    return p->replaceShapes(p->canvasW,p->canvasH,std::move(next));
}
}
CK_DLL_MFUN(vector_image_get_style) {
    RETURN->v_int=0;
    try {
        auto *s=shapeAt(vectorState(SELF,API),GET_NEXT_INT(ARGS));
        auto out=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        ShapePaint v;
        if(!s||!out||API->object->array_float_size(out)!=6||!readStyle(*s,v)) return;
        for(size_t k=0;k<6;++k) API->object->array_float_set_idx(out,k,v[k]);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_set_style) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        auto in=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!in||API->object->array_float_size(in)!=6) return;
        ShapePaint v;
        for(size_t k=0;k<6;++k) v[k]=API->object->array_float_get_idx(in,k);
        RETURN->v_int=setStyle(p,i,v);
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_set_fill) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS),rgb=GET_NEXT_INT(ARGS);
        double alpha=GET_NEXT_FLOAT(ARGS); auto *s=shapeAt(p,i); ShapePaint v;
        if(!s||!readStyle(*s,v)||rgb< -1||rgb>0xffffff) return;
        v[0]=rgb; v[1]=alpha; RETURN->v_int=setStyle(p,i,v);
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_set_stroke) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS),rgb=GET_NEXT_INT(ARGS);
        double alpha=GET_NEXT_FLOAT(ARGS),width=GET_NEXT_FLOAT(ARGS);
        auto *s=shapeAt(p,i); ShapePaint v;
        if(!s||!readStyle(*s,v)||rgb< -1||rgb>0xffffff) return;
        v[2]=rgb; v[3]=alpha; v[4]=width; RETURN->v_int=setStyle(p,i,v);
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_set_shape_opacity) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        double alpha=GET_NEXT_FLOAT(ARGS); auto *s=shapeAt(p,i); ShapePaint v;
        if(!s||!readStyle(*s,v)) return;
        v[5]=alpha; RETURN->v_int=setStyle(p,i,v);
    } catch(...) {}
}
namespace {
// Only internally generated groups are editable: no arbitrary XML parsing.
bool groupChildren(const std::string &s,std::vector<std::string> &out) {
    if(s.size()<7||s.compare(0,3,"<g>")!=0||s.compare(s.size()-4,4,"</g>")!=0) return false;
    size_t pos=3,end=s.size()-4;
    while(pos<end) {
        if(s[pos]!='<') return false;
        size_t start=pos;
        if(s.compare(pos,3,"<g>")==0) {
            int depth=1; pos+=3;
            while(pos<end&&depth>0) {
                if(s.compare(pos,3,"<g>")==0) { ++depth; pos+=3; }
                else if(s.compare(pos,4,"</g>")==0) { --depth; pos+=4; }
                else ++pos;
            }
            if(depth) return false;
        } else {
            auto close=s.find("/>",pos);
            if(close==std::string::npos||close+2>end) return false;
            pos=close+2;
        }
        out.push_back(s.substr(start,pos-start));
    }
    return pos==end&&!out.empty();
}
}
CK_DLL_MFUN(vector_image_duplicate_shape) {
    RETURN->v_int=-1;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        auto *s=shapeAt(p,i);
        if(!s||p->shapes.size()>=4096) return;
        auto next=p->shapes; auto copy=*s;
        next.insert(next.begin()+i+1,std::move(copy));
        if(p->replaceShapes(p->canvasW,p->canvasH,std::move(next))) RETURN->v_int=i+1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_move_shape) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto from=GET_NEXT_INT(ARGS),to=GET_NEXT_INT(ARGS);
        if(!shapeAt(p,from)||!shapeAt(p,to)) return;
        if(from==to) { RETURN->v_int=1; return; }
        auto next=p->shapes; auto item=std::move(next[from]);
        next.erase(next.begin()+from); next.insert(next.begin()+to,std::move(item));
        RETURN->v_int=p->replaceShapes(p->canvasW,p->canvasH,std::move(next));
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_group_shapes) {
    RETURN->v_int=-1;
    try {
        auto *p=vectorState(SELF,API); auto first=GET_NEXT_INT(ARGS),count=GET_NEXT_INT(ARGS);
        if(!shapeAt(p,first)||count<2||static_cast<uint64_t>(count)>p->shapes.size()-first) return;
        std::string group="<g>";
        for(t_CKINT k=0;k<count;++k) group+=p->shapes[first+k];
        group+="</g>";
        auto next=p->shapes;
        next.erase(next.begin()+first,next.begin()+first+count);
        next.insert(next.begin()+first,std::move(group));
        if(p->replaceShapes(p->canvasW,p->canvasH,std::move(next))) RETURN->v_int=first;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_group_size) {
    RETURN->v_int=-1;
    try {
        auto *s=shapeAt(vectorState(SELF,API),GET_NEXT_INT(ARGS));
        std::vector<std::string> children;
        if(s&&groupChildren(*s,children)) RETURN->v_int=static_cast<t_CKINT>(children.size());
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_ungroup_shape) {
    RETURN->v_int=-1;
    try {
        auto *p=vectorState(SELF,API); auto i=GET_NEXT_INT(ARGS);
        auto *s=shapeAt(p,i); std::vector<std::string> children;
        if(!s||!groupChildren(*s,children)) return;
        auto next=p->shapes;
        next.erase(next.begin()+i);
        next.insert(next.begin()+i,children.begin(),children.end());
        if(p->replaceShapes(p->canvasW,p->canvasH,std::move(next))) RETURN->v_int=static_cast<t_CKINT>(children.size());
    } catch(...) {}
}
namespace {
// Strict editable subset. Never adopt arbitrary XML into the generated-shape
// string representation; normalize only whitelisted elements and attributes.
struct EditableParser {
    const std::string &source;
    size_t pos=0,nodes=0;
    void space() { while(pos<source.size()&&(source[pos]==' '||source[pos]=='\n'||source[pos]=='\r'||source[pos]=='\t')) ++pos; }
    bool take(const std::string &s) {
        if(source.compare(pos,s.size(),s)!=0) return false;
        pos+=s.size(); return true;
    }
    bool name(std::string &s) {
        size_t start=pos;
        while(pos<source.size()&&((source[pos]>='a'&&source[pos]<='z')||
              (source[pos]>='A'&&source[pos]<='Z')||(source[pos]>='0'&&source[pos]<='9')||
              source[pos]=='-'||source[pos]==':')) ++pos;
        s=source.substr(start,pos-start); return !s.empty();
    }
    using Attrs=std::vector<std::pair<std::string,std::string>>;
    bool attrs(Attrs &a) {
        while(true) {
            size_t before=pos; space();
            if(pos>=source.size()) return false;
            if(source[pos]=='>'||source[pos]=='/') return true;
            if(before==pos||a.size()>=16) return false;
            std::string key,value;
            if(!name(key)) return false;
            for(const auto &p:a) if(p.first==key) return false;
            space(); if(!take("=")) return false; space();
            if(pos>=source.size()||(source[pos]!='"'&&source[pos]!='\'')) return false;
            char quote=source[pos++]; auto end=source.find(quote,pos);
            if(end==std::string::npos) return false;
            value=source.substr(pos,end-pos); pos=end+1;
            if(value.find_first_of("<>&\"'")!=std::string::npos) return false;
            a.emplace_back(std::move(key),std::move(value));
        }
    }
    static bool allowed(const std::string &tag,const std::string &key) {
        if(key=="fill"||key=="fill-opacity"||key=="stroke"||key=="stroke-opacity"||
           key=="stroke-width"||key=="opacity"||key=="transform") return true;
        if(tag=="rect") return key=="x"||key=="y"||key=="width"||key=="height";
        if(tag=="circle") return key=="cx"||key=="cy"||key=="r";
        if(tag=="line") return key=="x1"||key=="y1"||key=="x2"||key=="y2";
        if(tag=="polygon") return key=="points";
        if(tag=="path") return key=="d";
        return false;
    }
    static bool leafValid(std::string &s,const std::string &tag) {
        ShapeMatrix m; ShapePaint v;
        if(!readTransform(s,m)||!validTransform(m)||!readStyle(s,v)||
           !validPaint(v[0])||!validPaint(v[2])||!validAlpha(v[1])||
           !validAlpha(v[3])||!validAlpha(v[5])||!coord(v[4])||v[4]<=0||
           (tag=="line"&&v[0]!=-1)) return false;
        if(tag=="path") {
            std::string d;
            if(!shapeAttribute(s,"d",d)||d.empty()||d.size()>1024*1024)return false;
            std::string normalized;
            if(canonicalPath(d,normalized))return replaceAttribute(s,"d",normalized);
            return d.find_first_not_of("0123456789.eE+- ,\t\r\nMHVZ")==std::string::npos;
        }
        // Lines without explicit stroke-width use the SVG default.
        if(tag=="line"&&!putAttribute(s,"stroke-width",numberAttribute(v[4]))) return false;
        std::vector<double> geometry;
        return parseGeometry(s,geometry)&&updateGeometry(s,geometry);
    }
    bool item(std::string &out,int depth) {
        if(++nodes>4096||!take("<")) return false;
        std::string tag; Attrs a;
        if(!name(tag)||!attrs(a)) return false;
        if(tag=="g") {
            if(depth>=16||!a.empty()||!take(">")) return false;
            out="<g>"; size_t count=0;
            while(true) {
                space(); if(take("</g>")) break;
                std::string child;
                if(!item(child,depth+1)) return false;
                out+=child; ++count;
            }
            out+="</g>"; return count>0;
        }
        if(tag!="rect"&&tag!="circle"&&tag!="line"&&tag!="polygon"&&tag!="path") return false;
        if(!take("/>")) return false;
        out="<"+tag;
        for(const auto &p:a) {
            if(!allowed(tag,p.first)) return false;
            out+=" "+p.first+"=\""+p.second+"\"";
        }
        out+="/>";
        return leafValid(out,tag);
    }
    static bool dimension(const std::string &s,int &n) {
        if(s.empty()||s.size()>4||s.find_first_not_of("0123456789")!=std::string::npos) return false;
        n=std::stoi(s); return n>=1&&n<=4096;
    }
    bool parse(int &w,int &h,std::vector<std::string> &items) {
        if(source.empty()||source.size()>16*1024*1024) return false;
        space(); if(!take("<svg")) return false;
        Attrs a;
        if(!attrs(a)||!take(">")) return false;
        std::string view; bool ns=false; w=h=0;
        for(const auto &p:a) {
            if(p.first=="xmlns") ns=p.second=="http://www.w3.org/2000/svg";
            else if(p.first=="width") { if(!dimension(p.second,w)) return false; }
            else if(p.first=="height") { if(!dimension(p.second,h)) return false; }
            else if(p.first=="viewBox") view=p.second;
            else return false;
        }
        if(!ns||!w||!h||view!="0 0 "+std::to_string(w)+" "+std::to_string(h)) return false;
        while(true) {
            space(); if(take("</svg>")) break;
            std::string shape;
            if(!item(shape,0)) return false;
            items.push_back(std::move(shape));
        }
        space(); return pos==source.size();
    }
};
bool adoptEditable(VectorImageState *p,const std::string &svg) {
    if(!p) return false;
    EditableParser parser{svg}; int w=0,h=0; std::vector<std::string> items;
    if(!parser.parse(w,h,items)) return false;
    if(!p->replaceShapes(w,h,std::move(items))) return false;
    p->reset(); return true;
}
}
CK_DLL_MFUN(vector_image_set_editable_svg) {
    RETURN->v_int=0;
    try { auto *p=vectorState(SELF,API); auto svg=GET_NEXT_STRING_SAFE(ARGS);
        RETURN->v_int=adoptEditable(p,svg);
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_load_editable) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(!p) return;
        std::ifstream in(path,std::ios::binary);
        if(!in) return;
        in.seekg(0,std::ios::end); auto n=in.tellg();
        if(n<=0||n>16*1024*1024) return;
        in.seekg(0,std::ios::beg); std::string svg(static_cast<size_t>(n),'\0');
        if(!in.read(svg.data(),static_cast<std::streamsize>(n))) return;
        RETURN->v_int=adoptEditable(p,svg);
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_save) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->handle && path.size()>=4 && path.substr(path.size()-4)==".svg")
            RETURN->v_int=writeNew(path,reinterpret_cast<const unsigned char *>(p->markup.data()),p->markup.size());
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_export) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API); auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p || !a || w<1 || w>4096 || h<1 || h>4096 || API->object->array_int_size(a)!=w*h*4) return;
        auto surface=p->rasterize(w,h);
        if(!surface) return;
        auto *data=cairo_image_surface_get_data(surface.get());
        int stride=cairo_image_surface_get_stride(surface.get());
        if(!data) return;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            uint32_t v; std::memcpy(&v,data+static_cast<size_t>(y)*stride+x*4,4);
            unsigned alpha=v>>24;
            t_CKINT i=(static_cast<t_CKINT>(y)*w+x)*4;
            for(int c=0;c<3;++c) {
                unsigned q=(v>>(16-8*c))&255;
                API->object->array_int_set_idx(a,i+c,alpha?std::min(255u,(q*255+alpha/2)/alpha):0);
            }
            API->object->array_int_set_idx(a,i+3,alpha);
        }
        RETURN->v_int=1;
    } catch(...) {}
}

CK_DLL_MFUN(vector_image_waveform) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        auto rgb=GET_NEXT_INT(ARGS); auto alpha=GET_NEXT_FLOAT(ARGS);
        if(!p||w<1||w>4096||h<1||h>4096||!a||!color(rgb,alpha)) return;
        auto n=API->object->array_float_size(a);
        if(n<2||n>8192||n%2) return;
        auto o=svgStream(); o<<"<path d=\"";
        double step=2.0*w/n;
        for(t_CKINT i=0;i<n;i+=2) {
            double lo=API->object->array_float_get_idx(a,i);
            double hi=API->object->array_float_get_idx(a,i+1);
            if(!std::isfinite(lo)||!std::isfinite(hi)||lo< -1||hi>1||lo>hi) return;
            double x=(i/2)*step;
            double top=(1-hi)*0.5*(h-1);
            double bottom=(1-lo)*0.5*(h-1)+1;
            o<<"M "<<x<<' '<<top<<" H "<<x+step<<" V "<<bottom<<" H "<<x<<" Z ";
        }
        o<<"\""<<style(rgb,alpha,false)<<"/>";
        if(p->replaceShapes(w,h,{o.str()})) { p->reset(); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_alpha) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||w<1||w>4096||h<1||h>4096||
           API->object->array_int_size(a)!=w*h) return;
        auto surface=p->rasterize(w,h);
        if(!surface) return;
        auto *data=cairo_image_surface_get_data(surface.get());
        int stride=cairo_image_surface_get_stride(surface.get());
        if(!data) return;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            uint32_t v; std::memcpy(&v,data+static_cast<size_t>(y)*stride+x*4,4);
            API->object->array_int_set_idx(a,static_cast<size_t>(y)*w+x,v>>24);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_import_pixels) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||w<1||w>4096||h<1||h>4096||w*h>4194304||
           API->object->array_int_size(a)!=w*h*4) return;
        std::vector<uint32_t> pixels(w*h);
        for(size_t i=0;i<pixels.size();++i) {
            uint32_t packed=0;
            for(int c=0;c<4;++c) {
                auto v=API->object->array_int_get_idx(a,4*i+c);
                if(v<0||v>255) return;
                packed=(packed<<8)|static_cast<uint32_t>(v);
            }
            pixels[i]=packed;
        }
        std::vector<std::string> shapes;
        for(int y=0;y<h;++y) for(int x=0;x<w;) {
            uint32_t v=pixels[y*w+x];
            int end=x+1;
            while(end<w&&pixels[y*w+end]==v) ++end;
            if(v&255u) {
                if(shapes.size()>=4096) return;
                auto o=svgStream();
                o<<"<rect x=\""<<x<<"\" y=\""<<y<<"\" width=\""<<end-x
                 <<"\" height=\"1\""<<style(v>>8,(v&255u)/255.0,false)<<"/>";
                shapes.push_back(o.str());
            }
            x=end;
        }
        if(p->replaceShapes(w,h,std::move(shapes))) { p->reset(); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_import_triangles) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||w<1||h<1||w>4096||h>4096) return;
        auto n=API->object->array_float_size(a);
        if(n<7||n>4096*7||n%7) return;
        std::vector<std::string> shapes;
        for(t_CKINT i=0;i<n;i+=7) {
            double v[6];
            for(int k=0;k<6;++k) {
                v[k]=API->object->array_float_get_idx(a,i+k);
                if(!coord(v[k])) return;
            }
            double rgb=API->object->array_float_get_idx(a,i+6);
            if(!std::isfinite(rgb)||rgb<0||rgb>0xffffff||std::floor(rgb)!=rgb) return;
            double area=(v[2]-v[0])*(v[5]-v[1])-(v[3]-v[1])*(v[4]-v[0]);
            if(area==0) continue;
            auto o=svgStream(); o<<"<polygon points=\"";
            for(int k=0;k<6;k+=2) o<<v[k]<<','<<v[k+1]<<' ';
            o<<"\""<<style(static_cast<t_CKINT>(rgb),1.0,false)<<"/>";
            shapes.push_back(o.str());
        }
        if(p->replaceShapes(w,h,std::move(shapes))) { p->reset(); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_export_pitches) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto low=GET_NEXT_INT(ARGS),high=GET_NEXT_INT(ARGS),threshold=GET_NEXT_INT(ARGS),scan=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||w<1||h<1||w>4096||h>4096||w*h>100000||
           low<0||high>127||low>high||threshold<1||threshold>255||scan<0||scan>2||
           API->object->array_int_size(a)!=w*h) return;
        auto surface=p->rasterize(w,h);
        if(!surface) return;
        auto *data=cairo_image_surface_get_data(surface.get());
        int stride=cairo_image_surface_get_stride(surface.get());
        if(!data) return;
        for(t_CKINT i=0;i<w*h;++i) {
            auto x=scan==1?i/h:i%w,y=scan==1?i%h:i/w;
            if(scan==2&&y%2) x=w-1-x;
            uint32_t v;
            std::memcpy(&v,data+static_cast<size_t>(y)*stride+x*4,4);
            unsigned alpha=v>>24;
            t_CKINT pitch=-1;
            if(alpha>=static_cast<unsigned>(threshold)) {
                auto component=[&](int c) {
                    unsigned q=(v>>(16-8*c))&255;
                    return std::min(255u,(q*255+alpha/2)/alpha);
                };
                double lum=(0.2126*component(0)+0.7152*component(1)+0.0722*component(2))/255.0;
                pitch=low+std::lround(std::clamp(lum,0.0,1.0)*(high-low));
            }
            API->object->array_int_set_idx(a,i,pitch);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_export_frame) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),rgb=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||w<1||h<1||w>4096||h>4096||w*h>4194304||
           rgb<0||rgb>0xffffff||API->object->array_int_size(a)!=w*h*4) return;
        auto surface=p->rasterize(w,h);
        if(!surface) return;
        auto *data=cairo_image_surface_get_data(surface.get());
        int stride=cairo_image_surface_get_stride(surface.get());
        if(!data) return;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            uint32_t v; std::memcpy(&v,data+static_cast<size_t>(y)*stride+x*4,4);
            unsigned alpha=v>>24;
            size_t i=(static_cast<size_t>(y)*w+x)*4;
            for(int c=0;c<3;++c) {
                unsigned q=(v>>(16-8*c))&255;
                unsigned bg=(rgb>>(16-8*c))&255;
                // Cairo channels are already premultiplied by alpha.
                API->object->array_int_set_idx(a,i+c,std::min(255u,q+(bg*(255-alpha)+127)/255));
            }
            API->object->array_int_set_idx(a,i+3,255);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_import_rects) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||w<1||w>4096||h<1||h>4096) return;
        auto n=API->object->array_float_size(a);
        if(n<0||n>4096*6||n%6) return;
        std::vector<std::string> shapes;
        for(t_CKINT i=0;i<n;i+=6) {
            double v[6];
            for(int k=0;k<6;++k) v[k]=API->object->array_float_get_idx(a,i+k);
            for(int k=0;k<4;++k) if(!coord(v[k])) return;
            if(v[2]<=0||v[3]<=0||!std::isfinite(v[4])||v[4]<0||v[4]>0xffffff||
               std::floor(v[4])!=v[4]||!std::isfinite(v[5])||v[5]<0||v[5]>1) return;
            auto o=svgStream();
            o<<"<rect x=\""<<v[0]<<"\" y=\""<<v[1]<<"\" width=\""<<v[2]
             <<"\" height=\""<<v[3]<<"\""<<style(static_cast<t_CKINT>(v[4]),v[5],false)<<"/>";
            shapes.push_back(o.str());
        }
        if(p->replaceShapes(w,h,std::move(shapes))) { p->reset(); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(vector_image_audio_samples) {
    RETURN->v_int=0;
    try {
        auto *p=vectorState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),channels=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||w<1||h<1||w>4096||h>4096||w*h>4194304||
           (channels!=1&&channels!=2)||API->object->array_float_size(a)!=w*h*channels) return;
        auto surface=p->rasterize(w,h);
        if(!surface) return;
        auto *data=cairo_image_surface_get_data(surface.get());
        int stride=cairo_image_surface_get_stride(surface.get());
        if(!data) return;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            uint32_t v; std::memcpy(&v,data+static_cast<size_t>(y)*stride+x*4,4);
            unsigned alpha=v>>24;
            auto component=[&](int c) {
                unsigned q=(v>>(16-8*c))&255;
                return alpha?std::min(255u,(q*255+alpha/2)/alpha)/255.0:0.0;
            };
            double r=component(0),g=component(1),b=component(2),aNorm=alpha/255.0;
            size_t i=static_cast<size_t>(y)*w+x;
            if(channels==1) {
                double lum=0.2126*r+0.7152*g+0.0722*b;
                API->object->array_float_set_idx(a,i,std::clamp(2*lum-1,-1.0,1.0)*aNorm);
            } else {
                API->object->array_float_set_idx(a,2*i,(r+g-1)*aNorm);
                API->object->array_float_set_idx(a,2*i+1,(b+g-1)*aNorm);
            }
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_QUERY(VectorIO) {
    QUERY->setname(QUERY, "VectorIO");
    QUERY->begin_class(QUERY, "VectorIO", "Object");
    QUERY->add_sfun(QUERY, vector_valid, "int", "valid");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, vector_save, "int", "save");
    QUERY->add_arg(QUERY, "string", "svg");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, vector_render, "int", "render");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "width");
    QUERY->add_arg(QUERY, "int", "height");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"VectorImage","Object");
    vectorImageOffset=QUERY->add_mvar(QUERY,"int","@vectorImageNative",FALSE);
    if(vectorImageOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,vector_image_ctor);
    QUERY->add_dtor(QUERY,vector_image_dtor);
    QUERY->add_mfun(QUERY,vector_image_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,vector_image_set_svg,"int","setSvg");
    QUERY->add_arg(QUERY,"string","svg");
    QUERY->add_mfun(QUERY,vector_image_loaded,"int","loaded");
    QUERY->add_mfun(QUERY,vector_image_render,"int","render");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_mfun(QUERY,vector_image_reset,"void","reset");
    QUERY->add_mfun(QUERY,vector_image_clear,"void","clear");
    QUERY->add_mfun(QUERY,vector_image_get_x,"float","x");
    QUERY->add_mfun(QUERY,vector_image_set_x,"float","x");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,vector_image_get_y,"float","y");
    QUERY->add_mfun(QUERY,vector_image_set_y,"float","y");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,vector_image_get_scaleX,"float","scaleX");
    QUERY->add_mfun(QUERY,vector_image_set_scaleX,"float","scaleX");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,vector_image_get_scaleY,"float","scaleY");
    QUERY->add_mfun(QUERY,vector_image_set_scaleY,"float","scaleY");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,vector_image_get_rotation,"float","rotation");
    QUERY->add_mfun(QUERY,vector_image_set_rotation,"float","rotation");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,vector_image_get_opacity,"float","opacity");
    QUERY->add_mfun(QUERY,vector_image_set_opacity,"float","opacity");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,vector_image_waveform,"int","importWaveform");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float[]","bounds");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_create,"int","create");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_mfun(QUERY,vector_image_rect,"int","addRect");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","width");
    QUERY->add_arg(QUERY,"float","height");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_circle,"int","addCircle");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","radius");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_line,"int","addLine");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_arg(QUERY,"float","x2");
    QUERY->add_arg(QUERY,"float","y2");
    QUERY->add_arg(QUERY,"float","strokeWidth");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_polygon,"int","addPolygon");
    QUERY->add_arg(QUERY,"float[]","points");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_add_path,"int","addPath");
    QUERY->add_arg(QUERY,"string","commands");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_get_path,"string","getPath");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_set_path,"int","setPath");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"string","commands");
    QUERY->add_mfun(QUERY,vector_image_reverse_path,"int","reversePath");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_join_paths,"int","joinPaths");
    QUERY->add_arg(QUERY,"int","first");
    QUERY->add_arg(QUERY,"int","second");
    QUERY->add_mfun(QUERY,vector_image_count,"int","shapes");
    QUERY->add_mfun(QUERY,vector_image_remove,"int","removeShape");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_shape_type,"string","shapeType");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_geometry_size,"int","shapeGeometrySize");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_get_geometry,"int","getShapeGeometry");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_mfun(QUERY,vector_image_set_geometry,"int","setShapeGeometry");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_mfun(QUERY,vector_image_get_transform,"int","getShapeTransform");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float[]","matrix");
    QUERY->add_mfun(QUERY,vector_image_set_transform,"int","setShapeTransform");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float[]","matrix");
    QUERY->add_mfun(QUERY,vector_image_reset_transform,"int","resetShapeTransform");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_translate_shape,"int","translateShape");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","x");
    QUERY->add_arg(QUERY,"float","y");
    QUERY->add_mfun(QUERY,vector_image_rotate_shape,"int","rotateShape");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","degrees");
    QUERY->add_arg(QUERY,"float","pivotX");
    QUERY->add_arg(QUERY,"float","pivotY");
    QUERY->add_mfun(QUERY,vector_image_scale_shape,"int","scaleShape");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","scaleX");
    QUERY->add_arg(QUERY,"float","scaleY");
    QUERY->add_arg(QUERY,"float","pivotX");
    QUERY->add_arg(QUERY,"float","pivotY");
    QUERY->add_mfun(QUERY,vector_image_get_style,"int","getShapeStyle");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_mfun(QUERY,vector_image_set_style,"int","setShapeStyle");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_mfun(QUERY,vector_image_set_fill,"int","setShapeFill");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_set_stroke,"int","setShapeStroke");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_arg(QUERY,"float","width");
    QUERY->add_mfun(QUERY,vector_image_set_shape_opacity,"int","setShapeOpacity");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"float","alpha");
    QUERY->add_mfun(QUERY,vector_image_duplicate_shape,"int","duplicateShape");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_move_shape,"int","moveShape");
    QUERY->add_arg(QUERY,"int","from");
    QUERY->add_arg(QUERY,"int","to");
    QUERY->add_mfun(QUERY,vector_image_group_shapes,"int","groupShapes");
    QUERY->add_arg(QUERY,"int","first");
    QUERY->add_arg(QUERY,"int","count");
    QUERY->add_mfun(QUERY,vector_image_group_size,"int","groupSize");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_ungroup_shape,"int","ungroupShape");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_set_editable_svg,"int","setEditableSvg");
    QUERY->add_arg(QUERY,"string","svg");
    QUERY->add_mfun(QUERY,vector_image_load_editable,"int","loadEditable");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,vector_image_save,"int","save");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,vector_image_export_pitches,"int","exportPitches");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","lowPitch");
    QUERY->add_arg(QUERY,"int","highPitch");
    QUERY->add_arg(QUERY,"int","alphaThreshold");
    QUERY->add_arg(QUERY,"int","scan");
    QUERY->add_arg(QUERY,"int[]","pitches");
    QUERY->add_mfun(QUERY,vector_image_import_rects,"int","importRects");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float[]","rects");
    QUERY->add_mfun(QUERY,vector_image_import_triangles,"int","importTriangles");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float[]","triangles");
    QUERY->add_mfun(QUERY,vector_image_import_pixels,"int","importPixels");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,vector_image_alpha,"int","exportAlpha");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","alpha");
    QUERY->add_mfun(QUERY,vector_image_audio_samples,"int","exportAudioSamples");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,vector_image_export_frame,"int","exportFrame");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","backgroundRGB");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,vector_image_export,"int","exportPixels");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    return QUERY->end_class(QUERY);
}
