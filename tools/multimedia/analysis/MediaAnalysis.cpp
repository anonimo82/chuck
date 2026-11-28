#include "chuck_dl.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
namespace {
using Arr=Chuck_DL_Api::ArrayFloat;
using IntArr=Chuck_DL_Api::ArrayInt;
double nanValue(){return std::numeric_limits<double>::quiet_NaN();}
bool readValues(CK_DL_API API,Arr a,std::vector<double>&v){
    if(!a)return false;
    auto n=API->object->array_float_size(a);
    if(n<=0||n>4194304)return false;
    v.resize(n);
    for(t_CKINT i=0;i<n;i++){v[i]=API->object->array_float_get_idx(a,i);if(!std::isfinite(v[i]))return false;}
    return true;
}
}
CK_DLL_SFUN(stats_summary){
    RETURN->v_int=0;
    try{
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto sample=GET_NEXT_INT(ARGS);
        auto out=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        std::vector<double> v;
        if(!out||API->object->array_float_size(out)!=4||(sample!=0&&sample!=1)||!readValues(API,a,v)||(sample&&v.size()<2))return;
        long double mean=0,m2=0;size_t n=0;
        for(double x:v){long double d=x-mean;mean+=d/++n;m2+=d*(x-mean);}
        auto mm=std::minmax_element(v.begin(),v.end());
        double r[4]={static_cast<double>(mean),static_cast<double>(std::sqrt(std::max(0.0L,m2/(n-sample)))),*mm.first,*mm.second};
        for(double x:r)if(!std::isfinite(x))return;
        for(int i=0;i<4;i++)API->object->array_float_set_idx(out,i,r[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(stats_percentile){
    RETURN->v_float=nanValue();
    try{
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto p=GET_NEXT_FLOAT(ARGS);
        std::vector<double> v;
        if(!std::isfinite(p)||p<0||p>100||!readValues(API,a,v))return;
        std::sort(v.begin(),v.end());
        double k=(v.size()-1)*(p/100.0);size_t i=static_cast<size_t>(k),j=std::min(i+1,v.size()-1);
        double u=k-i;RETURN->v_float=v[i]*(1-u)+v[j]*u;
    }catch(...){}
}
CK_DLL_SFUN(stats_count){
    RETURN->v_int=-1;
    try{
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto lo=GET_NEXT_FLOAT(ARGS),hi=GET_NEXT_FLOAT(ARGS);
        std::vector<double> v;
        if(!std::isfinite(lo)||!std::isfinite(hi)||lo>hi||!readValues(API,a,v))return;
        RETURN->v_int=std::count_if(v.begin(),v.end(),[&](double x){return x>=lo&&x<=hi;});
    }catch(...){}
}
CK_DLL_SFUN(stats_histogram){
    RETURN->v_int=0;
    try{
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto lo=GET_NEXT_FLOAT(ARGS),hi=GET_NEXT_FLOAT(ARGS);
        auto out=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        std::vector<double> v;
        if(!out||!std::isfinite(lo)||!std::isfinite(hi)||lo>=hi||!std::isfinite(hi-lo)||!readValues(API,a,v))return;
        auto bins=API->object->array_int_size(out);if(bins<1||bins>65536)return;
        std::vector<t_CKINT> counts(bins,0);
        for(double x:v)if(x>=lo&&x<=hi){
            size_t i=x==hi?bins-1:std::min(static_cast<size_t>((x-lo)/(hi-lo)*bins),static_cast<size_t>(bins-1));
            counts[i]++;
        }
        for(t_CKINT i=0;i<bins;i++)API->object->array_int_set_idx(out,i,counts[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(stats_compare){
    RETURN->v_int=0;
    try{
        auto a=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto b=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto out=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        std::vector<double> x,y;
        if(!out||API->object->array_float_size(out)!=2||!readValues(API,a,x)||!readValues(API,b,y)||x.size()!=y.size())return;
        long double mae=0,mse=0;
        for(size_t i=0;i<x.size();i++){long double d=static_cast<long double>(x[i])-y[i];mae+=std::abs(d)/x.size();mse+=d*d/x.size();}
        double a1=mae,a2=mse;if(!std::isfinite(a1)||!std::isfinite(a2))return;
        API->object->array_float_set_idx(out,0,a1);API->object->array_float_set_idx(out,1,a2);RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_QUERY(MediaAnalysis){
    QUERY->setname(QUERY,"MediaAnalysis");
    QUERY->begin_class(QUERY,"MediaAnalysis","Object");
    QUERY->add_sfun(QUERY,stats_summary,"int","summary");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_arg(QUERY,"int","sample");
    QUERY->add_arg(QUERY,"float[]","result");
    QUERY->add_sfun(QUERY,stats_percentile,"float","percentile");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_arg(QUERY,"float","percent");
    QUERY->add_sfun(QUERY,stats_count,"int","countRange");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_arg(QUERY,"float","low");
    QUERY->add_arg(QUERY,"float","high");
    QUERY->add_sfun(QUERY,stats_histogram,"int","histogram");
    QUERY->add_arg(QUERY,"float[]","values");
    QUERY->add_arg(QUERY,"float","low");
    QUERY->add_arg(QUERY,"float","high");
    QUERY->add_arg(QUERY,"int[]","bins");
    QUERY->add_sfun(QUERY,stats_compare,"int","compare");
    QUERY->add_arg(QUERY,"float[]","first");
    QUERY->add_arg(QUERY,"float[]","other");
    QUERY->add_arg(QUERY,"float[]","result");
    return QUERY->end_class(QUERY);
}
