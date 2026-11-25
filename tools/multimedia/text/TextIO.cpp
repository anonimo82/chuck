// Procedural text foundation; offline Pango/Cairo layout, no markup parser.
#include "chuck_dl.h"
#include "../file_compat.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
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
struct Run { std::string text; Style style; };
struct ParagraphStyle {
    int alignment=0;
    int wrapMode=0;
    int direction=0;
    double lineSpacing=0;
    double indent=0;
    double marginLeft=0;
    double marginRight=0;
    double marginTop=0;
    double marginBottom=0;
};
struct Paragraph { std::string text; Style style; size_t baseBytes=0; std::vector<Run> runs; ParagraphStyle layout; };
struct Document { std::vector<Paragraph> paragraphs; size_t bytes=0,runs=0; };
t_CKUINT styleOffset=0,docOffset=0,runOffset=0,paragraphStyleOffset=0;
ParagraphStyle *paragraphStyleState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<ParagraphStyle *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,paragraphStyleOffset))):nullptr;
}
Run *runState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<Run *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,runOffset))):nullptr;
}
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
        d->paragraphs.push_back({s,*p,s.size(),{}}); d->bytes+=s.size(); RETURN->v_int=1;
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
        auto &p=d->paragraphs[i]; p.text.swap(s); p.baseBytes=next; d->runs-=p.runs.size(); p.runs.clear(); d->bytes=d->bytes-old+next; RETURN->v_int=1;
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
    if(indexOK(d,i)) { d->bytes-=d->paragraphs[i].text.size(); d->runs-=d->paragraphs[i].runs.size(); d->paragraphs.erase(d->paragraphs.begin()+i); RETURN->v_int=1; }
}
CK_DLL_MFUN(text_doc_clear) {
    if(auto *d=docState(SELF,API)) { d->paragraphs.clear(); d->bytes=0; d->runs=0; }
}
CK_DLL_CTOR(text_run_ctor) {
    OBJ_MEMBER_UINT(SELF,runOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,runOffset)=reinterpret_cast<uintptr_t>(new Run); } catch(...) {}
}
CK_DLL_DTOR(text_run_dtor) { delete runState(SELF,API); OBJ_MEMBER_UINT(SELF,runOffset)=0; }
CK_DLL_MFUN(text_run_set) {
    RETURN->v_int=0;
    try {
        auto *p=runState(SELF,API); auto s=GET_NEXT_STRING_SAFE(ARGS); auto *style=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(p&&style&&validText(s,65536)) { Run next{s,*style}; *p=std::move(next); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_run_text) {
    auto *p=runState(SELF,API);
    RETURN->v_string=API->object->create_string(VM,p?p->text.c_str():"",FALSE);
}
CK_DLL_MFUN(text_run_style) {
    RETURN->v_int=0;
    try {
        auto *p=runState(SELF,API); auto *s=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(p&&s) { Style copy=*s; p->style=std::move(copy); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_run_replace) {
    RETURN->v_int=0;
    try {
        auto *p=runState(SELF,API); auto s=GET_NEXT_STRING_SAFE(ARGS);
        if(p&&validText(s,65536)) { p->text.swap(s); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_append_run) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *r=runState(GET_NEXT_OBJECT(ARGS),API);
        if(!indexOK(d,i)||!r||r->text.empty()||d->runs>=16384) return;
        auto &p=d->paragraphs[i];
        if(p.runs.size()>=1024||p.text.size()+r->text.size()>65536||d->bytes+r->text.size()>1048576) return;
        auto next=p.text+r->text;
        p.runs.push_back(*r); p.text.swap(next); d->bytes+=r->text.size(); ++d->runs; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_run_count) {
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS);
    RETURN->v_int=indexOK(d,i)?static_cast<t_CKINT>(d->paragraphs[i].runs.size()):-1;
}
CK_DLL_MFUN(text_doc_run_text) {
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS),j=GET_NEXT_INT(ARGS);
    RETURN->v_string=API->object->create_string(VM,indexOK(d,i)&&j>=0&&static_cast<size_t>(j)<d->paragraphs[i].runs.size()?d->paragraphs[i].runs[j].text.c_str():"",FALSE);
}
CK_DLL_MFUN(text_doc_set_run_style) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS),j=GET_NEXT_INT(ARGS); auto *s=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(indexOK(d,i)&&s&&j>=0&&static_cast<size_t>(j)<d->paragraphs[i].runs.size()) {
            Style copy=*s; d->paragraphs[i].runs[j].style=std::move(copy); RETURN->v_int=1;
        }
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_replace_run) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS),j=GET_NEXT_INT(ARGS); auto *r=runState(GET_NEXT_OBJECT(ARGS),API);
        if(!indexOK(d,i)||!r||r->text.empty()) return;
        auto &p=d->paragraphs[i];
        if(j<0||static_cast<size_t>(j)>=p.runs.size()) return;
        size_t old=p.runs[j].text.size(),nextSize=p.text.size()-old+r->text.size();
        if(nextSize>65536||d->bytes-old+r->text.size()>1048576) return;
        Run copy=*r; size_t offset=p.baseBytes;
        for(size_t k=0;k<static_cast<size_t>(j);++k) offset+=p.runs[k].text.size();
        auto next=p.text; next.replace(offset,old,copy.text);
        p.runs[j]=std::move(copy); p.text.swap(next); d->bytes=d->bytes-old+r->text.size(); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_remove_run) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS),j=GET_NEXT_INT(ARGS);
        if(!indexOK(d,i)) return;
        auto &p=d->paragraphs[i];
        if(j<0||static_cast<size_t>(j)>=p.runs.size()) return;
        size_t offset=p.baseBytes,old=p.runs[j].text.size();
        for(size_t k=0;k<static_cast<size_t>(j);++k) offset+=p.runs[k].text.size();
        p.text.erase(offset,old); p.runs.erase(p.runs.begin()+j); d->bytes-=old; --d->runs; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_CTOR(text_paragraph_style_ctor) {
    OBJ_MEMBER_UINT(SELF,paragraphStyleOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,paragraphStyleOffset)=reinterpret_cast<uintptr_t>(new ParagraphStyle); } catch(...) {}
}
CK_DLL_DTOR(text_paragraph_style_dtor) { delete paragraphStyleState(SELF,API); OBJ_MEMBER_UINT(SELF,paragraphStyleOffset)=0; }
CK_DLL_MFUN(text_paragraph_style_inherit) {
    RETURN->v_int=0;
    auto *p=paragraphStyleState(SELF,API); auto *s=paragraphStyleState(GET_NEXT_OBJECT(ARGS),API);
    if(p&&s) { *p=*s; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_doc_set_paragraph_layout) {
    RETURN->v_int=0;
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *p=paragraphStyleState(GET_NEXT_OBJECT(ARGS),API);
    if(indexOK(d,i)&&p) { d->paragraphs[i].layout=*p; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_doc_get_paragraph_layout) {
    RETURN->v_int=0;
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *p=paragraphStyleState(GET_NEXT_OBJECT(ARGS),API);
    if(indexOK(d,i)&&p) { *p=d->paragraphs[i].layout; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_paragraph_alignment_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_int=p?p->alignment:-1;
}
CK_DLL_MFUN(text_paragraph_alignment_set) {
    auto *p=paragraphStyleState(SELF,API); t_CKINT v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=3) RETURN->v_int=p->alignment=v;
}
CK_DLL_MFUN(text_paragraph_wrapMode_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_int=p?p->wrapMode:-1;
}
CK_DLL_MFUN(text_paragraph_wrapMode_set) {
    auto *p=paragraphStyleState(SELF,API); t_CKINT v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=2) RETURN->v_int=p->wrapMode=v;
}
CK_DLL_MFUN(text_paragraph_direction_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_int=p?p->direction:-1;
}
CK_DLL_MFUN(text_paragraph_direction_set) {
    auto *p=paragraphStyleState(SELF,API); t_CKINT v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=2) RETURN->v_int=p->direction=v;
}
CK_DLL_MFUN(text_paragraph_lineSpacing_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_float=p?p->lineSpacing:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_paragraph_lineSpacing_set) {
    auto *p=paragraphStyleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=512) RETURN->v_float=p->lineSpacing=v;
}
CK_DLL_MFUN(text_paragraph_indent_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_float=p?p->indent:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_paragraph_indent_set) {
    auto *p=paragraphStyleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=-1024&&v<=1024) RETURN->v_float=p->indent=v;
}
CK_DLL_MFUN(text_paragraph_marginLeft_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_float=p?p->marginLeft:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_paragraph_marginLeft_set) {
    auto *p=paragraphStyleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=2048) RETURN->v_float=p->marginLeft=v;
}
CK_DLL_MFUN(text_paragraph_marginRight_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_float=p?p->marginRight:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_paragraph_marginRight_set) {
    auto *p=paragraphStyleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=2048) RETURN->v_float=p->marginRight=v;
}
CK_DLL_MFUN(text_paragraph_marginTop_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_float=p?p->marginTop:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_paragraph_marginTop_set) {
    auto *p=paragraphStyleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=2048) RETURN->v_float=p->marginTop=v;
}
CK_DLL_MFUN(text_paragraph_marginBottom_get) {
    auto *p=paragraphStyleState(SELF,API); RETURN->v_float=p?p->marginBottom:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_paragraph_marginBottom_set) {
    auto *p=paragraphStyleState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=2048) RETURN->v_float=p->marginBottom=v;
}
namespace {
using LayoutPtr=std::unique_ptr<PangoLayout,decltype(&g_object_unref)>;
LayoutPtr makeLayout(cairo_t *cr,const Paragraph &p,int width) {
    double inner=width-p.layout.marginLeft-p.layout.marginRight;
    if(inner<1||std::abs(p.layout.indent)>=inner) return LayoutPtr(nullptr,g_object_unref);
        std::unique_ptr<PangoLayout,decltype(&g_object_unref)> layout(pango_cairo_create_layout(cr),g_object_unref);
        if(!layout) return LayoutPtr(nullptr,g_object_unref);
        std::unique_ptr<PangoFontDescription,decltype(&pango_font_description_free)> font(pango_font_description_new(),pango_font_description_free);
        if(!font) return LayoutPtr(nullptr,g_object_unref);
        pango_font_description_set_family(font.get(),p.style.font.c_str());
        pango_font_description_set_absolute_size(font.get(),p.style.size*PANGO_SCALE);
        pango_font_description_set_weight(font.get(),static_cast<PangoWeight>(p.style.weight));
        pango_font_description_set_style(font.get(),p.style.italic?PANGO_STYLE_ITALIC:PANGO_STYLE_NORMAL);
        pango_layout_set_font_description(layout.get(),font.get());
        pango_layout_set_text(layout.get(),p.text.data(),static_cast<int>(p.text.size()));
        pango_layout_set_width(layout.get(),static_cast<int>(std::lround(inner*PANGO_SCALE)));
        const auto &flow=p.layout;
        pango_layout_set_wrap(layout.get(),flow.wrapMode==1?PANGO_WRAP_CHAR:flow.wrapMode==2?PANGO_WRAP_WORD:PANGO_WRAP_WORD_CHAR);
        pango_layout_set_indent(layout.get(),static_cast<int>(std::lround(flow.indent*PANGO_SCALE)));
        pango_layout_set_spacing(layout.get(),static_cast<int>(std::lround(flow.lineSpacing*PANGO_SCALE)));
        pango_layout_set_alignment(layout.get(),flow.alignment==1?PANGO_ALIGN_CENTER:flow.alignment==2?PANGO_ALIGN_RIGHT:PANGO_ALIGN_LEFT);
        pango_layout_set_justify(layout.get(),flow.alignment==3);
        if(flow.direction!=0) {
            pango_context_set_base_dir(pango_layout_get_context(layout.get()),flow.direction==1?PANGO_DIRECTION_LTR:PANGO_DIRECTION_RTL);
            pango_layout_set_auto_dir(layout.get(),FALSE);
            pango_layout_context_changed(layout.get());
        }
        std::unique_ptr<PangoAttrList,decltype(&pango_attr_list_unref)> attrs(pango_attr_list_new(),pango_attr_list_unref);
        if(!attrs) return LayoutPtr(nullptr,g_object_unref);
        auto apply=[&](const Style &style,guint begin,guint end) {
            auto add=[&](PangoAttribute *attr) { attr->start_index=begin; attr->end_index=end; pango_attr_list_insert(attrs.get(),attr); };
            add(pango_attr_family_new(style.font.c_str()));
            add(pango_attr_size_new_absolute(static_cast<int>(std::lround(style.size*PANGO_SCALE))));
            add(pango_attr_weight_new(static_cast<PangoWeight>(style.weight)));
            add(pango_attr_style_new(style.italic?PANGO_STYLE_ITALIC:PANGO_STYLE_NORMAL));
            auto rgb16=[](t_CKINT rgb,int shift) { return static_cast<guint16>(((rgb>>shift)&255)*257); };
            add(pango_attr_foreground_new(rgb16(style.rgb,16),rgb16(style.rgb,8),rgb16(style.rgb,0)));
            add(pango_attr_foreground_alpha_new(static_cast<guint16>(std::lround(style.opacity*65535))));
            if(style.background>=0) {
                add(pango_attr_background_new(rgb16(style.background,16),rgb16(style.background,8),rgb16(style.background,0)));
                add(pango_attr_background_alpha_new(static_cast<guint16>(std::lround(style.opacity*65535))));
            }
            if(style.underline) add(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
            if(style.strikethrough) add(pango_attr_strikethrough_new(TRUE));
            if(style.spacing!=0) add(pango_attr_letter_spacing_new(static_cast<int>(std::lround(style.spacing*PANGO_SCALE))));
            double rise=style.rise;
            if(style.script!=0) {
                add(pango_attr_scale_new(0.7));
                rise+=(style.script==1?0.35:-0.2)*style.size;
            }
            if(rise!=0) add(pango_attr_rise_new(static_cast<int>(std::lround(rise*PANGO_SCALE))));
        };
        if(p.baseBytes>0||p.runs.empty()) apply(p.style,0,p.baseBytes?static_cast<guint>(p.baseBytes):G_MAXUINT);
        guint offset=static_cast<guint>(p.baseBytes);
        for(const auto &run:p.runs) { guint next=offset+static_cast<guint>(run.text.size()); apply(run.style,offset,next); offset=next; }
        pango_layout_set_attributes(layout.get(),attrs.get());

    return layout;
}
bool paragraphDimensions(cairo_t *cr,const Paragraph &p,int width,int &w,int &h) {
    auto layout=makeLayout(cr,p,width);
    if(!layout) return false;
    int contentW=0,contentH=0; pango_layout_get_pixel_size(layout.get(),&contentW,&contentH);
    double totalW=contentW+p.layout.marginLeft+p.layout.marginRight;
    double totalH=contentH+p.layout.marginTop+p.layout.marginBottom;
    if(totalW<0||totalH<0||totalW>100000000||totalH>100000000) return false;
    w=static_cast<int>(std::ceil(totalW)); h=static_cast<int>(std::ceil(totalH)); return true;
}
}
CK_DLL_MFUN(text_doc_measure) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS),width=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!indexOK(d,i)||width<1||width>4096||!a||API->object->array_int_size(a)!=2) return;
        std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)> surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,1,1),cairo_surface_destroy);
        if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return;
        std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
        if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return;
        int w=0,h=0; if(!paragraphDimensions(cr.get(),d->paragraphs[i],width,w,h)) return;
        API->object->array_int_set_idx(a,0,w); API->object->array_int_set_idx(a,1,h); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_measure_document) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto width=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!d||width<1||width>4096||!a||API->object->array_int_size(a)!=2) return;
        std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)> surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,1,1),cairo_surface_destroy);
        if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return;
        std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
        if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return;
        int maxW=0; int64_t totalH=0;
        for(const auto &p:d->paragraphs) {
            int w=0,h=0; if(!paragraphDimensions(cr.get(),p,width,w,h)) return;
            if(w>maxW) maxW=w;
            totalH+=h; if(totalH>100000000) return;
        }
        API->object->array_int_set_idx(a,0,maxW); API->object->array_int_set_idx(a,1,totalH); RETURN->v_int=1;
    } catch(...) {}
}
namespace {
using SurfacePtr=std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)>;
bool validCanvas(t_CKINT w,t_CKINT h,t_CKINT bg) {
    return w>=1&&w<=4096&&h>=1&&h<=4096&&w*h<=4194304&&bg>=-1&&bg<=0xffffff;
}
SurfacePtr renderDocument(const Document &d,int w,int h,t_CKINT bg) {
    SurfacePtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h),cairo_surface_destroy);
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return SurfacePtr(nullptr,cairo_surface_destroy);
    std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
    if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return SurfacePtr(nullptr,cairo_surface_destroy);
    cairo_set_operator(cr.get(),CAIRO_OPERATOR_SOURCE);
    if(bg<0) cairo_set_source_rgba(cr.get(),0,0,0,0);
    else cairo_set_source_rgb(cr.get(),((bg>>16)&255)/255.0,((bg>>8)&255)/255.0,(bg&255)/255.0);
    cairo_paint(cr.get());
    cairo_set_operator(cr.get(),CAIRO_OPERATOR_OVER);
    // Validate all paragraphs, including those beyond the visible canvas.
    int64_t y=0;
    for(const auto &p:d.paragraphs) {
        auto layout=makeLayout(cr.get(),p,w);
        if(!layout) return SurfacePtr(nullptr,cairo_surface_destroy);
        int contentW=0,contentH=0;
        pango_layout_get_pixel_size(layout.get(),&contentW,&contentH);
        double totalW=contentW+p.layout.marginLeft+p.layout.marginRight;
        double totalH=contentH+p.layout.marginTop+p.layout.marginBottom;
        if(totalW<0||totalH<0||totalW>100000000||totalH>100000000) return SurfacePtr(nullptr,cairo_surface_destroy);
        if(y<h) {
            cairo_save(cr.get());
            cairo_rectangle(cr.get(),p.layout.marginLeft,0,w-p.layout.marginLeft-p.layout.marginRight,h);
            cairo_clip(cr.get());
            cairo_move_to(cr.get(),p.layout.marginLeft,y+p.layout.marginTop);
            cairo_set_source_rgb(cr.get(),0,0,0);
            pango_cairo_show_layout(cr.get(),layout.get());
            cairo_restore(cr.get());
        }
        y+=static_cast<int64_t>(std::ceil(totalH));
        if(y>100000000||cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return SurfacePtr(nullptr,cairo_surface_destroy);
    }
    cairo_surface_flush(surface.get());
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return SurfacePtr(nullptr,cairo_surface_destroy);
    return surface;
}
cairo_status_t textWritePng(void *closure,const unsigned char *data,unsigned int size) {
    return std::fwrite(data,1,size,static_cast<FILE *>(closure))==size?CAIRO_STATUS_SUCCESS:CAIRO_STATUS_WRITE_ERROR;
}
}
CK_DLL_MFUN(text_doc_export_pixels) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),bg=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!d||!validCanvas(w,h,bg)||!a||API->object->array_int_size(a)!=w*h*4) return;
        auto surface=renderDocument(*d,w,h,bg);
        if(!surface) return;
        auto *data=cairo_image_surface_get_data(surface.get());
        int stride=cairo_image_surface_get_stride(surface.get());
        if(!data) return;
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            uint32_t v; std::memcpy(&v,data+static_cast<size_t>(y)*stride+x*4,4);
            unsigned alpha=v>>24; size_t i=(static_cast<size_t>(y)*w+x)*4;
            for(int c=0;c<3;++c) {
                unsigned q=(v>>(16-8*c))&255;
                API->object->array_int_set_idx(a,i+c,alpha?std::min(255u,(q*255+alpha/2)/alpha):0);
            }
            API->object->array_int_set_idx(a,i+3,alpha);
        }
        RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_save_png) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS),bg=GET_NEXT_INT(ARGS);
        if(!d||!validCanvas(w,h,bg)||path.size()<4||path.substr(path.size()-4)!=".png") return;
        auto surface=renderDocument(*d,w,h,bg);
        if(!surface) return;
        FILE *f=mediaio::openNew(path.c_str());
        if(!f) return;
        bool ok=cairo_surface_write_to_png_stream(surface.get(),textWritePng,f)==CAIRO_STATUS_SUCCESS;
        if(std::fclose(f)!=0) ok=false;
        if(!ok) { std::remove(path.c_str()); return; }
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
    QUERY->begin_class(QUERY,"TextRun","Object");
    runOffset=QUERY->add_mvar(QUERY,"int","@textRunNative",FALSE);
    if(runOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_run_ctor); QUERY->add_dtor(QUERY,text_run_dtor);
    QUERY->add_mfun(QUERY,text_run_set,"int","set");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_arg(QUERY,"TextStyle","style");
    QUERY->add_mfun(QUERY,text_run_text,"string","text");
    QUERY->add_mfun(QUERY,text_run_replace,"int","setText");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_mfun(QUERY,text_run_style,"int","setStyle");
    QUERY->add_arg(QUERY,"TextStyle","style");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextParagraphStyle","Object");
    paragraphStyleOffset=QUERY->add_mvar(QUERY,"int","@textParagraphStyleNative",FALSE);
    if(paragraphStyleOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_paragraph_style_ctor); QUERY->add_dtor(QUERY,text_paragraph_style_dtor);
    QUERY->add_mfun(QUERY,text_paragraph_alignment_get,"int","alignment");
    QUERY->add_mfun(QUERY,text_paragraph_alignment_set,"int","alignment");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_paragraph_wrapMode_get,"int","wrapMode");
    QUERY->add_mfun(QUERY,text_paragraph_wrapMode_set,"int","wrapMode");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_paragraph_direction_get,"int","direction");
    QUERY->add_mfun(QUERY,text_paragraph_direction_set,"int","direction");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_paragraph_lineSpacing_get,"float","lineSpacing");
    QUERY->add_mfun(QUERY,text_paragraph_lineSpacing_set,"float","lineSpacing");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_paragraph_indent_get,"float","indent");
    QUERY->add_mfun(QUERY,text_paragraph_indent_set,"float","indent");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_paragraph_marginLeft_get,"float","marginLeft");
    QUERY->add_mfun(QUERY,text_paragraph_marginLeft_set,"float","marginLeft");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_paragraph_marginRight_get,"float","marginRight");
    QUERY->add_mfun(QUERY,text_paragraph_marginRight_set,"float","marginRight");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_paragraph_marginTop_get,"float","marginTop");
    QUERY->add_mfun(QUERY,text_paragraph_marginTop_set,"float","marginTop");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_paragraph_marginBottom_get,"float","marginBottom");
    QUERY->add_mfun(QUERY,text_paragraph_marginBottom_set,"float","marginBottom");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_paragraph_style_inherit,"int","inherit");
    QUERY->add_arg(QUERY,"TextParagraphStyle","source");
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
    QUERY->add_mfun(QUERY,text_doc_append_run,"int","appendRun");
    QUERY->add_arg(QUERY,"int","paragraph");
    QUERY->add_arg(QUERY,"TextRun","run");
    QUERY->add_mfun(QUERY,text_doc_run_count,"int","runCount");
    QUERY->add_arg(QUERY,"int","paragraph");
    QUERY->add_mfun(QUERY,text_doc_run_text,"string","runText");
    QUERY->add_arg(QUERY,"int","paragraph");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,text_doc_set_run_style,"int","setRunStyle");
    QUERY->add_arg(QUERY,"int","paragraph");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextStyle","style");
    QUERY->add_mfun(QUERY,text_doc_replace_run,"int","setRun");
    QUERY->add_arg(QUERY,"int","paragraph");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextRun","run");
    QUERY->add_mfun(QUERY,text_doc_remove_run,"int","removeRun");
    QUERY->add_arg(QUERY,"int","paragraph");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,text_doc_set_paragraph_layout,"int","setParagraphLayout");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextParagraphStyle","style");
    QUERY->add_mfun(QUERY,text_doc_get_paragraph_layout,"int","getParagraphLayout");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextParagraphStyle","style");
    QUERY->add_mfun(QUERY,text_doc_measure_document,"int","measureDocument");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int[]","dimensions");
    QUERY->add_mfun(QUERY,text_doc_export_pixels,"int","exportPixels");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","backgroundRGB");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,text_doc_save_png,"int","savePNG");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","backgroundRGB");
    QUERY->add_mfun(QUERY,text_doc_measure,"int","measureParagraph");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int[]","dimensions");
    return QUERY->end_class(QUERY);
}
