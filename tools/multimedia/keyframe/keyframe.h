#pragma once
// Shared, dependency-free scalar automation engine for offline multimedia.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <locale>
#include <string>
#include <vector>
namespace keyframes {
enum class Mode { Step, Linear, Smooth, EaseIn, EaseOut };
inline bool mode(const std::string &s,Mode &m) {
    if(s=="step") m=Mode::Step;
    else if(s=="linear") m=Mode::Linear;
    else if(s=="smooth") m=Mode::Smooth;
    else if(s=="easeIn") m=Mode::EaseIn;
    else if(s=="easeOut") m=Mode::EaseOut;
    else return false;
    return true;
}
inline double weight(double u,Mode m) {
    u=std::max(0.0,std::min(1.0,u));
    if(m==Mode::Step) return u>=1?1:0;
    if(m==Mode::Smooth) return u*u*(3-2*u);
    if(m==Mode::EaseIn) return u*u;
    if(m==Mode::EaseOut) return u*(2-u);
    return u;
}
inline bool interpolate(double a,double b,double u,Mode m,double &result) {
    if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(u)) return false;
    double t=weight(u,m);
    result=t==0?a:t==1?b:(1-t)*a+t*b;
    return std::isfinite(result);
}
struct Key { double time=0,value=0; Mode outgoing=Mode::Linear; };
struct Curve {
    std::vector<Key> keys;
    bool append(double time,double value,Mode outgoing) {
        if(keys.size()>=100000 || !std::isfinite(time)||time<0||time>1e12||
           !std::isfinite(value)||(!keys.empty()&&time<=keys.back().time)) return false;
        keys.push_back({time,value,outgoing});
        return true;
    }
    bool sample(double time,double &result) const {
        if(keys.empty()||!std::isfinite(time)) return false;
        if(time<=keys.front().time) { result=keys.front().value; return true; }
        if(time>=keys.back().time) { result=keys.back().value; return true; }
        auto b=std::upper_bound(keys.begin(),keys.end(),time,
            [](double t,const Key &k){return t<k.time;});
        auto a=b-1;
        return interpolate(a->value,b->value,(time-a->time)/(b->time-a->time),a->outgoing,result);
    }
};
inline bool load(const std::string &path,Curve &curve) {
    std::ifstream in(path,std::ios::binary);
    if(!in) return false;
    in.seekg(0,std::ios::end);
    auto length=in.tellg();
    if(length<=0||length>8*1024*1024) return false;
    in.seekg(0,std::ios::beg);
    Curve candidate;
    std::string line;
    while(std::getline(in,line)) {
        if(!line.empty()&&line.back()=='\r') line.pop_back();
        if(line.empty()||line[0]=='#') continue;
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        double t,v;
        std::string name,extra;
        Mode m;
        if(!(fields>>t>>v>>name)||(fields>>extra)||!mode(name,m)||!candidate.append(t,v,m)) return false;
    }
    if(!in.eof()||in.bad()||candidate.keys.empty()) return false;
    curve=std::move(candidate);
    return true;
}
}
