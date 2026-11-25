#pragma once
#include "CaptureFrame.h"
#include <algorithm>
#if defined(MEDIA_CAPTURE_OPENGL)
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif
namespace media_capture {
inline bool graphicsPixels(Frame &frame,bool bottomUp,bool opaque) {
    if(!frame.valid()) return false;
    size_t row=static_cast<size_t>(frame.width)*4;
    if(bottomUp) for(int y=0;y<frame.height/2;++y)
        std::swap_ranges(frame.rgba.begin()+y*row,frame.rgba.begin()+(y+1)*row,
                         frame.rgba.begin()+(frame.height-1-y)*row);
    if(opaque) for(size_t i=3;i<frame.rgba.size();i+=4) frame.rgba[i]=255;
    return true;
}
inline bool graphicsSupported() {
#if defined(MEDIA_CAPTURE_OPENGL)
    return true;
#else
    return false;
#endif
}
inline bool graphicsCurrent() {
#if defined(MEDIA_CAPTURE_OPENGL)
    return glGetString(GL_VERSION)!=nullptr;
#else
    return false;
#endif
}
inline bool graphicsSnapshot(int64_t x,int64_t y,int64_t w,int64_t h,double seconds,Frame &result) {
#if defined(MEDIA_CAPTURE_OPENGL)
    if(!Frame::dimensions(w,h)||x<0||y<0||x>2147483647-w||y>2147483647-h||
       !std::isfinite(seconds)||seconds<0||!graphicsCurrent()) return false;
    // Require desktop GL 2.1+ for safe detection of pixel pack buffers.
    const auto *version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if(!version||version[0]<'2'||version[0]>'9'||
       (version[0]=='2'&&(version[1]!='.'||version[2]<'1'))) return false;
    // Reject pre-existing errors instead of silently treating failed readback as black.
    // glGetError consumes one pending error; callers must enter with a clean error state.
    if(glGetError()!=GL_NO_ERROR) return false;
    GLint packBuffer=0,viewport[4]={0,0,0,0};
    glGetIntegerv(0x88ED,&packBuffer); // GL_PIXEL_PACK_BUFFER_BINDING
    glGetIntegerv(GL_VIEWPORT,viewport);
    if(glGetError()!=GL_NO_ERROR||packBuffer!=0||x<viewport[0]||y<viewport[1]||
       x+w>static_cast<int64_t>(viewport[0])+viewport[2]||
       y+h>static_cast<int64_t>(viewport[1])+viewport[3]) return false;
    Frame next;next.width=static_cast<int>(w);next.height=static_cast<int>(h);next.seconds=seconds;
    next.rgba.resize(static_cast<size_t>(w)*h*4);
    const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS};
    GLint saved[4]={};
    for(int i=0;i<4;++i) glGetIntegerv(keys[i],&saved[i]);
    if(glGetError()!=GL_NO_ERROR) return false;
    glPixelStorei(keys[0],1);
    for(int i=1;i<4;++i) glPixelStorei(keys[i],0);
    glReadPixels(static_cast<GLint>(x),static_cast<GLint>(y),static_cast<GLsizei>(w),
                 static_cast<GLsizei>(h),GL_RGBA,GL_UNSIGNED_BYTE,next.rgba.data());
    bool ok=glGetError()==GL_NO_ERROR;
    for(int i=0;i<4;++i) glPixelStorei(keys[i],saved[i]);
    if(glGetError()!=GL_NO_ERROR||!ok) return false;
    if(!graphicsPixels(next,true,true)) return false;
    result=std::move(next);return true;
#else
    return false;
#endif
}
}
