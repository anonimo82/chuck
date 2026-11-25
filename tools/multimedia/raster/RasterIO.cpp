// Optional offline raster I/O for ChucK; no VM changes.
#include "chuck_dl.h"
#include "../file_compat.h"
#include "../capture/CaptureFrame.h"
#include "../capture/CameraCapture.h"
#include "../keyframe/keyframe.h"
#include <array>
#include <vector>
#if __has_include(<MagickWand/MagickWand.h>)
#include <MagickWand/MagickWand.h>
#else
#include <wand/MagickWand.h>
#endif
#include <memory>
#include <cstdio>
#include <mutex>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <new>

namespace {
using Wand = std::unique_ptr<MagickWand, decltype(&DestroyMagickWand)>;
Wand makeWand() {
    static std::once_flag once;
    std::call_once(once, [] { MagickWandGenesis(); });
    return Wand(NewMagickWand(), DestroyMagickWand);
}
std::string format(const std::string &path) {
    auto pos = path.find_last_of('.');
    if(pos == std::string::npos) return "";
    auto ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if(ext == "jpg" || ext == "jpeg") return "JPEG";
    if(ext == "tif" || ext == "tiff") return "TIFF";
    if(ext == "png") return "PNG";
    if(ext == "bmp") return "BMP";
    if(ext == "webp") return "WEBP";
    if(ext == "gif") return "GIF";
    if(ext == "tga") return "TGA";
    return "";
}
void report(MagickWand *w, const char *stage) {
    ExceptionType severity;
    char *message = MagickGetException(w, &severity);
    std::fprintf(stderr, "[RasterIO] %s: %s\n", stage, message ? message : "unknown error");
    if(message) MagickRelinquishMemory(message);
}
bool read(MagickWand *w, const std::string &path) {
    const auto fmt = format(path);
    if(fmt.empty() || !w) return false;
    // Explicit format and FILE* avoid ImageMagick filename pseudo-protocols.
    std::unique_ptr<FILE, decltype(&fclose)> f(fopen(path.c_str(), "rb"), fclose);
    if(!f) { std::fprintf(stderr, "[RasterIO] cannot open %s\n", path.c_str()); return false; }
    if(MagickSetFormat(w, fmt.c_str()) == MagickFalse) { report(w, "set format"); return false; }
    if(MagickReadImageFile(w, f.get()) == MagickFalse) { report(w, "decode"); return false; }
    if(MagickSetIteratorIndex(w, 0) == MagickFalse) { report(w, "select frame"); return false; }
    return true;
}
}
CK_DLL_SFUN(raster_width) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        auto w = makeWand();
        if(read(w.get(), path)) RETURN->v_int = MagickGetImageWidth(w.get());
    } catch(...) {}
}
CK_DLL_SFUN(raster_height) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        auto w = makeWand();
        if(read(w.get(), path)) RETURN->v_int = MagickGetImageHeight(w.get());
    } catch(...) {}
}
CK_DLL_SFUN(raster_convert) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto fmt = format(dest);
        if(fmt.empty()) return;
        auto w = makeWand();
        if(!read(w.get(), source)) return;
        // Set both the wand output format and the current image format.
        // The wand retains the input format from read(), which can override encoding.
        if(MagickSetFormat(w.get(), fmt.c_str()) == MagickFalse) { report(w.get(), "output wand format"); return; }
        if(MagickSetImageFormat(w.get(), fmt.c_str()) == MagickFalse) { report(w.get(), "output format"); return; }
        size_t size = 0;
        unsigned char *blob = MagickGetImageBlob(w.get(), &size);
        if(!blob) { report(w.get(), "encode"); return; }
        std::unique_ptr<unsigned char, decltype(&MagickRelinquishMemory)> data(blob, MagickRelinquishMemory);
        // Exclusive creation: never overwrite an existing file.
        FILE *out = mediaio::openNew(dest.c_str());
        if(!out) return;
        bool ok = fwrite(blob, 1, size, out) == size;
        if(fclose(out) != 0) ok = false;
        if(!ok) { std::remove(dest.c_str()); return; }
        RETURN->v_int = 1;
    } catch(...) {}
}

namespace {
struct RasterAnimation {
    // RGB multipliers, opacity multiplier, brightness offset in normalized RGB.
    std::array<keyframes::Curve,5> curves;
    bool load(const std::string &path) {
        std::ifstream in(path,std::ios::binary);
        if(!in) return false;
        in.seekg(0,std::ios::end);
        auto size=in.tellg();
        if(size<=0 || size>8*1024*1024) return false;
        in.seekg(0,std::ios::beg);
        const std::array<std::string,5> names{{"red","green","blue","opacity","brightness"}};
        std::string line;
        bool any=false;
        while(std::getline(in,line)) {
            if(!line.empty() && line.back()=='\r') line.pop_back();
            if(line.empty() || line[0]=='#') continue;
            std::istringstream fields(line);
            fields.imbue(std::locale::classic());
            std::string name,mode,extra;
            double t,v;
            keyframes::Mode m;
            if(!(fields>>name>>t>>v>>mode) || (fields>>extra) ||
               !keyframes::mode(mode,m) || !std::isfinite(v)) return false;
            auto it=std::find(names.begin(),names.end(),name);
            if(it==names.end()) return false;
            size_t i=it-names.begin();
            if((i<3 && (v<0 || v>4)) || (i==3 && (v<0 || v>1)) ||
               (i==4 && (v< -1 || v>1)) || !curves[i].append(t,v,m)) return false;
            any=true;
        }
        return any && in.eof() && !in.bad();
    }
    bool sample(double time,std::array<double,5> &v) const {
        v={{1,1,1,1,0}};
        for(size_t i=0;i<v.size();++i)
            if(!curves[i].keys.empty() && !curves[i].sample(time,v[i])) return false;
        return true;
    }
};
struct SequenceOutput {
    std::vector<std::string> created;
    bool committed=false;
    ~SequenceOutput() {
        if(!committed) for(const auto &path:created) std::remove(path.c_str());
    }
    bool write(const std::string &path,const unsigned char *data,size_t size) {
        created.push_back(path);
        FILE *out=mediaio::openNew(path.c_str());
        if(!out) { created.pop_back(); return false; }
        bool ok=std::fwrite(data,1,size,out)==size;
        if(std::fclose(out)!=0) ok=false;
        return ok;
    }
};

struct RasterPixels {
    size_t width=0,height=0;
    std::vector<unsigned char> rgba;
    bool load(const std::string &path) {
        auto input=makeWand();
        if(!read(input.get(),path)) return false;
        size_t w=MagickGetImageWidth(input.get()),h=MagickGetImageHeight(input.get());
        if(!w || !h || w>8192 || h>8192 || w*h>16777216) return false;
        if(MagickTransformImageColorspace(input.get(),sRGBColorspace)==MagickFalse) return false;
        std::vector<unsigned char> data(w*h*4);
        if(MagickExportImagePixels(input.get(),0,0,w,h,"RGBA",CharPixel,data.data())==MagickFalse)
            return false;
        width=w; height=h; rgba=std::move(data);
        return true;
    }
    bool loaded() const { return !rgba.empty(); }
    bool render(const std::array<double,5> &v,const std::string &fmt,
                SequenceOutput &outputs,const std::string &path) const {
        if(!loaded() || fmt.empty()) return false;
        std::vector<unsigned char> pixels(rgba.size());
        for(size_t j=0;j<rgba.size();j+=4) {
            for(int c=0;c<3;++c) {
                double x=rgba[j+c]*v[c]+255*v[4];
                pixels[j+c]=static_cast<unsigned char>(std::lround(std::max(0.0,std::min(255.0,x))));
            }
            pixels[j+3]=static_cast<unsigned char>(std::lround(rgba[j+3]*v[3]));
        }
        auto frame=makeWand();
        if(!frame || MagickConstituteImage(frame.get(),width,height,"RGBA",CharPixel,pixels.data())==MagickFalse ||
           MagickSetImageDepth(frame.get(),8)==MagickFalse ||
           MagickSetFormat(frame.get(),fmt.c_str())==MagickFalse ||
           MagickSetImageFormat(frame.get(),fmt.c_str())==MagickFalse) return false;
        size_t size=0;
        auto *blob=MagickGetImageBlob(frame.get(),&size);
        if(!blob) return false;
        std::unique_ptr<unsigned char,decltype(&MagickRelinquishMemory)> data(blob,MagickRelinquishMemory);
        return outputs.write(path,blob,size);
    }
};

bool animateRaster(const std::string &source,const std::string &manifest,
                   const std::string &prefix,int count,int fps) {
    RasterAnimation animation;
    if(prefix.empty() || !animation.load(manifest)) return false;
    RasterPixels input;
    if(!input.load(source)) return false;
    SequenceOutput outputs;
    outputs.created.reserve(count);
    for(int i=0;i<count;++i) {
        std::array<double,5> v;
        if(!animation.sample(static_cast<double>(i)/fps,v)) return false;
        char suffix[32];
        std::snprintf(suffix,sizeof(suffix),"%06d.png",i);
        if(!input.render(v,"PNG",outputs,prefix+suffix)) return false;
    }
    outputs.committed=true;
    return true;
}

}
CK_DLL_SFUN(raster_animate) {
    RETURN->v_int=0;
    try {
        auto source=GET_NEXT_STRING_SAFE(ARGS);
        auto manifest=GET_NEXT_STRING_SAFE(ARGS);
        auto prefix=GET_NEXT_STRING_SAFE(ARGS);
        auto count=GET_NEXT_INT(ARGS),fps=GET_NEXT_INT(ARGS);
        if(count<1 || count>10000 || fps<1 || fps>120) return;
        if(animateRaster(source,manifest,prefix,static_cast<int>(count),static_cast<int>(fps)))
            RETURN->v_int=1;
    } catch(...) {}
}


namespace {
struct RasterImageState {
    RasterPixels input;
    std::array<double,5> controls{{1,1,1,1,0}};
    void reset() { controls={{1,1,1,1,0}}; }
};
t_CKUINT rasterImageOffset=0;
RasterImageState *imageState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<RasterImageState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,rasterImageOffset)));
}
double imageControl(RasterImageState *state,size_t i) {
    return state?state->controls[i]:std::numeric_limits<double>::quiet_NaN();
}
double setImageControl(RasterImageState *state,size_t i,double value) {
    if(!state || !std::isfinite(value) ||
       (i<3 && (value<0 || value>4)) ||
       (i==3 && (value<0 || value>1)) ||
       (i==4 && (value< -1 || value>1)))
        return std::numeric_limits<double>::quiet_NaN();
    return state->controls[i]=value;
}
}
CK_DLL_CTOR(raster_image_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold native pointer");
    OBJ_MEMBER_UINT(SELF,rasterImageOffset)=0;
    auto *p=new(std::nothrow) RasterImageState;
    OBJ_MEMBER_UINT(SELF,rasterImageOffset)=reinterpret_cast<uintptr_t>(p);
}
CK_DLL_DTOR(raster_image_dtor) {
    delete imageState(SELF,API);
    OBJ_MEMBER_UINT(SELF,rasterImageOffset)=0;
}
CK_DLL_MFUN(raster_image_load) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->input.load(path)) {
            p->reset();
            RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_save) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        SequenceOutput output;
        if(p && p->input.render(p->controls,format(path),output,path)) {
            output.committed=true;
            RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_loaded) {
    auto *p=imageState(SELF,API);
    RETURN->v_int=p && p->input.loaded();
}
CK_DLL_MFUN(raster_image_width) {
    auto *p=imageState(SELF,API);
    RETURN->v_int=p?static_cast<t_CKINT>(p->input.width):0;
}
CK_DLL_MFUN(raster_image_height) {
    auto *p=imageState(SELF,API);
    RETURN->v_int=p?static_cast<t_CKINT>(p->input.height):0;
}
CK_DLL_MFUN(raster_image_clear) {
    if(auto *p=imageState(SELF,API)) {
        p->input=RasterPixels{};
        p->reset();
    }
}
CK_DLL_MFUN(raster_image_reset) {
    if(auto *p=imageState(SELF,API)) p->reset();
}
CK_DLL_MFUN(raster_image_get_red) {
    RETURN->v_float=imageControl(imageState(SELF,API),0);
}
CK_DLL_MFUN(raster_image_set_red) {
    RETURN->v_float=setImageControl(imageState(SELF,API),0,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(raster_image_get_green) {
    RETURN->v_float=imageControl(imageState(SELF,API),1);
}
CK_DLL_MFUN(raster_image_set_green) {
    RETURN->v_float=setImageControl(imageState(SELF,API),1,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(raster_image_get_blue) {
    RETURN->v_float=imageControl(imageState(SELF,API),2);
}
CK_DLL_MFUN(raster_image_set_blue) {
    RETURN->v_float=setImageControl(imageState(SELF,API),2,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(raster_image_get_opacity) {
    RETURN->v_float=imageControl(imageState(SELF,API),3);
}
CK_DLL_MFUN(raster_image_set_opacity) {
    RETURN->v_float=setImageControl(imageState(SELF,API),3,GET_NEXT_FLOAT(ARGS));
}
CK_DLL_MFUN(raster_image_get_brightness) {
    RETURN->v_float=imageControl(imageState(SELF,API),4);
}
CK_DLL_MFUN(raster_image_set_brightness) {
    RETURN->v_float=setImageControl(imageState(SELF,API),4,GET_NEXT_FLOAT(ARGS));
}


namespace {
bool rasterDimensions(t_CKINT w,t_CKINT h) {
    return w>0 && h>0 && w<=8192 && h<=8192 && w*h<=16777216;
}
bool rasterPosition(RasterImageState *p,t_CKINT x,t_CKINT y) {
    return p && p->input.loaded() && x>=0 && y>=0 &&
        static_cast<uint64_t>(x)<p->input.width && static_cast<uint64_t>(y)<p->input.height;
}
}
CK_DLL_MFUN(raster_image_create) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        if(!p || !rasterDimensions(w,h)) return;
        RasterPixels next;
        next.width=w; next.height=h;
        next.rgba.resize(static_cast<size_t>(w)*h*4,0);
        p->input=std::move(next); p->reset(); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_import_pixels) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p || !a || !rasterDimensions(w,h)) return;
        size_t n=static_cast<size_t>(w)*h*4;
        if(API->object->array_int_size(a)!=static_cast<t_CKINT>(n)) return;
        RasterPixels next; next.width=w; next.height=h; next.rgba.resize(n);
        for(size_t i=0;i<n;++i) {
            auto v=API->object->array_int_get_idx(a,i);
            if(v<0 || v>255) return;
            next.rgba[i]=static_cast<unsigned char>(v);
        }
        p->input=std::move(next); p->reset(); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_export_pixels) {
    RETURN->v_int=0;
    auto *p=imageState(SELF,API);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
    if(!p || !a || !p->input.loaded() ||
       API->object->array_int_size(a)!=static_cast<t_CKINT>(p->input.rgba.size())) return;
    for(size_t i=0;i<p->input.rgba.size();++i) {
        size_t c=i%4;
        double v=p->input.rgba[i]*p->controls[c]+(c<3?255*p->controls[4]:0);
        API->object->array_int_set_idx(a,i,static_cast<t_CKINT>(std::lround(std::max(0.0,std::min(255.0,v)))));
    }
    RETURN->v_int=1;
}
CK_DLL_MFUN(raster_image_set_pixel) {
    RETURN->v_int=0;
    auto *p=imageState(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS);
    auto r=GET_NEXT_INT(ARGS),g=GET_NEXT_INT(ARGS),b=GET_NEXT_INT(ARGS),a=GET_NEXT_INT(ARGS);
    if(!rasterPosition(p,x,y) || r<0 || r>255 || g<0 || g>255 || b<0 || b>255 || a<0 || a>255) return;
    size_t i=(static_cast<size_t>(y)*p->input.width+x)*4;
    p->input.rgba[i]=r; p->input.rgba[i+1]=g; p->input.rgba[i+2]=b; p->input.rgba[i+3]=a;
    RETURN->v_int=1;
}
CK_DLL_MFUN(raster_image_pixel) {
    RETURN->v_int=-1;
    auto *p=imageState(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS);
    if(rasterPosition(p,x,y) && c>=0 && c<4)
        RETURN->v_int=p->input.rgba[(static_cast<size_t>(y)*p->input.width+x)*4+c];
}


namespace {
using RGBA=std::array<unsigned char,4>;
bool readColor(void *&args,RGBA &color) {
    for(auto &c:color) {
        auto v=GET_NEXT_INT(args);
        if(v<0 || v>255) return false;
        c=static_cast<unsigned char>(v);
    }
    return true;
}
void paint(RasterPixels &p,int x,int y,const RGBA &c) {
    auto i=(static_cast<size_t>(y)*p.width+x)*4;
    std::copy(c.begin(),c.end(),p.rgba.begin()+i);
}
bool insideRect(RasterImageState *p,t_CKINT x,t_CKINT y,t_CKINT w,t_CKINT h) {
    return rasterPosition(p,x,y) && w>0 && h>0 &&
        static_cast<uint64_t>(w)<=p->input.width-x &&
        static_cast<uint64_t>(h)<=p->input.height-y;
}
}
CK_DLL_MFUN(raster_image_fill) {
    RETURN->v_int=0;
    auto *p=imageState(SELF,API);
    RGBA c;
    if(!p || !p->input.loaded() || !readColor(ARGS,c)) return;
    for(size_t i=0;i<p->input.rgba.size();i+=4)
        std::copy(c.begin(),c.end(),p->input.rgba.begin()+i);
    RETURN->v_int=1;
}
CK_DLL_MFUN(raster_image_rect) {
    RETURN->v_int=0;
    auto *p=imageState(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
    RGBA c;
    if(!insideRect(p,x,y,w,h) || !readColor(ARGS,c)) return;
    for(int j=y;j<y+h;++j) for(int i=x;i<x+w;++i) paint(p->input,i,j,c);
    RETURN->v_int=1;
}
CK_DLL_MFUN(raster_image_line) {
    RETURN->v_int=0;
    auto *p=imageState(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS),x2=GET_NEXT_INT(ARGS),y2=GET_NEXT_INT(ARGS);
    RGBA c;
    if(!rasterPosition(p,x,y) || !rasterPosition(p,x2,y2) || !readColor(ARGS,c)) return;
    int dx=std::abs(static_cast<int>(x2-x)),dy=-std::abs(static_cast<int>(y2-y));
    int sx=x<x2?1:-1,sy=y<y2?1:-1,err=dx+dy;
    for(;;) {
        paint(p->input,x,y,c);
        if(x==x2 && y==y2) break;
        int e=2*err;
        if(e>=dy) { err+=dy; x+=sx; }
        if(e<=dx) { err+=dx; y+=sy; }
    }
    RETURN->v_int=1;
}
CK_DLL_MFUN(raster_image_circle) {
    RETURN->v_int=0;
    auto *p=imageState(SELF,API);
    auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS),radius=GET_NEXT_INT(ARGS);
    RGBA c;
    if(!rasterPosition(p,x,y) || radius<0 || radius>8192 || !readColor(ARGS,c)) return;
    int loX=std::max<int>(0,x-radius),hiX=std::min<int>(p->input.width-1,x+radius);
    int loY=std::max<int>(0,y-radius),hiY=std::min<int>(p->input.height-1,y+radius);
    for(int j=loY;j<=hiY;++j) for(int i=loX;i<=hiX;++i) {
        int64_t dx=i-x,dy=j-y;
        if(dx*dx+dy*dy<=radius*radius) paint(p->input,i,j,c);
    }
    RETURN->v_int=1;
}
CK_DLL_MFUN(raster_image_crop) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        if(!insideRect(p,x,y,w,h)) return;
        RasterPixels next; next.width=w; next.height=h; next.rgba.resize(static_cast<size_t>(w)*h*4);
        for(int j=0;j<h;++j)
            std::copy_n(p->input.rgba.begin()+((static_cast<size_t>(y)+j)*p->input.width+x)*4,
                        w*4,next.rgba.begin()+static_cast<size_t>(j)*w*4);
        p->input=std::move(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_resize) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        if(!p || !p->input.loaded() || !rasterDimensions(w,h)) return;
        RasterPixels next; next.width=w; next.height=h; next.rgba.resize(static_cast<size_t>(w)*h*4);
        for(size_t y=0;y<next.height;++y) for(size_t x=0;x<next.width;++x) {
            size_t src=((y*p->input.height/next.height)*p->input.width+x*p->input.width/next.width)*4;
            std::copy_n(p->input.rgba.begin()+src,4,next.rgba.begin()+(y*next.width+x)*4);
        }
        p->input=std::move(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_composite) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto x=GET_NEXT_INT(ARGS),y=GET_NEXT_INT(ARGS),w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto arr=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        auto opacity=GET_NEXT_FLOAT(ARGS);
        if(!p || !p->input.loaded() || !arr || !rasterDimensions(w,h) ||
           x< -8192 || x>8192 || y< -8192 || y>8192 ||
           !std::isfinite(opacity) || opacity<0 || opacity>1 ||
           API->object->array_int_size(arr)!=w*h*4) return;
        for(t_CKINT i=0;i<w*h*4;++i) {
            auto v=API->object->array_int_get_idx(arr,i);
            if(v<0 || v>255) return;
        }
        for(int j=std::max<t_CKINT>(0,-y);j<h && y+j<static_cast<t_CKINT>(p->input.height);++j)
            for(int i=std::max<t_CKINT>(0,-x);i<w && x+i<static_cast<t_CKINT>(p->input.width);++i) {
                size_t src=(static_cast<size_t>(j)*w+i)*4;
                size_t dst=((static_cast<size_t>(y+j)*p->input.width)+x+i)*4;
                double a=API->object->array_int_get_idx(arr,src+3)/255.0*opacity;
                if(a==0) continue;
                double b=p->input.rgba[dst+3]/255.0*(1-a),out=a+b;
                for(int c=0;c<3;++c) p->input.rgba[dst+c]=static_cast<unsigned char>(
                    std::lround(std::clamp((API->object->array_int_get_idx(arr,src+c)*a+p->input.rgba[dst+c]*b)/out,0.0,255.0)));
                p->input.rgba[dst+3]=static_cast<unsigned char>(std::lround(std::clamp(out*255,0.0,255.0)));
            }
        RETURN->v_int=1;
    } catch(...) {}
}

CK_DLL_MFUN(raster_image_audio_samples) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto channels=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->input.loaded()||!a||(channels!=1&&channels!=2)) return;
        size_t count=p->input.rgba.size()/4;
        if(API->object->array_float_size(a)!=static_cast<t_CKINT>(count*channels)) return;
        auto component=[&](size_t i,int c) {
            double v=p->input.rgba[4*i+c]*p->controls[c]+(c<3?255*p->controls[4]:0);
            return std::lround(std::clamp(v,0.0,255.0))/255.0;
        };
        for(size_t i=0;i<count;++i) {
            double red=component(i,0),green=component(i,1),blue=component(i,2),alpha=component(i,3);
            double lum=0.2126*red+0.7152*green+0.0722*blue;
            if(channels==1) API->object->array_float_set_idx(a,i,std::clamp(2*lum-1,-1.0,1.0)*alpha);
            else {
                double left=(red+green)*0.5,right=(blue+green)*0.5;
                API->object->array_float_set_idx(a,2*i,(2*left-1)*alpha);
                API->object->array_float_set_idx(a,2*i+1,(2*right-1)*alpha);
            }
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_heightmap) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayFloat>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->input.loaded()||!a) return;
        size_t n=p->input.rgba.size()/4;
        if(API->object->array_float_size(a)!=static_cast<t_CKINT>(n)) return;
        for(size_t i=0;i<n;++i) {
            auto component=[&](int c) {
                double v=p->input.rgba[4*i+c]*p->controls[c]+(c<3?255*p->controls[4]:0);
                return std::lround(std::clamp(v,0.0,255.0))/255.0;
            };
            double v=0.2126*component(0)+0.7152*component(1)+0.0722*component(2);
            API->object->array_float_set_idx(a,i,std::clamp(v*component(3),0.0,1.0));
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_export_pitches) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto low=GET_NEXT_INT(ARGS),high=GET_NEXT_INT(ARGS),threshold=GET_NEXT_INT(ARGS),scan=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->input.loaded()||!a||low<0||high>127||low>high||
           threshold<1||threshold>255||scan<0||scan>2) return;
        size_t w=p->input.width,h=p->input.height,n=w*h;
        if(n>100000||API->object->array_int_size(a)!=static_cast<t_CKINT>(n)) return;
        for(size_t i=0;i<n;++i) {
            size_t x=scan==1?i/h:i%w,y=scan==1?i%h:i/w;
            if(scan==2&&y%2) x=w-1-x;
            size_t pos=(y*w+x)*4;
            auto component=[&](int c) {
                double v=p->input.rgba[pos+c]*p->controls[c]+(c<3?255*p->controls[4]:0);
                return std::lround(std::clamp(v,0.0,255.0));
            };
            t_CKINT pitch=-1;
            if(component(3)>=threshold) {
                double lum=(0.2126*component(0)+0.7152*component(1)+0.0722*component(2))/255.0;
                pitch=low+std::lround(std::clamp(lum,0.0,1.0)*(high-low));
            }
            API->object->array_int_set_idx(a,i,pitch);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_image_export_blend) {
    RETURN->v_int=0;
    try {
        auto *p=imageState(SELF,API);
        auto other=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        double t=GET_NEXT_FLOAT(ARGS);
        auto rgb=GET_NEXT_INT(ARGS);
        auto out=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!p->input.loaded()||!other||!out||!std::isfinite(t)||t<0||t>1||
           rgb<0||rgb>0xffffff) return;
        size_t n=p->input.rgba.size();
        if(n>16777216||API->object->array_int_size(other)!=static_cast<t_CKINT>(n)||
           API->object->array_int_size(out)!=static_cast<t_CKINT>(n)) return;
        for(size_t i=0;i<n;++i) {
            auto v=API->object->array_int_get_idx(other,i);
            if(v<0||v>255) return;
        }
        auto component=[&](size_t i,int c) {
            double v=p->input.rgba[i+c]*p->controls[c]+(c<3?255*p->controls[4]:0);
            return std::lround(std::clamp(v,0.0,255.0));
        };
        for(size_t i=0;i<n;i+=4) {
            double a=component(i,3)/255.0;
            double b=API->object->array_int_get_idx(other,i+3)/255.0;
            for(int c=0;c<3;++c) {
                double bg=(rgb>>(16-8*c))&255;
                double left=component(i,c)*a+bg*(1-a);
                double right=API->object->array_int_get_idx(other,i+c)*b+bg*(1-b);
                API->object->array_int_set_idx(out,i+c,std::lround(std::clamp(left*(1-t)+right*t,0.0,255.0)));
            }
            API->object->array_int_set_idx(out,i+3,255);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
namespace {
struct RasterCaptureState {
    media_capture::Frame frame;
};
t_CKUINT rasterCaptureOffset=0;
RasterCaptureState *captureState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<RasterCaptureState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,rasterCaptureOffset)));
}
}
CK_DLL_CTOR(raster_capture_ctor) {
    OBJ_MEMBER_UINT(SELF,rasterCaptureOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) RasterCaptureState);
}
CK_DLL_DTOR(raster_capture_dtor) {
    delete captureState(SELF,API); OBJ_MEMBER_UINT(SELF,rasterCaptureOffset)=0;
}
CK_DLL_MFUN(raster_capture_platform) {
    RETURN->v_int=static_cast<int>(media_capture::platform());
}
CK_DLL_MFUN(raster_capture_supported) {
    auto source=GET_NEXT_INT(ARGS);
    RETURN->v_int=source==1&&media_capture::cameraSupported();
}
CK_DLL_MFUN(raster_capture_devices) {
    auto source=GET_NEXT_INT(ARGS);
    RETURN->v_int=-1;
    try {
        if(source==2) { RETURN->v_int=0; return; }
        if(source==1) { std::vector<std::string> names; RETURN->v_int=media_capture::cameraDevices(names); }
    } catch(...) {}
}
CK_DLL_MFUN(raster_capture_device_name) {
    std::string name;
    try {
        auto source=GET_NEXT_INT(ARGS),index=GET_NEXT_INT(ARGS);
        std::vector<std::string> names;
        if(source==1&&index>=0&&media_capture::cameraDevices(names)>=0&&
           static_cast<uint64_t>(index)<names.size()) name=names[index];
    } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,name.c_str(),FALSE);
}
CK_DLL_MFUN(raster_capture_camera) {
    RETURN->v_int=0;
    try {
        auto *p=captureState(SELF,API); auto name=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        if(!p||!media_capture::Frame::dimensions(w,h)) return;
        media_capture::Frame next;
        if(!media_capture::cameraSnapshot(name,static_cast<int>(w),static_cast<int>(h),next)||!next.valid()) return;
        p->frame=std::move(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_capture_ingest) {
    RETURN->v_int=0;
    try {
        auto *p=captureState(SELF,API); auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        double seconds=GET_NEXT_FLOAT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!p||!a||!media_capture::Frame::dimensions(w,h)||!std::isfinite(seconds)||seconds<0) return;
        size_t n=static_cast<size_t>(w)*h*4;
        if(API->object->array_int_size(a)!=static_cast<t_CKINT>(n)) return;
        media_capture::Frame next; next.width=static_cast<int>(w); next.height=static_cast<int>(h); next.seconds=seconds;
        next.rgba.resize(n);
        for(size_t k=0;k<n;++k) {
            auto v=API->object->array_int_get_idx(a,k);
            if(v<0||v>255) return;
            next.rgba[k]=static_cast<unsigned char>(v);
        }
        p->frame=std::move(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(raster_capture_ready) {
    auto *p=captureState(SELF,API); RETURN->v_int=p&&p->frame.valid();
}
CK_DLL_MFUN(raster_capture_width) {
    auto *p=captureState(SELF,API); RETURN->v_int=p&&p->frame.valid()?p->frame.width:-1;
}
CK_DLL_MFUN(raster_capture_height) {
    auto *p=captureState(SELF,API); RETURN->v_int=p&&p->frame.valid()?p->frame.height:-1;
}
CK_DLL_MFUN(raster_capture_seconds) {
    auto *p=captureState(SELF,API); RETURN->v_float=p&&p->frame.valid()?p->frame.seconds:-1;
}
CK_DLL_MFUN(raster_capture_export) {
    RETURN->v_int=0;
    auto *p=captureState(SELF,API);
    auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
    if(!p||!p->frame.valid()||!a||API->object->array_int_size(a)!=static_cast<t_CKINT>(p->frame.rgba.size())) return;
    for(size_t k=0;k<p->frame.rgba.size();++k) API->object->array_int_set_idx(a,k,p->frame.rgba[k]);
    RETURN->v_int=1;
}
CK_DLL_MFUN(raster_capture_clear) {
    if(auto *p=captureState(SELF,API)) p->frame=media_capture::Frame{};
}
CK_DLL_QUERY(RasterIO) {
    QUERY->setname(QUERY, "RasterIO");
    QUERY->begin_class(QUERY, "RasterIO", "Object");
    QUERY->add_sfun(QUERY, raster_width, "int", "width");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, raster_height, "int", "height");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, raster_convert, "int", "convert");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_sfun(QUERY,raster_animate,"int","animate");
    QUERY->add_arg(QUERY,"string","source");
    QUERY->add_arg(QUERY,"string","animationFile");
    QUERY->add_arg(QUERY,"string","outputPrefix");
    QUERY->add_arg(QUERY,"int","frameCount");
    QUERY->add_arg(QUERY,"int","fps");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"RasterCapture","Object");
    rasterCaptureOffset=QUERY->add_mvar(QUERY,"int","@rasterCaptureNative",FALSE);
    if(rasterCaptureOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,raster_capture_ctor);
    QUERY->add_dtor(QUERY,raster_capture_dtor);
    QUERY->add_mfun(QUERY,raster_capture_platform,"int","platform");
    QUERY->add_mfun(QUERY,raster_capture_supported,"int","supported");
    QUERY->add_arg(QUERY,"int","source");
    QUERY->add_mfun(QUERY,raster_capture_devices,"int","deviceCount");
    QUERY->add_arg(QUERY,"int","source");
    QUERY->add_mfun(QUERY,raster_capture_device_name,"string","deviceName");
    QUERY->add_arg(QUERY,"int","source");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,raster_capture_camera,"int","captureCamera");
    QUERY->add_arg(QUERY,"string","name");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_mfun(QUERY,raster_capture_ingest,"int","ingestFrame");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"float","seconds");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,raster_capture_ready,"int","ready");
    QUERY->add_mfun(QUERY,raster_capture_width,"int","width");
    QUERY->add_mfun(QUERY,raster_capture_height,"int","height");
    QUERY->add_mfun(QUERY,raster_capture_seconds,"float","timestamp");
    QUERY->add_mfun(QUERY,raster_capture_export,"int","exportPixels");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,raster_capture_clear,"void","clear");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"RasterImage","Object");
    rasterImageOffset=QUERY->add_mvar(QUERY,"int","@rasterImageNative",FALSE);
    if(rasterImageOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,raster_image_ctor);
    QUERY->add_dtor(QUERY,raster_image_dtor);
    QUERY->add_mfun(QUERY,raster_image_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,raster_image_save,"int","save");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,raster_image_loaded,"int","loaded");
    QUERY->add_mfun(QUERY,raster_image_width,"int","width");
    QUERY->add_mfun(QUERY,raster_image_height,"int","height");
    QUERY->add_mfun(QUERY,raster_image_clear,"void","clear");
    QUERY->add_mfun(QUERY,raster_image_reset,"void","reset");
    QUERY->add_mfun(QUERY,raster_image_get_red,"float","red");
    QUERY->add_mfun(QUERY,raster_image_set_red,"float","red");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,raster_image_get_green,"float","green");
    QUERY->add_mfun(QUERY,raster_image_set_green,"float","green");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,raster_image_get_blue,"float","blue");
    QUERY->add_mfun(QUERY,raster_image_set_blue,"float","blue");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,raster_image_get_opacity,"float","opacity");
    QUERY->add_mfun(QUERY,raster_image_set_opacity,"float","opacity");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,raster_image_get_brightness,"float","brightness");
    QUERY->add_mfun(QUERY,raster_image_set_brightness,"float","brightness");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,raster_image_create,"int","create");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_mfun(QUERY,raster_image_import_pixels,"int","importPixels");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,raster_image_export_blend,"int","exportBlend");
    QUERY->add_arg(QUERY,"int[]","targetPixels");
    QUERY->add_arg(QUERY,"float","mix");
    QUERY->add_arg(QUERY,"int","backgroundRGB");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,raster_image_export_pitches,"int","exportPitches");
    QUERY->add_arg(QUERY,"int","lowPitch");
    QUERY->add_arg(QUERY,"int","highPitch");
    QUERY->add_arg(QUERY,"int","alphaThreshold");
    QUERY->add_arg(QUERY,"int","scan");
    QUERY->add_arg(QUERY,"int[]","pitches");
    QUERY->add_mfun(QUERY,raster_image_heightmap,"int","exportHeightmap");
    QUERY->add_arg(QUERY,"float[]","heights");
    QUERY->add_mfun(QUERY,raster_image_audio_samples,"int","exportAudioSamples");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"float[]","samples");
    QUERY->add_mfun(QUERY,raster_image_export_pixels,"int","exportPixels");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,raster_image_set_pixel,"int","setPixel");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","red");
    QUERY->add_arg(QUERY,"int","green");
    QUERY->add_arg(QUERY,"int","blue");
    QUERY->add_arg(QUERY,"int","alpha");
    QUERY->add_mfun(QUERY,raster_image_pixel,"int","pixelAt");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","channel");
    QUERY->add_mfun(QUERY,raster_image_fill,"int","fill");
    QUERY->add_arg(QUERY,"int","red");
    QUERY->add_arg(QUERY,"int","green");
    QUERY->add_arg(QUERY,"int","blue");
    QUERY->add_arg(QUERY,"int","alpha");
    QUERY->add_mfun(QUERY,raster_image_rect,"int","fillRect");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","red");
    QUERY->add_arg(QUERY,"int","green");
    QUERY->add_arg(QUERY,"int","blue");
    QUERY->add_arg(QUERY,"int","alpha");
    QUERY->add_mfun(QUERY,raster_image_line,"int","line");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","x2");
    QUERY->add_arg(QUERY,"int","y2");
    QUERY->add_arg(QUERY,"int","red");
    QUERY->add_arg(QUERY,"int","green");
    QUERY->add_arg(QUERY,"int","blue");
    QUERY->add_arg(QUERY,"int","alpha");
    QUERY->add_mfun(QUERY,raster_image_circle,"int","fillCircle");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","radius");
    QUERY->add_arg(QUERY,"int","red");
    QUERY->add_arg(QUERY,"int","green");
    QUERY->add_arg(QUERY,"int","blue");
    QUERY->add_arg(QUERY,"int","alpha");
    QUERY->add_mfun(QUERY,raster_image_crop,"int","crop");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_mfun(QUERY,raster_image_resize,"int","resize");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_mfun(QUERY,raster_image_composite,"int","composite");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_arg(QUERY,"float","opacity");
    return QUERY->end_class(QUERY);
}
