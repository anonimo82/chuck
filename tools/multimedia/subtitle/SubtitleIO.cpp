#include "chuck_dl.h"
#include "subtitle_io.h"
CK_DLL_SFUN(sub_count) {
    RETURN->v_int=-1;
    try {
        std::vector<subtitles::Cue> cues;
        if(subtitles::load(GET_NEXT_STRING_SAFE(ARGS),cues)) RETURN->v_int=cues.size();
    } catch(...) {}
}
CK_DLL_SFUN(sub_start) {
    RETURN->v_float=-1;
    try {
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto index=GET_NEXT_INT(ARGS);
        std::vector<subtitles::Cue> cues;
        if(index>=0 && subtitles::load(path,cues) && static_cast<size_t>(index)<cues.size())
            RETURN->v_float=cues[index].start/1000.0;
    } catch(...) {}
}
CK_DLL_SFUN(sub_end) {
    RETURN->v_float=-1;
    try {
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto index=GET_NEXT_INT(ARGS);
        std::vector<subtitles::Cue> cues;
        if(index>=0 && subtitles::load(path,cues) && static_cast<size_t>(index)<cues.size())
            RETURN->v_float=cues[index].end/1000.0;
    } catch(...) {}
}
CK_DLL_SFUN(sub_text) {
    std::string result;
    try {
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto index=GET_NEXT_INT(ARGS);
        std::vector<subtitles::Cue> cues;
        if(index>=0 && subtitles::load(path,cues) && static_cast<size_t>(index)<cues.size())
            result=cues[index].text;
    } catch(...) {}
    RETURN->v_string=API->object->create_string(VM,result.c_str(),FALSE);
}
CK_DLL_SFUN(sub_convert) {
    RETURN->v_int=0;
    try {
        auto source=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        std::vector<subtitles::Cue> cues;
        if(subtitles::load(source,cues) && subtitles::save(dest,cues)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(sub_write) {
    RETURN->v_int=0;
    try {
        auto dest=GET_NEXT_STRING_SAFE(ARGS), content=GET_NEXT_STRING_SAFE(ARGS);
        auto begin=GET_NEXT_INT(ARGS), finish=GET_NEXT_INT(ARGS);
        if(begin<0 || finish<=begin || finish>=360000000 || content.empty() ||
           content.size()>16384 || content.front()=='\n' || content.back()=='\n' ||
           content.find("\n\n")!=std::string::npos || content.find_first_of("\r<>&")!=std::string::npos ||
           !g_utf8_validate(content.data(),static_cast<gssize>(content.size()),nullptr)) return;
        if(subtitles::save(dest,{{begin,finish,content}})) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_QUERY(SubtitleIO) {
    QUERY->setname(QUERY,"SubtitleIO");
    QUERY->begin_class(QUERY,"SubtitleIO","Object");
    QUERY->add_sfun(QUERY,sub_count,"int","count");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_sfun(QUERY,sub_start,"float","startTime");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_sfun(QUERY,sub_end,"float","endTime");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_sfun(QUERY,sub_text,"string","text");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","index");
    QUERY->add_sfun(QUERY,sub_convert,"int","convert");
    QUERY->add_arg(QUERY,"string","source");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_sfun(QUERY,sub_write,"int","writeCue");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"string","content");
    QUERY->add_arg(QUERY,"int","beginMs");
    QUERY->add_arg(QUERY,"int","endMs");
    return QUERY->end_class(QUERY);
}
