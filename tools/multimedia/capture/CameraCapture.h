#pragma once
#include "CaptureFrame.h"
#include <string>
#include <chrono>
#include <memory>
#include <algorithm>
#include <cerrno>
#if defined(MEDIA_CAPTURE_AVDEVICE)
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#endif
namespace media_capture {
inline const char *cameraFormat() {
#if defined(_WIN32)
    return "dshow";
#elif defined(__APPLE__)
    return "avfoundation";
#elif defined(__linux__)
    return "video4linux2";
#else
    return "";
#endif
}
inline bool cameraSupported() {
#if defined(MEDIA_CAPTURE_AVDEVICE)
    avdevice_register_all();
    return av_find_input_format(cameraFormat())!=nullptr;
#else
    return false;
#endif
}
inline std::string cameraAddress(const std::string &name) {
    if(name.empty()||name.size()>1024||name.find('\0')!=std::string::npos) return {};
#if defined(_WIN32)
    // DirectShow uses colon to delimit video and audio source names.
    if(name.find(':')!=std::string::npos) return {};
    return "video="+name;
#elif defined(__APPLE__)
    if(name.find(':')!=std::string::npos||name=="default"||name=="none"||name.find("Capture screen")==0) return {};
    return name+":none";
#elif defined(__linux__)
    if(name.compare(0,10,"/dev/video")!=0||name.size()==10||
       name.find_first_not_of("0123456789",10)!=std::string::npos) return {};
    return name;
#else
    return {};
#endif
}
inline int cameraDevices(std::vector<std::string> &names) {
    names.clear();
#if defined(MEDIA_CAPTURE_AVDEVICE)
    if(!cameraSupported()) return 0;
    AVDeviceInfoList *raw=nullptr;
    int count=avdevice_list_input_sources(av_find_input_format(cameraFormat()),nullptr,nullptr,&raw);
    auto release=[](AVDeviceInfoList *p){avdevice_free_list_devices(&p);};
    std::unique_ptr<AVDeviceInfoList,decltype(release)> list(raw,release);
    if(count<0||!list) return -1;
    for(int i=0;i<list->nb_devices&&i<1024;++i) {
        auto *d=list->devices[i];
        if(!d||!d->device_name) continue;
        bool video=d->nb_media_types==0;
        for(int k=0;k<d->nb_media_types;++k) if(d->media_types[k]==AVMEDIA_TYPE_VIDEO) video=true;
        if(video&&!cameraAddress(d->device_name).empty()) names.emplace_back(d->device_name);
    }
#endif
    return static_cast<int>(names.size());
}
inline bool cameraSnapshot(const std::string &name,int w,int h,Frame &result) {
#if defined(MEDIA_CAPTURE_AVDEVICE)
    if(!Frame::dimensions(w,h)||!cameraSupported()) return false;
    auto address=cameraAddress(name);
    if(address.empty()) return false;
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now(),deadline=start+std::chrono::seconds(5);
    AVFormatContext *raw=avformat_alloc_context();
    if(!raw) return false;
    raw->interrupt_callback.callback=[](void *p)->int {
        return Clock::now()>=*static_cast<Clock::time_point*>(p);
    };
    raw->interrupt_callback.opaque=&deadline;
    AVDictionary *options=nullptr;
    auto dimensions=std::to_string(w)+"x"+std::to_string(h);
    av_dict_set(&options,"video_size",dimensions.c_str(),0);
    int opened=avformat_open_input(&raw,address.c_str(),av_find_input_format(cameraFormat()),&options);
    av_dict_free(&options);
    if(opened<0) { if(raw) avformat_free_context(raw); return false; }
    auto close=[](AVFormatContext *p){avformat_close_input(&p);};
    std::unique_ptr<AVFormatContext,decltype(close)> input(raw,close);
    int stream=-1;
    for(unsigned k=0;k<raw->nb_streams;++k)
        if(raw->streams[k]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {stream=static_cast<int>(k);break;}
    if(stream<0) return false;
    auto *par=raw->streams[stream]->codecpar;
    auto *codec=avcodec_find_decoder(par->codec_id);
    if(!codec) return false;
    auto freeCodec=[](AVCodecContext *p){avcodec_free_context(&p);};
    std::unique_ptr<AVCodecContext,decltype(freeCodec)> decoder(avcodec_alloc_context3(codec),freeCodec);
    if(!decoder||avcodec_parameters_to_context(decoder.get(),par)<0) return false;
    decoder->max_pixels=4194304;
    if(avcodec_open2(decoder.get(),codec,nullptr)<0) return false;
    auto freePacket=[](AVPacket *p){av_packet_free(&p);};
    auto freeFrame=[](AVFrame *p){av_frame_free(&p);};
    std::unique_ptr<AVPacket,decltype(freePacket)> packet(av_packet_alloc(),freePacket);
    std::unique_ptr<AVFrame,decltype(freeFrame)> frame(av_frame_alloc(),freeFrame);
    if(!packet||!frame) return false;
    for(int i=0;i<256&&Clock::now()<deadline;++i) {
        if(av_read_frame(raw,packet.get())<0) return false;
        if(packet->stream_index!=stream) {av_packet_unref(packet.get());continue;}
        int sent=avcodec_send_packet(decoder.get(),packet.get());
        av_packet_unref(packet.get());
        if(sent<0&&sent!=AVERROR(EAGAIN)) return false;
        int got=avcodec_receive_frame(decoder.get(),frame.get());
        if(got==AVERROR(EAGAIN)) continue;
        if(got<0||!Frame::dimensions(frame->width,frame->height)) return false;
        Frame next; next.width=frame->width; next.height=frame->height;
        next.rgba.resize(static_cast<size_t>(next.width)*next.height*4);
        std::unique_ptr<SwsContext,decltype(&sws_freeContext)> scaler(
            sws_getContext(next.width,next.height,static_cast<AVPixelFormat>(frame->format),
                next.width,next.height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr),sws_freeContext);
        if(!scaler) return false;
        uint8_t *dest[]={next.rgba.data(),nullptr,nullptr,nullptr};
        int stride[]={next.width*4,0,0,0};
        if(sws_scale(scaler.get(),frame->data,frame->linesize,0,next.height,dest,stride)!=next.height) return false;
        next.seconds=std::chrono::duration<double>(Clock::now()-start).count();
        result=std::move(next); return true;
    }
#endif
    return false;
}

 // Persistent camera decoder. A session owns all native resources.
class CameraSession {
#if defined(MEDIA_CAPTURE_AVDEVICE)
    using Clock=std::chrono::steady_clock;
    Clock::time_point start{},deadline{};
    AVFormatContext *input=nullptr;
    AVCodecContext *decoder=nullptr;
    AVPacket *packet=nullptr;
    AVFrame *frame=nullptr;
    int stream=-1;
    static int interrupted(void *p) {
        return Clock::now()>=static_cast<CameraSession*>(p)->deadline;
    }
#endif
public:
    CameraSession()=default;
    CameraSession(const CameraSession&)=delete;
    CameraSession& operator=(const CameraSession&)=delete;
    ~CameraSession() {
#if defined(MEDIA_CAPTURE_AVDEVICE)
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&decoder);
        if(input) avformat_close_input(&input);
#endif
    }
    bool open(const std::string &name,int w,int h,int fps) {
#if defined(MEDIA_CAPTURE_AVDEVICE)
        if(input||!Frame::dimensions(w,h)||fps<1||fps>120||!cameraSupported()) return false;
        auto address=cameraAddress(name);
        if(address.empty()) return false;
        start=Clock::now(); deadline=start+std::chrono::seconds(5);
        input=avformat_alloc_context();
        if(!input) return false;
        input->interrupt_callback={interrupted,this};
        auto size=std::to_string(w)+"x"+std::to_string(h), rate=std::to_string(fps);
        AVDictionary *opts=nullptr;
        av_dict_set(&opts,"video_size",size.c_str(),0);
        av_dict_set(&opts,"framerate",rate.c_str(),0);
        int ret=avformat_open_input(&input,address.c_str(),av_find_input_format(cameraFormat()),&opts);
        av_dict_free(&opts);
        if(ret<0) return false;
        for(unsigned i=0;i<input->nb_streams;++i)
            if(input->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {stream=static_cast<int>(i);break;}
        if(stream<0) return false;
        auto *par=input->streams[stream]->codecpar;
        auto *codec=avcodec_find_decoder(par->codec_id);
        if(!codec) return false;
        decoder=avcodec_alloc_context3(codec);
        if(!decoder||avcodec_parameters_to_context(decoder,par)<0) return false;
        decoder->max_pixels=4194304;
        if(avcodec_open2(decoder,codec,nullptr)<0) return false;
        packet=av_packet_alloc(); frame=av_frame_alloc();
        return packet&&frame;
#else
        return false;
#endif
    }
    bool next(Frame &result) {
#if defined(MEDIA_CAPTURE_AVDEVICE)
        if(!input||!decoder||!packet||!frame) return false;
        deadline=Clock::now()+std::chrono::seconds(5);
        for(int i=0;i<256&&Clock::now()<deadline;++i) {
            int got=avcodec_receive_frame(decoder,frame);
            if(got==0) {
                if(!Frame::dimensions(frame->width,frame->height)) return false;
                Frame next; next.width=frame->width; next.height=frame->height;
                next.rgba.resize(static_cast<size_t>(next.width)*next.height*4);
                std::unique_ptr<SwsContext,decltype(&sws_freeContext)> scaler(
                    sws_getContext(next.width,next.height,static_cast<AVPixelFormat>(frame->format),
                        next.width,next.height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr),sws_freeContext);
                if(!scaler) return false;
                uint8_t *dest[]={next.rgba.data(),nullptr,nullptr,nullptr};
                int stride[]={next.width*4,0,0,0};
                if(sws_scale(scaler.get(),frame->data,frame->linesize,0,next.height,dest,stride)!=next.height) return false;
                next.seconds=std::chrono::duration<double>(Clock::now()-start).count();
                result=std::move(next); return true;
            }
            if(got!=AVERROR(EAGAIN)) return false;
            if(av_read_frame(input,packet)<0) return false;
            int sent=0;
            if(packet->stream_index==stream) sent=avcodec_send_packet(decoder,packet);
            av_packet_unref(packet);
            if(sent<0) return false;
        }
#endif
        return false;
    }
};
}
