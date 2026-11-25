#pragma once
#include "MediaRasterStream.h"
#include "MediaRasterCompressed.h"
#include "MediaRasterPng.h"
#include "MediaVector.h"
#include "MediaModel.h"

namespace mediawire {
// Validate the complete media-specific packet before identifying its format.
inline int graphicsKind(const Bytes &wire,Header &h,uint64_t &a,uint64_t &b) {
    Header candidate; Bytes data;
    if(!decode(wire,candidate,data)) return 0;
    uint64_t x=0,y=0;int kind=0;
    if(candidate.type==1) {
        RasterInfo info;Fragment fragment;Bytes pixels;
        if(decodeRaster(wire,candidate,info,pixels)) {
            kind=1;x=info.width;y=info.height;
        } else if(decodeFragment(wire,candidate,fragment,pixels)) {
            kind=2;x=fragment.width;y=fragment.height;
        } else if(decodeRasterCompressed(wire,candidate,info,pixels)) {
            kind=5;x=info.width;y=info.height;
        } else if(decodeRasterPng(wire,candidate,info,pixels)) {
            kind=6;x=info.width;y=info.height;
        }
    } else if(candidate.type==2) {
        Bytes svg;
        if(decodeVector(wire,candidate,svg)) {kind=3;x=svg.size();}
    } else if(candidate.type==3) {
        std::vector<double> xyz;std::vector<uint32_t> indices;
        if(decodeModel(wire,candidate,xyz,indices)) {kind=4;x=xyz.size()/3;y=indices.size()/3;}
    }
    if(kind){h=candidate;a=x;b=y;}
    return kind;
}
}
