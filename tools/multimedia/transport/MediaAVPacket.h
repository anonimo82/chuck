#pragma once
#include "MediaAudioVideo.h"
#include <array>

namespace mediawire {
// Metadata: kind, stream, sequence, timestamp, rate/width, channels/height,
// sample-frame count / fragment-frame index, pixel offset, pixel count.
// Kind 1=PCM, 2=CKVF, 3=CKVS. All unused fields are zero.
inline int inspectAV(const Bytes &wire,std::array<uint64_t,9> &out) {
    Header h; Bytes payload;
    if(!decode(wire,h,payload)) return 0;
    std::array<uint64_t,9> m{};
    if(h.type==4) {
        PcmInfo p; std::vector<double> samples;
        if(!decodePcm(wire,h,p,samples)) return 0;
        m[0]=1;m[4]=p.rate;m[5]=p.channels;m[6]=p.frames;
    } else if(h.type==6) {
        VideoFrameInfo p; VideoFragment f; Bytes pixels;
        if(decodeVideoFrame(wire,h,p,pixels)) {
            m[0]=2;m[4]=p.width;m[5]=p.height;m[8]=pixels.size()/4;
        } else if(decodeVideoFragment(wire,h,f,pixels)) {
            m[0]=3;m[4]=f.width;m[5]=f.height;m[6]=f.frame;m[7]=f.offset;m[8]=f.count;
        } else return 0;
    } else return 0;
    m[1]=h.stream;m[2]=h.sequence;m[3]=h.timestamp;
    out=m;return static_cast<int>(m[0]);
}
}
