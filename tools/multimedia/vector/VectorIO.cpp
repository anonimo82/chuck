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
    QUERY->add_mfun(QUERY,vector_image_count,"int","shapes");
    QUERY->add_mfun(QUERY,vector_image_remove,"int","removeShape");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,vector_image_save,"int","save");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,vector_image_export,"int","exportPixels");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    return QUERY->end_class(QUERY);
}
