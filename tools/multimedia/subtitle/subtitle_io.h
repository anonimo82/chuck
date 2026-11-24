#pragma once
#include "../file_compat.h"
#include <glib.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <algorithm>
namespace subtitles {
struct Cue { int64_t start=0,end=0; std::string text; };
inline int format(const std::string &p) {
    if(p.size()>=4 && p.substr(p.size()-4)==".srt") return 1;
    if(p.size()>=4 && p.substr(p.size()-4)==".vtt") return 2;
    return 0;
}
inline bool timestamp(const std::string &s,int fmt,int64_t &ms) {
    size_t pos=0;
    auto digits=[&](int n,int &v) {
        v=0;
        for(int i=0;i<n;++i) {
            if(pos>=s.size() || s[pos]<'0' || s[pos]>'9') return false;
            v=v*10+s[pos++]-'0';
        }
        return true;
    };
    auto sep=[&](char c){return pos<s.size() && s[pos++]==c;};
    int h=0,m=0,sec=0,f=0;
    if(fmt==2 && s.size()==9) {
        if(!digits(2,m)) return false;
    } else if(!digits(2,h)||!sep(':')||!digits(2,m)) return false;
    if(!sep(':')||!digits(2,sec)||!sep(fmt==1?',':'.')||!digits(3,f)||
       pos!=s.size() || m>=60 || sec>=60) return false;
    ms=((h*60LL+m)*60+sec)*1000+f;
    return true;
}
inline bool load(const std::string &path,std::vector<Cue> &cues) {
    int fmt=format(path);
    if(!fmt) return false;
    std::ifstream in(path,std::ios::binary);
    if(!in) return false;
    in.seekg(0,std::ios::end);
    auto size=in.tellg();
    if(size<=0 || size>16*1024*1024) return false;
    in.seekg(0,std::ios::beg);
    std::string data(static_cast<size_t>(size),'\0');
    if(!in.read(&data[0],size)) return false;
    if(data.compare(0,3,"\xef\xbb\xbf")==0) data.erase(0,3);
    if(!g_utf8_validate(data.data(),static_cast<gssize>(data.size()),nullptr)) return false;
    std::vector<std::string> lines;
    size_t at=0;
    while(at<data.size()) {
        size_t e=data.find('\n',at);
        if(e==std::string::npos) e=data.size();
        std::string line=data.substr(at,e-at);
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.find('\r')!=std::string::npos) return false;
        lines.push_back(std::move(line));
        at=e+1;
    }
    size_t i=0;
    if(fmt==2) {
        if(lines.empty() || lines[0]!="WEBVTT") return false;
        ++i;
        if(i<lines.size() && !lines[i].empty()) return false;
    }
    while(i<lines.size()) {
        while(i<lines.size() && lines[i].empty()) ++i;
        if(i==lines.size()) break;
        if(cues.size()>=100000) return false;
        if(fmt==1) {
            if(lines[i].empty() || lines[i].find_first_not_of("0123456789")!=std::string::npos) return false;
            ++i;
        }
        // Deliberately accept plain WebVTT cues only, no IDs/settings/NOTE/STYLE.
        if(i==lines.size()) return false;
        const auto &timing=lines[i++];
        auto arrow=timing.find(" --> ");
        Cue cue;
        if(arrow==std::string::npos || !timestamp(timing.substr(0,arrow),fmt,cue.start) ||
           !timestamp(timing.substr(arrow+5),fmt,cue.end) || cue.end<=cue.start ||
           (!cues.empty() && cue.start<cues.back().start)) return false;
        while(i<lines.size() && !lines[i].empty()) {
            if(lines[i].find("-->")!=std::string::npos ||
               lines[i].find('<')!=std::string::npos || lines[i].find('>')!=std::string::npos ||
               lines[i].find('&')!=std::string::npos) return false;
            if(!cue.text.empty()) cue.text+='\n';
            cue.text+=lines[i++];
            if(cue.text.size()>16384) return false;
        }
        if(cue.text.empty()) return false;
        cues.push_back(std::move(cue));
    }
    return !cues.empty();
}
inline std::string stamp(int64_t ms,int fmt) {
    char buf[32];
    std::snprintf(buf,sizeof(buf),"%02d:%02d:%02d%c%03d",
        int(ms/3600000),int(ms/60000%60),int(ms/1000%60),fmt==1?',':'.',int(ms%1000));
    return buf;
}
inline bool save(const std::string &path,const std::vector<Cue> &cues) {
    int fmt=format(path);
    if(!fmt || cues.empty()) return false;
    std::string data=fmt==2?"WEBVTT\n\n":"";
    for(size_t i=0;i<cues.size();++i) {
        const auto &c=cues[i];
        if(fmt==1) data+=std::to_string(i+1)+"\n";
        data+=stamp(c.start,fmt)+" --> "+stamp(c.end,fmt)+"\n"+c.text+"\n\n";
    }
    if(data.size()>16*1024*1024) return false;
    FILE *f=mediaio::openNew(path.c_str());
    if(!f) return false;
    bool ok=std::fwrite(data.data(),1,data.size(),f)==data.size();
    if(std::fclose(f)!=0) ok=false;
    if(!ok) std::remove(path.c_str());
    return ok;
}
}
