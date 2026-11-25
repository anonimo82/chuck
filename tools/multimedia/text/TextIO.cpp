// Procedural text foundation; offline Pango/Cairo layout, no markup parser.
#include "chuck_dl.h"
#include <pango/pangocairo.h>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdint>
#include <new>
#include <utility>
#include <limits>

namespace {
struct Style {
    std::string font="Sans";
    double size=16;
    int weight=400,italic=0,underline=0,strikethrough=0,script=0;
    t_CKINT rgb=0,background=-1;
    double spacing=0,rise=0,opacity=1;
};
struct Paragraph { std::string text; Style style; };
struct Document { std::vector<Paragraph> paragraphs; size_t bytes=0; };
t_CKUINT styleOffset=0,docOffset=0;
Style *styleState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<Style *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,styleOffset))):nullptr;
}
Document *docState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<Document *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,docOffset))):nullptr;
}
bool validText(const std::string &s,size_t limit) {
    return s.size()<=limit && s.find('\0')==std::string::npos &&
        g_utf8_validate(s.data(),static_cast<gssize>(s.size()),nullptr);
}
bool indexOK(Document *d,t_CKINT i) {
    return d&&i>=0&&static_cast<uint64_t>(i)<d->paragraphs.size();
}
}
CK_DLL_CTOR(text_style_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold pointer");
    OBJ_MEMBER_UINT(SELF,styleOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,styleOffset)=reinterpret_cast<uintptr_t>(new Style); } catch(...) {}
}
CK_DLL_DTOR(text_style_dtor) { delete styleState(SELF,API); OBJ_MEMBER_UINT(SELF,styleOffset)=0; }
CK_DLL_CTOR(text_doc_ctor) {
    OBJ_MEMBER_UINT(SELF,docOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,docOffset)=reinterpret_cast<uintptr_t>(new Document); } catch(...) {}
}
CK_DLL_DTOR(text_doc_dtor) { delete docState(SELF,API); OBJ_MEMBER_UINT(SELF,docOffset)=0; }
CK_DLL_MFUN(text_style_font_get) {
    auto *p=styleState(SELF,API);
    RETURN->v_string=API->object->create_string(VM,p?p->font.c_str():"",FALSE);
}
CK_DLL_MFUN(text_style_font_set) {
    RETURN->v_string=API->object->create_string(VM,"",FALSE);
    try {
        auto *p=styleState(SELF,API); auto v=GET_NEXT_STRING_SAFE(ARGS);
        if(p&&!v.empty()&&validText(v,256)) { p->font=v; RETURN->v_string=API->object->create_string(VM,p->font.c_str(),FALSE); }
    } catch(...) {}
}
CK_DLL_MFUN(text_style_size_get) {
    auto *p=styleState(SELF,API); RETURN->v_float=p?p->size:-1;
}
CK_DLL_MFUN(text_style_size_set) {
    auto *p=styleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=-1;
    if(p&&std::isfinite(v)&&v>=1&&v<=512) RETURN->v_float=p->size=v;
}
#define TEXT_INT_PROPERTY(NAME,MEMBER,MAXIMUM) \
CK_DLL_MFUN(text_style_##NAME##_get) { auto *p=styleState(SELF,API); RETURN->v_int=p?p->MEMBER:-1; } \
CK_DLL_MFUN(text_style_##NAME##_set) { auto *p=styleState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1; if(p&&v>=0&&v<=MAXIMUM) RETURN->v_int=p->MEMBER=v; }
TEXT_INT_PROPERTY(italic,italic,1)
TEXT_INT_PROPERTY(underline,underline,1)
TEXT_INT_PROPERTY(color,rgb,0xffffff)
CK_DLL_MFUN(text_style_bold_get) {
    auto *p=styleState(SELF,API); RETURN->v_int=p?(p->weight>=700):-1;
}
CK_DLL_MFUN(text_style_bold_set) {
    auto *p=styleState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&(v==0||v==1)) { p->weight=v?700:400; RETURN->v_int=v; }
}
CK_DLL_MFUN(text_style_weight_get) {
    auto *p=styleState(SELF,API); RETURN->v_int=p?p->weight:-1;
}
CK_DLL_MFUN(text_style_weight_set) {
    auto *p=styleState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=100&&v<=1000) RETURN->v_int=p->weight=v;
}
TEXT_INT_PROPERTY(strikethrough,strikethrough,1)
TEXT_INT_PROPERTY(script,script,2)
CK_DLL_MFUN(text_style_backgroundColor_get) {
    auto *p=styleState(SELF,API); RETURN->v_int=p?p->background:-1;
}
CK_DLL_MFUN(text_style_backgroundColor_set) {
    auto *p=styleState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-2;
    if(p&&v>=-1&&v<=0xffffff) RETURN->v_int=p->background=v;
}
CK_DLL_MFUN(text_style_letterSpacing_get) {
    auto *p=styleState(SELF,API); RETURN->v_float=p?p->spacing:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_style_letterSpacing_set) {
    auto *p=styleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=-32&&v<=128) RETURN->v_float=p->spacing=v;
}
CK_DLL_MFUN(text_style_baselineShift_get) {
    auto *p=styleState(SELF,API); RETURN->v_float=p?p->rise:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_style_baselineShift_set) {
    auto *p=styleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=-512&&v<=512) RETURN->v_float=p->rise=v;
}
CK_DLL_MFUN(text_style_opacity_get) {
    auto *p=styleState(SELF,API); RETURN->v_float=p?p->opacity:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_style_opacity_set) {
    auto *p=styleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=1) RETURN->v_float=p->opacity=v;
}
CK_DLL_MFUN(text_style_inherit) {
    RETURN->v_int=0;
    try {
        auto *p=styleState(SELF,API); auto *other=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(p&&other) { Style copy=*other; *p=std::move(copy); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_add) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto s=GET_NEXT_STRING_SAFE(ARGS);
        auto *p=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(!d||!p||d->paragraphs.size()>=4096||!validText(s,65536)||d->bytes+s.size()>1048576) return;
        d->paragraphs.push_back({s,*p}); d->bytes+=s.size(); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_count) {
    auto *d=docState(SELF,API); RETURN->v_int=d?static_cast<t_CKINT>(d->paragraphs.size()):0;
}
CK_DLL_MFUN(text_doc_text) {
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS);
    RETURN->v_string=API->object->create_string(VM,indexOK(d,i)?d->paragraphs[i].text.c_str():"",FALSE);
}
CK_DLL_MFUN(text_doc_replace) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto s=GET_NEXT_STRING_SAFE(ARGS);
        if(!indexOK(d,i)||!validText(s,65536)||d->bytes-d->paragraphs[i].text.size()+s.size()>1048576) return;
        auto old=d->paragraphs[i].text.size(),next=s.size();
        d->paragraphs[i].text.swap(s); d->bytes=d->bytes-old+next; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_style) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *p=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(indexOK(d,i)&&p) { Style copy=*p; d->paragraphs[i].style=std::move(copy); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_remove) {
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); RETURN->v_int=0;
    if(indexOK(d,i)) { d->bytes-=d->paragraphs[i].text.size(); d->paragraphs.erase(d->paragraphs.begin()+i); RETURN->v_int=1; }
}
CK_DLL_MFUN(text_doc_clear) {
    if(auto *d=docState(SELF,API)) { d->paragraphs.clear(); d->bytes=0; }
}
CK_DLL_MFUN(text_doc_measure) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS),width=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!indexOK(d,i)||width<1||width>4096||!a||API->object->array_int_size(a)!=2) return;
        auto &p=d->paragraphs[i];
        std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)> surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,1,1),cairo_surface_destroy);
        if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return;
        std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
        if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return;
        std::unique_ptr<PangoLayout,decltype(&g_object_unref)> layout(pango_cairo_create_layout(cr.get()),g_object_unref);
        if(!layout) return;
        std::unique_ptr<PangoFontDescription,decltype(&pango_font_description_free)> font(pango_font_description_new(),pango_font_description_free);
        if(!font) return;
        pango_font_description_set_family(font.get(),p.style.font.c_str());
        pango_font_description_set_absolute_size(font.get(),p.style.size*PANGO_SCALE);
        pango_font_description_set_weight(font.get(),static_cast<PangoWeight>(p.style.weight));
        pango_font_description_set_style(font.get(),p.style.italic?PANGO_STYLE_ITALIC:PANGO_STYLE_NORMAL);
        pango_layout_set_font_description(layout.get(),font.get());
        pango_layout_set_text(layout.get(),p.text.data(),static_cast<int>(p.text.size()));
        pango_layout_set_width(layout.get(),width*PANGO_SCALE);
        pango_layout_set_wrap(layout.get(),PANGO_WRAP_WORD_CHAR);
        std::unique_ptr<PangoAttrList,decltype(&pango_attr_list_unref)> attrs(pango_attr_list_new(),pango_attr_list_unref);
        if(!attrs) return;
        auto add=[&](PangoAttribute *attr) { pango_attr_list_insert(attrs.get(),attr); };
        auto rgb16=[](t_CKINT rgb,int shift) { return static_cast<guint16>(((rgb>>shift)&255)*257); };
        add(pango_attr_foreground_new(rgb16(p.style.rgb,16),rgb16(p.style.rgb,8),rgb16(p.style.rgb,0)));
        add(pango_attr_foreground_alpha_new(static_cast<guint16>(std::lround(p.style.opacity*65535))));
        if(p.style.background>=0) {
            add(pango_attr_background_new(rgb16(p.style.background,16),rgb16(p.style.background,8),rgb16(p.style.background,0)));
            add(pango_attr_background_alpha_new(static_cast<guint16>(std::lround(p.style.opacity*65535))));
        }
        if(p.style.underline) add(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
        if(p.style.strikethrough) add(pango_attr_strikethrough_new(TRUE));
        if(p.style.spacing!=0) add(pango_attr_letter_spacing_new(static_cast<int>(std::lround(p.style.spacing*PANGO_SCALE))));
        double rise=p.style.rise;
        if(p.style.script!=0) {
            add(pango_attr_scale_new(0.7));
            rise+=(p.style.script==1?0.35:-0.2)*p.style.size;
        }
        if(rise!=0) add(pango_attr_rise_new(static_cast<int>(std::lround(rise*PANGO_SCALE))));
        pango_layout_set_attributes(layout.get(),attrs.get());
        int w=0,h=0; pango_layout_get_pixel_size(layout.get(),&w,&h);
        API->object->array_int_set_idx(a,0,w); API->object->array_int_set_idx(a,1,h);
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_QUERY(TextIO) {
    QUERY->setname(QUERY,"TextIO");
    QUERY->begin_class(QUERY,"TextIO","Object");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextStyle","Object");
    styleOffset=QUERY->add_mvar(QUERY,"int","@textStyleNative",FALSE);
    if(styleOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_style_ctor); QUERY->add_dtor(QUERY,text_style_dtor);
    QUERY->add_mfun(QUERY,text_style_font_get,"string","font");
    QUERY->add_mfun(QUERY,text_style_font_set,"string","font");
    QUERY->add_arg(QUERY,"string","value");
    QUERY->add_mfun(QUERY,text_style_size_get,"float","fontSize");
    QUERY->add_mfun(QUERY,text_style_size_set,"float","fontSize");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_style_bold_get,"int","bold");
    QUERY->add_mfun(QUERY,text_style_bold_set,"int","bold");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_italic_get,"int","italic");
    QUERY->add_mfun(QUERY,text_style_italic_set,"int","italic");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_underline_get,"int","underline");
    QUERY->add_mfun(QUERY,text_style_underline_set,"int","underline");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_color_get,"int","color");
    QUERY->add_mfun(QUERY,text_style_color_set,"int","color");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_weight_get,"int","weight");
    QUERY->add_mfun(QUERY,text_style_weight_set,"int","weight");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_strikethrough_get,"int","strikethrough");
    QUERY->add_mfun(QUERY,text_style_strikethrough_set,"int","strikethrough");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_backgroundColor_get,"int","backgroundColor");
    QUERY->add_mfun(QUERY,text_style_backgroundColor_set,"int","backgroundColor");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_letterSpacing_get,"float","letterSpacing");
    QUERY->add_mfun(QUERY,text_style_letterSpacing_set,"float","letterSpacing");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_style_baselineShift_get,"float","baselineShift");
    QUERY->add_mfun(QUERY,text_style_baselineShift_set,"float","baselineShift");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_style_script_get,"int","script");
    QUERY->add_mfun(QUERY,text_style_script_set,"int","script");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_style_opacity_get,"float","opacity");
    QUERY->add_mfun(QUERY,text_style_opacity_set,"float","opacity");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_style_inherit,"int","inherit");
    QUERY->add_arg(QUERY,"TextStyle","source");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextDocument","Object");
    docOffset=QUERY->add_mvar(QUERY,"int","@textDocumentNative",FALSE);
    if(docOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_doc_ctor); QUERY->add_dtor(QUERY,text_doc_dtor);
    QUERY->add_mfun(QUERY,text_doc_add,"int","addParagraph");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_arg(QUERY,"TextStyle","style");
    QUERY->add_mfun(QUERY,text_doc_count,"int","paragraphCount");
    QUERY->add_mfun(QUERY,text_doc_text,"string","paragraphText");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,text_doc_replace,"int","setParagraphText");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_mfun(QUERY,text_doc_style,"int","setParagraphStyle");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextStyle","style");
    QUERY->add_mfun(QUERY,text_doc_remove,"int","removeParagraph");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,text_doc_clear,"void","clear");
    QUERY->add_mfun(QUERY,text_doc_measure,"int","measureParagraph");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int[]","dimensions");
    return QUERY->end_class(QUERY);
}
