#pragma once
#include "GraphicsCapture.h"
#if defined(MEDIA_CAPTURE_OSMESA)
#include <GL/osmesa.h>
#endif
namespace media_capture {
class OffscreenSession {
#if defined(MEDIA_CAPTURE_OSMESA)
    OSMesaContext context=nullptr;
    std::vector<unsigned char> buffer;
#endif
    int width=0,height=0;
public:
    OffscreenSession()=default;
    OffscreenSession(const OffscreenSession&)=delete;
    OffscreenSession& operator=(const OffscreenSession&)=delete;
    ~OffscreenSession() {
#if defined(MEDIA_CAPTURE_OSMESA)
        if(context) OSMesaDestroyContext(context);
#endif
    }
    static bool supported() {
#if defined(MEDIA_CAPTURE_OSMESA)
        return true;
#else
        return false;
#endif
    }
    bool open(int w,int h) {
#if defined(MEDIA_CAPTURE_OSMESA)
        if(context||!Frame::dimensions(w,h)) return false;
        buffer.resize(static_cast<size_t>(w)*h*4);
        context=OSMesaCreateContextExt(OSMESA_RGBA,24,0,0,nullptr);
        if(!context) return false;
        width=w;height=h;return true;
#else
        return false;
#endif
    }
    bool render(double seconds,int r,int g,int b,Frame &out) {
#if defined(MEDIA_CAPTURE_OSMESA)
        if(!context||!std::isfinite(seconds)||seconds<0||
           r<0||r>255||g<0||g>255||b<0||b>255) return false;
        if(OSMesaGetCurrentContext()) return false;
        if(!OSMesaMakeCurrent(context,buffer.data(),GL_UNSIGNED_BYTE,width,height)) return false;
        struct Release {~Release(){OSMesaMakeCurrent(nullptr,nullptr,GL_UNSIGNED_BYTE,0,0);}} release;
        OSMesaPixelStore(OSMESA_Y_UP,0);
        glViewport(0,0,width,height);
        glClearColor(r/255.0f,g/255.0f,b/255.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glFinish();
        if(glGetError()!=GL_NO_ERROR) return false;
        Frame next;next.width=width;next.height=height;next.seconds=seconds;next.rgba=buffer;
        if(!graphicsPixels(next,false,true)) return false;
        out=std::move(next);return true;
#else
        return false;
#endif
    }
};
}
