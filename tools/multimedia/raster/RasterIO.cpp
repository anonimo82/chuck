// Optional offline raster I/O for ChucK; no VM changes.
#include "chuck_dl.h"
#include "../file_compat.h"
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
    return QUERY->end_class(QUERY);
}
