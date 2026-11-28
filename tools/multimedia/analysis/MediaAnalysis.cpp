#include "chuck_dl.h"
#include <algorithm>
#include <array>
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
namespace {
double srgbDecode(double v){return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4);}
double srgbEncode(double v){return v<=0.0031308?12.92*v:1.055*std::pow(v,1.0/2.4)-0.055;}
bool unit(double v){return std::isfinite(v)&&v>=0&&v<=1;}
double hueWrap(double v){v=std::fmod(v,360.0);return v<0?v+360:v;}
double labF(double v){constexpr double d=6.0/29;return v>d*d*d?std::cbrt(v):v/(3*d*d)+4.0/29;}
double labInv(double v){constexpr double d=6.0/29;return v>d?v*v*v:3*d*d*(v-4.0/29);}
bool colorConvert(int source,int dest,const double in[3],double out[3]){
    for(int i=0;i<3;i++)if(!std::isfinite(in[i]))return false;
    double r=0,g=0,b=0;
    if(source==0||source==1){
        if(!unit(in[0])||!unit(in[1])||!unit(in[2]))return false;
        r=in[0];g=in[1];b=in[2];
        if(source==1){r=srgbEncode(r);g=srgbEncode(g);b=srgbEncode(b);}
    }else if(source==2||source==3){
        if(!unit(in[1])||!unit(in[2]))return false;
        double h=hueWrap(in[0])/60,s=in[1],v=in[2];
        double c=source==2?v*s:(1-std::abs(2*v-1))*s;
        double x=c*(1-std::abs(std::fmod(h,2.0)-1)),m=source==2?v-c:v-c/2;
        if(h<1){r=c;g=x;}else if(h<2){r=x;g=c;}else if(h<3){g=c;b=x;}
        else if(h<4){g=x;b=c;}else if(h<5){r=x;b=c;}else{r=c;b=x;}
        r+=m;g+=m;b+=m;
    }else if(source==4||source==5){
        double L=in[0],a=in[1],bb=in[2];
        if(L<0||L>100)return false;
        if(source==5){if(a<0)return false;double h=hueWrap(bb)*std::acos(-1.0)/180;bb=a*std::sin(h);a*=std::cos(h);}
        // Direct Lab/LCH conversion preserves out-of-sRGB colors.
        if(dest==4||dest==5){
            out[0]=L;out[1]=dest==4?a:std::hypot(a,bb);
            out[2]=dest==4?bb:(out[1]==0?0:hueWrap(std::atan2(bb,a)*180/std::acos(-1.0)));
            return std::isfinite(out[1])&&std::isfinite(out[2]);
        }
        double fy=(L+16)/116,xx=0.95047*labInv(fy+a/500),yy=labInv(fy),zz=1.08883*labInv(fy-bb/200);
        double lr=3.2404542*xx-1.5371385*yy-0.4985314*zz;
        double lg=-0.9692660*xx+1.8760108*yy+0.0415560*zz;
        double lb=0.0556434*xx-0.2040259*yy+1.0572252*zz;
        if(!std::isfinite(lr)||!std::isfinite(lg)||!std::isfinite(lb))return false;
        r=srgbEncode(std::clamp(lr,0.0,1.0));g=srgbEncode(std::clamp(lg,0.0,1.0));b=srgbEncode(std::clamp(lb,0.0,1.0));
    }else return false;
    r=std::clamp(r,0.0,1.0);g=std::clamp(g,0.0,1.0);b=std::clamp(b,0.0,1.0);
    if(dest==0){out[0]=r;out[1]=g;out[2]=b;}
    else if(dest==1){out[0]=srgbDecode(r);out[1]=srgbDecode(g);out[2]=srgbDecode(b);}
    else if(dest==2||dest==3){
        double maxv=std::max({r,g,b}),minv=std::min({r,g,b}),d=maxv-minv,h=0,l=(maxv+minv)/2;
        if(d>0){if(maxv==r)h=(g-b)/d;else if(maxv==g)h=(b-r)/d+2;else h=(r-g)/d+4;}
        out[0]=hueWrap(h*60);out[1]=d==0?0:(dest==2?d/maxv:d/(1-std::abs(2*l-1)));out[2]=dest==2?maxv:l;
        out[1]=std::clamp(out[1],0.0,1.0);
    }else if(dest==4||dest==5){
        double lr=srgbDecode(r),lg=srgbDecode(g),lb=srgbDecode(b);
        double fx=labF((0.4124564*lr+0.3575761*lg+0.1804375*lb)/0.95047);
        double fy=labF(0.2126729*lr+0.7151522*lg+0.0721750*lb);
        double fz=labF((0.0193339*lr+0.1191920*lg+0.9503041*lb)/1.08883);
        double a=500*(fx-fy),bb=200*(fy-fz);
        out[0]=std::clamp(116*fy-16,0.0,100.0);out[1]=dest==4?a:std::hypot(a,bb);
        out[2]=dest==4?bb:(out[1]==0?0:hueWrap(std::atan2(bb,a)*180/std::acos(-1.0)));
    }else if(dest==6){out[0]=out[1]=out[2]=srgbEncode(std::clamp(0.2126*srgbDecode(r)+0.7152*srgbDecode(g)+0.0722*srgbDecode(b),0.0,1.0));}
    else return false;
    return std::isfinite(out[0])&&std::isfinite(out[1])&&std::isfinite(out[2]);
}
}
CK_DLL_SFUN(color_convert){
    RETURN->v_int=0;
    auto source=GET_NEXT_INT(ARGS),dest=GET_NEXT_INT(ARGS);
    auto input=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
    auto output=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
    if(!input||!output||source<0||source>5||dest<0||dest>6||
       API->object->array_float_size(input)!=3||API->object->array_float_size(output)!=3)return;
    double in[3],out[3];for(int i=0;i<3;i++)in[i]=API->object->array_float_get_idx(input,i);
    if(!colorConvert(static_cast<int>(source),static_cast<int>(dest),in,out))return;
    for(int i=0;i<3;i++)API->object->array_float_set_idx(output,i,out[i]);
    RETURN->v_int=1;
}

namespace {
bool indexedDimensions(t_CKINT w,t_CKINT h){return w>0&&h>0&&w<=4096&&h<=4096&&w*h<=4194304;}
using PaletteEntry=std::array<int,4>;
bool readPalette(CK_DL_API API,IntArr input,std::vector<PaletteEntry>&p){
    if(!input)return false;
    auto n=API->object->array_int_size(input);
    if(n<4||n>1024||n%4)return false;
    p.resize(n/4);
    for(t_CKINT i=0;i<n;i++){
        auto v=API->object->array_int_get_idx(input,i);
        if(v<0||v>255)return false;
        p[i/4][i%4]=static_cast<int>(v);
    }
    return true;
}
size_t nearestColor(const std::vector<PaletteEntry>&p,const double rgba[4]){
    double best=std::numeric_limits<double>::infinity();size_t idx=0;
    for(size_t i=0;i<p.size();i++){
        double d=0;
        for(int c=0;c<4;c++){double e=rgba[c]-p[i][c];d+=e*e;}
        if(d<best){best=d;idx=i;}
    }
    return idx;
}
}
CK_DLL_SFUN(palette_encode){
    RETURN->v_int=0;
    try{
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        auto pixels=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        auto colors=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        auto mode=GET_NEXT_INT(ARGS);
        auto output=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        std::vector<PaletteEntry> p;
        if(!indexedDimensions(w,h)||!pixels||!output||mode<0||mode>1||!readPalette(API,colors,p))return;
        size_t n=static_cast<size_t>(w)*h;
        if(API->object->array_int_size(pixels)!=static_cast<t_CKINT>(4*n)||
           API->object->array_int_size(output)!=static_cast<t_CKINT>(n))return;
        std::vector<PaletteEntry> input(n);
        for(size_t i=0;i<n;i++)for(int c=0;c<4;c++){
            auto v=API->object->array_int_get_idx(pixels,i*4+c);
            if(v<0||v>255)return;
            input[i][c]=static_cast<int>(v);
        }
        std::vector<t_CKINT> result(n);
        // Floyd-Steinberg, left-to-right, RGB error only; alpha remains categorical input.
        std::vector<std::array<double,3>> row(static_cast<size_t>(w)+2),next(row.size());
        for(size_t y=0;y<static_cast<size_t>(h);y++){
            for(size_t x=0;x<static_cast<size_t>(w);x++){
                size_t i=y*w+x;double rgba[4];
                for(int c=0;c<4;c++)rgba[c]=std::clamp(static_cast<double>(input[i][c])+(mode&&c<3?row[x+1][c]:0.0),0.0,255.0);
                size_t k=nearestColor(p,rgba);result[i]=static_cast<t_CKINT>(k);
                if(mode)for(int c=0;c<3;c++){
                    double e=rgba[c]-p[k][c];
                    row[x+2][c]+=e*7/16;next[x][c]+=e*3/16;
                    next[x+1][c]+=e*5/16;next[x+2][c]+=e/16;
                }
            }
            row.swap(next);std::fill(next.begin(),next.end(),std::array<double,3>{});
        }
        for(size_t i=0;i<n;i++)API->object->array_int_set_idx(output,i,result[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(palette_decode){
    RETURN->v_int=0;
    try{
        auto indices=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        auto colors=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        auto output=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        std::vector<PaletteEntry> p;
        if(!indices||!output||!readPalette(API,colors,p))return;
        auto n=API->object->array_int_size(indices);
        if(n<1||n>4194304||API->object->array_int_size(output)!=n*4)return;
        std::vector<t_CKINT> result(static_cast<size_t>(n)*4);
        for(t_CKINT i=0;i<n;i++){
            auto k=API->object->array_int_get_idx(indices,i);
            if(k<0||static_cast<size_t>(k)>=p.size())return;
            for(int c=0;c<4;c++)result[i*4+c]=p[k][c];
        }
        for(size_t i=0;i<result.size();i++)API->object->array_int_set_idx(output,i,result[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(palette_reorder){
    RETURN->v_int=0;
    try{
        auto indices=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        auto colors=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        auto order=reinterpret_cast<IntArr>(GET_NEXT_OBJECT(ARGS));
        std::vector<PaletteEntry> p;
        if(!indices||!colors||!order||indices==colors||order==colors||order==indices||
           !readPalette(API,colors,p)||API->object->array_int_size(order)!=static_cast<t_CKINT>(p.size()))return;
        auto n=API->object->array_int_size(indices);
        if(n<1||n>4194304)return;
        std::vector<int> inverse(p.size(),-1);std::vector<PaletteEntry> reordered(p.size());
        for(size_t i=0;i<p.size();i++){
            auto k=API->object->array_int_get_idx(order,i);
            if(k<0||static_cast<size_t>(k)>=p.size()||inverse[k]>=0)return;
            inverse[k]=static_cast<int>(i);reordered[i]=p[k];
        }
        std::vector<t_CKINT> remapped(n);
        for(t_CKINT i=0;i<n;i++){
            auto k=API->object->array_int_get_idx(indices,i);
            if(k<0||static_cast<size_t>(k)>=p.size())return;
            remapped[i]=inverse[k];
        }
        for(size_t i=0;i<p.size();i++)for(int c=0;c<4;c++)API->object->array_int_set_idx(colors,i*4+c,reordered[i][c]);
        for(t_CKINT i=0;i<n;i++)API->object->array_int_set_idx(indices,i,remapped[i]);
        RETURN->v_int=1;
    }catch(...){}
}

namespace {
bool curveRead(CK_DL_API API,Arr knots,Arr ordinates,t_CKINT mode,std::vector<double>&xs,std::vector<double>&ys){
    if(mode<0||mode>2||!knots||!ordinates)return false;
    auto n=API->object->array_float_size(knots);
    if(n<2||n>65536||API->object->array_float_size(ordinates)!=n)return false;
    if(!readValues(API,knots,xs)||!readValues(API,ordinates,ys))return false;
    for(size_t i=1;i<xs.size();i++)
        if(xs[i]<=xs[i-1]||!std::isfinite(xs[i]-xs[i-1]))return false;
    return true;
}
double curveEvaluate(const std::vector<double>&xs,const std::vector<double>&ys,double v,t_CKINT mode){
    if(!std::isfinite(v))return nanValue();
    if(v<=xs.front())return ys.front();
    if(v>=xs.back())return ys.back();
    size_t j=static_cast<size_t>(std::upper_bound(xs.begin(),xs.end(),v)-xs.begin());
    size_t i=j-1;
    if(mode==0)return ys[i];
    double t=(v-xs[i])/(xs[j]-xs[i]);
    if(mode==2)t=t*t*(3-2*t);
    return ys[i]*(1-t)+ys[j]*t;
}
}
CK_DLL_SFUN(curve_evaluate){
    RETURN->v_float=nanValue();
    try{
        auto knots=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto ordinates=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto v=GET_NEXT_FLOAT(ARGS);
        auto mode=GET_NEXT_INT(ARGS);
        std::vector<double> xs,ys;
        if(!curveRead(API,knots,ordinates,mode,xs,ys))return;
        RETURN->v_float=curveEvaluate(xs,ys,v,mode);
    }catch(...){}
}
CK_DLL_SFUN(curve_apply){
    RETURN->v_int=0;
    try{
        auto knots=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto ordinates=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto values=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto mode=GET_NEXT_INT(ARGS);
        auto output=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        std::vector<double> xs,ys,v;
        if(!output||!curveRead(API,knots,ordinates,mode,xs,ys)||!readValues(API,values,v)||
           API->object->array_float_size(output)!=static_cast<t_CKINT>(v.size()))return;
        for(auto &sampleValue:v){sampleValue=curveEvaluate(xs,ys,sampleValue,mode);if(!std::isfinite(sampleValue))return;}
        for(size_t i=0;i<v.size();i++)API->object->array_float_set_idx(output,i,v[i]);
        RETURN->v_int=1;
    }catch(...){}
}
CK_DLL_SFUN(curve_lut){
    RETURN->v_int=0;
    try{
        auto knots=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto ordinates=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        auto lo=GET_NEXT_FLOAT(ARGS),hi=GET_NEXT_FLOAT(ARGS);
        auto mode=GET_NEXT_INT(ARGS);
        auto output=reinterpret_cast<Arr>(GET_NEXT_OBJECT(ARGS));
        std::vector<double> xs,ys;
        if(!output||!std::isfinite(lo)||!std::isfinite(hi)||lo>=hi||!std::isfinite(hi-lo)||
           !curveRead(API,knots,ordinates,mode,xs,ys))return;
        auto n=API->object->array_float_size(output);
        if(n<2||n>4194304)return;
        std::vector<double> v(n);
        for(t_CKINT i=0;i<n;i++){
            double t=static_cast<double>(i)/(n-1);
            v[i]=curveEvaluate(xs,ys,i==n-1?hi:lo+(hi-lo)*t,mode);
            if(!std::isfinite(v[i]))return;
        }
        for(t_CKINT i=0;i<n;i++)API->object->array_float_set_idx(output,i,v[i]);
        RETURN->v_int=1;
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
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaColor","Object");
    QUERY->add_sfun(QUERY,color_convert,"int","convertColor");
    QUERY->add_arg(QUERY,"int","sourceSpace");
    QUERY->add_arg(QUERY,"int","destinationSpace");
    QUERY->add_arg(QUERY,"float[]","inputColor");
    QUERY->add_arg(QUERY,"float[]","outputColor");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaPalette","Object");
    QUERY->add_sfun(QUERY,palette_encode,"int","encodeIndexed");
    QUERY->add_arg(QUERY,"int","imageWidth");
    QUERY->add_arg(QUERY,"int","imageHeight");
    QUERY->add_arg(QUERY,"int[]","rgbaPixels");
    QUERY->add_arg(QUERY,"int[]","paletteColors");
    QUERY->add_arg(QUERY,"int","ditherMode");
    QUERY->add_arg(QUERY,"int[]","outputIndices");
    QUERY->add_sfun(QUERY,palette_decode,"int","decodeIndexed");
    QUERY->add_arg(QUERY,"int[]","inputIndices");
    QUERY->add_arg(QUERY,"int[]","paletteColors");
    QUERY->add_arg(QUERY,"int[]","outputPixels");
    QUERY->add_sfun(QUERY,palette_reorder,"int","reorderPalette");
    QUERY->add_arg(QUERY,"int[]","imageIndices");
    QUERY->add_arg(QUERY,"int[]","paletteColors");
    QUERY->add_arg(QUERY,"int[]","newOrder");
    if(!QUERY->end_class(QUERY))return FALSE;
    QUERY->begin_class(QUERY,"MediaCurve","Object");
    QUERY->add_sfun(QUERY,curve_evaluate,"float","evaluate");
    QUERY->add_arg(QUERY,"float[]","inputKnots");
    QUERY->add_arg(QUERY,"float[]","outputKnots");
    QUERY->add_arg(QUERY,"float","inputValue");
    QUERY->add_arg(QUERY,"int","interpolationMode");
    QUERY->add_sfun(QUERY,curve_apply,"int","apply");
    QUERY->add_arg(QUERY,"float[]","inputKnots");
    QUERY->add_arg(QUERY,"float[]","outputKnots");
    QUERY->add_arg(QUERY,"float[]","inputValues");
    QUERY->add_arg(QUERY,"int","interpolationMode");
    QUERY->add_arg(QUERY,"float[]","outputValues");
    QUERY->add_sfun(QUERY,curve_lut,"int","buildLUT");
    QUERY->add_arg(QUERY,"float[]","inputKnots");
    QUERY->add_arg(QUERY,"float[]","outputKnots");
    QUERY->add_arg(QUERY,"float","rangeLow");
    QUERY->add_arg(QUERY,"float","rangeHigh");
    QUERY->add_arg(QUERY,"int","interpolationMode");
    QUERY->add_arg(QUERY,"float[]","outputValues");
    return QUERY->end_class(QUERY);
}
