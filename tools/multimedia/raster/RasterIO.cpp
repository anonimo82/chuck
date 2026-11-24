// Optional offline raster I/O for ChucK; no VM changes.
#include "chuck_dl.h"
#include "../file_compat.h"
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
bool read(MagickWand *w, const std::string &path) {
    const auto fmt = format(path);
    if(fmt.empty() || !w) return false;
    // Explicit format and FILE* avoid ImageMagick filename pseudo-protocols.
    std::unique_ptr<FILE, decltype(&fclose)> f(fopen(path.c_str(), "rb"), fclose);
    return f && MagickSetFormat(w, fmt.c_str()) != MagickFalse
             && MagickReadImageFile(w, f.get()) != MagickFalse
             && MagickSetIteratorIndex(w, 0) != MagickFalse;
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
        if(MagickSetImageFormat(w.get(), fmt.c_str()) == MagickFalse) return;
        size_t size = 0;
        unsigned char *blob = MagickGetImageBlob(w.get(), &size);
        if(!blob) return;
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
    return QUERY->end_class(QUERY);
}
