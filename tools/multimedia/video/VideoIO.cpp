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
#include <cctype>
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
bool writeFramePPM(AVFrame *frame, const std::string &dest) {
    const int w = frame->width, h = frame->height;
    if(w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    Scale scale;
    scale.p = sws_getContext(w, h, static_cast<AVPixelFormat>(frame->format),
                            w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                            nullptr, nullptr, nullptr);
    if(!scale.p) return false;
    // Align and pad rows for swscale SIMD writes.
    const int stride = (w * 3 + 31) & ~31;
    std::vector<unsigned char> rgb(static_cast<size_t>(stride) * h + 64);
    uint8_t *planes[4] = {rgb.data(), nullptr, nullptr, nullptr};
    int strides[4] = {stride, 0, 0, 0};
    if(sws_scale(scale.p, frame->data, frame->linesize, 0, h, planes, strides) != h)
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
    return writeFramePPM(frame.p, dest);
}

 
// Keep completed outputs only when the entire extraction succeeds.
struct ExtractedFiles {
    std::vector<std::string> paths;
    bool complete = false;
    ~ExtractedFiles() {
        if(!complete) for(const auto &path : paths) std::remove(path.c_str());
    }
};
// Extract up to limit decoded frames, starting at zero-based frame index start.
// Return actual count (including a shorter sequence at EOF), or -1 on error.
int extractSequence(Input &input, const std::string &prefix, int start, int limit) {
    if(prefix.empty() || start < 0 || limit < 1 || limit > 100000) return -1;
    auto *stream = input.ctx->streams[input.video];
    const auto *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if(!decoder) return -1;
    Codec codec;
    codec.ctx = avcodec_alloc_context3(decoder);
    if(!codec.ctx || avcodec_parameters_to_context(codec.ctx, stream->codecpar) < 0)
        return -1;
    codec.ctx->pkt_timebase = stream->time_base;
    if(avcodec_open2(codec.ctx, decoder, nullptr) < 0) return -1;
    Frame frame;
    Packet packet;
    if(!frame.p || !packet.p) return -1;
    ExtractedFiles outputs;
    outputs.paths.reserve(static_cast<size_t>(limit));
    int64_t index = 0;
    int written = 0;
    auto drain = [&](bool flushing) {
        while(written < limit) {
            const int ret = avcodec_receive_frame(codec.ctx, frame.p);
            if(ret == AVERROR_EOF) return flushing;
            if(ret == AVERROR(EAGAIN)) return !flushing;
            if(ret < 0) return false;
            if(index++ >= start) {
                char suffix[32];
                std::snprintf(suffix, sizeof suffix, "%06d.ppm", written);
                std::string path = prefix + suffix;
                // Allocate tracking storage before creating the file.
                outputs.paths.push_back(path);
                bool ok;
                try {
                    ok = writeFramePPM(frame.p, path);
                } catch(...) {
                    outputs.paths.pop_back();
                    throw;
                }
                // Never remove a pre-existing file when exclusive creation fails.
                if(!ok) { outputs.paths.pop_back(); return false; }
                ++written;
            }
            av_frame_unref(frame.p);
        }
        return true;
    };
    int ret = 0;
    while(written < limit && (ret = av_read_frame(input.ctx, packet.p)) >= 0) {
        if(packet.p->stream_index == input.video) {
            int sent = avcodec_send_packet(codec.ctx, packet.p);
            av_packet_unref(packet.p);
            if(sent < 0 || !drain(false)) return -1;
        } else av_packet_unref(packet.p);
    }
    if(written < limit) {
        if(ret != AVERROR_EOF || avcodec_send_packet(codec.ctx, nullptr) < 0 ||
           !drain(true)) return -1;
    }
    outputs.complete = true;
    return written;
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



// Merge the selected video and audio streams, retaining their packet timestamps.
// No decoding, resampling, trimming or time stretching.
bool muxAudio(const std::string &videoPath, const std::string &audioPath, const std::string &dest) {
    const auto dot = dest.find_last_of('.');
    if(dot == std::string::npos) return false;
    const auto ext = dest.substr(dot);
    const char *format = ext == ".mkv" ? "matroska" : ext == ".mp4" ? "mp4" :
                         ext == ".mov" ? "mov" : ext == ".webm" ? "webm" : nullptr;
    if(!format) return false;
    Input video, audio;
    if(!video.open(videoPath)) return false;
    {
        std::unique_ptr<FILE, decltype(&fclose)> file(std::fopen(audioPath.c_str(), "rb"), fclose);
        if(!file) return false;
    }
    if(avformat_open_input(&audio.ctx, audioPath.c_str(), nullptr, nullptr) < 0 ||
       avformat_find_stream_info(audio.ctx, nullptr) < 0) return false;
    int audioIndex = av_find_best_stream(audio.ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if(audioIndex < 0) return false;
    Output out(dest);
    if(!out.open(format)) return false;
    AVFormatContext *inputs[2] = {video.ctx, audio.ctx};
    int indices[2] = {video.video, audioIndex};
    for(int i = 0; i < 2; ++i) {
        auto *src = inputs[i]->streams[indices[i]];
        auto *dst = avformat_new_stream(out.ctx, nullptr);
        if(!dst || avcodec_parameters_copy(dst->codecpar, src->codecpar) < 0) return false;
        dst->codecpar->codec_tag = 0;
        dst->time_base = src->time_base;
        dst->avg_frame_rate = src->avg_frame_rate;
        dst->sample_aspect_ratio = src->sample_aspect_ratio;
        if(av_dict_copy(&dst->metadata, src->metadata, 0) < 0) return false;
    }
    if(avformat_write_header(out.ctx, nullptr) < 0) return false;
    Packet packets[2];
    if(!packets[0].p || !packets[1].p) return false;
    bool ready[2] = {false, false}, ended[2] = {false, false}, wrote[2] = {false, false};
    for(;;) {
        for(int i = 0; i < 2; ++i) {
            if(ready[i] || ended[i]) continue;
            int ret;
            while((ret = av_read_frame(inputs[i], packets[i].p)) >= 0) {
                if(packets[i].p->stream_index == indices[i]) {
                    ready[i] = true;
                    break;
                }
                av_packet_unref(packets[i].p);
            }
            if(!ready[i]) {
                if(ret != AVERROR_EOF) return false;
                ended[i] = true;
            }
        }
        if(!ready[0] && !ready[1]) break;
        int i = ready[0] ? 0 : 1;
        if(ready[0] && ready[1]) {
            auto timestamp = [](AVPacket *p) { return p->dts == AV_NOPTS_VALUE ? p->pts : p->dts; };
            auto a = timestamp(packets[0].p), b = timestamp(packets[1].p);
            if(a == AV_NOPTS_VALUE || b == AV_NOPTS_VALUE) return false;
            if(av_compare_ts(a, inputs[0]->streams[indices[0]]->time_base,
                             b, inputs[1]->streams[indices[1]]->time_base) > 0) i = 1;
        }
        auto *p = packets[i].p;
        av_packet_rescale_ts(p, inputs[i]->streams[indices[i]]->time_base,
                             out.ctx->streams[i]->time_base);
        p->stream_index = i;
        p->pos = -1;
        int result = av_interleaved_write_frame(out.ctx, p);
        av_packet_unref(p);
        if(result < 0) return false;
        ready[i] = false;
        wrote[i] = true;
    }
    return wrote[0] && wrote[1] && out.finish();
}

// Strict P6 RGB8 input, with standard whitespace/comments in the header.
bool ppmToken(FILE *f, std::string &token) {
    token.clear();
    int c;
    do {
        c = std::fgetc(f);
        if(c == '#') { do { c = std::fgetc(f); } while(c != EOF && c != '\n'); }
    } while(c != EOF && std::isspace(static_cast<unsigned char>(c)));
    while(c != EOF && !std::isspace(static_cast<unsigned char>(c))) {
        if(token.size() >= 32) return false;
        token += static_cast<char>(c);
        c = std::fgetc(f);
    }
    return c != EOF && !token.empty();
}
bool ppmNumber(FILE *f, int &value) {
    std::string token;
    if(!ppmToken(f, token)) return false;
    value = 0;
    for(char c : token) {
        if(c < '0' || c > '9' || value > 8192) return false;
        value = value * 10 + c - '0';
    }
    return value > 0 && value <= 8192;
}
bool loadPPM(const std::string &path, int &w, int &h, std::vector<unsigned char> &rgb) {
    std::unique_ptr<FILE, decltype(&fclose)> f(std::fopen(path.c_str(), "rb"), fclose);
    std::string magic;
    int maxval;
    if(!f || !ppmToken(f.get(), magic) || magic != "P6" ||
       !ppmNumber(f.get(), w) || !ppmNumber(f.get(), h) ||
       !ppmNumber(f.get(), maxval) || maxval != 255) return false;
    rgb.resize(static_cast<size_t>(w) * h * 3);
    return std::fread(rgb.data(), 1, rgb.size(), f.get()) == rgb.size() &&
           std::fgetc(f.get()) == EOF && !std::ferror(f.get());
}
// Read prefix000000.ppm, prefix000001.ppm, ... and encode lossless RGB.
bool encodeSequence(const std::string &prefix, const std::string &dest, int count, int fps,
                    const std::string &mode = "ffv1", int quality = 0) {
    if(count < 1 || count > 100000 || fps < 1 || fps > 120 ||
       quality < 0 || quality > 51) return false;
    const bool lossless = mode == "ffv1";
    const bool h264 = mode == "h264";
    const bool vp9 = mode == "vp9";
    const char *container = lossless ? "matroska" : h264 ? "mp4" : vp9 ? "webm" : nullptr;
    const char *extension = lossless ? ".mkv" : h264 ? ".mp4" : ".webm";
    const std::string ext(extension);
    if(!container || dest.size() < ext.size() || dest.substr(dest.size()-ext.size()) != ext ||
       (lossless && quality != 0)) return false;
    const auto *encoder = avcodec_find_encoder_by_name(lossless ? "ffv1" : h264 ? "libx264" : "libvpx-vp9");
    if(!encoder) return false;
    Codec codec;
    codec.ctx = avcodec_alloc_context3(encoder);
    if(!codec.ctx) return false;
    Output out(dest);
    Frame frame;
    Packet packet;
    if(!frame.p || !packet.p) return false;
    AVStream *stream = nullptr;
    std::vector<unsigned char> rgb;
    Scale scale;
    auto drain = [&](bool flushing) {
        for(;;) {
            int ret = avcodec_receive_packet(codec.ctx, packet.p);
            if(ret == AVERROR_EOF) return flushing;
            if(ret == AVERROR(EAGAIN)) return !flushing;
            if(ret < 0) return false;
            av_packet_rescale_ts(packet.p, codec.ctx->time_base, stream->time_base);
            packet.p->stream_index = stream->index;
            int written = av_interleaved_write_frame(out.ctx, packet.p);
            av_packet_unref(packet.p);
            if(written < 0) return false;
        }
    };
    for(int i = 0; i < count; ++i) {
        char suffix[32];
        std::snprintf(suffix, sizeof suffix, "%06d.ppm", i);
        int w, h;
        if(!loadPPM(prefix + suffix, w, h, rgb)) return false;
        if(i == 0) {
            if(!lossless && ((w & 1) || (h & 1))) return false;
            if(!out.open(container)) return false;
            codec.ctx->width = w;
            codec.ctx->height = h;
            codec.ctx->pix_fmt = lossless ? AV_PIX_FMT_BGR0 : AV_PIX_FMT_YUV420P;
            codec.ctx->time_base = AVRational{1, fps};
            codec.ctx->framerate = AVRational{fps, 1};
            if(out.ctx->oformat->flags & AVFMT_GLOBALHEADER)
                codec.ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            AVDictionary *options = nullptr;
            if(!lossless) {
                const auto crf = std::to_string(quality);
                if(av_dict_set(&options, "crf", crf.c_str(), 0) < 0 ||
                   (h264 && av_dict_set(&options, "preset", "medium", 0) < 0)) {
                    av_dict_free(&options);
                    return false;
                }
                if(vp9) codec.ctx->bit_rate = 0;
            }
            const int opened = avcodec_open2(codec.ctx, encoder, &options);
            const bool unused = av_dict_count(options) != 0;
            av_dict_free(&options);
            if(opened < 0 || unused) return false;
            if(!lossless) {
                scale.p = sws_getContext(w, h, AV_PIX_FMT_RGB24, w, h,
                    AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if(!scale.p) return false;
            }
            stream = avformat_new_stream(out.ctx, nullptr);
            if(!stream || avcodec_parameters_from_context(stream->codecpar, codec.ctx) < 0) return false;
            stream->time_base = codec.ctx->time_base;
            stream->avg_frame_rate = codec.ctx->framerate;
            if(avformat_write_header(out.ctx, nullptr) < 0) return false;
            frame.p->format = codec.ctx->pix_fmt;
            frame.p->width = w;
            frame.p->height = h;
            if(av_frame_get_buffer(frame.p, 32) < 0) return false;
        }
        if(w != codec.ctx->width || h != codec.ctx->height ||
           av_frame_make_writable(frame.p) < 0) return false;
        if(lossless) {
        for(int y = 0; y < h; ++y) {
            auto *dst = frame.p->data[0] + static_cast<size_t>(y) * frame.p->linesize[0];
            const auto *src = rgb.data() + static_cast<size_t>(y) * w * 3;
            for(int x = 0; x < w; ++x) {
                dst[4*x] = src[3*x+2]; dst[4*x+1] = src[3*x+1];
                dst[4*x+2] = src[3*x]; dst[4*x+3] = 0;
            }
        }
        } else {
            // Pad the input for SIMD reads by swscale.
            rgb.resize(rgb.size() + 64, 0);
            const uint8_t *planes[4] = {rgb.data(), nullptr, nullptr, nullptr};
            int strides[4] = {w * 3, 0, 0, 0};
            if(sws_scale(scale.p, planes, strides, 0, h, frame.p->data, frame.p->linesize) != h)
                return false;
        }
        frame.p->pts = i;
        if(avcodec_send_frame(codec.ctx, frame.p) < 0 || !drain(false)) return false;
    }
    if(avcodec_send_frame(codec.ctx, nullptr) < 0 || !drain(true)) return false;
    return out.finish();
}


// Transcode one video stream and copy compatible audio streams.
// Reject unsupported extra streams instead of silently discarding them.
bool transcode(const std::string &source, const std::string &dest,
               int w, int h, const std::string &mode, int quality) {
    const bool ffv1 = mode == "ffv1", h264 = mode == "h264", vp9 = mode == "vp9";
    const char *container = ffv1 ? "matroska" : h264 ? "mp4" : vp9 ? "webm" : nullptr;
    const std::string ext = ffv1 ? ".mkv" : h264 ? ".mp4" : ".webm";
    if(!container || dest.size() < ext.size() || dest.substr(dest.size()-ext.size()) != ext ||
       w < 1 || h < 1 || w > 8192 || h > 8192 || quality < 0 || quality > 51 ||
       (ffv1 && quality != 0) || (!ffv1 && ((w & 1) || (h & 1)))) return false;
    Input in;
    if(!in.open(source)) return false;
    for(unsigned i = 0; i < in.ctx->nb_streams; ++i) {
        if(static_cast<int>(i) != in.video &&
           in.ctx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;
    }
    auto *src = in.ctx->streams[in.video];
    const auto *decoder = avcodec_find_decoder(src->codecpar->codec_id);
    const auto *encoder = avcodec_find_encoder_by_name(ffv1 ? "ffv1" : h264 ? "libx264" : "libvpx-vp9");
    if(!decoder || !encoder) return false;
    Codec dec, enc;
    dec.ctx = avcodec_alloc_context3(decoder);
    enc.ctx = avcodec_alloc_context3(encoder);
    if(!dec.ctx || !enc.ctx || avcodec_parameters_to_context(dec.ctx, src->codecpar) < 0)
        return false;
    dec.ctx->pkt_timebase = src->time_base;
    if(avcodec_open2(dec.ctx, decoder, nullptr) < 0) return false;
    Output out(dest);
    if(!out.open(container)) return false;
    enc.ctx->width = w;
    enc.ctx->height = h;
    enc.ctx->pix_fmt = ffv1 ? AV_PIX_FMT_BGR0 : AV_PIX_FMT_YUV420P;
    enc.ctx->time_base = src->time_base;
    enc.ctx->framerate = av_guess_frame_rate(in.ctx, src, nullptr);
    enc.ctx->sample_aspect_ratio = AVRational{1,1};
    if(out.ctx->oformat->flags & AVFMT_GLOBALHEADER) enc.ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary *options = nullptr;
    if(!ffv1) {
        const auto crf = std::to_string(quality);
        if(av_dict_set(&options, "crf", crf.c_str(), 0) < 0 ||
           (h264 && av_dict_set(&options, "preset", "medium", 0) < 0)) {
            av_dict_free(&options); return false;
        }
        if(vp9) enc.ctx->bit_rate = 0;
    }
    int opened = avcodec_open2(enc.ctx, encoder, &options);
    bool unused = av_dict_count(options) != 0;
    av_dict_free(&options);
    if(opened < 0 || unused) return false;
    auto *dst = avformat_new_stream(out.ctx, nullptr);
    if(!dst || avcodec_parameters_from_context(dst->codecpar, enc.ctx) < 0) return false;
    dst->time_base = enc.ctx->time_base;
    dst->avg_frame_rate = enc.ctx->framerate;
    dst->sample_aspect_ratio = enc.ctx->sample_aspect_ratio;
    std::vector<int> mapping(in.ctx->nb_streams, -1);
    mapping[in.video] = dst->index;
    for(unsigned i = 0; i < in.ctx->nb_streams; ++i) {
        if(static_cast<int>(i) == in.video) continue;
        auto *audio = in.ctx->streams[i];
        if(avformat_query_codec(out.ctx->oformat, audio->codecpar->codec_id, FF_COMPLIANCE_NORMAL) <= 0)
            return false;
        auto *copy = avformat_new_stream(out.ctx, nullptr);
        if(!copy || avcodec_parameters_copy(copy->codecpar, audio->codecpar) < 0) return false;
        copy->codecpar->codec_tag = 0;
        copy->time_base = audio->time_base;
        copy->disposition = audio->disposition;
        if(av_dict_copy(&copy->metadata, audio->metadata, 0) < 0) return false;
        mapping[i] = copy->index;
    }
    if(av_dict_copy(&dst->metadata, src->metadata, 0) < 0 ||
       av_dict_copy(&out.ctx->metadata, in.ctx->metadata, 0) < 0) return false;
    if(avformat_write_header(out.ctx, nullptr) < 0) return false;
    Frame decoded, scaled;
    Packet inputPacket, outputPacket;
    Scale scale;
    if(!decoded.p || !scaled.p || !inputPacket.p || !outputPacket.p) return false;
    scaled.p->width = w;
    scaled.p->height = h;
    scaled.p->format = enc.ctx->pix_fmt;
    if(av_frame_get_buffer(scaled.p, 32) < 0) return false;
    auto drainEncoder = [&](bool flush) {
        for(;;) {
            int ret = avcodec_receive_packet(enc.ctx, outputPacket.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            av_packet_rescale_ts(outputPacket.p, enc.ctx->time_base, dst->time_base);
            outputPacket.p->stream_index = dst->index;
            int written = av_interleaved_write_frame(out.ctx, outputPacket.p);
            av_packet_unref(outputPacket.p);
            if(written < 0) return false;
        }
    };
    int64_t previous = AV_NOPTS_VALUE;
    auto drainDecoder = [&](bool flush) {
        for(;;) {
            int ret = avcodec_receive_frame(dec.ctx, decoded.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            auto *frame = decoded.p;
            if(frame->width < 1 || frame->height < 1 || frame->width > 8192 || frame->height > 8192)
                return false;
            int64_t pts = frame->best_effort_timestamp;
            if(pts == AV_NOPTS_VALUE || (previous != AV_NOPTS_VALUE && pts <= previous)) return false;
            scale.p = sws_getCachedContext(scale.p, frame->width, frame->height,
                static_cast<AVPixelFormat>(frame->format), w, h, enc.ctx->pix_fmt,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if(!scale.p || av_frame_make_writable(scaled.p) < 0 ||
               sws_scale(scale.p, frame->data, frame->linesize, 0, frame->height,
                         scaled.p->data, scaled.p->linesize) != h) return false;
            scaled.p->pts = pts;
            scaled.p->pict_type = AV_PICTURE_TYPE_NONE;
            previous = pts;
            av_frame_unref(frame);
            if(avcodec_send_frame(enc.ctx, scaled.p) < 0 || !drainEncoder(false)) return false;
        }
    };
    int ret;
    while((ret = av_read_frame(in.ctx, inputPacket.p)) >= 0) {
        int i = inputPacket.p->stream_index;
        if(i < 0 || static_cast<unsigned>(i) >= mapping.size()) return false;
        if(i == in.video) {
            int sent = avcodec_send_packet(dec.ctx, inputPacket.p);
            av_packet_unref(inputPacket.p);
            if(sent < 0 || !drainDecoder(false)) return false;
        } else {
            auto *packet = inputPacket.p;
            auto *audio = out.ctx->streams[mapping[i]];
            av_packet_rescale_ts(packet, in.ctx->streams[i]->time_base, audio->time_base);
            packet->stream_index = audio->index;
            packet->pos = -1;
            int written = av_interleaved_write_frame(out.ctx, packet);
            av_packet_unref(packet);
            if(written < 0) return false;
        }
    }
    if(ret != AVERROR_EOF || avcodec_send_packet(dec.ctx, nullptr) < 0 ||
       !drainDecoder(true) || previous == AV_NOPTS_VALUE ||
       avcodec_send_frame(enc.ctx, nullptr) < 0 || !drainEncoder(true)) return false;
    return out.finish();
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
CK_DLL_SFUN(video_sequence) {
    RETURN->v_int = 0;
    try {
        auto prefix = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto count = GET_NEXT_INT(ARGS);
        auto fps = GET_NEXT_INT(ARGS);
        if(count >= 1 && count <= 100000 && fps >= 1 && fps <= 120 &&
           encodeSequence(prefix, dest, static_cast<int>(count), static_cast<int>(fps)))
            RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_sequence_compressed) {
    RETURN->v_int = 0;
    try {
        auto prefix = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto count = GET_NEXT_INT(ARGS);
        auto fps = GET_NEXT_INT(ARGS);
        auto mode = GET_NEXT_STRING_SAFE(ARGS);
        auto quality = GET_NEXT_INT(ARGS);
        if(count >= 1 && count <= 100000 && fps >= 1 && fps <= 120 &&
           quality >= 0 && quality <= 51 &&
           encodeSequence(prefix, dest, static_cast<int>(count), static_cast<int>(fps),
                          mode, static_cast<int>(quality))) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_encoder_available) {
    RETURN->v_int = 0;
    try {
        auto mode = GET_NEXT_STRING_SAFE(ARGS);
        const char *name = mode == "ffv1" ? "ffv1" : mode == "h264" ? "libx264" :
                           mode == "vp9" ? "libvpx-vp9" : nullptr;
        if(name && avcodec_find_encoder_by_name(name)) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_mux_audio) {
    RETURN->v_int = 0;
    try {
        auto video = GET_NEXT_STRING_SAFE(ARGS);
        auto audio = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        if(muxAudio(video, audio, dest)) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_transcode) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto w = GET_NEXT_INT(ARGS);
        auto h = GET_NEXT_INT(ARGS);
        auto mode = GET_NEXT_STRING_SAFE(ARGS);
        auto quality = GET_NEXT_INT(ARGS);
        if(w >= 1 && w <= 8192 && h >= 1 && h <= 8192 && quality >= 0 && quality <= 51 &&
           transcode(source, dest, static_cast<int>(w), static_cast<int>(h), mode,
                     static_cast<int>(quality))) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_extract_sequence) {
    RETURN->v_int = -1;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto prefix = GET_NEXT_STRING_SAFE(ARGS);
        auto start = GET_NEXT_INT(ARGS);
        auto limit = GET_NEXT_INT(ARGS);
        Input in;
        if(start >= 0 && start <= 2147483647 && limit >= 1 && limit <= 100000 &&
           in.open(source))
            RETURN->v_int = extractSequence(in, prefix, static_cast<int>(start), static_cast<int>(limit));
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
    QUERY->add_sfun(QUERY, video_sequence, "int", "encodeSequence");
    QUERY->add_arg(QUERY, "string", "prefix");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "count");
    QUERY->add_arg(QUERY, "int", "fps");
    QUERY->add_sfun(QUERY, video_sequence_compressed, "int", "encodeSequence");
    QUERY->add_arg(QUERY, "string", "prefix");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "count");
    QUERY->add_arg(QUERY, "int", "fps");
    QUERY->add_arg(QUERY, "string", "codec");
    QUERY->add_arg(QUERY, "int", "quality");
    QUERY->add_sfun(QUERY, video_encoder_available, "int", "encoderAvailable");
    QUERY->add_arg(QUERY, "string", "codec");
    QUERY->add_sfun(QUERY, video_mux_audio, "int", "muxAudio");
    QUERY->add_arg(QUERY, "string", "video");
    QUERY->add_arg(QUERY, "string", "audio");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_sfun(QUERY, video_transcode, "int", "transcode");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "width");
    QUERY->add_arg(QUERY, "int", "height");
    QUERY->add_arg(QUERY, "string", "codec");
    QUERY->add_arg(QUERY, "int", "quality");
    QUERY->add_sfun(QUERY, video_extract_sequence, "int", "extractSequence");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "prefix");
    QUERY->add_arg(QUERY, "int", "start");
    QUERY->add_arg(QUERY, "int", "limit");
    return QUERY->end_class(QUERY);
}
