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
bool animateRaster(const std::string &source,const std::string &manifest,
                   const std::string &prefix,int count,int fps) {
    RasterAnimation animation;
    if(prefix.empty() || !animation.load(manifest)) return false;
    auto input=makeWand();
    if(!read(input.get(),source)) return false;
    size_t w=MagickGetImageWidth(input.get()),h=MagickGetImageHeight(input.get());
    if(!w || !h || w>8192 || h>8192 || w*h>16777216) return false;
    // Convert once to sRGB; work with straight-alpha RGBA bytes.
    if(MagickTransformImageColorspace(input.get(),sRGBColorspace)==MagickFalse) return false;
    std::vector<unsigned char> original(w*h*4),pixels(original.size());
    if(MagickExportImagePixels(input.get(),0,0,w,h,"RGBA",CharPixel,original.data())==MagickFalse)
        return false;
    SequenceOutput outputs;
    outputs.created.reserve(count);
    for(int i=0;i<count;++i) {
        std::array<double,5> v;
        if(!animation.sample(static_cast<double>(i)/fps,v)) return false;
        for(size_t j=0;j<original.size();j+=4) {
            for(int c=0;c<3;++c) {
                double x=original[j+c]*v[c]+255*v[4];
                pixels[j+c]=static_cast<unsigned char>(std::lround(std::max(0.0,std::min(255.0,x))));
            }
            pixels[j+3]=static_cast<unsigned char>(std::lround(original[j+3]*v[3]));
        }
        auto frame=makeWand();
        if(!frame || MagickConstituteImage(frame.get(),w,h,"RGBA",CharPixel,pixels.data())==MagickFalse ||
           MagickSetImageDepth(frame.get(),8)==MagickFalse ||
           MagickSetFormat(frame.get(),"PNG")==MagickFalse ||
           MagickSetImageFormat(frame.get(),"PNG")==MagickFalse) return false;
        size_t size=0;
        auto *blob=MagickGetImageBlob(frame.get(),&size);
        if(!blob) return false;
        std::unique_ptr<unsigned char,decltype(&MagickRelinquishMemory)> data(blob,MagickRelinquishMemory);
        char suffix[32];
        std::snprintf(suffix,sizeof(suffix),"%06d.png",i);
        if(!outputs.write(prefix+suffix,blob,size)) return false;
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
    return QUERY->end_class(QUERY);
}
