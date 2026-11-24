// Optional offline video I/O for ChucK.
#include "chuck_dl.h"
#include "../file_compat.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/mem.h>
}
#include <cstdio>
#include <cerrno>
#include <memory>
#include <string>
#include <vector>
namespace {
struct Input {
    AVFormatContext *ctx = nullptr;
    int video = -1;
    ~Input() { avformat_close_input(&ctx); }
    bool open(const std::string &path) {
        FILE *f = std::fopen(path.c_str(), "rb");
        if(!f) return false;
        std::fclose(f);
        if(avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0 ||
           avformat_find_stream_info(ctx, nullptr) < 0) return false;
        video = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        return video >= 0;
    }
};
struct Codec {
    AVCodecContext *ctx = nullptr;
    ~Codec() { avcodec_free_context(&ctx); }
};
struct Frame {
    AVFrame *p = av_frame_alloc();
    ~Frame() { av_frame_free(&p); }
};
struct Packet {
    AVPacket *p = av_packet_alloc();
    ~Packet() { av_packet_free(&p); }
};
struct Scale {
    SwsContext *p = nullptr;
    ~Scale() { sws_freeContext(p); }
};
bool extract(Input &input, const std::string &dest) {
    auto *par = input.ctx->streams[input.video]->codecpar;
    const auto *decoder = avcodec_find_decoder(par->codec_id);
    if(!decoder) return false;
    Codec codec;
    codec.ctx = avcodec_alloc_context3(decoder);
    if(!codec.ctx || avcodec_parameters_to_context(codec.ctx, par) < 0 ||
       avcodec_open2(codec.ctx, decoder, nullptr) < 0) return false;
    Frame frame;
    Packet packet;
    if(!frame.p || !packet.p) return false;
    bool found = false;
    while(av_read_frame(input.ctx, packet.p) >= 0) {
        if(packet.p->stream_index == input.video) {
            int sent = avcodec_send_packet(codec.ctx, packet.p);
            if(sent >= 0 || sent == AVERROR(EAGAIN))
                found = avcodec_receive_frame(codec.ctx, frame.p) >= 0;
        }
        av_packet_unref(packet.p);
        if(found) break;
    }
    if(!found && avcodec_send_packet(codec.ctx, nullptr) >= 0)
        found = avcodec_receive_frame(codec.ctx, frame.p) >= 0;
    if(!found) return false;
    const int w = frame.p->width, h = frame.p->height;
    if(w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    Scale scale;
    scale.p = sws_getContext(w, h, static_cast<AVPixelFormat>(frame.p->format),
                            w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                            nullptr, nullptr, nullptr);
    if(!scale.p) return false;
    // Align and pad rows for swscale SIMD writes.
    const int stride = (w * 3 + 31) & ~31;
    std::vector<unsigned char> rgb(static_cast<size_t>(stride) * h + 64);
    uint8_t *planes[4] = {rgb.data(), nullptr, nullptr, nullptr};
    int strides[4] = {stride, 0, 0, 0};
    if(sws_scale(scale.p, frame.p->data, frame.p->linesize, 0, h, planes, strides) != h)
        return false;
    // PPM P6 is lossless RGB; no additional image encoder dependency.
    FILE *out = mediaio::openNew(dest.c_str());
    if(!out) return false;
    bool ok = std::fprintf(out, "P6\n%d %d\n255\n", w, h) > 0;
    for(int y = 0; ok && y < h; ++y)
        ok = std::fwrite(rgb.data() + static_cast<size_t>(y)*stride, 1, w*3, out)
             == static_cast<size_t>(w*3);
    if(std::fclose(out) != 0) ok = false;
    if(!ok) std::remove(dest.c_str());
    return ok;
}

 // Export video and audio streams without decoding or re-encoding.
 // A custom seekable AVIO writes only to an exclusively created local file.
int writeBytes(void *opaque, const uint8_t *buf, int size) {
    return std::fwrite(buf, 1, size, static_cast<FILE *>(opaque)) == static_cast<size_t>(size)
        ? size : AVERROR(EIO);
}
int64_t seekBytes(void *opaque, int64_t offset, int whence) {
    FILE *f = static_cast<FILE *>(opaque);
#ifdef _WIN32
    auto seek = _fseeki64;
    auto tell = _ftelli64;
#else
    auto seek = fseeko;
    auto tell = ftello;
#endif
    if(whence == AVSEEK_SIZE) {
        auto pos = tell(f);
        if(pos < 0 || seek(f, 0, SEEK_END)) return AVERROR(EIO);
        auto size = tell(f);
        if(seek(f, pos, SEEK_SET)) return AVERROR(EIO);
        return size < 0 ? AVERROR(EIO) : size;
    }
    whence &= ~AVSEEK_FORCE;
    if(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return AVERROR(EINVAL);
    if(seek(f, offset, whence)) return AVERROR(EIO);
    auto pos = tell(f);
    return pos < 0 ? AVERROR(EIO) : pos;
}
struct Output {
    AVFormatContext *ctx = nullptr;
    AVIOContext *io = nullptr;
    FILE *file = nullptr;
    const std::string &path;
    bool created = false;
    bool complete = false;
    explicit Output(const std::string &p) : path(p) {}
    ~Output() {
        if(ctx) { ctx->pb = nullptr; avformat_free_context(ctx); }
        if(io) { av_freep(&io->buffer); avio_context_free(&io); }
        if(file) std::fclose(file);
        if(created && !complete) std::remove(path.c_str());
    }
    bool open(const char *format) {
        if(avformat_alloc_output_context2(&ctx, nullptr, format, nullptr) < 0 || !ctx)
            return false;
        file = mediaio::openNew(path.c_str());
        if(!file) return false;
        created = true;
        auto *buffer = static_cast<unsigned char *>(av_malloc(32768));
        if(!buffer) return false;
#if LIBAVFORMAT_VERSION_MAJOR >= 61
        io = avio_alloc_context(buffer, 32768, 1, file, nullptr, writeBytes, seekBytes);
#else
        io = avio_alloc_context(buffer, 32768, 1, file, nullptr,
            [](void *o, uint8_t *b, int n) { return writeBytes(o, b, n); }, seekBytes);
#endif
        if(!io) { av_free(buffer); return false; }
        ctx->pb = io;
        ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
        return true;
    }
    bool finish() {
        if(av_write_trailer(ctx) < 0) return false;
        avio_flush(io);
        bool ok = io->error >= 0;
        if(std::fclose(file) != 0) ok = false;
        file = nullptr;
        complete = ok;
        return ok;
    }
};
bool remux(Input &input, const std::string &dest) {
    const auto dot = dest.find_last_of('.');
    if(dot == std::string::npos) return false;
    const auto ext = dest.substr(dot);
    const char *format = ext == ".mkv" ? "matroska" : ext == ".mp4" ? "mp4" :
                         ext == ".mov" ? "mov" : ext == ".avi" ? "avi" :
                         ext == ".webm" ? "webm" : nullptr;
    if(!format) return false;
    Output out(dest);
    if(!out.open(format)) return false;
    // Preserve all audio and video streams; reject other stream types
    // rather than silently discard subtitles, attachments or data.
    for(unsigned i = 0; i < input.ctx->nb_streams; ++i) {
        auto *src = input.ctx->streams[i];
        if(src->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
           src->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;
        auto *dst = avformat_new_stream(out.ctx, nullptr);
        if(!dst || avcodec_parameters_copy(dst->codecpar, src->codecpar) < 0) return false;
        dst->codecpar->codec_tag = 0;
        dst->time_base = src->time_base;
        dst->avg_frame_rate = src->avg_frame_rate;
        dst->sample_aspect_ratio = src->sample_aspect_ratio;
        dst->disposition = src->disposition;
        if(av_dict_copy(&dst->metadata, src->metadata, 0) < 0) return false;
    }
    if(av_dict_copy(&out.ctx->metadata, input.ctx->metadata, 0) < 0) return false;
    if(avformat_write_header(out.ctx, nullptr) < 0) return false;
    Packet packet;
    if(!packet.p) return false;
    int result;
    while((result = av_read_frame(input.ctx, packet.p)) >= 0) {
        const int i = packet.p->stream_index;
        if(i < 0 || static_cast<unsigned>(i) >= out.ctx->nb_streams) return false;
        av_packet_rescale_ts(packet.p, input.ctx->streams[i]->time_base, out.ctx->streams[i]->time_base);
        packet.p->pos = -1;
        const int written = av_interleaved_write_frame(out.ctx, packet.p);
        av_packet_unref(packet.p);
        if(written < 0) return false;
    }
    return result == AVERROR_EOF && out.finish();
}

}
CK_DLL_SFUN(video_width) {
    RETURN->v_int = -1;
    try { auto path = GET_NEXT_STRING_SAFE(ARGS); Input in;
        if(in.open(path)) RETURN->v_int = in.ctx->streams[in.video]->codecpar->width;
    } catch(...) {}
}
CK_DLL_SFUN(video_height) {
    RETURN->v_int = -1;
    try { auto path = GET_NEXT_STRING_SAFE(ARGS); Input in;
        if(in.open(path)) RETURN->v_int = in.ctx->streams[in.video]->codecpar->height;
    } catch(...) {}
}
CK_DLL_SFUN(video_seconds) {
    RETURN->v_float = -1;
    try { auto path = GET_NEXT_STRING_SAFE(ARGS); Input in;
        if(in.open(path) && in.ctx->duration != AV_NOPTS_VALUE)
            RETURN->v_float = static_cast<double>(in.ctx->duration) / AV_TIME_BASE;
    } catch(...) {}
}
CK_DLL_SFUN(video_fps) {
    RETURN->v_float = -1;
    try { auto path = GET_NEXT_STRING_SAFE(ARGS); Input in;
        if(in.open(path)) {
            AVRational rate = av_guess_frame_rate(in.ctx, in.ctx->streams[in.video], nullptr);
            if(rate.num > 0 && rate.den > 0) RETURN->v_float = av_q2d(rate);
        }
    } catch(...) {}
}
CK_DLL_SFUN(video_frame) {
    RETURN->v_int = 0;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        if(dest.size() < 4 || dest.substr(dest.size()-4) != ".ppm") return;
        Input in;
        if(in.open(path) && extract(in, dest)) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_remux) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        Input in;
        if(in.open(source) && remux(in, dest)) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_QUERY(VideoIO) {
    QUERY->setname(QUERY, "VideoIO");
    QUERY->begin_class(QUERY, "VideoIO", "Object");
    QUERY->add_sfun(QUERY, video_width, "int", "width");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, video_height, "int", "height");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, video_seconds, "float", "seconds");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, video_fps, "float", "fps");
    QUERY->add_arg(QUERY, "string", "path");
    QUERY->add_sfun(QUERY, video_frame, "int", "firstFrame");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_sfun(QUERY, video_remux, "int", "remux");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    return QUERY->end_class(QUERY);
}
