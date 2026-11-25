#pragma once
#include "CaptureFrame.h"
#include <chrono>
#include <string>
#include <memory>
#if defined(MEDIA_CAPTURE_GDI)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(MEDIA_CAPTURE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
namespace media_capture {
inline bool screenSupported() {
#if defined(MEDIA_CAPTURE_GDI) || defined(MEDIA_CAPTURE_X11)
    return true;
#else
    return false;
#endif
}
inline bool screenSize(int &w,int &h) {
#if defined(MEDIA_CAPTURE_GDI)
    w=GetSystemMetrics(SM_CXSCREEN); h=GetSystemMetrics(SM_CYSCREEN);
    return w>0&&h>0;
#elif defined(MEDIA_CAPTURE_X11)
    Display *d=XOpenDisplay(nullptr);
    if(!d) return false;
    w=DisplayWidth(d,DefaultScreen(d)); h=DisplayHeight(d,DefaultScreen(d));
    XCloseDisplay(d); return w>0&&h>0;
#else
    return false;
#endif
}
inline bool screenRegion(int64_t x,int64_t y,int64_t w,int64_t h,int sw,int sh) {
    return Frame::dimensions(w,h)&&x>=0&&y>=0&&x<=sw&&y<=sh&&w<=sw-x&&h<=sh-y;
}
#if defined(MEDIA_CAPTURE_GDI)
struct ScreenDC {
    HDC dc=GetDC(nullptr);
    ~ScreenDC(){if(dc) ReleaseDC(nullptr,dc);}
};
struct MemoryDC {
    HDC dc;
    explicit MemoryDC(HDC parent):dc(CreateCompatibleDC(parent)){}
    ~MemoryDC(){if(dc) DeleteDC(dc);}
};
struct ScreenBitmap {
    HBITMAP bitmap=nullptr;
    ~ScreenBitmap(){if(bitmap) DeleteObject(bitmap);}
};
#elif defined(MEDIA_CAPTURE_X11)
inline unsigned char screenComponent(unsigned long pixel,unsigned long mask) {
    if(!mask) return 0;
    while(!(mask&1)) {mask>>=1;pixel>>=1;}
    return static_cast<unsigned char>(std::lround(255.0*double(pixel&mask)/double(mask)));
}
#endif
inline bool screenSnapshot(int64_t x,int64_t y,int64_t w,int64_t h,Frame &result) {
    int sw=0,sh=0;
    if(!Frame::dimensions(w,h)||x<0||y<0||!screenSize(sw,sh)||!screenRegion(x,y,w,h,sw,sh)) return false;
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now();
    Frame next; next.width=static_cast<int>(w); next.height=static_cast<int>(h);
    next.rgba.resize(static_cast<size_t>(w)*h*4);
#if defined(MEDIA_CAPTURE_GDI)
    ScreenDC screen;
    if(!screen.dc) return false;
    ScreenBitmap bitmap;
    MemoryDC mem(screen.dc);
    if(!mem.dc) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=next.width;
    info.bmiHeader.biHeight=-next.height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    info.bmiHeader.biCompression=BI_RGB;
    void *bits=nullptr;
    bitmap.bitmap=CreateDIBSection(screen.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap.bitmap||!bits) return false;
    auto old=SelectObject(mem.dc,bitmap.bitmap);
    if(!old||old==HGDI_ERROR) return false;
    bool ok=BitBlt(mem.dc,0,0,next.width,next.height,screen.dc,
        static_cast<int>(x),static_cast<int>(y),SRCCOPY|CAPTUREBLT)!=0;
    GdiFlush();
    SelectObject(mem.dc,old);
    if(!ok) return false;
    auto *src=static_cast<const unsigned char*>(bits);
    for(size_t k=0;k<next.rgba.size();k+=4) {
        next.rgba[k]=src[k+2]; next.rgba[k+1]=src[k+1];
        next.rgba[k+2]=src[k]; next.rgba[k+3]=255;
    }
#elif defined(MEDIA_CAPTURE_X11)
    std::unique_ptr<Display,decltype(&XCloseDisplay)> d(XOpenDisplay(nullptr),XCloseDisplay);
    if(!d) return false;
    int screen=DefaultScreen(d.get());
    if(!screenRegion(x,y,w,h,DisplayWidth(d.get(),screen),DisplayHeight(d.get(),screen))) return false;
    auto *visual=DefaultVisual(d.get(),screen);
    if(!visual||visual->c_class!=TrueColor) return false;
    auto destroy=[](XImage *image){XDestroyImage(image);};
    std::unique_ptr<XImage,decltype(destroy)> image(XGetImage(d.get(),RootWindow(d.get(),screen),
        static_cast<int>(x),static_cast<int>(y),static_cast<unsigned>(w),static_cast<unsigned>(h),AllPlanes,ZPixmap),destroy);
    if(!image) return false;
    for(int row=0;row<next.height;++row) for(int col=0;col<next.width;++col) {
        auto pixel=XGetPixel(image.get(),col,row);
        size_t k=(static_cast<size_t>(row)*next.width+col)*4;
        next.rgba[k]=screenComponent(pixel,image->red_mask);
        next.rgba[k+1]=screenComponent(pixel,image->green_mask);
        next.rgba[k+2]=screenComponent(pixel,image->blue_mask);
        next.rgba[k+3]=255;
    }
#else
    return false;
#endif
    next.seconds=std::chrono::duration<double>(Clock::now()-start).count();
    result=std::move(next); return true;
}
}
