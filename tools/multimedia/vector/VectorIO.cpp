// Optional SVG I/O using librsvg and Cairo; synchronous, offline operations.
#include "chuck_dl.h"
#include "../file_compat.h"
#include <librsvg/rsvg.h>
#include <cairo.h>
#include <cstdio>
#include <memory>
#include <string>

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
    return QUERY->end_class(QUERY);
}
