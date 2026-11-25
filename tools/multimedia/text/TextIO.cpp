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
struct Container {
    double paddingLeft=0;
    double paddingRight=0;
    double paddingTop=0;
    double paddingBottom=0;
    double borderWidth=0;
    t_CKINT borderColor=0;
    t_CKINT backgroundColor=-1;
    double opacity=1;
};
struct ListStyle {
    std::string marker="*";
    t_CKINT mode=0;
    t_CKINT number=1;
    t_CKINT level=0;
    double levelIndent=20;
    double markerWidth=32;
};
std::string listLabel(const ListStyle &p) {
    if(p.mode==1) return "\xe2\x80\xa2";
    if(p.mode==2) return std::to_string(p.number)+".";
    if(p.mode==3) return p.marker;
    return "";
}
double listSpace(const ListStyle &p) { return p.mode?p.level*p.levelIndent+p.markerWidth:0; }
struct Paragraph { std::string text; Style style; size_t baseBytes=0; std::vector<Run> runs; ParagraphStyle layout; Container box; ListStyle list; };
// Value-owned tree: children are snapshots, not references to VM objects.
struct Node {
    int kind=0; // 0 group, 1 paragraph
    Paragraph paragraph;
    std::vector<Node> children;
};
struct NodeStats { size_t nodes=0,bytes=0; int depth=0; };
bool nodeStats(const Node &n,NodeStats &out,int depth=1) {
    if(depth>32||++out.nodes>4096) return false;
    out.depth=std::max(out.depth,depth);
    if(n.paragraph.text.size()>1048576-out.bytes) return false;
    out.bytes+=n.paragraph.text.size();
    if(n.kind==1&&!n.children.empty()) return false;
    for(const auto &child:n.children) if(!nodeStats(child,out,depth+1)) return false;
    return true;
}
bool nodeChildOK(const Node *n,t_CKINT i) {
    return n&&i>=0&&static_cast<uint64_t>(i)<n->children.size();
}
struct Table {
    int rows=0,cols=0;
    std::vector<Paragraph> cells;
    std::vector<double> weights;
    size_t bytes=0;
    int columnGap=0,rowGap=0;
    bool grid=false;
};
bool cellOK(Table *t,t_CKINT row,t_CKINT col) {
    return t&&row>=0&&col>=0&&row<t->rows&&col<t->cols;
}
struct TableBlock { size_t after=0; Table table; };
struct PageStyle {
    t_CKINT width=640;
    t_CKINT height=480;
    t_CKINT marginLeft=0;
    t_CKINT marginRight=0;
    t_CKINT marginTop=0;
    t_CKINT marginBottom=0;
    t_CKINT backgroundColor=16777215;
};
struct Document { std::vector<Paragraph> paragraphs; size_t bytes=0,runs=0; std::vector<TableBlock> tables; size_t tableCells=0,tableBytes=0; };
t_CKUINT styleOffset=0,docOffset=0,runOffset=0,paragraphStyleOffset=0,containerOffset=0,listOffset=0,tableOffset=0,gridOffset=0,pageOffset=0,nodeOffset=0;
Node *nodeState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<Node *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,nodeOffset))):nullptr;
}
PageStyle *pageState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<PageStyle *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,pageOffset))):nullptr;
}
Table *gridState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<Table *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,gridOffset))):nullptr;
}
Table *tableState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<Table *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,tableOffset))):nullptr;
}
ListStyle *listState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<ListStyle *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,listOffset))):nullptr;
}
Container *containerState(Chuck_Object *self,CK_DL_API API) {
    return self?reinterpret_cast<Container *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,containerOffset))):nullptr;
}
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
    if(indexOK(d,i)) { d->bytes-=d->paragraphs[i].text.size(); d->runs-=d->paragraphs[i].runs.size(); d->paragraphs.erase(d->paragraphs.begin()+i); for(auto &b:d->tables) if(b.after>static_cast<size_t>(i)) --b.after; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_doc_clear) {
    if(auto *d=docState(SELF,API)) { d->paragraphs.clear(); d->bytes=0; d->runs=0; d->tables.clear(); d->tableCells=0; d->tableBytes=0; }
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
CK_DLL_CTOR(text_container_ctor) {
    OBJ_MEMBER_UINT(SELF,containerOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,containerOffset)=reinterpret_cast<uintptr_t>(new Container); } catch(...) {}
}
CK_DLL_DTOR(text_container_dtor) { delete containerState(SELF,API); OBJ_MEMBER_UINT(SELF,containerOffset)=0; }
CK_DLL_MFUN(text_container_inherit) {
    RETURN->v_int=0;
    auto *p=containerState(SELF,API); auto *s=containerState(GET_NEXT_OBJECT(ARGS),API);
    if(p&&s) { *p=*s; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_doc_set_container) {
    RETURN->v_int=0;
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *p=containerState(GET_NEXT_OBJECT(ARGS),API);
    if(indexOK(d,i)&&p) { d->paragraphs[i].box=*p; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_doc_get_container) {
    RETURN->v_int=0;
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *p=containerState(GET_NEXT_OBJECT(ARGS),API);
    if(indexOK(d,i)&&p) { *p=d->paragraphs[i].box; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_container_paddingLeft_get) {
    auto *p=containerState(SELF,API); RETURN->v_float=p?p->paddingLeft:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_container_paddingLeft_set) {
    auto *p=containerState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=512) RETURN->v_float=p->paddingLeft=v;
}
CK_DLL_MFUN(text_container_paddingRight_get) {
    auto *p=containerState(SELF,API); RETURN->v_float=p?p->paddingRight:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_container_paddingRight_set) {
    auto *p=containerState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=512) RETURN->v_float=p->paddingRight=v;
}
CK_DLL_MFUN(text_container_paddingTop_get) {
    auto *p=containerState(SELF,API); RETURN->v_float=p?p->paddingTop:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_container_paddingTop_set) {
    auto *p=containerState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=512) RETURN->v_float=p->paddingTop=v;
}
CK_DLL_MFUN(text_container_paddingBottom_get) {
    auto *p=containerState(SELF,API); RETURN->v_float=p?p->paddingBottom:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_container_paddingBottom_set) {
    auto *p=containerState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=512) RETURN->v_float=p->paddingBottom=v;
}
CK_DLL_MFUN(text_container_borderWidth_get) {
    auto *p=containerState(SELF,API); RETURN->v_float=p?p->borderWidth:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_container_borderWidth_set) {
    auto *p=containerState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=128) RETURN->v_float=p->borderWidth=v;
}
CK_DLL_MFUN(text_container_borderColor_get) {
    auto *p=containerState(SELF,API); RETURN->v_int=p?p->borderColor:-1;
}
CK_DLL_MFUN(text_container_borderColor_set) {
    auto *p=containerState(SELF,API); t_CKINT v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=16777215) RETURN->v_int=p->borderColor=v;
}
CK_DLL_MFUN(text_container_backgroundColor_get) {
    auto *p=containerState(SELF,API); RETURN->v_int=p?p->backgroundColor:-2;
}
CK_DLL_MFUN(text_container_backgroundColor_set) {
    auto *p=containerState(SELF,API); t_CKINT v=GET_NEXT_INT(ARGS); RETURN->v_int=-2;
    if(p&&v>=-1&&v<=16777215) RETURN->v_int=p->backgroundColor=v;
}
CK_DLL_MFUN(text_container_opacity_get) {
    auto *p=containerState(SELF,API); RETURN->v_float=p?p->opacity:std::numeric_limits<double>::quiet_NaN();
}
CK_DLL_MFUN(text_container_opacity_set) {
    auto *p=containerState(SELF,API); double v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=1) RETURN->v_float=p->opacity=v;
}
CK_DLL_CTOR(text_list_ctor) {
    OBJ_MEMBER_UINT(SELF,listOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,listOffset)=reinterpret_cast<uintptr_t>(new ListStyle); } catch(...) {}
}
CK_DLL_DTOR(text_list_dtor) { delete listState(SELF,API); OBJ_MEMBER_UINT(SELF,listOffset)=0; }
CK_DLL_MFUN(text_list_marker_get) {
    auto *p=listState(SELF,API);
    RETURN->v_string=API->object->create_string(VM,p?p->marker.c_str():"",FALSE);
}
CK_DLL_MFUN(text_list_marker_set) {
    RETURN->v_string=API->object->create_string(VM,"",FALSE);
    try {
        auto *p=listState(SELF,API); auto v=GET_NEXT_STRING_SAFE(ARGS);
        if(p&&!v.empty()&&validText(v,64)&&v.find_first_of("\r\n\t")==std::string::npos) {
            p->marker=v; RETURN->v_string=API->object->create_string(VM,v.c_str(),FALSE);
        }
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_set_list) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *p=listState(GET_NEXT_OBJECT(ARGS),API);
        if(indexOK(d,i)&&p) { ListStyle copy=*p; d->paragraphs[i].list=std::move(copy); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_get_list) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *p=listState(GET_NEXT_OBJECT(ARGS),API);
        if(indexOK(d,i)&&p) { ListStyle copy=d->paragraphs[i].list; *p=std::move(copy); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_list_label) {
    std::string label;
    try { auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); if(indexOK(d,i)) label=listLabel(d->paragraphs[i].list); } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,label.c_str(),FALSE);
}
CK_DLL_MFUN(text_list_mode_get) { auto *p=listState(SELF,API); RETURN->v_int=p?p->mode:-1; }
CK_DLL_MFUN(text_list_mode_set) {
    auto *p=listState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=3) RETURN->v_int=p->mode=v;
}
CK_DLL_MFUN(text_list_number_get) { auto *p=listState(SELF,API); RETURN->v_int=p?p->number:-1; }
CK_DLL_MFUN(text_list_number_set) {
    auto *p=listState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=1&&v<=999999) RETURN->v_int=p->number=v;
}
CK_DLL_MFUN(text_list_level_get) { auto *p=listState(SELF,API); RETURN->v_int=p?p->level:-1; }
CK_DLL_MFUN(text_list_level_set) {
    auto *p=listState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=16) RETURN->v_int=p->level=v;
}
CK_DLL_MFUN(text_list_levelIndent_get) { auto *p=listState(SELF,API); RETURN->v_float=p?p->levelIndent:std::numeric_limits<double>::quiet_NaN(); }
CK_DLL_MFUN(text_list_levelIndent_set) {
    auto *p=listState(SELF,API); auto v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=0&&v<=128) RETURN->v_float=p->levelIndent=v;
}
CK_DLL_MFUN(text_list_markerWidth_get) { auto *p=listState(SELF,API); RETURN->v_float=p?p->markerWidth:std::numeric_limits<double>::quiet_NaN(); }
CK_DLL_MFUN(text_list_markerWidth_set) {
    auto *p=listState(SELF,API); auto v=GET_NEXT_FLOAT(ARGS); RETURN->v_float=std::numeric_limits<double>::quiet_NaN();
    if(p&&std::isfinite(v)&&v>=1&&v<=512) RETURN->v_float=p->markerWidth=v;
}
CK_DLL_CTOR(text_table_ctor) {
    OBJ_MEMBER_UINT(SELF,tableOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,tableOffset)=reinterpret_cast<uintptr_t>(new Table); } catch(...) {}
}
CK_DLL_DTOR(text_table_dtor) { delete tableState(SELF,API); OBJ_MEMBER_UINT(SELF,tableOffset)=0; }
CK_DLL_MFUN(text_table_create) {
    RETURN->v_int=0;
    try {
        auto *t=tableState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS);
        if(!t||r<1||r>256||c<1||c>32||r*c>1024) return;
        Table next; next.rows=r; next.cols=c;
        next.cells.resize(r*c); next.weights.assign(c,1.0);
        for(auto &p:next.cells) { p.box.borderWidth=1; p.box.paddingLeft=4; p.box.paddingRight=4; p.box.paddingTop=4; p.box.paddingBottom=4; }
        *t=std::move(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_table_rows) { auto *t=tableState(SELF,API); RETURN->v_int=t?t->rows:0; }
CK_DLL_MFUN(text_table_cols) { auto *t=tableState(SELF,API); RETURN->v_int=t?t->cols:0; }
CK_DLL_MFUN(text_table_set_cell) {
    RETURN->v_int=0;
    try {
        auto *t=tableState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS);
        auto text=GET_NEXT_STRING_SAFE(ARGS); auto *st=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(!cellOK(t,r,c)||!st||!validText(text,65536)) return;
        auto &p=t->cells[r*t->cols+c]; size_t next=t->bytes-p.text.size()+text.size();
        if(next>1048576) return;
        Paragraph copy=p; copy.text=std::move(text); copy.baseBytes=copy.text.size(); copy.style=*st;
        p=std::move(copy); t->bytes=next; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_table_cell_text) {
    auto *t=tableState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS);
    RETURN->v_string=API->object->create_string(VM,cellOK(t,r,c)?t->cells[r*t->cols+c].text.c_str():"",FALSE);
}
CK_DLL_MFUN(text_table_layout) {
    RETURN->v_int=0;
    auto *t=tableState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS); auto *p=paragraphStyleState(GET_NEXT_OBJECT(ARGS),API);
    if(cellOK(t,r,c)&&p) { t->cells[r*t->cols+c].layout=*p; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_table_box) {
    RETURN->v_int=0;
    auto *t=tableState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS); auto *p=containerState(GET_NEXT_OBJECT(ARGS),API);
    if(cellOK(t,r,c)&&p) { t->cells[r*t->cols+c].box=*p; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_table_weight_get) {
    auto *t=tableState(SELF,API); auto c=GET_NEXT_INT(ARGS);
    RETURN->v_float=t&&c>=0&&c<t->cols?t->weights[c]:-1;
}
CK_DLL_MFUN(text_table_weight_set) {
    auto *t=tableState(SELF,API); auto c=GET_NEXT_INT(ARGS); double w=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=-1;
    if(t&&c>=0&&c<t->cols&&std::isfinite(w)&&w>=0.01&&w<=100) RETURN->v_float=t->weights[c]=w;
}
CK_DLL_CTOR(text_grid_ctor) {
    OBJ_MEMBER_UINT(SELF,gridOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,gridOffset)=reinterpret_cast<uintptr_t>(new Table); } catch(...) {}
}
CK_DLL_DTOR(text_grid_dtor) { delete gridState(SELF,API); OBJ_MEMBER_UINT(SELF,gridOffset)=0; }
CK_DLL_MFUN(text_grid_create) {
    RETURN->v_int=0;
    try {
        auto *t=gridState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS);
        if(!t||r<1||r>256||c<1||c>32||r*c>1024) return;
        Table next; next.rows=r; next.cols=c; next.grid=true;
        next.cells.resize(r*c); next.weights.assign(c,1.0);
        *t=std::move(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_grid_rows) { auto *t=gridState(SELF,API); RETURN->v_int=t?t->rows:0; }
CK_DLL_MFUN(text_grid_cols) { auto *t=gridState(SELF,API); RETURN->v_int=t?t->cols:0; }
CK_DLL_MFUN(text_grid_set_cell) {
    RETURN->v_int=0;
    try {
        auto *t=gridState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS);
        auto text=GET_NEXT_STRING_SAFE(ARGS); auto *st=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(!cellOK(t,r,c)||!st||!validText(text,65536)) return;
        auto &p=t->cells[r*t->cols+c]; size_t next=t->bytes-p.text.size()+text.size();
        if(next>1048576) return;
        Paragraph copy=p; copy.text=std::move(text); copy.baseBytes=copy.text.size(); copy.style=*st;
        p=std::move(copy); t->bytes=next; RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_grid_cell_text) {
    auto *t=gridState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS);
    RETURN->v_string=API->object->create_string(VM,cellOK(t,r,c)?t->cells[r*t->cols+c].text.c_str():"",FALSE);
}
CK_DLL_MFUN(text_grid_layout) {
    RETURN->v_int=0;
    auto *t=gridState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS); auto *p=paragraphStyleState(GET_NEXT_OBJECT(ARGS),API);
    if(cellOK(t,r,c)&&p) { t->cells[r*t->cols+c].layout=*p; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_grid_box) {
    RETURN->v_int=0;
    auto *t=gridState(SELF,API); auto r=GET_NEXT_INT(ARGS),c=GET_NEXT_INT(ARGS); auto *p=containerState(GET_NEXT_OBJECT(ARGS),API);
    if(cellOK(t,r,c)&&p) { t->cells[r*t->cols+c].box=*p; RETURN->v_int=1; }
}
CK_DLL_MFUN(text_grid_weight_get) {
    auto *t=gridState(SELF,API); auto c=GET_NEXT_INT(ARGS);
    RETURN->v_float=t&&c>=0&&c<t->cols?t->weights[c]:-1;
}
CK_DLL_MFUN(text_grid_weight_set) {
    auto *t=gridState(SELF,API); auto c=GET_NEXT_INT(ARGS); double w=GET_NEXT_FLOAT(ARGS);
    RETURN->v_float=-1;
    if(t&&c>=0&&c<t->cols&&std::isfinite(w)&&w>=0.01&&w<=100) RETURN->v_float=t->weights[c]=w;
}
CK_DLL_MFUN(text_grid_columnGap_get) {
    auto *t=gridState(SELF,API); RETURN->v_int=t?t->columnGap:-1;
}
CK_DLL_MFUN(text_grid_columnGap_set) {
    auto *t=gridState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(t&&v>=0&&v<=512) RETURN->v_int=t->columnGap=v;
}
CK_DLL_MFUN(text_grid_rowGap_get) {
    auto *t=gridState(SELF,API); RETURN->v_int=t?t->rowGap:-1;
}
CK_DLL_MFUN(text_grid_rowGap_set) {
    auto *t=gridState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(t&&v>=0&&v<=512) RETURN->v_int=t->rowGap=v;
}
namespace {
size_t blockIndex(const Document &d,t_CKINT index,bool grid) {
    if(index<0) return d.tables.size();
    for(size_t i=0;i<d.tables.size();++i) if(d.tables[i].table.grid==grid) {
        if(index==0) return i;
        --index;
    }
    return d.tables.size();
}
bool appendBlock(Document *d,const Table *t,bool grid) {
    if(!d||!t||!t->rows||t->grid!=grid||d->tables.size()>=256||d->tableCells+t->cells.size()>4096||d->tableBytes+t->bytes>1048576) return false;
    d->tables.push_back({d->paragraphs.size(),*t});
    d->tableCells+=t->cells.size(); d->tableBytes+=t->bytes; return true;
}
bool replaceBlock(Document *d,t_CKINT i,const Table *t,bool grid) {
    if(!d||!t||!t->rows||t->grid!=grid) return false;
    size_t at=blockIndex(*d,i,grid); if(at==d->tables.size()) return false;
    auto &old=d->tables[at].table;
    auto cells=d->tableCells-old.cells.size()+t->cells.size(),bytes=d->tableBytes-old.bytes+t->bytes;
    if(cells>4096||bytes>1048576) return false;
    Table next=*t; old=std::move(next); d->tableCells=cells; d->tableBytes=bytes; return true;
}
bool removeBlock(Document *d,t_CKINT i,bool grid) {
    if(!d) return false;
    size_t at=blockIndex(*d,i,grid); if(at==d->tables.size()) return false;
    d->tableCells-=d->tables[at].table.cells.size(); d->tableBytes-=d->tables[at].table.bytes;
    d->tables.erase(d->tables.begin()+at); return true;
}
}
CK_DLL_MFUN(text_doc_append_table) {
    RETURN->v_int=0;
    try { auto *d=docState(SELF,API); auto *t=tableState(GET_NEXT_OBJECT(ARGS),API); RETURN->v_int=appendBlock(d,t,false); } catch(...) {}
}
CK_DLL_MFUN(text_doc_table_count) {
    auto *d=docState(SELF,API); RETURN->v_int=0;
    if(d) for(const auto &b:d->tables) if(b.table.grid==false) ++RETURN->v_int;
}
CK_DLL_MFUN(text_doc_set_table) {
    RETURN->v_int=0;
    try { auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *t=tableState(GET_NEXT_OBJECT(ARGS),API); RETURN->v_int=replaceBlock(d,i,t,false); } catch(...) {}
}
CK_DLL_MFUN(text_doc_remove_table) {
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); RETURN->v_int=removeBlock(d,i,false);
}
CK_DLL_MFUN(text_doc_append_grid) {
    RETURN->v_int=0;
    try { auto *d=docState(SELF,API); auto *t=gridState(GET_NEXT_OBJECT(ARGS),API); RETURN->v_int=appendBlock(d,t,true); } catch(...) {}
}
CK_DLL_MFUN(text_doc_grid_count) {
    auto *d=docState(SELF,API); RETURN->v_int=0;
    if(d) for(const auto &b:d->tables) if(b.table.grid==true) ++RETURN->v_int;
}
CK_DLL_MFUN(text_doc_set_grid) {
    RETURN->v_int=0;
    try { auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *t=gridState(GET_NEXT_OBJECT(ARGS),API); RETURN->v_int=replaceBlock(d,i,t,true); } catch(...) {}
}
CK_DLL_MFUN(text_doc_remove_grid) {
    auto *d=docState(SELF,API); auto i=GET_NEXT_INT(ARGS); RETURN->v_int=removeBlock(d,i,true);
}
CK_DLL_CTOR(text_page_ctor) {
    OBJ_MEMBER_UINT(SELF,pageOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,pageOffset)=reinterpret_cast<uintptr_t>(new PageStyle); } catch(...) {}
}
CK_DLL_DTOR(text_page_dtor) { delete pageState(SELF,API); OBJ_MEMBER_UINT(SELF,pageOffset)=0; }
CK_DLL_MFUN(text_page_width_get) { auto *p=pageState(SELF,API); RETURN->v_int=p?p->width:-1; }
CK_DLL_MFUN(text_page_width_set) {
    auto *p=pageState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=1&&v<=4096) RETURN->v_int=p->width=v;
}
CK_DLL_MFUN(text_page_height_get) { auto *p=pageState(SELF,API); RETURN->v_int=p?p->height:-1; }
CK_DLL_MFUN(text_page_height_set) {
    auto *p=pageState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=1&&v<=4096) RETURN->v_int=p->height=v;
}
CK_DLL_MFUN(text_page_marginLeft_get) { auto *p=pageState(SELF,API); RETURN->v_int=p?p->marginLeft:-1; }
CK_DLL_MFUN(text_page_marginLeft_set) {
    auto *p=pageState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=2048) RETURN->v_int=p->marginLeft=v;
}
CK_DLL_MFUN(text_page_marginRight_get) { auto *p=pageState(SELF,API); RETURN->v_int=p?p->marginRight:-1; }
CK_DLL_MFUN(text_page_marginRight_set) {
    auto *p=pageState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=2048) RETURN->v_int=p->marginRight=v;
}
CK_DLL_MFUN(text_page_marginTop_get) { auto *p=pageState(SELF,API); RETURN->v_int=p?p->marginTop:-1; }
CK_DLL_MFUN(text_page_marginTop_set) {
    auto *p=pageState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=2048) RETURN->v_int=p->marginTop=v;
}
CK_DLL_MFUN(text_page_marginBottom_get) { auto *p=pageState(SELF,API); RETURN->v_int=p?p->marginBottom:-1; }
CK_DLL_MFUN(text_page_marginBottom_set) {
    auto *p=pageState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-1;
    if(p&&v>=0&&v<=2048) RETURN->v_int=p->marginBottom=v;
}
CK_DLL_MFUN(text_page_backgroundColor_get) { auto *p=pageState(SELF,API); RETURN->v_int=p?p->backgroundColor:-2; }
CK_DLL_MFUN(text_page_backgroundColor_set) {
    auto *p=pageState(SELF,API); auto v=GET_NEXT_INT(ARGS); RETURN->v_int=-2;
    if(p&&v>=-1&&v<=16777215) RETURN->v_int=p->backgroundColor=v;
}
CK_DLL_CTOR(text_node_ctor) {
    OBJ_MEMBER_UINT(SELF,nodeOffset)=0;
    try { OBJ_MEMBER_UINT(SELF,nodeOffset)=reinterpret_cast<uintptr_t>(new Node); } catch(...) {}
}
CK_DLL_DTOR(text_node_dtor) { delete nodeState(SELF,API); OBJ_MEMBER_UINT(SELF,nodeOffset)=0; }
CK_DLL_MFUN(text_node_kind) { auto *n=nodeState(SELF,API); RETURN->v_int=n?n->kind:-1; }
CK_DLL_MFUN(text_node_clear) {
    auto *n=nodeState(SELF,API); RETURN->v_int=0;
    try { if(n) { Node next; *n=std::move(next); RETURN->v_int=1; } } catch(...) {}
}
CK_DLL_MFUN(text_node_set_paragraph) {
    RETURN->v_int=0;
    try {
        auto *n=nodeState(SELF,API); auto text=GET_NEXT_STRING_SAFE(ARGS); auto *style=styleState(GET_NEXT_OBJECT(ARGS),API);
        if(!n||!style||!validText(text,65536)) return;
        Node next; next.kind=1; next.paragraph.text=std::move(text);
        next.paragraph.baseBytes=next.paragraph.text.size(); next.paragraph.style=*style;
        *n=std::move(next); RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(text_node_text) {
    auto *n=nodeState(SELF,API);
    RETURN->v_string=API->object->create_string(VM,n&&n->kind==1?n->paragraph.text.c_str():"",FALSE);
}
CK_DLL_MFUN(text_node_child_count) {
    auto *n=nodeState(SELF,API); RETURN->v_int=n?n->children.size():0;
}
CK_DLL_MFUN(text_node_depth) {
    auto *n=nodeState(SELF,API); NodeStats st;
    RETURN->v_int=n&&nodeStats(*n,st)?st.depth:-1;
}
CK_DLL_MFUN(text_node_count) {
    auto *n=nodeState(SELF,API); NodeStats st;
    RETURN->v_int=n&&nodeStats(*n,st)?static_cast<t_CKINT>(st.nodes):-1;
}
CK_DLL_MFUN(text_node_bytes) {
    auto *n=nodeState(SELF,API); NodeStats st;
    RETURN->v_int=n&&nodeStats(*n,st)?static_cast<t_CKINT>(st.bytes):-1;
}
CK_DLL_MFUN(text_node_copy) {
    RETURN->v_int=0;
    try {
        auto *n=nodeState(SELF,API); auto *source=nodeState(GET_NEXT_OBJECT(ARGS),API);
        if(n&&source) { Node next=*source; *n=std::move(next); RETURN->v_int=1; }
    } catch(...) {}
}
namespace {
bool changeNodeChild(Node *n,t_CKINT index,const Node *child,bool replace) {
    if(!n||!child||n==child||n->kind!=0||index<0) return false;
    if(static_cast<uint64_t>(index)>n->children.size()||(replace&&static_cast<uint64_t>(index)==n->children.size())) return false;
    NodeStats st;
    if(!nodeStats(*n,st)) return false;
    if(replace) {
        NodeStats old;
        if(!nodeStats(n->children[index],old)) return false;
        st.nodes-=old.nodes; st.bytes-=old.bytes;
    }
    if(!nodeStats(*child,st,2)) return false;
    Node copy=*child;
    if(replace) n->children[index]=std::move(copy);
    else n->children.insert(n->children.begin()+index,std::move(copy));
    return true;
}
}
CK_DLL_MFUN(text_node_append) {
    RETURN->v_int=0;
    try { auto *n=nodeState(SELF,API); auto *c=nodeState(GET_NEXT_OBJECT(ARGS),API); if(n) RETURN->v_int=changeNodeChild(n,n->children.size(),c,false); } catch(...) {}
}
CK_DLL_MFUN(text_node_insert) {
    RETURN->v_int=0;
    try { auto *n=nodeState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *c=nodeState(GET_NEXT_OBJECT(ARGS),API); RETURN->v_int=changeNodeChild(n,i,c,false); } catch(...) {}
}
CK_DLL_MFUN(text_node_replace) {
    RETURN->v_int=0;
    try { auto *n=nodeState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *c=nodeState(GET_NEXT_OBJECT(ARGS),API); RETURN->v_int=changeNodeChild(n,i,c,true); } catch(...) {}
}
CK_DLL_MFUN(text_node_get) {
    RETURN->v_int=0;
    try {
        auto *n=nodeState(SELF,API); auto i=GET_NEXT_INT(ARGS); auto *dest=nodeState(GET_NEXT_OBJECT(ARGS),API);
        if(nodeChildOK(n,i)&&dest&&dest!=n) { Node copy=n->children[i]; *dest=std::move(copy); RETURN->v_int=1; }
    } catch(...) {}
}
CK_DLL_MFUN(text_node_remove) {
    RETURN->v_int=0;
    auto *n=nodeState(SELF,API); auto i=GET_NEXT_INT(ARGS);
    if(nodeChildOK(n,i)) { n->children.erase(n->children.begin()+i); RETURN->v_int=1; }
}
namespace {
using LayoutPtr=std::unique_ptr<PangoLayout,decltype(&g_object_unref)>;
LayoutPtr makeLayout(cairo_t *cr,const Paragraph &p,int width) {
    double inner=width-p.layout.marginLeft-p.layout.marginRight-p.box.paddingLeft-p.box.paddingRight-2*p.box.borderWidth-listSpace(p.list);
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
LayoutPtr makeMarker(cairo_t *cr,const Paragraph &p) {
    Paragraph marker;
    marker.text=listLabel(p.list);
    marker.baseBytes=marker.text.size();
    marker.style=p.style;
    marker.layout.wrapMode=2;
    return makeLayout(cr,marker,static_cast<int>(std::ceil(p.list.markerWidth)));
}
bool paragraphDimensions(cairo_t *cr,const Paragraph &p,int width,int &w,int &h) {
    auto layout=makeLayout(cr,p,width);
    if(!layout) return false;
    int contentW=0,contentH=0; pango_layout_get_pixel_size(layout.get(),&contentW,&contentH);
    if(p.list.mode) { auto marker=makeMarker(cr,p); if(!marker) return false; int mw=0,mh=0; pango_layout_get_pixel_size(marker.get(),&mw,&mh); contentH=std::max(contentH,mh); }
    double totalW=contentW+p.layout.marginLeft+p.layout.marginRight+p.box.paddingLeft+p.box.paddingRight+2*p.box.borderWidth+listSpace(p.list);
    double totalH=contentH+p.layout.marginTop+p.layout.marginBottom+p.box.paddingTop+p.box.paddingBottom+2*p.box.borderWidth;
    if(totalW<0||totalH<0||totalW>100000000||totalH>100000000) return false;
    w=static_cast<int>(std::ceil(totalW)); h=static_cast<int>(std::ceil(totalH)); return true;
}
bool tableGeometry(cairo_t *cr,const Table &t,int width,std::vector<int> &xs,std::vector<int> &heights,int &height) {
    xs.assign(t.cols+1,0); heights.assign(t.rows,0); height=0;
    int available=width-(t.cols-1)*t.columnGap;
    if(available<t.cols) return false;
    double sum=0,partial=0; for(double w:t.weights) sum+=w;
    for(int c=0;c<t.cols;c++) { partial+=t.weights[c]; xs[c+1]=c+1==t.cols?available:static_cast<int>(std::lround(available*partial/sum)); }
    for(int r=0;r<t.rows;r++) {
        for(int c=0;c<t.cols;c++) {
            int w=0,h=0,cw=xs[c+1]-xs[c];
            if(cw<1||!paragraphDimensions(cr,t.cells[r*t.cols+c],cw,w,h)) return false;
            heights[r]=std::max(heights[r],h);
        }
        if(heights[r]>100000000-height) return false;
        height+=heights[r];
        if(r+1<t.rows) { if(t.rowGap>100000000-height) return false; height+=t.rowGap; }
    }
    return true;
}
bool drawTable(cairo_t *cr,const Table &t,int width,int canvasHeight,int64_t &y,bool draw) {
    std::vector<int> xs,heights; int height=0;
    if(!tableGeometry(cr,t,width,xs,heights,height)||height>100000000-y) return false;
    for(int r=0;r<t.rows;r++) {
        if(draw&&y<canvasHeight) for(int c=0;c<t.cols;c++) {
            const auto &p=t.cells[r*t.cols+c]; const auto &b=p.box;
            if(b.opacity==0) continue;
            int cw=xs[c+1]-xs[c];
            auto l=makeLayout(cr,p,cw); if(!l) return false;
            double x=xs[c]+c*t.columnGap+p.layout.marginLeft,top=y+p.layout.marginTop;
            double bw=cw-p.layout.marginLeft-p.layout.marginRight,bh=heights[r]-p.layout.marginTop-p.layout.marginBottom;
            auto color=[&](t_CKINT rgb){cairo_set_source_rgb(cr,((rgb>>16)&255)/255.0,((rgb>>8)&255)/255.0,(rgb&255)/255.0);};
            cairo_save(cr); cairo_rectangle(cr,x,top,bw,bh); cairo_clip(cr);
            if(b.opacity<1) cairo_push_group(cr);
            if(b.backgroundColor>=0) { color(b.backgroundColor); cairo_paint(cr); }
            if(b.borderWidth>0) {
                color(b.borderColor); cairo_set_line_width(cr,b.borderWidth);
                double half=b.borderWidth/2;
                cairo_rectangle(cr,x+half,top+half,bw-b.borderWidth,bh-b.borderWidth); cairo_stroke(cr);
            }
            double tx=x+b.borderWidth+b.paddingLeft,ty=top+b.borderWidth+b.paddingTop;
            cairo_save(cr); cairo_rectangle(cr,tx,ty,bw-2*b.borderWidth-b.paddingLeft-b.paddingRight,bh-2*b.borderWidth-b.paddingTop-b.paddingBottom); cairo_clip(cr);
            cairo_set_source_rgb(cr,0,0,0); cairo_move_to(cr,tx,ty); pango_cairo_show_layout(cr,l.get()); cairo_restore(cr);
            if(b.opacity<1) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr,b.opacity); }
            cairo_restore(cr);
        }
        y+=heights[r]; if(r+1<t.rows) y+=t.rowGap;
    }
    return cairo_status(cr)==CAIRO_STATUS_SUCCESS;
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
        for(const auto &b:d->tables) {
            if(!drawTable(cr.get(),b.table,width,0,totalH,false)) return;
            maxW=std::max(maxW,static_cast<int>(width));
        }
        API->object->array_int_set_idx(a,0,maxW); API->object->array_int_set_idx(a,1,totalH); RETURN->v_int=1;
    } catch(...) {}
}
namespace {
using SurfacePtr=std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)>;
bool validCanvas(t_CKINT w,t_CKINT h,t_CKINT bg) {
    return w>=1&&w<=4096&&h>=1&&h<=4096&&w*h<=4194304&&bg>=-1&&bg<=0xffffff;
}
bool drawDocument(cairo_t *cr,const Document &d,int w,int h) {
    // Validate all paragraphs, including those beyond the visible canvas.
    int64_t y=0; size_t block=0;
    for(size_t pi=0;pi<=d.paragraphs.size();++pi) {
        while(block<d.tables.size()&&d.tables[block].after==pi) {
            if(!drawTable(cr,d.tables[block].table,w,h,y,true)) return false;
            ++block;
        }
        if(pi==d.paragraphs.size()) break;
        const auto &p=d.paragraphs[pi];
        auto layout=makeLayout(cr,p,w);
        if(!layout) return false;
        int contentW=0,contentH=0;
        pango_layout_get_pixel_size(layout.get(),&contentW,&contentH);
        LayoutPtr marker(nullptr,g_object_unref);
        if(p.list.mode) { marker=makeMarker(cr,p); if(!marker) return false; int mw=0,mh=0; pango_layout_get_pixel_size(marker.get(),&mw,&mh); contentH=std::max(contentH,mh); }
        double totalW=contentW+p.layout.marginLeft+p.layout.marginRight+p.box.paddingLeft+p.box.paddingRight+2*p.box.borderWidth+listSpace(p.list);
        double totalH=contentH+p.layout.marginTop+p.layout.marginBottom+p.box.paddingTop+p.box.paddingBottom+2*p.box.borderWidth;
        if(totalW<0||totalH<0||totalW>100000000||totalH>100000000) return false;
        if(y<h&&!p.list.mode&&p.box.opacity==1&&p.box.borderWidth==0&&p.box.backgroundColor<0&&p.box.paddingLeft==0&&p.box.paddingRight==0&&p.box.paddingTop==0&&p.box.paddingBottom==0) {
            cairo_save(cr);
            cairo_rectangle(cr,p.layout.marginLeft,0,w-p.layout.marginLeft-p.layout.marginRight,h);
            cairo_clip(cr);
            cairo_move_to(cr,p.layout.marginLeft,y+p.layout.marginTop);
            cairo_set_source_rgb(cr,0,0,0);
            pango_cairo_show_layout(cr,layout.get());
            cairo_restore(cr);
        } else if(y<h&&p.box.opacity>0) {
            const auto &box=p.box;
            double x=p.layout.marginLeft, top=y+p.layout.marginTop;
            double bw=w-p.layout.marginLeft-p.layout.marginRight;
            double bh=contentH+box.paddingTop+box.paddingBottom+2*box.borderWidth;
            auto color=[&](t_CKINT rgb) { cairo_set_source_rgb(cr,((rgb>>16)&255)/255.0,((rgb>>8)&255)/255.0,(rgb&255)/255.0); };
            cairo_save(cr);
            cairo_rectangle(cr,x,top,bw,bh);
            cairo_clip(cr);
            if(box.opacity<1) cairo_push_group(cr);
            if(box.backgroundColor>=0) {
                color(box.backgroundColor); cairo_paint(cr);
            }
            if(box.borderWidth>0) {
                color(box.borderColor);
                cairo_set_line_width(cr,box.borderWidth);
                double half=box.borderWidth/2;
                cairo_rectangle(cr,x+half,top+half,bw-box.borderWidth,bh-box.borderWidth);
                cairo_stroke(cr);
            }
            cairo_save(cr);
            double tx=x+box.borderWidth+box.paddingLeft,ty=top+box.borderWidth+box.paddingTop;
            cairo_rectangle(cr,tx,ty,bw-2*box.borderWidth-box.paddingLeft-box.paddingRight,contentH);
            cairo_clip(cr);
            cairo_set_source_rgb(cr,0,0,0);
            if(marker) {
                double mx=tx+p.list.level*p.list.levelIndent;
                cairo_save(cr);
                cairo_rectangle(cr,mx,ty,p.list.markerWidth,contentH); cairo_clip(cr);
                cairo_move_to(cr,mx,ty); pango_cairo_show_layout(cr,marker.get());
                cairo_restore(cr);
            }
            cairo_rectangle(cr,tx+listSpace(p.list),ty,bw-2*box.borderWidth-box.paddingLeft-box.paddingRight-listSpace(p.list),contentH);
            cairo_clip(cr);
            cairo_move_to(cr,tx+listSpace(p.list),ty);
            pango_cairo_show_layout(cr,layout.get());
            cairo_restore(cr);
            if(box.opacity<1) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr,box.opacity); }
            cairo_restore(cr);
        }
        y+=static_cast<int64_t>(std::ceil(totalH));
        if(y>100000000||cairo_status(cr)!=CAIRO_STATUS_SUCCESS) return false;
    }
    return cairo_status(cr)==CAIRO_STATUS_SUCCESS;
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
    if(!drawDocument(cr.get(),d,w,h)) return SurfacePtr(nullptr,cairo_surface_destroy);
    cairo_surface_flush(surface.get());
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return SurfacePtr(nullptr,cairo_surface_destroy);
    return surface;
}
bool validPage(const PageStyle &p) {
    return validCanvas(p.width,p.height,p.backgroundColor)&&p.width>p.marginLeft+p.marginRight&&p.height>p.marginTop+p.marginBottom;
}
struct PageEntry { bool table=false; size_t index=0; int page=0; };
bool planPages(const Document &d,const PageStyle &p,std::vector<PageEntry> &entries,int &count) {
    if(!validPage(p)) return false;
    SurfacePtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,1,1),cairo_surface_destroy);
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return false;
    std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
    if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return false;
    int width=p.width-p.marginLeft-p.marginRight, height=p.height-p.marginTop-p.marginBottom;
    count=1; int used=0;
    auto place=[&](bool table,size_t index,int h) {
        if(h<0||h>height) return false;
        if(h>height-used) { if(count>=4096) return false; ++count; used=0; }
        entries.push_back({table,index,count-1}); used+=h; return true;
    };
    size_t block=0;
    for(size_t pi=0;pi<=d.paragraphs.size();++pi) {
        while(block<d.tables.size()&&d.tables[block].after==pi) {
            std::vector<int> xs,hs; int h=0;
            if(!tableGeometry(cr.get(),d.tables[block].table,width,xs,hs,h)||!place(true,block,h)) return false;
            ++block;
        }
        if(pi==d.paragraphs.size()) break;
        int w=0,h=0;
        if(!paragraphDimensions(cr.get(),d.paragraphs[pi],width,w,h)||!place(false,pi,h)) return false;
    }
    return true;
}
SurfacePtr renderPage(const Document &d,const PageStyle &p,t_CKINT page) {
    std::vector<PageEntry> entries; int count=0;
    if(!planPages(d,p,entries,count)||page<0||page>=count) return SurfacePtr(nullptr,cairo_surface_destroy);
    Document selected;
    for(const auto &e:entries) if(e.page==page) {
        if(e.table) selected.tables.push_back({selected.paragraphs.size(),d.tables[e.index].table});
        else selected.paragraphs.push_back(d.paragraphs[e.index]);
    }
    SurfacePtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,p.width,p.height),cairo_surface_destroy);
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return SurfacePtr(nullptr,cairo_surface_destroy);
    std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
    if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return SurfacePtr(nullptr,cairo_surface_destroy);
    cairo_set_operator(cr.get(),CAIRO_OPERATOR_SOURCE);
    if(p.backgroundColor<0) cairo_set_source_rgba(cr.get(),0,0,0,0);
    else cairo_set_source_rgb(cr.get(),((p.backgroundColor>>16)&255)/255.0,((p.backgroundColor>>8)&255)/255.0,(p.backgroundColor&255)/255.0);
    cairo_paint(cr.get()); cairo_set_operator(cr.get(),CAIRO_OPERATOR_OVER);
    int w=p.width-p.marginLeft-p.marginRight,h=p.height-p.marginTop-p.marginBottom;
    cairo_translate(cr.get(),p.marginLeft,p.marginTop);
    cairo_rectangle(cr.get(),0,0,w,h); cairo_clip(cr.get());
    if(!drawDocument(cr.get(),selected,w,h)) return SurfacePtr(nullptr,cairo_surface_destroy);
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
CK_DLL_MFUN(text_doc_page_count) {
    RETURN->v_int=-1;
    try {
        auto *d=docState(SELF,API); auto *p=pageState(GET_NEXT_OBJECT(ARGS),API);
        std::vector<PageEntry> entries; int n=0;
        if(d&&p&&planPages(*d,*p,entries,n)) RETURN->v_int=n;
    } catch(...) {}
}
CK_DLL_MFUN(text_doc_export_page) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto *p=pageState(GET_NEXT_OBJECT(ARGS),API); auto page=GET_NEXT_INT(ARGS);
        auto a=reinterpret_cast<Chuck_DL_Api::ArrayInt>(GET_NEXT_OBJECT(ARGS));
        if(!d||!p||!validPage(*p)||!a||API->object->array_int_size(a)!=p->width*p->height*4) return;
        int w=p->width,h=p->height;
        auto surface=renderPage(*d,*p,page);
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
CK_DLL_MFUN(text_doc_save_page) {
    RETURN->v_int=0;
    try {
        auto *d=docState(SELF,API); auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto *p=pageState(GET_NEXT_OBJECT(ARGS),API); auto page=GET_NEXT_INT(ARGS);
        if(!d||!p||!validPage(*p)||path.size()<4||path.substr(path.size()-4)!=".png") return;
        auto surface=renderPage(*d,*p,page);
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
    QUERY->begin_class(QUERY,"TextContainer","Object");
    containerOffset=QUERY->add_mvar(QUERY,"int","@textContainerNative",FALSE);
    if(containerOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_container_ctor); QUERY->add_dtor(QUERY,text_container_dtor);
    QUERY->add_mfun(QUERY,text_container_paddingLeft_get,"float","paddingLeft");
    QUERY->add_mfun(QUERY,text_container_paddingLeft_set,"float","paddingLeft");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_container_paddingRight_get,"float","paddingRight");
    QUERY->add_mfun(QUERY,text_container_paddingRight_set,"float","paddingRight");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_container_paddingTop_get,"float","paddingTop");
    QUERY->add_mfun(QUERY,text_container_paddingTop_set,"float","paddingTop");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_container_paddingBottom_get,"float","paddingBottom");
    QUERY->add_mfun(QUERY,text_container_paddingBottom_set,"float","paddingBottom");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_container_borderWidth_get,"float","borderWidth");
    QUERY->add_mfun(QUERY,text_container_borderWidth_set,"float","borderWidth");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_container_borderColor_get,"int","borderColor");
    QUERY->add_mfun(QUERY,text_container_borderColor_set,"int","borderColor");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_container_backgroundColor_get,"int","backgroundColor");
    QUERY->add_mfun(QUERY,text_container_backgroundColor_set,"int","backgroundColor");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_container_opacity_get,"float","opacity");
    QUERY->add_mfun(QUERY,text_container_opacity_set,"float","opacity");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_container_inherit,"int","inherit");
    QUERY->add_arg(QUERY,"TextContainer","source");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextList","Object");
    listOffset=QUERY->add_mvar(QUERY,"int","@textListNative",FALSE);
    if(listOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_list_ctor); QUERY->add_dtor(QUERY,text_list_dtor);
    QUERY->add_mfun(QUERY,text_list_marker_get,"string","marker");
    QUERY->add_mfun(QUERY,text_list_marker_set,"string","marker");
    QUERY->add_arg(QUERY,"string","value");
    QUERY->add_mfun(QUERY,text_list_mode_get,"int","mode");
    QUERY->add_mfun(QUERY,text_list_mode_set,"int","mode");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_list_number_get,"int","number");
    QUERY->add_mfun(QUERY,text_list_number_set,"int","number");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_list_level_get,"int","level");
    QUERY->add_mfun(QUERY,text_list_level_set,"int","level");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_list_levelIndent_get,"float","levelIndent");
    QUERY->add_mfun(QUERY,text_list_levelIndent_set,"float","levelIndent");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_list_markerWidth_get,"float","markerWidth");
    QUERY->add_mfun(QUERY,text_list_markerWidth_set,"float","markerWidth");
    QUERY->add_arg(QUERY,"float","value");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextTable","Object");
    tableOffset=QUERY->add_mvar(QUERY,"int","@textTableNative",FALSE);
    if(tableOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_table_ctor); QUERY->add_dtor(QUERY,text_table_dtor);
    QUERY->add_mfun(QUERY,text_table_create,"int","create");
    QUERY->add_arg(QUERY,"int","rows");
    QUERY->add_arg(QUERY,"int","columns");
    QUERY->add_mfun(QUERY,text_table_rows,"int","rows");
    QUERY->add_mfun(QUERY,text_table_cols,"int","columns");
    QUERY->add_mfun(QUERY,text_table_set_cell,"int","setCell");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_arg(QUERY,"TextStyle","style");
    QUERY->add_mfun(QUERY,text_table_cell_text,"string","cellText");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_mfun(QUERY,text_table_layout,"int","setCellLayout");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"TextParagraphStyle","style");
    QUERY->add_mfun(QUERY,text_table_box,"int","setCellContainer");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"TextContainer","container");
    QUERY->add_mfun(QUERY,text_table_weight_get,"float","columnWeight");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_mfun(QUERY,text_table_weight_set,"float","columnWeight");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"float","value");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextGrid","Object");
    gridOffset=QUERY->add_mvar(QUERY,"int","@textGridNative",FALSE);
    if(gridOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_grid_ctor); QUERY->add_dtor(QUERY,text_grid_dtor);
    QUERY->add_mfun(QUERY,text_grid_create,"int","create");
    QUERY->add_arg(QUERY,"int","rows");
    QUERY->add_arg(QUERY,"int","columns");
    QUERY->add_mfun(QUERY,text_grid_rows,"int","rows");
    QUERY->add_mfun(QUERY,text_grid_cols,"int","columns");
    QUERY->add_mfun(QUERY,text_grid_set_cell,"int","setPanel");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_arg(QUERY,"TextStyle","style");
    QUERY->add_mfun(QUERY,text_grid_cell_text,"string","panelText");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_mfun(QUERY,text_grid_layout,"int","setPanelLayout");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"TextParagraphStyle","style");
    QUERY->add_mfun(QUERY,text_grid_box,"int","setPanelContainer");
    QUERY->add_arg(QUERY,"int","row");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"TextContainer","container");
    QUERY->add_mfun(QUERY,text_grid_weight_get,"float","columnWeight");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_mfun(QUERY,text_grid_weight_set,"float","columnWeight");
    QUERY->add_arg(QUERY,"int","column");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,text_grid_columnGap_get,"int","columnGap");
    QUERY->add_mfun(QUERY,text_grid_columnGap_set,"int","columnGap");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_grid_rowGap_get,"int","rowGap");
    QUERY->add_mfun(QUERY,text_grid_rowGap_set,"int","rowGap");
    QUERY->add_arg(QUERY,"int","value");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextPage","Object");
    pageOffset=QUERY->add_mvar(QUERY,"int","@textPageNative",FALSE);
    if(pageOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_page_ctor); QUERY->add_dtor(QUERY,text_page_dtor);
    QUERY->add_mfun(QUERY,text_page_width_get,"int","width");
    QUERY->add_mfun(QUERY,text_page_width_set,"int","width");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_page_height_get,"int","height");
    QUERY->add_mfun(QUERY,text_page_height_set,"int","height");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_page_marginLeft_get,"int","marginLeft");
    QUERY->add_mfun(QUERY,text_page_marginLeft_set,"int","marginLeft");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_page_marginRight_get,"int","marginRight");
    QUERY->add_mfun(QUERY,text_page_marginRight_set,"int","marginRight");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_page_marginTop_get,"int","marginTop");
    QUERY->add_mfun(QUERY,text_page_marginTop_set,"int","marginTop");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_page_marginBottom_get,"int","marginBottom");
    QUERY->add_mfun(QUERY,text_page_marginBottom_set,"int","marginBottom");
    QUERY->add_arg(QUERY,"int","value");
    QUERY->add_mfun(QUERY,text_page_backgroundColor_get,"int","backgroundColor");
    QUERY->add_mfun(QUERY,text_page_backgroundColor_set,"int","backgroundColor");
    QUERY->add_arg(QUERY,"int","value");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"TextNode","Object");
    nodeOffset=QUERY->add_mvar(QUERY,"int","@textNodeNative",FALSE);
    if(nodeOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,text_node_ctor); QUERY->add_dtor(QUERY,text_node_dtor);
    QUERY->add_mfun(QUERY,text_node_kind,"int","kind");
    QUERY->add_mfun(QUERY,text_node_clear,"int","clear");
    QUERY->add_mfun(QUERY,text_node_set_paragraph,"int","setParagraph");
    QUERY->add_arg(QUERY,"string","text");
    QUERY->add_arg(QUERY,"TextStyle","style");
    QUERY->add_mfun(QUERY,text_node_text,"string","text");
    QUERY->add_mfun(QUERY,text_node_child_count,"int","childCount");
    QUERY->add_mfun(QUERY,text_node_depth,"int","depth");
    QUERY->add_mfun(QUERY,text_node_count,"int","nodeCount");
    QUERY->add_mfun(QUERY,text_node_bytes,"int","textBytes");
    QUERY->add_mfun(QUERY,text_node_copy,"int","copyFrom");
    QUERY->add_arg(QUERY,"TextNode","source");
    QUERY->add_mfun(QUERY,text_node_append,"int","appendChild");
    QUERY->add_arg(QUERY,"TextNode","child");
    QUERY->add_mfun(QUERY,text_node_insert,"int","insertChild");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextNode","child");
    QUERY->add_mfun(QUERY,text_node_replace,"int","setChild");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextNode","child");
    QUERY->add_mfun(QUERY,text_node_get,"int","getChild");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextNode","destination");
    QUERY->add_mfun(QUERY,text_node_remove,"int","removeChild");
    QUERY->add_arg(QUERY,"int","index");
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
    QUERY->add_mfun(QUERY,text_doc_set_container,"int","setParagraphContainer");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextContainer","container");
    QUERY->add_mfun(QUERY,text_doc_get_container,"int","getParagraphContainer");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextContainer","container");
    QUERY->add_mfun(QUERY,text_doc_set_list,"int","setParagraphList");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextList","list");
    QUERY->add_mfun(QUERY,text_doc_get_list,"int","getParagraphList");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextList","list");
    QUERY->add_mfun(QUERY,text_doc_list_label,"string","paragraphListLabel");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,text_doc_append_table,"int","appendTable");
    QUERY->add_arg(QUERY,"TextTable","table");
    QUERY->add_mfun(QUERY,text_doc_table_count,"int","tableCount");
    QUERY->add_mfun(QUERY,text_doc_set_table,"int","setTable");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextTable","table");
    QUERY->add_mfun(QUERY,text_doc_remove_table,"int","removeTable");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,text_doc_append_grid,"int","appendGrid");
    QUERY->add_arg(QUERY,"TextGrid","grid");
    QUERY->add_mfun(QUERY,text_doc_grid_count,"int","gridCount");
    QUERY->add_mfun(QUERY,text_doc_set_grid,"int","setGrid");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"TextGrid","grid");
    QUERY->add_mfun(QUERY,text_doc_remove_grid,"int","removeGrid");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_mfun(QUERY,text_doc_page_count,"int","pageCount");
    QUERY->add_arg(QUERY,"TextPage","page");
    QUERY->add_mfun(QUERY,text_doc_export_page,"int","exportPage");
    QUERY->add_arg(QUERY,"TextPage","page");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_arg(QUERY,"int[]","pixels");
    QUERY->add_mfun(QUERY,text_doc_save_page,"int","savePagePNG");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"TextPage","page");
    QUERY->add_arg(QUERY,"int","index");
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
