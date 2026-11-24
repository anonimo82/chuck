// Optional offline video I/O for ChucK.
#include "chuck_dl.h"
#include "../file_compat.h"
#include "../subtitle/subtitle_io.h"
#include "../keyframe/keyframe.h"
#include <array>
#include <limits>
#include <new>
#include <cstdint>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/mem.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}
#include <cairo.h>
#include <pango/pangocairo.h>
#include <cstdio>
#include <sndfile.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <locale>
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




// Conservative packet-copy trim for streams without reordered video timestamps.
// The first keyframe at or after start is the common timestamp origin.
bool trim(const std::string &source, const std::string &dest, double start, double end) {
    Input in;
    if(!in.open(source)) return false;
    if(!std::isfinite(start) || !std::isfinite(end) || start < 0 ||
       end <= start || end > 1000000000.0) return false;
    const auto dot = dest.find_last_of('.');
    if(dot == std::string::npos) return false;
    const auto ext = dest.substr(dot);
    const char *format = ext == ".mkv" ? "matroska" : ext == ".mp4" ? "mp4" :
                         ext == ".mov" ? "mov" : ext == ".webm" ? "webm" : nullptr;
    if(!format) return false;
    for(unsigned i = 0; i < in.ctx->nb_streams; ++i)
        if(static_cast<int>(i) != in.video &&
           in.ctx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;
    const int64_t origin = in.ctx->start_time == AV_NOPTS_VALUE ? 0 : in.ctx->start_time;
    const AVRational us{1, AV_TIME_BASE};
    const int64_t begin = origin + static_cast<int64_t>(std::llround(start * AV_TIME_BASE));
    const int64_t stop = origin + static_cast<int64_t>(std::llround(end * AV_TIME_BASE));
    Packet packet;
    if(!packet.p) return false;
    // First pass finds a suitable video boundary. Reopen for the actual copy,
    // so audio packets interleaved before the keyframe are not accidentally lost.
    int64_t anchor = AV_NOPTS_VALUE;
    int ret;
    while((ret = av_read_frame(in.ctx, packet.p)) >= 0) {
        auto *p = packet.p;
        if(p->stream_index == in.video) {
            if(p->pts == AV_NOPTS_VALUE || p->dts == AV_NOPTS_VALUE || p->pts != p->dts)
                return false;
            int64_t pts = av_rescale_q(p->pts, in.ctx->streams[in.video]->time_base, us);
            if(pts >= begin && pts < stop && (p->flags & AV_PKT_FLAG_KEY)) {
                anchor = pts;
                av_packet_unref(p);
                break;
            }
            if(pts >= stop) return false;
        }
        av_packet_unref(p);
    }
    if(anchor == AV_NOPTS_VALUE) return false;
    Input copy;
    if(!copy.open(source) || copy.video != in.video ||
       copy.ctx->nb_streams != in.ctx->nb_streams) return false;
    Output out(dest);
    if(!out.open(format)) return false;
    for(unsigned i = 0; i < copy.ctx->nb_streams; ++i) {
        auto *src = copy.ctx->streams[i];
        if(avformat_query_codec(out.ctx->oformat, src->codecpar->codec_id, FF_COMPLIANCE_NORMAL) <= 0)
            return false;
        auto *dst = avformat_new_stream(out.ctx, nullptr);
        if(!dst || avcodec_parameters_copy(dst->codecpar, src->codecpar) < 0) return false;
        dst->codecpar->codec_tag = 0;
        dst->time_base = src->time_base;
        dst->avg_frame_rate = src->avg_frame_rate;
        dst->sample_aspect_ratio = src->sample_aspect_ratio;
        dst->disposition = src->disposition;
        if(av_dict_copy(&dst->metadata, src->metadata, 0) < 0) return false;
    }
    if(avformat_write_header(out.ctx, nullptr) < 0) return false;
    std::vector<bool> written(copy.ctx->nb_streams, false);
    std::vector<int64_t> previous(copy.ctx->nb_streams, AV_NOPTS_VALUE);
    while((ret = av_read_frame(copy.ctx, packet.p)) >= 0) {
        auto *p = packet.p;
        int i = p->stream_index;
        if(i < 0 || static_cast<unsigned>(i) >= copy.ctx->nb_streams) return false;
        auto tb = copy.ctx->streams[i]->time_base;
        if(p->pts == AV_NOPTS_VALUE || p->dts == AV_NOPTS_VALUE || p->pts != p->dts)
            return false;
        int64_t pts = av_rescale_q(p->pts, tb, us);
        if(previous[i] != AV_NOPTS_VALUE && pts <= previous[i]) return false;
        previous[i] = pts;
        if(pts < anchor || pts >= stop) { av_packet_unref(p); continue; }
        // Keep only complete audio packets within the requested interval.
        if(i != copy.video && (p->duration <= 0 ||
           av_rescale_q(p->duration, tb, us) > stop - pts)) {
            av_packet_unref(p); continue;
        }
        if(i == copy.video && !written[i] && !(p->flags & AV_PKT_FLAG_KEY)) return false;
        int64_t offset = av_rescale_q(anchor, us, tb);
        p->pts -= offset;
        p->dts -= offset;
        av_packet_rescale_ts(p, tb, out.ctx->streams[i]->time_base);
        p->pos = -1;
        int status = av_interleaved_write_frame(out.ctx, p);
        av_packet_unref(p);
        if(status < 0) return false;
        written[i] = true;
    }
    if(ret != AVERROR_EOF) return false;
    // Refuse empty audio tracks rather than silently dropping them.
    for(bool ok : written) if(!ok) return false;
    return out.finish();
}


// Conservative two-clip concatenation. Require identical codec configuration
// and non-reordered, fully timestamped packets with known positive durations.
bool compatibleStreams(const AVStream *a, const AVStream *b) {
    const auto *x = a->codecpar, *y = b->codecpar;
    if(x->codec_type != y->codec_type || x->codec_id != y->codec_id ||
       x->format != y->format || x->profile != y->profile || x->level != y->level ||
       x->extradata_size != y->extradata_size ||
       (x->extradata_size && std::memcmp(x->extradata, y->extradata, x->extradata_size) != 0))
        return false;
    if(x->codec_type == AVMEDIA_TYPE_VIDEO)
        return x->width == y->width && x->height == y->height &&
               av_cmp_q(a->sample_aspect_ratio, b->sample_aspect_ratio) == 0 &&
               av_cmp_q(a->avg_frame_rate, b->avg_frame_rate) == 0 &&
               x->color_range == y->color_range && x->color_space == y->color_space &&
               x->color_primaries == y->color_primaries && x->color_trc == y->color_trc &&
               x->field_order == y->field_order;
    if(x->codec_type == AVMEDIA_TYPE_AUDIO)
        return x->sample_rate == y->sample_rate &&
               av_channel_layout_compare(&x->ch_layout, &y->ch_layout) == 0 &&
               x->bits_per_coded_sample == y->bits_per_coded_sample &&
               x->bits_per_raw_sample == y->bits_per_raw_sample &&
               x->block_align == y->block_align &&
               x->initial_padding == 0 && y->initial_padding == 0 &&
               x->trailing_padding == 0 && y->trailing_padding == 0;
    return false;
}
bool concatenate(const std::string &first, const std::string &second, const std::string &dest) {
    Input a, b;
    if(!a.open(first) || !b.open(second) || a.video != b.video ||
       a.ctx->nb_streams != b.ctx->nb_streams) return false;
    const auto dot = dest.find_last_of('.');
    if(dot == std::string::npos) return false;
    const auto ext = dest.substr(dot);
    const char *format = ext == ".mkv" ? "matroska" : ext == ".mp4" ? "mp4" :
                         ext == ".mov" ? "mov" : ext == ".webm" ? "webm" : nullptr;
    if(!format) return false;
    // A single video stream, plus any corresponding compatible audio streams.
    for(unsigned i = 0; i < a.ctx->nb_streams; ++i) {
        if(static_cast<int>(i) != a.video &&
           a.ctx->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return false;
        if(!compatibleStreams(a.ctx->streams[i], b.ctx->streams[i])) return false;
    }
    Output out(dest);
    if(!out.open(format)) return false;
    for(unsigned i = 0; i < a.ctx->nb_streams; ++i) {
        auto *src = a.ctx->streams[i];
        if(avformat_query_codec(out.ctx->oformat, src->codecpar->codec_id, FF_COMPLIANCE_NORMAL) <= 0)
            return false;
        auto *dst = avformat_new_stream(out.ctx, nullptr);
        if(!dst || avcodec_parameters_copy(dst->codecpar, src->codecpar) < 0) return false;
        dst->codecpar->codec_tag = 0;
        dst->time_base = src->time_base;
        dst->avg_frame_rate = src->avg_frame_rate;
        dst->sample_aspect_ratio = src->sample_aspect_ratio;
        dst->disposition = src->disposition;
        if(av_dict_copy(&dst->metadata, src->metadata, 0) < 0) return false;
    }
    if(avformat_write_header(out.ctx, nullptr) < 0) return false;
    const AVRational us{1, AV_TIME_BASE};
    const int64_t maxSpan = INT64_C(1000000000000000);
    int64_t offset = 0;
    std::vector<int64_t> previous(a.ctx->nb_streams, AV_NOPTS_VALUE);
    Packet packet;
    if(!packet.p) return false;
    Input *clips[] = {&a, &b};
    for(auto *clip : clips) {
        const int64_t origin = clip->ctx->start_time;
        if(origin == AV_NOPTS_VALUE || origin < -maxSpan || origin > maxSpan) return false;
        std::vector<bool> wrote(clip->ctx->nb_streams, false);
        int64_t span = 0;
        int ret;
        while((ret = av_read_frame(clip->ctx, packet.p)) >= 0) {
            auto *p = packet.p;
            int i = p->stream_index;
            if(i < 0 || static_cast<unsigned>(i) >= clip->ctx->nb_streams ||
               p->pts == AV_NOPTS_VALUE || p->dts != p->pts || p->duration <= 0 ||
               av_packet_get_side_data(p, AV_PKT_DATA_SKIP_SAMPLES, nullptr)) return false;
            if(i == clip->video && !wrote[i] && !(p->flags & AV_PKT_FLAG_KEY)) return false;
            auto tb = clip->ctx->streams[i]->time_base;
            auto target = out.ctx->streams[i]->time_base;
            int64_t absolute = av_rescale_q(p->pts, tb, us);
            int64_t duration = av_rescale_q(p->duration, tb, us);
            if(absolute < -maxSpan || absolute > maxSpan || duration <= 0 || duration > maxSpan)
                return false;
            int64_t relative = absolute - origin;
            if(relative < 0 || relative > maxSpan || duration > maxSpan-relative) return false;
            span = std::max(span, relative+duration);
            int64_t timestamp = av_rescale_q(offset+relative, us, target);
            if(previous[i] != AV_NOPTS_VALUE && timestamp <= previous[i]) return false;
            previous[i] = timestamp;
            p->pts = p->dts = timestamp;
            p->duration = av_rescale_q(p->duration, tb, target);
            if(p->duration <= 0) return false;
            p->pos = -1;
            int status = av_interleaved_write_frame(out.ctx, p);
            av_packet_unref(p);
            if(status < 0) return false;
            wrote[i] = true;
        }
        if(ret != AVERROR_EOF || span <= 0) return false;
        for(bool ok : wrote) if(!ok) return false;
        // Offset by the longest actual packet span, not estimated file duration.
        offset += span;
    }
    return out.finish();
}


// Join video-only clips into a single constant-frame-rate FFV1 stream.
// Sample-and-hold on each clip's presentation timeline; no intermediate files.

// Separate audio pass: each clip's first video timestamp is its common origin.
// Normalize the first/only audio track to stereo or mono S16 at target rate.
// Reject discontinuities; trim or silence-pad at clip boundaries.
bool normalizedAudio(const std::string &source, int audioIndex, int rate, int channels,
                     int64_t videoOrigin, AVRational videoTB, int64_t offset, int64_t length,
                     AVStream *dst, AVFormatContext *out, std::vector<uint8_t> *capture = nullptr) {
    if(length <= 0 || offset < 0) return false;
    int64_t written = 0;
    auto write = [&](const uint8_t *data, int64_t n) {
        if(capture) {
            if(n < 0 || n > (INT64_C(128)*1024*1024-static_cast<int64_t>(capture->size()))/(channels*2)) return false;
            size_t bytes=static_cast<size_t>(n)*channels*2;
            if(data) capture->insert(capture->end(),data,data+bytes);
            else capture->resize(capture->size()+bytes,0);
            written+=n;
            return true;
        }
        while(n > 0) {
            int block = static_cast<int>(std::min<int64_t>(n, 4096));
            Packet p;
            if(!p.p || av_new_packet(p.p, block*channels*2) < 0) return false;
            if(data) { std::memcpy(p.p->data, data, p.p->size); data += p.p->size; }
            else std::memset(p.p->data, 0, p.p->size);
            p.p->pts = p.p->dts = offset + written;
            p.p->duration = block;
            p.p->stream_index = dst->index;
            p.p->flags = AV_PKT_FLAG_KEY;
            av_packet_rescale_ts(p.p, AVRational{1,rate}, dst->time_base);
            if(av_interleaved_write_frame(out, p.p) < 0) return false;
            written += block;
            n -= block;
        }
        return true;
    };
    if(audioIndex < 0) return write(nullptr, length);
    Input in;
    if(!in.open(source) || static_cast<unsigned>(audioIndex) >= in.ctx->nb_streams) return false;
    auto *src = in.ctx->streams[audioIndex];
    const auto *decoder = avcodec_find_decoder(src->codecpar->codec_id);
    if(!decoder) return false;
    Codec dec;
    Frame frame;
    Packet packet;
    dec.ctx = avcodec_alloc_context3(decoder);
    if(!dec.ctx || !frame.p || !packet.p ||
       avcodec_parameters_to_context(dec.ctx, src->codecpar) < 0) return false;
    dec.ctx->pkt_timebase = src->time_base;
    if(avcodec_open2(dec.ctx, decoder, nullptr) < 0 || dec.ctx->sample_rate < 1 ||
       dec.ctx->sample_rate > 384000 || dec.ctx->ch_layout.nb_channels < 1 ||
       dec.ctx->ch_layout.nb_channels > 8 || dec.ctx->sample_fmt == AV_SAMPLE_FMT_NONE) return false;
    if(dec.ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&dec.ctx->ch_layout, dec.ctx->ch_layout.nb_channels);
    struct Resampler {
        SwrContext *p = nullptr;
        ~Resampler() { swr_free(&p); }
    } swr;
    AVChannelLayout target{};
    av_channel_layout_default(&target, channels);
    int configured = swr_alloc_set_opts2(&swr.p, &target, AV_SAMPLE_FMT_S16, rate,
                                        &dec.ctx->ch_layout, dec.ctx->sample_fmt,
                                        dec.ctx->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&target);
    if(configured < 0 || !swr.p || swr_init(swr.p) < 0) return false;
    int64_t first = AV_NOPTS_VALUE, samples = 0, position = 0;
    const AVRational sampleTB{1,rate};
    const int64_t origin = av_rescale_q(videoOrigin, videoTB, sampleTB);
    auto convert = [&](const uint8_t **planes, int n) {
        int capacity = swr_get_out_samples(swr.p, n);
        if(capacity < 0 || capacity > 2097152) return false;
        if(capacity == 0) return true;
        std::vector<uint8_t> data(static_cast<size_t>(capacity)*channels*2);
        uint8_t *output[] = {data.data()};
        int got = swr_convert(swr.p, output, capacity, planes, n);
        if(got < 0) return false;
        int64_t lo = std::max<int64_t>(0, position);
        int64_t hi = std::min<int64_t>(length, position+got);
        if(hi > lo) {
            if(lo < written || (lo > written && !write(nullptr, lo-written))) return false;
            if(!write(data.data()+(lo-position)*channels*2, hi-lo)) return false;
        }
        position += got;
        return true;
    };
    auto drain = [&](bool flush) {
        for(;;) {
            int ret = avcodec_receive_frame(dec.ctx, frame.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            auto *f = frame.p;
            if(f->sample_rate != dec.ctx->sample_rate || f->format != dec.ctx->sample_fmt ||
               f->ch_layout.nb_channels != dec.ctx->ch_layout.nb_channels ||
               (f->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC &&
                av_channel_layout_compare(&f->ch_layout, &dec.ctx->ch_layout) != 0) ||
               f->nb_samples < 1 || f->nb_samples > 1048576 ||
               f->best_effort_timestamp == AV_NOPTS_VALUE) return false;
            int64_t pts = f->best_effort_timestamp;
            if(first == AV_NOPTS_VALUE) {
                first = pts;
                position = av_rescale_q(pts, src->time_base, sampleTB)-origin;
                if(position < -INT64_C(1000000000000000) || position > INT64_C(1000000000000000)) return false;
            }
            double elapsed = (static_cast<double>(pts)-static_cast<double>(first))*av_q2d(src->time_base);
            if(std::abs(elapsed-static_cast<double>(samples)/dec.ctx->sample_rate) >
               std::max(0.002, 2*av_q2d(src->time_base))) return false;
            const uint8_t *planes[8] = {};
            int count = av_sample_fmt_is_planar(dec.ctx->sample_fmt) ? dec.ctx->ch_layout.nb_channels : 1;
            for(int i=0;i<count;++i) planes[i]=f->extended_data[i];
            if(!convert(planes,f->nb_samples)) return false;
            samples += f->nb_samples;
            av_frame_unref(f);
        }
    };
    int ret;
    while((ret=av_read_frame(in.ctx,packet.p)) >= 0) {
        if(packet.p->stream_index == audioIndex) {
            int sent = avcodec_send_packet(dec.ctx,packet.p);
            av_packet_unref(packet.p);
            if(sent < 0 || !drain(false)) return false;
        } else av_packet_unref(packet.p);
    }
    if(ret != AVERROR_EOF || avcodec_send_packet(dec.ctx,nullptr)<0 ||
       !drain(true) || first == AV_NOPTS_VALUE || !convert(nullptr,0)) return false;
    return write(nullptr,length-written);
}


// Streaming presentation-order reader with one-frame lookahead. The last
// frame remains visible until its known duration expires.
struct OverlayReader {
    Input in;
    Codec dec;
    Frame current, next;
    Packet packet;
    Scale scale;
    AVStream *stream=nullptr;
    int64_t origin=AV_NOPTS_VALUE, previous=AV_NOPTS_VALUE;
    bool flushing=false, eof=false;
    int audio=-1;
    bool open(const std::string &path) {
        if(!in.open(path) || !current.p || !next.p || !packet.p) return false;
        for(unsigned i=0;i<in.ctx->nb_streams;++i) {
            if(static_cast<int>(i)==in.video) continue;
            if(audio>=0 || in.ctx->streams[i]->codecpar->codec_type!=AVMEDIA_TYPE_AUDIO) return false;
            audio=static_cast<int>(i);
        }
        stream=in.ctx->streams[in.video];
        const auto *codec=avcodec_find_decoder(stream->codecpar->codec_id);
        if(!codec) return false;
        dec.ctx=avcodec_alloc_context3(codec);
        if(!dec.ctx || avcodec_parameters_to_context(dec.ctx,stream->codecpar)<0) return false;
        dec.ctx->pkt_timebase=stream->time_base;
        return avcodec_open2(dec.ctx,codec,nullptr)>=0 && read(current.p) && !eof && read(next.p);
    }
    bool read(AVFrame *frame) {
        av_frame_unref(frame);
        if(eof) return true;
        for(;;) {
            int ret=avcodec_receive_frame(dec.ctx,frame);
            if(ret==AVERROR_EOF) { eof=true; return true; }
            if(ret>=0) {
                int64_t pts=frame->best_effort_timestamp;
                if(pts==AV_NOPTS_VALUE || pts < -INT64_C(1000000000000000) ||
                   pts > INT64_C(1000000000000000) ||
                   (previous!=AV_NOPTS_VALUE && pts<=previous) ||
                   frame->width<1 || frame->height<1 || frame->width>8192 || frame->height>8192)
                    return false;
                if(origin==AV_NOPTS_VALUE) origin=pts;
                previous=pts;
                return true;
            }
            if(ret!=AVERROR(EAGAIN) || flushing) return false;
            while((ret=av_read_frame(in.ctx,packet.p))>=0) {
                if(packet.p->stream_index==in.video) break;
                av_packet_unref(packet.p);
            }
            if(ret==AVERROR_EOF) {
                flushing=true;
                if(avcodec_send_packet(dec.ctx,nullptr)<0) return false;
            } else {
                if(ret<0) return false;
                int sent=avcodec_send_packet(dec.ctx,packet.p);
                av_packet_unref(packet.p);
                if(sent<0) return false;
            }
        }
    }
    bool at(int64_t index, int fps, bool &visible) {
        const AVRational grid{1,fps};
        while(!eof && av_compare_ts(next.p->best_effort_timestamp-origin,stream->time_base,index,grid)<=0) {
            std::swap(current.p,next.p);
            if(!read(next.p)) return false;
        }
        visible=true;
        if(eof) {
#if LIBAVUTIL_VERSION_MAJOR >= 58
            int64_t duration=current.p->duration;
#else
            int64_t duration=current.p->pkt_duration;
#endif
            if(duration<=0 || duration>INT64_C(1000000000000000)) return false;
            visible=av_compare_ts(current.p->best_effort_timestamp-origin+duration,stream->time_base,index,grid)>0;
        }
        return true;
    }
    bool render(AVFrame *dest) {
        auto *f=current.p;
        scale.p=sws_getCachedContext(scale.p,f->width,f->height,static_cast<AVPixelFormat>(f->format),
            dest->width,dest->height,AV_PIX_FMT_BGR0,SWS_BILINEAR,nullptr,nullptr,nullptr);
        return scale.p && av_frame_make_writable(dest)>=0 &&
            sws_scale(scale.p,f->data,f->linesize,0,f->height,dest->data,dest->linesize)==dest->height;
    }
};

// Decode one static PNG/JPEG/BMP image, retaining straight alpha in BGRA.
bool loadWatermark(const std::string &path, AVFrame *dest) {
    Input input;
    if(!input.open(path) || input.ctx->nb_streams!=1) return false;
    auto *stream=input.ctx->streams[input.video];
    auto id=stream->codecpar->codec_id;
    if(id!=AV_CODEC_ID_PNG && id!=AV_CODEC_ID_MJPEG && id!=AV_CODEC_ID_BMP) return false;
    const auto *decoder=avcodec_find_decoder(id);
    if(!decoder) return false;
    Codec dec;
    Frame frame;
    Packet packet;
    Scale scale;
    dec.ctx=avcodec_alloc_context3(decoder);
    if(!dec.ctx || !frame.p || !packet.p ||
       avcodec_parameters_to_context(dec.ctx,stream->codecpar)<0 ||
       avcodec_open2(dec.ctx,decoder,nullptr)<0) return false;
    int count=0;
    auto drain=[&](bool flush) {
        for(;;) {
            int ret=avcodec_receive_frame(dec.ctx,frame.p);
            if(ret==AVERROR_EOF) return flush;
            if(ret==AVERROR(EAGAIN)) return !flush;
            if(ret<0 || ++count!=1 || frame.p->width<1 || frame.p->height<1 ||
               frame.p->width>8192 || frame.p->height>8192) return false;
            scale.p=sws_getContext(frame.p->width,frame.p->height,
                static_cast<AVPixelFormat>(frame.p->format),dest->width,dest->height,
                AV_PIX_FMT_BGRA,SWS_BILINEAR,nullptr,nullptr,nullptr);
            if(!scale.p || sws_scale(scale.p,frame.p->data,frame.p->linesize,0,
                frame.p->height,dest->data,dest->linesize)!=dest->height) return false;
            av_frame_unref(frame.p);
        }
    };
    int ret;
    while((ret=av_read_frame(input.ctx,packet.p))>=0) {
        int sent=avcodec_send_packet(dec.ctx,packet.p);
        av_packet_unref(packet.p);
        if(sent<0 || !drain(false)) return false;
    }
    return ret==AVERROR_EOF && avcodec_send_packet(dec.ctx,nullptr)>=0 &&
           drain(true) && count==1;
}

bool overlayVideo(const std::string &base,const std::string &layer,const std::string &dest,
                  int w,int h,int fps,int x,int y,int lw,int lh,double opacity,int rate,int channels,
                  int key=-1,double tolerance=0,double softness=0,
                  double baseGain=1,double layerGain=0,bool still=false) {
    if(w<1 || h<1 || w>8192 || h>8192 || fps<1 || fps>120 ||
       lw<1 || lh<1 || lw>w || lh>h || x<0 || y<0 || x>w-lw || y>h-lh ||
       !std::isfinite(opacity) || opacity<0 || opacity>1 ||
       rate<8000 || rate>96000 || channels<1 || channels>2 ||
       dest.size()<4 || dest.substr(dest.size()-4)!=".mkv") return false;
    if(key < -1 || key > 0xffffff || !std::isfinite(tolerance) || !std::isfinite(softness) ||
       tolerance<0 || tolerance>1 || softness<0 || softness>1 || tolerance+softness>1 ||
       !std::isfinite(baseGain) || !std::isfinite(layerGain) ||
       baseGain<0 || baseGain>4 || layerGain<0 || layerGain>4) return false;
    OverlayReader main, inset;
    if(!main.open(base) || (!still && !inset.open(layer))) return false;
    if(still && (baseGain!=1 || layerGain!=0)) return false;
    const auto *encoder=avcodec_find_encoder_by_name("ffv1");
    if(!encoder) return false;
    Output out(dest);
    Codec enc;
    Frame canvas, small;
    Packet packet;
    if(!out.open("matroska") || !canvas.p || !small.p || !packet.p) return false;
    enc.ctx=avcodec_alloc_context3(encoder);
    if(!enc.ctx) return false;
    enc.ctx->width=w; enc.ctx->height=h; enc.ctx->pix_fmt=AV_PIX_FMT_BGR0;
    enc.ctx->time_base=AVRational{1,fps}; enc.ctx->framerate=AVRational{fps,1};
    enc.ctx->sample_aspect_ratio=AVRational{1,1};
    if(out.ctx->oformat->flags&AVFMT_GLOBALHEADER) enc.ctx->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    if(avcodec_open2(enc.ctx,encoder,nullptr)<0) return false;
    auto *video=avformat_new_stream(out.ctx,nullptr);
    auto *audio=avformat_new_stream(out.ctx,nullptr);
    if(!video || !audio || avcodec_parameters_from_context(video->codecpar,enc.ctx)<0) return false;
    video->time_base=enc.ctx->time_base; video->avg_frame_rate=enc.ctx->framerate;
    video->sample_aspect_ratio=enc.ctx->sample_aspect_ratio;
    auto *p=audio->codecpar;
    p->codec_type=AVMEDIA_TYPE_AUDIO; p->codec_id=AV_CODEC_ID_PCM_S16LE;
    p->format=AV_SAMPLE_FMT_S16; p->sample_rate=rate;
    p->bits_per_coded_sample=p->bits_per_raw_sample=16;
    p->block_align=channels*2; p->bit_rate=static_cast<int64_t>(rate)*channels*16;
    av_channel_layout_default(&p->ch_layout,channels);
    audio->time_base=AVRational{1,rate};
    out.ctx->max_interleave_delta=0;
    if(avformat_write_header(out.ctx,nullptr)<0) return false;
    canvas.p->width=w; canvas.p->height=h; canvas.p->format=AV_PIX_FMT_BGR0;
    small.p->width=lw; small.p->height=lh; small.p->format=still?AV_PIX_FMT_BGRA:AV_PIX_FMT_BGR0;
    if(av_frame_get_buffer(canvas.p,32)<0 || av_frame_get_buffer(small.p,32)<0) return false;
    if(still && !loadWatermark(layer,small.p)) return false;
    auto drain=[&](bool flush) {
        for(;;) {
            int ret=avcodec_receive_packet(enc.ctx,packet.p);
            if(ret==AVERROR_EOF) return flush;
            if(ret==AVERROR(EAGAIN)) return !flush;
            if(ret<0) return false;
            if(packet.p->duration<=0) packet.p->duration=1;
            av_packet_rescale_ts(packet.p,enc.ctx->time_base,video->time_base);
            packet.p->stream_index=video->index;
            int written=av_interleaved_write_frame(out.ctx,packet.p);
            av_packet_unref(packet.p);
            if(written<0) return false;
        }
    };
    int alpha=static_cast<int>(std::llround(opacity*65535));
    int64_t count=0, layerFrames=0;
    for(;;) {
        bool active=false, show=false;
        if(!main.at(count,fps,active)) return false;
        if(!active) break;
        if(count>=1000000 || !main.render(canvas.p)) return false;
        if(still) show=true;
        else if(!inset.at(count,fps,show)) return false;
        if(show) ++layerFrames;
        if(show && alpha) {
            if(!still && !inset.render(small.p)) return false;
            for(int row=0;row<lh;++row) {
                auto *d=canvas.p->data[0]+static_cast<size_t>(y+row)*canvas.p->linesize[0]+x*4;
                auto *v=small.p->data[0]+static_cast<size_t>(row)*small.p->linesize[0];
                for(int col=0;col<lw;++col) {
                    int pixelAlpha=still?(alpha*v[col*4+3]+127)/255:alpha;
                    if(key>=0) {
                        int j=col*4;
                        int db=v[j]-(key&255), dg=v[j+1]-((key>>8)&255), dr=v[j+2]-((key>>16)&255);
                        double distance=std::sqrt((db*db+dg*dg+dr*dr)/195075.0);
                        if(distance<=tolerance) pixelAlpha=0;
                        else if(softness>0 && distance<tolerance+softness)
                            pixelAlpha=static_cast<int>(std::llround(alpha*(distance-tolerance)/softness));
                    }
                    for(int k=0;k<3;++k) {
                        int j=col*4+k;
                        d[j]=static_cast<uint8_t>((d[j]*(65535-pixelAlpha)+v[j]*pixelAlpha+32767)/65535);
                    }
                }
            }
        }
        canvas.p->pts=count++; canvas.p->pict_type=AV_PICTURE_TYPE_NONE;
        if(avcodec_send_frame(enc.ctx,canvas.p)<0 || !drain(false)) return false;
    }
    if(!count || avcodec_send_frame(enc.ctx,nullptr)<0 || !drain(true)) return false;
    int64_t length=av_rescale_q(count,enc.ctx->time_base,AVRational{1,rate});
    if(baseGain==1 && layerGain==0)
        return normalizedAudio(base,main.audio,rate,channels,main.origin,main.stream->time_base,
                               0,length,audio,out.ctx) && out.finish();
    // Bound full PCM capture for this initial offline mixing implementation.
    if(length > INT64_C(128)*1024*1024/(channels*2)) return false;
    std::vector<uint8_t> basePCM, layerPCM;
    if(!normalizedAudio(base,baseGain==0?-1:main.audio,rate,channels,main.origin,main.stream->time_base,
                        0,length,nullptr,nullptr,&basePCM)) return false;
    int64_t layerLength=std::min(length,av_rescale_q(layerFrames,enc.ctx->time_base,AVRational{1,rate}));
    if(layerGain>0 && layerLength>0 &&
       !normalizedAudio(layer,inset.audio,rate,channels,inset.origin,inset.stream->time_base,
                        0,layerLength,nullptr,nullptr,&layerPCM)) return false;
    for(int64_t i=0;i<length*channels;++i) {
        size_t pos=static_cast<size_t>(i)*2;
        int16_t a=0,b=0;
        std::memcpy(&a,basePCM.data()+pos,2);
        if(pos+2<=layerPCM.size()) std::memcpy(&b,layerPCM.data()+pos,2);
        auto value=std::llround(a*baseGain+b*layerGain);
        int16_t mixed=static_cast<int16_t>(std::max<long long>(-32768,std::min<long long>(32767,value)));
        std::memcpy(basePCM.data()+pos,&mixed,2);
    }
    for(int64_t offset=0;offset<length;) {
        int block=static_cast<int>(std::min<int64_t>(4096,length-offset));
        if(av_new_packet(packet.p,block*channels*2)<0) return false;
        std::memcpy(packet.p->data,basePCM.data()+static_cast<size_t>(offset)*channels*2,packet.p->size);
        packet.p->pts=packet.p->dts=offset; packet.p->duration=block;
        packet.p->flags=AV_PKT_FLAG_KEY; packet.p->stream_index=audio->index;
        av_packet_rescale_ts(packet.p,AVRational{1,rate},audio->time_base);
        int status=av_interleaved_write_frame(out.ctx,packet.p);
        av_packet_unref(packet.p);
        if(status<0) return false;
        offset+=block;
    }
    return out.finish();
}


bool renderTitle(AVFrame *dest,const std::string &text,const std::string &font,int size,int rgb,bool caption=false) {
    if(text.empty() || text.size()>16384 || font.empty() || font.size()>256 ||
       !g_utf8_validate(text.data(),static_cast<gssize>(text.size()),nullptr) ||
       !g_utf8_validate(font.data(),static_cast<gssize>(font.size()),nullptr) ||
       size<1 || size>512 || rgb<0 || rgb>0xffffff) return false;
    // Cairo ARGB32 is native endian and premultiplied; convert to straight BGRA.
    std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)> surface(
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32,dest->width,dest->height),cairo_surface_destroy);
    if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS) return false;
    std::unique_ptr<cairo_t,decltype(&cairo_destroy)> cr(cairo_create(surface.get()),cairo_destroy);
    if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return false;
    auto *layout=pango_cairo_create_layout(cr.get());
    if(!layout) return false;
    auto *desc=pango_font_description_from_string(font.c_str());
    if(!desc) { g_object_unref(layout); return false; }
    pango_font_description_set_absolute_size(desc,size*PANGO_SCALE);
    pango_layout_set_font_description(layout,desc);
    pango_font_description_free(desc);
    pango_layout_set_text(layout,text.data(),static_cast<int>(text.size()));
    pango_layout_set_width(layout,dest->width*PANGO_SCALE);
    pango_layout_set_wrap(layout,PANGO_WRAP_WORD_CHAR);
    if(caption) {
        pango_layout_set_alignment(layout,PANGO_ALIGN_CENTER);
        PangoRectangle logical;
        pango_layout_get_pixel_extents(layout,nullptr,&logical);
        cairo_move_to(cr.get(),0,std::max(0,dest->height-logical.height-logical.y));
        pango_cairo_layout_path(cr.get(),layout);
        cairo_set_source_rgb(cr.get(),0,0,0);
        cairo_set_line_join(cr.get(),CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_width(cr.get(),std::max(1.0,size/12.0));
        cairo_stroke_preserve(cr.get());
        cairo_set_source_rgb(cr.get(),((rgb>>16)&255)/255.0,((rgb>>8)&255)/255.0,(rgb&255)/255.0);
        cairo_fill(cr.get());
    } else {
        cairo_set_source_rgb(cr.get(),((rgb>>16)&255)/255.0,((rgb>>8)&255)/255.0,(rgb&255)/255.0);
        pango_cairo_show_layout(cr.get(),layout);
    }
    g_object_unref(layout);
    if(cairo_status(cr.get())!=CAIRO_STATUS_SUCCESS) return false;
    cairo_surface_flush(surface.get());
    auto *src=cairo_image_surface_get_data(surface.get());
    int stride=cairo_image_surface_get_stride(surface.get());
    if(!src) return false;
    for(int y=0;y<dest->height;++y) {
        auto *d=dest->data[0]+static_cast<size_t>(y)*dest->linesize[0];
        for(int x=0;x<dest->width;++x) {
            uint32_t pixel;
            std::memcpy(&pixel,src+static_cast<size_t>(y)*stride+x*4,4);
            unsigned a=pixel>>24;
            d[x*4+3]=static_cast<uint8_t>(a);
            for(int k=0;k<3;++k) {
                unsigned c=(pixel>>(k*8))&255;
                d[x*4+k]=a?static_cast<uint8_t>(std::min(255u,(c*255+a/2)/a)):0;
            }
        }
    }
    return true;
}


struct OverlayAnimation {
    std::array<keyframes::Curve,6> curves;
    bool load(const std::string &path) {
        std::ifstream in(path,std::ios::binary);
        if(!in) return false;
        in.seekg(0,std::ios::end);
        auto size=in.tellg();
        if(size<=0 || size>8*1024*1024) return false;
        in.seekg(0,std::ios::beg);
        const std::array<std::string,6> names{{"x","y","width","height","rotation","opacity"}};
        std::string line;
        while(std::getline(in,line)) {
            if(!line.empty() && line.back()=='\r') line.pop_back();
            if(line.empty() || line[0]=='#') continue;
            std::istringstream fields(line);
            fields.imbue(std::locale::classic());
            std::string name,mode,extra;
            double time,value;
            keyframes::Mode m;
            if(!(fields>>name>>time>>value>>mode) || (fields>>extra) ||
               !keyframes::mode(mode,m) || !std::isfinite(value)) return false;
            auto it=std::find(names.begin(),names.end(),name);
            if(it==names.end()) return false;
            size_t i=it-names.begin();
            if((i<2 && std::abs(value)>32768) ||
               ((i==2 || i==3) && (value<1 || value>8192)) ||
               (i==4 && std::abs(value)>1e9) ||
               (i==5 && (value<0 || value>1)) ||
               !curves[i].append(time,value,m)) return false;
        }
        if(!in.eof() || in.bad()) return false;
        for(const auto &c:curves) if(c.keys.empty()) return false;
        return true;
    }
    bool blend(AVFrame *canvas,const AVFrame *image,double time) const {
        std::array<double,6> v;
        for(size_t i=0;i<v.size();++i) if(!curves[i].sample(time,v[i])) return false;
        if(v[5]==0) return true;
        // Positive degrees rotate clockwise in image coordinates, around rectangle center.
        double a=std::remainder(v[4],360.0)*3.14159265358979323846/180.0;
        double c=std::cos(a),s=std::sin(a);
        double cx=v[0]+v[2]/2,cy=v[1]+v[3]/2;
        double ex=(std::abs(c)*v[2]+std::abs(s)*v[3])/2;
        double ey=(std::abs(s)*v[2]+std::abs(c)*v[3])/2;
        int x0=std::max(0,static_cast<int>(std::floor(cx-ex)));
        int y0=std::max(0,static_cast<int>(std::floor(cy-ey)));
        int x1=std::min(canvas->width,static_cast<int>(std::ceil(cx+ex)));
        int y1=std::min(canvas->height,static_cast<int>(std::ceil(cy+ey)));
        int alpha=static_cast<int>(std::llround(v[5]*65535));
        for(int y=y0;y<y1;++y) {
            auto *d=canvas->data[0]+static_cast<size_t>(y)*canvas->linesize[0];
            for(int x=x0;x<x1;++x) {
                double dx=x+0.5-cx,dy=y+0.5-cy;
                double u=(c*dx+s*dy)/v[2]+0.5;
                double z=(-s*dx+c*dy)/v[3]+0.5;
                if(u<0 || u>=1 || z<0 || z>=1) continue;
                int ix=std::min(image->width-1,static_cast<int>(u*image->width));
                int iy=std::min(image->height-1,static_cast<int>(z*image->height));
                auto *p=image->data[0]+static_cast<size_t>(iy)*image->linesize[0]+ix*4;
                int pa=(alpha*p[3]+127)/255;
                for(int k=0;k<3;++k)
                    d[x*4+k]=static_cast<uint8_t>((d[x*4+k]*(65535-pa)+p[k]*pa+32767)/65535);
            }
        }
        return true;
    }
};

struct CompositeLayer {
    std::string path;
    int x=0,y=0,w=0,h=0;
    double opacity=1;
    bool still=false;
    bool title=false;
    bool captions=false;
    std::unique_ptr<OverlayAnimation> animation;
    std::vector<subtitles::Cue> cues;
    size_t nextCue=0;
    std::vector<size_t> activeCues;
    std::string cachedText;
    std::string font;
    int fontSize=48,rgb=0xffffff;
    double begin=0,end=1e15;
    OverlayReader reader;
    Frame pixels;
};
bool multilayerVideo(const std::string &base,const std::string &dest,int w,int h,int fps,
                     int rate,int channels,std::vector<std::unique_ptr<CompositeLayer>> &layers) {
    if(w<1 || h<1 || w>8192 || h>8192 || fps<1 || fps>120 ||
       rate<8000 || rate>96000 || channels<1 || channels>2 ||
       dest.size()<4 || dest.substr(dest.size()-4)!=".mkv" ||
       layers.empty() || layers.size()>8) return false;
    OverlayReader main;
    if(!main.open(base)) return false;
    int64_t pixelBudget=0;
    for(auto &ptr:layers) {
        auto &l=*ptr;
        if(l.w<1 || l.h<1 || l.w>8192 || l.h>8192 ||
           (!l.animation && (l.w>w || l.h>h || l.x<0 || l.y<0 || l.x>w-l.w || l.y>h-l.h)) || !std::isfinite(l.opacity) ||
           l.opacity<0 || l.opacity>1 || !l.pixels.p) return false;
        pixelBudget+=static_cast<int64_t>(l.w)*l.h*4;
        if(pixelBudget>INT64_C(256)*1024*1024) return false;
        l.pixels.p->width=l.w; l.pixels.p->height=l.h;
        l.pixels.p->format=l.still?AV_PIX_FMT_BGRA:AV_PIX_FMT_BGR0;
        if(av_frame_get_buffer(l.pixels.p,32)<0) return false;
        if(l.still) {
            if(l.captions) {
                if(l.cues.empty() || l.font.empty() || l.font.size()>256 ||
                   !g_utf8_validate(l.font.data(),static_cast<gssize>(l.font.size()),nullptr)) return false;
            } else if(l.title) {
                if(!renderTitle(l.pixels.p,l.path,l.font,l.fontSize,l.rgb)) return false;
            } else if(!loadWatermark(l.path,l.pixels.p)) return false;
        } else if(!l.reader.open(l.path)) return false;
    }
    const auto *encoder=avcodec_find_encoder_by_name("ffv1");
    if(!encoder) return false;
    Output out(dest);
    Codec enc;
    Frame canvas;
    Packet packet;
    if(!out.open("matroska") || !canvas.p || !packet.p) return false;
    enc.ctx=avcodec_alloc_context3(encoder);
    if(!enc.ctx) return false;
    enc.ctx->width=w; enc.ctx->height=h; enc.ctx->pix_fmt=AV_PIX_FMT_BGR0;
    enc.ctx->time_base=AVRational{1,fps}; enc.ctx->framerate=AVRational{fps,1};
    enc.ctx->sample_aspect_ratio=AVRational{1,1};
    if(out.ctx->oformat->flags&AVFMT_GLOBALHEADER) enc.ctx->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    if(avcodec_open2(enc.ctx,encoder,nullptr)<0) return false;
    auto *video=avformat_new_stream(out.ctx,nullptr);
    auto *audio=avformat_new_stream(out.ctx,nullptr);
    if(!video || !audio || avcodec_parameters_from_context(video->codecpar,enc.ctx)<0) return false;
    video->time_base=enc.ctx->time_base; video->avg_frame_rate=enc.ctx->framerate;
    video->sample_aspect_ratio=enc.ctx->sample_aspect_ratio;
    auto *p=audio->codecpar;
    p->codec_type=AVMEDIA_TYPE_AUDIO; p->codec_id=AV_CODEC_ID_PCM_S16LE;
    p->format=AV_SAMPLE_FMT_S16; p->sample_rate=rate;
    p->bits_per_coded_sample=p->bits_per_raw_sample=16;
    p->block_align=channels*2; p->bit_rate=static_cast<int64_t>(rate)*channels*16;
    av_channel_layout_default(&p->ch_layout,channels);
    audio->time_base=AVRational{1,rate};
    out.ctx->max_interleave_delta=0;
    if(avformat_write_header(out.ctx,nullptr)<0) return false;
    canvas.p->width=w; canvas.p->height=h; canvas.p->format=AV_PIX_FMT_BGR0;
    if(av_frame_get_buffer(canvas.p,32)<0) return false;
    auto drain=[&](bool flush) {
        for(;;) {
            int ret=avcodec_receive_packet(enc.ctx,packet.p);
            if(ret==AVERROR_EOF) return flush;
            if(ret==AVERROR(EAGAIN)) return !flush;
            if(ret<0) return false;
            if(packet.p->duration<=0) packet.p->duration=1;
            av_packet_rescale_ts(packet.p,enc.ctx->time_base,video->time_base);
            packet.p->stream_index=video->index;
            int written=av_interleaved_write_frame(out.ctx,packet.p);
            av_packet_unref(packet.p);
            if(written<0) return false;
        }
    };
    int64_t count=0;
    for(;;) {
        bool active=false;
        if(!main.at(count,fps,active)) return false;
        if(!active) break;
        if(count>=1000000 || !main.render(canvas.p)) return false;
        for(auto &ptr:layers) {
            auto &l=*ptr;
            double seconds=static_cast<double>(count)/fps;
            if(seconds<l.begin || seconds>=l.end) continue;
            if(l.animation) {
                if(!l.animation->blend(canvas.p,l.pixels.p,seconds)) return false;
                continue;
            }
            bool show=l.still;
            if(l.captions) {
                // Exact rational timestamp comparison, with half-open cue intervals.
                auto reached=[&](int64_t ms){return count*1000>=ms*fps;};
                auto &active=l.activeCues;
                active.erase(std::remove_if(active.begin(),active.end(),
                    [&](size_t i){return reached(l.cues[i].end);}),active.end());
                while(l.nextCue<l.cues.size() && reached(l.cues[l.nextCue].start)) {
                    if(!reached(l.cues[l.nextCue].end)) active.push_back(l.nextCue);
                    ++l.nextCue;
                }
                std::string content;
                for(auto i:active) {
                    if(!content.empty()) content+='\n';
                    content+=l.cues[i].text;
                    if(content.size()>16384) return false;
                }
                show=!content.empty();
                if(show && content!=l.cachedText) {
                    if(!renderTitle(l.pixels.p,content,l.font,l.fontSize,l.rgb,true)) return false;
                    l.cachedText=std::move(content);
                }
            }
            if(!l.still && !l.reader.at(count,fps,show)) return false;
            int alpha=static_cast<int>(std::llround(l.opacity*65535));
            if(!show || !alpha) continue;
            if(!l.still && !l.reader.render(l.pixels.p)) return false;
            for(int row=0;row<l.h;++row) {
                auto *d=canvas.p->data[0]+static_cast<size_t>(l.y+row)*canvas.p->linesize[0]+l.x*4;
                auto *v=l.pixels.p->data[0]+static_cast<size_t>(row)*l.pixels.p->linesize[0];
                for(int col=0;col<l.w;++col) {
                    int pa=l.still?(alpha*v[col*4+3]+127)/255:alpha;
                    for(int k=0;k<3;++k) {
                        int j=col*4+k;
                        d[j]=static_cast<uint8_t>((d[j]*(65535-pa)+v[j]*pa+32767)/65535);
                    }
                }
            }
        }
        canvas.p->pts=count++; canvas.p->pict_type=AV_PICTURE_TYPE_NONE;
        if(avcodec_send_frame(enc.ctx,canvas.p)<0 || !drain(false)) return false;
    }
    if(!count || avcodec_send_frame(enc.ctx,nullptr)<0 || !drain(true)) return false;
    int64_t length=av_rescale_q(count,enc.ctx->time_base,AVRational{1,rate});
    return normalizedAudio(base,main.audio,rate,channels,main.origin,main.stream->time_base,
                               0,length,audio,out.ctx) && out.finish();
}


bool readCompositeLayers(const std::string &manifest,
                         std::vector<std::unique_ptr<CompositeLayer>> &layers) {
    std::ifstream input(manifest,std::ios::binary);
    if(!input) return false;
    std::string line;
    // Bound the manifest before parsing it.
    input.seekg(0,std::ios::end);
    auto size=input.tellg();
    if(size<=0 || size>65536) return false;
    input.seekg(0,std::ios::beg);
    while(std::getline(input,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.empty() || line[0]=='#') continue;
        if(layers.size()>=8) return false;
        std::istringstream fields(line);
        std::string values[7];
        for(auto &v:values) if(!std::getline(fields,v,'\t') || v.empty()) return false;
        if(fields.peek()!=std::char_traits<char>::eof() || line.back()=='\t') return false;
        auto l=std::unique_ptr<CompositeLayer>(new CompositeLayer);
        if(values[0]=="image") l->still=true;
        else if(values[0]!="video") return false;
        l->path=values[1];
        if(l->path.find('\0')!=std::string::npos) return false;
        auto number=[](const std::string &str,auto &value) {
            std::istringstream in(str);
            in.imbue(std::locale::classic());
            in >> std::noskipws >> value;
            return !in.fail() && in.eof();
        };
        if(!number(values[2],l->x) || !number(values[3],l->y) ||
           !number(values[4],l->w) || !number(values[5],l->h) ||
           !number(values[6],l->opacity)) return false;
        layers.push_back(std::move(l));
    }
    return input.eof() && !input.bad() && !layers.empty();
}

bool concatNormalized(const std::string &first, const std::string &secondClip,
                      const std::string &dest, int w, int h, int fps, int rate = 0, int channels = 0, int fade = 0,
                      const std::string &visual = "linear", const std::string &audioCurve = "linear") {
    if(w < 1 || h < 1 || w > 8192 || h > 8192 || fps < 1 || fps > 120 ||
       dest.size() < 4 || dest.substr(dest.size()-4) != ".mkv") return false;
    Input a, b;
    if(!a.open(first) || !b.open(secondClip)) return false;
    if(rate && (rate < 8000 || rate > 96000 || channels < 1 || channels > 2)) return false;
    Input *clips[] = {&a, &b};
    int audioIndices[] = {-1,-1};
    for(int c=0;c<2;++c) {
        auto *clip=clips[c];
        for(unsigned i=0;i<clip->ctx->nb_streams;++i) {
            if(static_cast<int>(i)==clip->video) continue;
            if(!rate || audioIndices[c]>=0 ||
               clip->ctx->streams[i]->codecpar->codec_type!=AVMEDIA_TYPE_AUDIO) return false;
            audioIndices[c]=static_cast<int>(i);
        }
    }
    if(visual != "linear" && visual != "wipeLeft" && visual != "wipeDown" && visual != "black") return false;
    if(audioCurve != "linear" && audioCurve != "equalPower") return false;
    if(fade && (!rate || fade < 2 || fade > 1200 || static_cast<int64_t>(fade)*w*h*4 > INT64_C(256)*1024*1024 || rate%fps != 0)) return false;
    std::vector<std::vector<uint8_t>> tail;
    std::vector<uint8_t> audioTail;
    int64_t fadeSamples = fade ? static_cast<int64_t>(fade)*(rate/fps) : 0;
    const auto *encoder = avcodec_find_encoder_by_name("ffv1");
    if(!encoder) return false;
    Output out(dest);
    if(!out.open("matroska")) return false;
    Codec enc;
    enc.ctx = avcodec_alloc_context3(encoder);
    if(!enc.ctx) return false;
    enc.ctx->width = w;
    enc.ctx->height = h;
    enc.ctx->pix_fmt = AV_PIX_FMT_BGR0;
    enc.ctx->time_base = AVRational{1,fps};
    enc.ctx->framerate = AVRational{fps,1};
    enc.ctx->sample_aspect_ratio = AVRational{1,1};
    if(out.ctx->oformat->flags & AVFMT_GLOBALHEADER) enc.ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if(avcodec_open2(enc.ctx, encoder, nullptr) < 0) return false;
    auto *dst = avformat_new_stream(out.ctx, nullptr);
    if(!dst || avcodec_parameters_from_context(dst->codecpar, enc.ctx) < 0) return false;
    dst->time_base = enc.ctx->time_base;
    dst->avg_frame_rate = enc.ctx->framerate;
    dst->sample_aspect_ratio = enc.ctx->sample_aspect_ratio;
    AVStream *audioDst = nullptr;
    if(rate) {
        audioDst = avformat_new_stream(out.ctx,nullptr);
        if(!audioDst) return false;
        auto *p = audioDst->codecpar;
        p->codec_type=AVMEDIA_TYPE_AUDIO;
        p->codec_id=AV_CODEC_ID_PCM_S16LE;
        p->format=AV_SAMPLE_FMT_S16;
        p->sample_rate=rate;
        p->bits_per_coded_sample=p->bits_per_raw_sample=16;
        p->block_align=channels*2;
        p->bit_rate=static_cast<int64_t>(rate)*channels*16;
        av_channel_layout_default(&p->ch_layout,channels);
        audioDst->time_base=AVRational{1,rate};
        // Avoid early interleave flush before the clip's separate audio pass.
        out.ctx->max_interleave_delta=0;
    }
    if(avformat_write_header(out.ctx, nullptr) < 0) return false;
    Frame scaled;
    Packet encoded;
    if(!scaled.p || !encoded.p) return false;
    scaled.p->format = enc.ctx->pix_fmt;
    scaled.p->width = w;
    scaled.p->height = h;
    if(av_frame_get_buffer(scaled.p, 32) < 0) return false;
    int64_t total = 0;
    auto drainEncoder = [&](bool flush) {
        for(;;) {
            int ret = avcodec_receive_packet(enc.ctx, encoded.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            if(encoded.p->duration <= 0) encoded.p->duration = 1;
            av_packet_rescale_ts(encoded.p, enc.ctx->time_base, dst->time_base);
            encoded.p->stream_index = dst->index;
            int status = av_interleaved_write_frame(out.ctx, encoded.p);
            av_packet_unref(encoded.p);
            if(status < 0) return false;
        }
    };
    auto emit = [&]() {
        scaled.p->pts=total++;
        scaled.p->pict_type=AV_PICTURE_TYPE_NONE;
        return avcodec_send_frame(enc.ctx,scaled.p)>=0 && drainEncoder(false);
    };
    Frame mixed;
    if(fade) {
        if(!mixed.p) return false;
        mixed.p->format=AV_PIX_FMT_BGR0; mixed.p->width=w; mixed.p->height=h;
        if(av_frame_get_buffer(mixed.p,32)<0) return false;
        tail.reserve(fade);
    }
    for(int c=0;c<2;++c) {
        auto *clip=clips[c];
        int64_t clipOffset=total;
        auto *stream = clip->ctx->streams[clip->video];
        const auto *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if(!decoder) return false;
        Codec dec;
        dec.ctx = avcodec_alloc_context3(decoder);
        Frame frame;
        Packet packet;
        Scale scale;
        if(!dec.ctx || !frame.p || !packet.p ||
           avcodec_parameters_to_context(dec.ctx, stream->codecpar) < 0) return false;
        dec.ctx->pkt_timebase = stream->time_base;
        if(avcodec_open2(dec.ctx, decoder, nullptr) < 0) return false;
        int64_t origin = AV_NOPTS_VALUE, previous = AV_NOPTS_VALUE, duration = 0, local = 0;
        const int64_t bound = INT64_C(1000000000000000);
        auto emitUntil = [&](int64_t boundary) {
            if(boundary <= origin || boundary > bound) return false;
            // Emit for grid times strictly before boundary (ceiling count).
            int64_t limit = av_rescale_q_rnd(boundary-origin, stream->time_base,
                                              enc.ctx->time_base, AV_ROUND_UP);
            if(limit < local || limit > 1000000 || total + (limit-local) > 1000000)
                return false;
            while(local < limit) {
                if(fade && c==0) {
                    std::vector<uint8_t> pixels(static_cast<size_t>(w)*h*4);
                    for(int y=0;y<h;++y) std::memcpy(pixels.data()+static_cast<size_t>(y)*w*4,
                        scaled.p->data[0]+static_cast<size_t>(y)*scaled.p->linesize[0],w*4);
                    if(tail.size()==static_cast<size_t>(fade)) {
                        if(av_frame_make_writable(mixed.p)<0) return false;
                        for(int y=0;y<h;++y) std::memcpy(mixed.p->data[0]+static_cast<size_t>(y)*mixed.p->linesize[0],
                            tail.front().data()+static_cast<size_t>(y)*w*4,w*4);
                        mixed.p->pts=total++; mixed.p->pict_type=AV_PICTURE_TYPE_NONE;
                        if(avcodec_send_frame(enc.ctx,mixed.p)<0 || !drainEncoder(false)) return false;
                        tail.erase(tail.begin());
                    }
                    tail.push_back(std::move(pixels));
                } else if(fade && local<fade) {
                    if(tail.size()!=static_cast<size_t>(fade) || av_frame_make_writable(mixed.p)<0) return false;
                    for(int y=0;y<h;++y) {
                        auto *d=mixed.p->data[0]+static_cast<size_t>(y)*mixed.p->linesize[0];
                        auto *src=scaled.p->data[0]+static_cast<size_t>(y)*scaled.p->linesize[0];
                        auto *old=tail[local].data()+static_cast<size_t>(y)*w*4;
                        for(int x=0;x<w*4;++x) {
                            if(visual == "wipeLeft") {
                                const int64_t edge = (static_cast<int64_t>(w)*local)/(fade-1);
                                d[x] = x/4 < edge ? src[x] : old[x];
                            } else if(visual == "wipeDown") {
                                const int64_t edge = (static_cast<int64_t>(h)*local)/(fade-1);
                                d[x] = y < edge ? src[x] : old[x];
                            } else if(visual == "black") {
                                // Exact black midpoint for odd overlap lengths.
                                const int64_t twice = 2*local, last = fade-1;
                                const int64_t weight = std::abs(last-twice);
                                d[x] = static_cast<uint8_t>(((twice <= last ? old[x] : src[x])*weight+last/2)/last);
                            } else {
                                d[x]=static_cast<uint8_t>((old[x]*(fade-1-local)+src[x]*local+(fade-1)/2)/(fade-1));
                            }
                        }
                    }
                    mixed.p->pts=total++; mixed.p->pict_type=AV_PICTURE_TYPE_NONE;
                    if(avcodec_send_frame(enc.ctx,mixed.p)<0 || !drainEncoder(false)) return false;
                } else if(!emit()) return false;
                ++local;
            }
            return true;
        };
        auto drain = [&](bool flush) {
            for(;;) {
                int ret = avcodec_receive_frame(dec.ctx, frame.p);
                if(ret == AVERROR_EOF) return flush;
                if(ret == AVERROR(EAGAIN)) return !flush;
                if(ret < 0) return false;
                auto *f = frame.p;
                int64_t pts = f->best_effort_timestamp;
                if(pts == AV_NOPTS_VALUE || pts < -bound || pts > bound ||
                   (previous != AV_NOPTS_VALUE && pts <= previous) ||
                   f->width < 1 || f->height < 1 || f->width > 8192 || f->height > 8192)
                    return false;
                if(origin == AV_NOPTS_VALUE) origin = pts;
                else if(!emitUntil(pts)) return false;
                scale.p = sws_getCachedContext(scale.p, f->width, f->height,
                    static_cast<AVPixelFormat>(f->format), w, h, enc.ctx->pix_fmt,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                if(!scale.p || av_frame_make_writable(scaled.p) < 0 ||
                   sws_scale(scale.p, f->data, f->linesize, 0, f->height,
                             scaled.p->data, scaled.p->linesize) != h) return false;
                previous = pts;
#if LIBAVUTIL_VERSION_MAJOR >= 58
                duration = f->duration;
#else
                duration = f->pkt_duration;
#endif
                av_frame_unref(f);
            }
        };
        int ret;
        while((ret = av_read_frame(clip->ctx, packet.p)) >= 0) {
            if(packet.p->stream_index != clip->video) { av_packet_unref(packet.p); continue; }
            int sent = avcodec_send_packet(dec.ctx, packet.p);
            av_packet_unref(packet.p);
            if(sent < 0 || !drain(false)) return false;
        }
        if(ret != AVERROR_EOF || avcodec_send_packet(dec.ctx, nullptr) < 0 ||
           !drain(true) || previous == AV_NOPTS_VALUE || duration <= 0 ||
           duration > bound || !emitUntil(previous+duration) || local == 0) return false;
        if(fade && local<fade) return false;
        if(fade) {
            int64_t length=local*(rate/fps);
            std::vector<uint8_t> pcm;
            if(!normalizedAudio(c==0?first:secondClip,audioIndices[c],rate,channels,origin,
                                stream->time_base,0,length,nullptr,nullptr,&pcm)) return false;
            auto writePCM = [&](const uint8_t *data,int64_t n,int64_t offset) {
                while(n>0) {
                    int block=static_cast<int>(std::min<int64_t>(n,4096));
                    Packet p;
                    if(!p.p || av_new_packet(p.p,block*channels*2)<0) return false;
                    std::memcpy(p.p->data,data,p.p->size);
                    p.p->pts=p.p->dts=offset; p.p->duration=block;
                    p.p->stream_index=audioDst->index; p.p->flags=AV_PKT_FLAG_KEY;
                    av_packet_rescale_ts(p.p,AVRational{1,rate},audioDst->time_base);
                    if(av_interleaved_write_frame(out.ctx,p.p)<0) return false;
                    data+=block*channels*2; offset+=block; n-=block;
                }
                return true;
            };
            size_t overlap=static_cast<size_t>(fadeSamples)*channels*2;
            if(c==0) {
                if(!writePCM(pcm.data(),length-fadeSamples,0)) return false;
                audioTail.assign(pcm.end()-overlap,pcm.end());
            } else {
                if(audioTail.size()!=overlap || fadeSamples<2) return false;
                for(int64_t i=0;i<fadeSamples;++i) for(int ch=0;ch<channels;++ch) {
                    size_t k=static_cast<size_t>(i*channels+ch)*2;
                    int16_t x,y;
                    std::memcpy(&x,audioTail.data()+k,2); std::memcpy(&y,pcm.data()+k,2);
                    int16_t z;
                    if(audioCurve == "equalPower") {
                        double angle=static_cast<double>(i)/(fadeSamples-1)*1.57079632679489661923;
                        auto value=std::llround(x*std::cos(angle)+y*std::sin(angle));
                        z=static_cast<int16_t>(std::max<long long>(-32768,std::min<long long>(32767,value)));
                    } else {
                        z=static_cast<int16_t>((static_cast<int64_t>(x)*(fadeSamples-1-i)+
                                                   static_cast<int64_t>(y)*i)/(fadeSamples-1));
                    }
                    std::memcpy(pcm.data()+k,&z,2);
                }
                if(!writePCM(pcm.data(),length,clipOffset*(rate/fps))) return false;
            }
        } else if(rate) {
            int64_t begin=av_rescale_q(clipOffset, enc.ctx->time_base, AVRational{1,rate});
            int64_t end=av_rescale_q(total, enc.ctx->time_base, AVRational{1,rate});
            if(!normalizedAudio(c==0?first:secondClip,audioIndices[c],rate,channels,origin,
                                stream->time_base,begin,end-begin,audioDst,out.ctx)) return false;
        }
    }
    return avcodec_send_frame(enc.ctx, nullptr) >= 0 && drainEncoder(true) && out.finish();
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


// Audio track indices are ordinal among audio streams, not container stream IDs.
int audioTrackCount(const Input &in) {
    int count = 0;
    for(unsigned i = 0; i < in.ctx->nb_streams; ++i)
        if(in.ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ++count;
    return count;
}
bool extractAudio(Input &in, const std::string &dest, int track) {
    const auto dot = dest.find_last_of('.');
    if(dot == std::string::npos || track < 0) return false;
    const auto ext = dest.substr(dot);
    const char *format = ext == ".mka" ? "matroska" : ext == ".wav" ? "wav" :
                         ext == ".flac" ? "flac" : ext == ".mp3" ? "mp3" :
                         ext == ".m4a" ? "ipod" : ext == ".ogg" ? "ogg" :
                         ext == ".opus" ? "opus" : nullptr;
    if(!format) return false;
    int index = -1;
    for(unsigned i = 0; i < in.ctx->nb_streams; ++i) {
        if(in.ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && track-- == 0) {
            index = static_cast<int>(i);
            break;
        }
    }
    if(index < 0) return false;
    auto *src = in.ctx->streams[index];
    // Do not silently put an unrelated codec into a codec-specific extension.
    const auto id = src->codecpar->codec_id;
    if((ext == ".flac" && id != AV_CODEC_ID_FLAC) ||
       (ext == ".mp3" && id != AV_CODEC_ID_MP3) ||
       (ext == ".opus" && id != AV_CODEC_ID_OPUS) ||
       (ext == ".m4a" && id != AV_CODEC_ID_AAC && id != AV_CODEC_ID_ALAC)) return false;
    Output out(dest);
    if(!out.open(format)) return false;
    if(avformat_query_codec(out.ctx->oformat, id, FF_COMPLIANCE_NORMAL) <= 0) return false;
    auto *dst = avformat_new_stream(out.ctx, nullptr);
    if(!dst || avcodec_parameters_copy(dst->codecpar, src->codecpar) < 0) return false;
    dst->codecpar->codec_tag = 0;
    dst->time_base = src->time_base;
    dst->disposition = src->disposition;
    if(av_dict_copy(&dst->metadata, src->metadata, 0) < 0) return false;
    if(avformat_write_header(out.ctx, nullptr) < 0) return false;
    Packet packet;
    if(!packet.p) return false;
    int ret;
    bool wrote = false;
    while((ret = av_read_frame(in.ctx, packet.p)) >= 0) {
        if(packet.p->stream_index != index) {
            av_packet_unref(packet.p);
            continue;
        }
        av_packet_rescale_ts(packet.p, src->time_base, dst->time_base);
        packet.p->stream_index = dst->index;
        packet.p->pos = -1;
        const int written = av_interleaved_write_frame(out.ctx, packet.p);
        av_packet_unref(packet.p);
        if(written < 0) return false;
        wrote = true;
    }
    return ret == AVERROR_EOF && wrote && out.finish();
}


// Decode one selected audio stream and write interleaved floating-point samples
// through libsndfile. Preserve sample rate and channel count; reject changes.
bool convertAudio(Input &in, const std::string &dest, int track) {
    const auto dot = dest.find_last_of('.');
    if(dot == std::string::npos || track < 0) return false;
    const auto ext = dest.substr(dot);
    int format = ext == ".wav" ? SF_FORMAT_WAV | SF_FORMAT_PCM_24 :
                 ext == ".flac" ? SF_FORMAT_FLAC | SF_FORMAT_PCM_24 :
                 ext == ".mp3" ? SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III :
                 ext == ".ogg" ? SF_FORMAT_OGG | SF_FORMAT_VORBIS :
                 ext == ".aiff" || ext == ".aif" ? SF_FORMAT_AIFF | SF_FORMAT_PCM_24 : 0;
    if(!format) return false;
    int index = -1;
    for(unsigned i = 0; i < in.ctx->nb_streams; ++i)
        if(in.ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && track-- == 0) {
            index = static_cast<int>(i); break;
        }
    if(index < 0) return false;
    auto *src = in.ctx->streams[index];
    const auto *decoder = avcodec_find_decoder(src->codecpar->codec_id);
    if(!decoder) return false;
    Codec codec;
    codec.ctx = avcodec_alloc_context3(decoder);
    if(!codec.ctx || avcodec_parameters_to_context(codec.ctx, src->codecpar) < 0 ||
       avcodec_open2(codec.ctx, decoder, nullptr) < 0) return false;
    // RAII closes both handles before deleting a failed output.
    struct AudioOutput {
        const std::string &path;
        FILE *file = nullptr;
        SNDFILE *sound = nullptr;
        bool created = false, complete = false;
        explicit AudioOutput(const std::string &p) : path(p) {}
        ~AudioOutput() {
            if(sound) sf_close(sound);
            if(file) std::fclose(file);
            if(created && !complete) std::remove(path.c_str());
        }
        bool open(SF_INFO &info) {
            if(!sf_format_check(&info)) return false;
            file = mediaio::openNew(path.c_str());
            if(!file) return false;
            created = true;
#ifdef _WIN32
            int fd = _fileno(file);
#else
            int fd = fileno(file);
#endif
            sound = sf_open_fd(fd, SFM_WRITE, &info, SF_FALSE);
            return sound != nullptr;
        }
        bool finish() {
            bool ok = sf_error(sound) == SF_ERR_NO_ERROR;
            if(sf_close(sound) != 0) ok = false;
            sound = nullptr;
            if(std::fclose(file) != 0) ok = false;
            file = nullptr;
            complete = ok;
            return ok;
        }
    } out(dest);
    Frame frame;
    Packet packet;
    if(!frame.p || !packet.p) return false;
    SF_INFO info{};
    info.format = format;
    std::vector<double> samples;
    auto drain = [&](bool flushing) {
        for(;;) {
            int ret = avcodec_receive_frame(codec.ctx, frame.p);
            if(ret == AVERROR_EOF) return flushing;
            if(ret == AVERROR(EAGAIN)) return !flushing;
            if(ret < 0) return false;
            auto *a = frame.p;
            const int channels = a->ch_layout.nb_channels;
            if(channels < 1 || channels > 8 || a->sample_rate < 1 ||
               a->nb_samples < 1 || a->nb_samples > 1048576) return false;
            if(!out.sound) {
                info.channels = channels;
                info.samplerate = a->sample_rate;
                if(!out.open(info)) return false;
            }
            if(channels != info.channels || a->sample_rate != info.samplerate) return false;
            const auto fmt = static_cast<AVSampleFormat>(a->format);
            const auto packed = av_get_packed_sample_fmt(fmt);
            const bool planar = av_sample_fmt_is_planar(fmt) != 0;
            const int bytes = av_get_bytes_per_sample(fmt);
            if(bytes <= 0 || !a->extended_data) return false;
            samples.resize(static_cast<size_t>(a->nb_samples) * channels);
            for(int n = 0; n < a->nb_samples; ++n) {
                for(int c = 0; c < channels; ++c) {
                    const size_t i = static_cast<size_t>(n) * channels + c;
                    const auto *base = a->extended_data[planar ? c : 0];
                    if(!base) return false;
                    const auto *p = base + (planar ? static_cast<size_t>(n) : i) * bytes;
                    double v;
                    switch(packed) {
                    case AV_SAMPLE_FMT_U8: v = (static_cast<int>(*p) - 128) / 128.0; break;
                    case AV_SAMPLE_FMT_S16: { int16_t x; std::memcpy(&x,p,2); v = x / 32768.0; break; }
                    case AV_SAMPLE_FMT_S32: { int32_t x; std::memcpy(&x,p,4); v = x / 2147483648.0; break; }
                    case AV_SAMPLE_FMT_S64: { int64_t x; std::memcpy(&x,p,8); v = static_cast<double>(x) / 9223372036854775808.0; break; }
                    case AV_SAMPLE_FMT_FLT: { float x; std::memcpy(&x,p,4); v = x; break; }
                    case AV_SAMPLE_FMT_DBL: std::memcpy(&v,p,8); break;
                    default: return false;
                    }
                    if(!std::isfinite(v)) return false;
                    samples[i] = std::max(-1.0, std::min(1.0, v));
                }
            }
            if(sf_writef_double(out.sound, samples.data(), a->nb_samples) != a->nb_samples)
                return false;
            av_frame_unref(a);
        }
    };
    int ret;
    while((ret = av_read_frame(in.ctx, packet.p)) >= 0) {
        if(packet.p->stream_index == index) {
            int sent = avcodec_send_packet(codec.ctx, packet.p);
            av_packet_unref(packet.p);
            if(sent < 0 || !drain(false)) return false;
        } else av_packet_unref(packet.p);
    }
    return ret == AVERROR_EOF && avcodec_send_packet(codec.ctx, nullptr) >= 0 &&
           drain(true) && out.sound && out.finish();
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

// Audio transcoder for continuous mono/stereo tracks. The first presentation
// timestamp anchors the sample clock; reject discontinuities instead of hiding them.
struct AudioEncoder {
    Codec dec, enc;
    Frame decoded, converted, pending;
    Packet packet;
    SwrContext *swr = nullptr;
    AVAudioFifo *fifo = nullptr;
    AVStream *src = nullptr, *dst = nullptr;
    AVFormatContext *output = nullptr;
    int64_t nextPts = AV_NOPTS_VALUE, firstPts = AV_NOPTS_VALUE, inputSamples = 0;
    ~AudioEncoder() { swr_free(&swr); if(fifo) av_audio_fifo_free(fifo); }
    bool open(AVStream *source, AVFormatContext *out, bool opus, int kbps) {
        src = source; output = out;
        const auto *decoder = avcodec_find_decoder(src->codecpar->codec_id);
        const auto *encoder = avcodec_find_encoder_by_name(opus ? "libopus" : "aac");
        if(!decoder || !encoder || !decoded.p || !converted.p || !pending.p || !packet.p) return false;
        dec.ctx = avcodec_alloc_context3(decoder);
        enc.ctx = avcodec_alloc_context3(encoder);
        if(!dec.ctx || !enc.ctx || avcodec_parameters_to_context(dec.ctx, src->codecpar) < 0) return false;
        dec.ctx->pkt_timebase = src->time_base;
        if(avcodec_open2(dec.ctx, decoder, nullptr) < 0) return false;
        int channels = dec.ctx->ch_layout.nb_channels;
        if(channels < 1 || channels > 2 || dec.ctx->sample_rate <= 0 ||
           dec.ctx->sample_fmt == AV_SAMPLE_FMT_NONE) return false;
        if(dec.ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&dec.ctx->ch_layout, channels);
        av_channel_layout_default(&enc.ctx->ch_layout, channels);
        // Only canonical mono/stereo layouts are accepted in this initial API.
        if(av_channel_layout_compare(&dec.ctx->ch_layout, &enc.ctx->ch_layout) != 0) return false;
        enc.ctx->sample_rate = 48000;
        enc.ctx->sample_fmt = opus ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP;
        enc.ctx->time_base = AVRational{1,48000};
        enc.ctx->bit_rate = static_cast<int64_t>(kbps) * 1000;
        if(out->oformat->flags & AVFMT_GLOBALHEADER) enc.ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if(avcodec_open2(enc.ctx, encoder, nullptr) < 0 || enc.ctx->frame_size <= 0) return false;
        if(swr_alloc_set_opts2(&swr, &enc.ctx->ch_layout, enc.ctx->sample_fmt, enc.ctx->sample_rate,
                              &dec.ctx->ch_layout, dec.ctx->sample_fmt, dec.ctx->sample_rate, 0, nullptr) < 0 ||
           !swr || swr_init(swr) < 0) return false;
        fifo = av_audio_fifo_alloc(enc.ctx->sample_fmt, channels, enc.ctx->frame_size);
        if(!fifo) return false;
        dst = avformat_new_stream(out, nullptr);
        if(!dst || avcodec_parameters_from_context(dst->codecpar, enc.ctx) < 0) return false;
        dst->time_base = enc.ctx->time_base;
        dst->disposition = src->disposition;
        return av_dict_copy(&dst->metadata, src->metadata, 0) >= 0;
    }
    bool drainEncoder(bool flush) {
        for(;;) {
            int ret = avcodec_receive_packet(enc.ctx, packet.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            av_packet_rescale_ts(packet.p, enc.ctx->time_base, dst->time_base);
            packet.p->stream_index = dst->index;
            int written = av_interleaved_write_frame(output, packet.p);
            av_packet_unref(packet.p);
            if(written < 0) return false;
        }
    }
    bool prepare(AVFrame *f, int n) {
        av_frame_unref(f);
        f->nb_samples = n;
        f->format = enc.ctx->sample_fmt;
        f->sample_rate = enc.ctx->sample_rate;
        return av_channel_layout_copy(&f->ch_layout, &enc.ctx->ch_layout) >= 0 &&
               av_frame_get_buffer(f, 0) >= 0;
    }
    bool emit(bool last) {
        const int block = enc.ctx->frame_size;
        while(av_audio_fifo_size(fifo) >= block || (last && av_audio_fifo_size(fifo) > 0)) {
            const int n = std::min(block, av_audio_fifo_size(fifo));
            // AAC and libopus support small final frames; otherwise pad explicitly.
            const int size = n < block && !(enc.ctx->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME)
                ? block : n;
            if(!prepare(pending.p, size) ||
               av_audio_fifo_read(fifo, reinterpret_cast<void **>(pending.p->extended_data), n) != n)
                return false;
            if(size > n && av_samples_set_silence(pending.p->extended_data, n, size-n,
                enc.ctx->ch_layout.nb_channels, enc.ctx->sample_fmt) < 0) return false;
            pending.p->pts = nextPts;
            nextPts += size;
            if(avcodec_send_frame(enc.ctx, pending.p) < 0 || !drainEncoder(false)) return false;
        }
        return true;
    }
    bool resample(const uint8_t **data, int n) {
        int capacity = swr_get_out_samples(swr, n);
        if(capacity < 0 || capacity > 2097152) return false;
        if(capacity == 0) return true;
        if(!prepare(converted.p, capacity)) return false;
        int got = swr_convert(swr, converted.p->extended_data, capacity, data, n);
        if(got < 0) return false;
        if(got && av_audio_fifo_write(fifo, reinterpret_cast<void **>(converted.p->extended_data), got) != got)
            return false;
        return emit(false);
    }
    bool drainDecoder(bool flush) {
        for(;;) {
            int ret = avcodec_receive_frame(dec.ctx, decoded.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            auto *f = decoded.p;
            if(f->sample_rate != dec.ctx->sample_rate || f->format != dec.ctx->sample_fmt ||
               f->ch_layout.nb_channels != dec.ctx->ch_layout.nb_channels ||
               (f->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC &&
                av_channel_layout_compare(&f->ch_layout, &dec.ctx->ch_layout) != 0) ||
               f->nb_samples <= 0 || f->nb_samples > 1048576) return false;
            int64_t pts = f->best_effort_timestamp;
            if(pts == AV_NOPTS_VALUE) return false;
            if(firstPts == AV_NOPTS_VALUE) {
                firstPts = pts;
                nextPts = av_rescale_q(pts, src->time_base, enc.ctx->time_base);
            }
            // Allow container timestamp quantization (at least two milliseconds).
            double elapsed = (static_cast<double>(pts) - static_cast<double>(firstPts)) * av_q2d(src->time_base);
            double expected = static_cast<double>(inputSamples) / dec.ctx->sample_rate;
            if(std::abs(elapsed - expected) > std::max(0.002, 2.0 * av_q2d(src->time_base)))
                return false;
            const uint8_t *planes[2] = {f->extended_data[0], nullptr};
            if(av_sample_fmt_is_planar(dec.ctx->sample_fmt) && dec.ctx->ch_layout.nb_channels == 2)
                planes[1] = f->extended_data[1];
            if(!resample(planes, f->nb_samples)) return false;
            inputSamples += f->nb_samples;
            av_frame_unref(f);
        }
    }
    bool push(AVPacket *p) {
        return avcodec_send_packet(dec.ctx, p) >= 0 && drainDecoder(false);
    }
    bool finish() {
        if(avcodec_send_packet(dec.ctx, nullptr) < 0 || !drainDecoder(true) ||
           firstPts == AV_NOPTS_VALUE) return false;
        // The flush capacity is an upper bound for the remaining resampled samples.
        if(!resample(nullptr, 0) || !emit(true)) return false;
        return avcodec_send_frame(enc.ctx, nullptr) >= 0 && drainEncoder(true);
    }
};


// Locate the exact presentation interval, including the next frame boundary.
// At EOF require a known final frame duration.
bool frameInterval(const std::string &source, int64_t first, int64_t count,
                   int64_t &begin, int64_t &end, AVRational &tb) {
    Input in;
    if(!in.open(source)) return false;
    auto *stream = in.ctx->streams[in.video];
    tb = stream->time_base;
    const auto *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if(!decoder) return false;
    Codec dec;
    dec.ctx = avcodec_alloc_context3(decoder);
    Frame frame;
    Packet packet;
    if(!dec.ctx || !frame.p || !packet.p ||
       avcodec_parameters_to_context(dec.ctx, stream->codecpar) < 0) return false;
    dec.ctx->pkt_timebase = tb;
    if(avcodec_open2(dec.ctx, decoder, nullptr) < 0) return false;
    int64_t seen = 0, previous = AV_NOPTS_VALUE, duration = 0;
    bool done = false;
    auto drain = [&](bool flush) {
        while(!done) {
            int ret = avcodec_receive_frame(dec.ctx, frame.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            int64_t pts = frame.p->best_effort_timestamp;
            if(pts == AV_NOPTS_VALUE || (previous != AV_NOPTS_VALUE && pts <= previous) ||
               pts < -INT64_C(1000000000000000) || pts > INT64_C(1000000000000000))
                return false;
            if(seen == first) begin = pts;
            if(seen == first + count) { end = pts; done = true; }
            ++seen;
            previous = pts;
#if LIBAVUTIL_VERSION_MAJOR >= 58
            duration = frame.p->duration;
#else
            duration = frame.p->pkt_duration;
#endif
            av_frame_unref(frame.p);
        }
        return true;
    };
    int ret = 0;
    while(!done && (ret = av_read_frame(in.ctx, packet.p)) >= 0) {
        if(packet.p->stream_index == in.video) {
            int sent = avcodec_send_packet(dec.ctx, packet.p);
            av_packet_unref(packet.p);
            if(sent < 0 || !drain(false)) return false;
        } else av_packet_unref(packet.p);
    }
    if(!done) {
        if(ret != AVERROR_EOF || avcodec_send_packet(dec.ctx, nullptr) < 0 || !drain(true))
            return false;
        if(!done) {
            if(seen != first + count || duration <= 0 || duration > INT64_C(1000000000000000))
                return false;
            end = previous + duration;
        }
    }
    return begin != AV_NOPTS_VALUE && end > begin;
}

// PCM S16LE is packet-addressable: slice complete interleaved sample frames.
// Track a continuous sample clock, tolerating only container timestamp rounding.
struct PCMTrim {
    int rate = 0, bytes = 0;
    int64_t start = 0, stop = 0, next = AV_NOPTS_VALUE, written = 0;
    bool setup(const AVCodecParameters *p, int64_t begin, int64_t end, AVRational videoTB) {
        if(p->codec_id != AV_CODEC_ID_PCM_S16LE || p->sample_rate <= 0 ||
           p->sample_rate > 384000 || p->ch_layout.nb_channels < 1 ||
           p->ch_layout.nb_channels > 8) return false;
        rate = p->sample_rate;
        bytes = p->ch_layout.nb_channels * 2;
        start = av_rescale_q_rnd(begin, videoTB, AVRational{1,rate}, AV_ROUND_UP);
        stop = av_rescale_q_rnd(end, videoTB, AVRational{1,rate}, AV_ROUND_UP);
        return start > -INT64_C(1000000000000000) && stop < INT64_C(1000000000000000) &&
               stop > start;
    }
    bool push(AVPacket *p, AVStream *src, AVStream *dst, AVFormatContext *out) {
        if(p->pts == AV_NOPTS_VALUE || p->dts != p->pts || p->size <= 0 ||
           p->size % bytes || av_packet_get_side_data(p, AV_PKT_DATA_SKIP_SAMPLES, nullptr))
            return false;
        int64_t pos = av_rescale_q(p->pts, src->time_base, AVRational{1,rate});
        if(pos < -INT64_C(1000000000000000) || pos > INT64_C(1000000000000000)) return false;
        int64_t tolerance = std::max<int64_t>(1, av_rescale_q_rnd(1, src->time_base,
                                                    AVRational{1,rate}, AV_ROUND_UP));
        if(next != AV_NOPTS_VALUE) {
            if(std::abs(pos-next) > tolerance) return false;
            pos = next;
        }
        next = pos + p->size / bytes;
        const int64_t lo = std::max(pos, start), hi = std::min(next, stop);
        if(hi <= lo) return true;
        if(lo != start + written) return false; // no silent gaps or padding
        Packet cut;
        int size = static_cast<int>((hi-lo)*bytes);
        if(!cut.p || av_new_packet(cut.p, size) < 0) return false;
        std::memcpy(cut.p->data, p->data + (lo-pos)*bytes, size);
        cut.p->pts = cut.p->dts = lo-start;
        cut.p->duration = hi-lo;
        av_packet_rescale_ts(cut.p, AVRational{1,rate}, dst->time_base);
        cut.p->stream_index = dst->index;
        cut.p->flags = AV_PKT_FLAG_KEY;
        cut.p->pos = -1;
        if(av_interleaved_write_frame(out, cut.p) < 0) return false;
        written += hi-lo;
        return true;
    }
};


// Decode compressed audio to native-rate interleaved S16, then reuse the
// continuous PCM sample clock and range writer. No sample-rate conversion.
struct DecodedTrim {
    Codec dec;
    Frame frame, converted;
    SwrContext *swr = nullptr;
    AVStream *src = nullptr, *dst = nullptr;
    AVFormatContext *out = nullptr;
    PCMTrim *range = nullptr;
    ~DecodedTrim() { swr_free(&swr); }
    bool setup(AVStream *input, AVStream *output, AVFormatContext *format,
               PCMTrim &pcm, int64_t begin, int64_t end, AVRational videoTB) {
        src = input; dst = output; out = format; range = &pcm;
        const auto *codec = avcodec_find_decoder(src->codecpar->codec_id);
        if(!codec || !frame.p || !converted.p) return false;
        dec.ctx = avcodec_alloc_context3(codec);
        if(!dec.ctx || avcodec_parameters_to_context(dec.ctx, src->codecpar) < 0) return false;
        dec.ctx->pkt_timebase = src->time_base;
        if(avcodec_open2(dec.ctx, codec, nullptr) < 0 || dec.ctx->sample_rate <= 0 ||
           dec.ctx->sample_rate > 384000 || dec.ctx->ch_layout.nb_channels < 1 ||
           dec.ctx->ch_layout.nb_channels > 8 || dec.ctx->sample_fmt == AV_SAMPLE_FMT_NONE)
            return false;
        if(dec.ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_default(&dec.ctx->ch_layout, dec.ctx->ch_layout.nb_channels);
        auto *p = dst->codecpar;
        p->codec_type = AVMEDIA_TYPE_AUDIO;
        p->codec_id = AV_CODEC_ID_PCM_S16LE;
        p->format = AV_SAMPLE_FMT_S16;
        p->sample_rate = dec.ctx->sample_rate;
        p->bits_per_coded_sample = p->bits_per_raw_sample = 16;
        p->block_align = dec.ctx->ch_layout.nb_channels * 2;
        p->bit_rate = static_cast<int64_t>(p->sample_rate) * p->block_align * 8;
        if(av_channel_layout_copy(&p->ch_layout, &dec.ctx->ch_layout) < 0 ||
           !pcm.setup(p, begin, end, videoTB)) return false;
        dst->time_base = AVRational{1,p->sample_rate};
        if(swr_alloc_set_opts2(&swr, &p->ch_layout, AV_SAMPLE_FMT_S16, p->sample_rate,
                             &dec.ctx->ch_layout, dec.ctx->sample_fmt, dec.ctx->sample_rate,
                             0, nullptr) < 0 || !swr || swr_init(swr) < 0) return false;
        return true;
    }
    bool drain(bool flush) {
        for(;;) {
            int ret = avcodec_receive_frame(dec.ctx, frame.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            auto *f = frame.p;
            if(f->sample_rate != dec.ctx->sample_rate || f->format != dec.ctx->sample_fmt ||
               f->ch_layout.nb_channels != dec.ctx->ch_layout.nb_channels ||
               (f->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC &&
                av_channel_layout_compare(&f->ch_layout, &dec.ctx->ch_layout) != 0) ||
               f->nb_samples <= 0 || f->nb_samples > 1048576 ||
               f->best_effort_timestamp == AV_NOPTS_VALUE) return false;
            auto *c = converted.p;
            av_frame_unref(c);
            c->nb_samples = f->nb_samples;
            c->format = AV_SAMPLE_FMT_S16;
            c->sample_rate = dec.ctx->sample_rate;
            if(av_channel_layout_copy(&c->ch_layout, &dec.ctx->ch_layout) < 0 ||
               av_frame_get_buffer(c, 0) < 0) return false;
            const uint8_t *planes[8] = {};
            int nplanes = av_sample_fmt_is_planar(dec.ctx->sample_fmt)
                ? dec.ctx->ch_layout.nb_channels : 1;
            for(int i = 0; i < nplanes; ++i) planes[i] = f->extended_data[i];
            int got = swr_convert(swr, c->extended_data, c->nb_samples, planes, f->nb_samples);
            if(got != f->nb_samples || swr_get_delay(swr, dec.ctx->sample_rate) != 0) return false;
            // PCMTrim only reads this packet; do not transfer ownership of frame data.
            AVPacket view{};
            view.data = c->data[0];
            view.size = got * range->bytes;
            view.pts = view.dts = f->best_effort_timestamp;
            if(!range->push(&view, src, dst, out)) return false;
            av_frame_unref(f);
        }
    }
    bool push(AVPacket *p) {
        return avcodec_send_packet(dec.ctx, p) >= 0 && drain(false);
    }
    bool finish() {
        return avcodec_send_packet(dec.ctx, nullptr) >= 0 && drain(true);
    }
};

bool transcode(const std::string &source, const std::string &dest,
               int w, int h, const std::string &mode, int quality, int audioKbps = 0,
               int64_t firstFrame = 0, int64_t frameCount = 0, bool trimAudio = false) {
    const bool ffv1 = mode == "ffv1", h264 = mode == "h264", vp9 = mode == "vp9";
    const char *container = ffv1 ? "matroska" : h264 ? "mp4" : vp9 ? "webm" : nullptr;
    const std::string ext = ffv1 ? ".mkv" : h264 ? ".mp4" : ".webm";
    if(!container || dest.size() < ext.size() || dest.substr(dest.size()-ext.size()) != ext ||
       (!(frameCount > 0 && w == 0 && h == 0) && (w < 1 || h < 1 || w > 8192 || h > 8192)) || quality < 0 || quality > 51 ||
       (ffv1 && quality != 0) || (!ffv1 && ((w & 1) || (h & 1)))) return false;
    if(audioKbps && (ffv1 || audioKbps < 32 || audioKbps > 256)) return false;
    Input in;
    if(!in.open(source)) return false;
    const bool cutting = frameCount > 0;
    if(cutting && (firstFrame < 0 || (!trimAudio && in.ctx->nb_streams != 1) || audioKbps)) return false;
    int64_t cutBegin = AV_NOPTS_VALUE, cutEnd = AV_NOPTS_VALUE;
    AVRational cutTB{};
    if(trimAudio && (!cutting || !ffv1 || !frameInterval(source, firstFrame, frameCount,
                                                        cutBegin, cutEnd, cutTB))) return false;
    std::vector<PCMTrim> pcm(in.ctx->nb_streams);
    std::vector<std::unique_ptr<DecodedTrim>> decodedAudio(in.ctx->nb_streams);
    if(cutting && w == 0 && h == 0) {
        w = in.ctx->streams[in.video]->codecpar->width;
        h = in.ctx->streams[in.video]->codecpar->height;
        if(w < 1 || h < 1 || w > 8192 || h > 8192 || (!ffv1 && ((w & 1) || (h & 1))))
            return false;
    }
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
    std::vector<std::unique_ptr<AudioEncoder>> audioEncoders(in.ctx->nb_streams);
    for(unsigned i = 0; i < in.ctx->nb_streams; ++i) {
        if(static_cast<int>(i) == in.video) continue;
        auto *audio = in.ctx->streams[i];
        if(trimAudio && audio->codecpar->codec_id != AV_CODEC_ID_PCM_S16LE) {
            auto *target = avformat_new_stream(out.ctx, nullptr);
            auto decoder = std::make_unique<DecodedTrim>();
            if(!target || !decoder->setup(audio, target, out.ctx, pcm[i], cutBegin, cutEnd, cutTB))
                return false;
            target->disposition = audio->disposition;
            if(av_dict_copy(&target->metadata, audio->metadata, 0) < 0) return false;
            mapping[i] = target->index;
            decodedAudio[i] = std::move(decoder);
            continue;
        }
        if(trimAudio && !pcm[i].setup(audio->codecpar, cutBegin, cutEnd, cutTB)) return false;
        if(audioKbps) {
            auto encoder = std::make_unique<AudioEncoder>();
            if(!encoder->open(audio, out.ctx, vp9, audioKbps)) return false;
            mapping[i] = encoder->dst->index;
            audioEncoders[i] = std::move(encoder);
            continue;
        }
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
    int64_t previous = AV_NOPTS_VALUE, seen = 0, selected = 0, anchor = AV_NOPTS_VALUE;
    auto drainDecoder = [&](bool flush) {
        for(;;) {
            if(cutting && selected == frameCount) return true;
            int ret = avcodec_receive_frame(dec.ctx, decoded.p);
            if(ret == AVERROR_EOF) return flush;
            if(ret == AVERROR(EAGAIN)) return !flush;
            if(ret < 0) return false;
            auto *frame = decoded.p;
            if(frame->width < 1 || frame->height < 1 || frame->width > 8192 || frame->height > 8192)
                return false;
            int64_t pts = frame->best_effort_timestamp;
            if(pts == AV_NOPTS_VALUE || (previous != AV_NOPTS_VALUE && pts <= previous)) return false;
            if(cutting && seen++ < firstFrame) {
                previous = pts;
                av_frame_unref(frame);
                continue;
            }
            if(cutting && anchor == AV_NOPTS_VALUE) anchor = pts;
            scale.p = sws_getCachedContext(scale.p, frame->width, frame->height,
                static_cast<AVPixelFormat>(frame->format), w, h, enc.ctx->pix_fmt,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if(!scale.p || av_frame_make_writable(scaled.p) < 0 ||
               sws_scale(scale.p, frame->data, frame->linesize, 0, frame->height,
                         scaled.p->data, scaled.p->linesize) != h) return false;
            scaled.p->pts = cutting ? pts - anchor : pts;
            if(cutting) ++selected;
            scaled.p->pict_type = AV_PICTURE_TYPE_NONE;
            previous = pts;
            av_frame_unref(frame);
            if(avcodec_send_frame(enc.ctx, scaled.p) < 0 || !drainEncoder(false)) return false;
        }
    };
    int ret = 0;
    while((trimAudio || !cutting || selected < frameCount) && (ret = av_read_frame(in.ctx, inputPacket.p)) >= 0) {
        int i = inputPacket.p->stream_index;
        if(i < 0 || static_cast<unsigned>(i) >= mapping.size()) return false;
        if(i == in.video) {
            if(cutting && selected == frameCount) { av_packet_unref(inputPacket.p); continue; }
            int sent = avcodec_send_packet(dec.ctx, inputPacket.p);
            av_packet_unref(inputPacket.p);
            if(sent < 0 || !drainDecoder(false)) return false;
        } else if(trimAudio) {
            bool ok = decodedAudio[i] ? decodedAudio[i]->push(inputPacket.p) :
                pcm[i].push(inputPacket.p, in.ctx->streams[i], out.ctx->streams[mapping[i]], out.ctx);
            av_packet_unref(inputPacket.p);
            if(!ok) return false;
        } else if(audioEncoders[i]) {
            bool ok = audioEncoders[i]->push(inputPacket.p);
            av_packet_unref(inputPacket.p);
            if(!ok) return false;
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
    if(!cutting || selected < frameCount) {
        if(ret != AVERROR_EOF || avcodec_send_packet(dec.ctx, nullptr) < 0 ||
           !drainDecoder(true)) return false;
    }
    if(previous == AV_NOPTS_VALUE || (cutting && selected != frameCount) ||
       avcodec_send_frame(enc.ctx, nullptr) < 0 || !drainEncoder(true)) return false;
    if(trimAudio) {
        if(ret != AVERROR_EOF) return false;
        for(auto &audio : decodedAudio) if(audio && !audio->finish()) return false;
        for(unsigned i = 0; i < pcm.size(); ++i)
            if(static_cast<int>(i) != in.video && pcm[i].written != pcm[i].stop-pcm[i].start) return false;
    }
    for(auto &audio : audioEncoders) if(audio && !audio->finish()) return false;
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
CK_DLL_SFUN(video_audio_tracks) {
    RETURN->v_int = -1;
    try {
        auto path = GET_NEXT_STRING_SAFE(ARGS);
        Input in;
        if(in.open(path)) RETURN->v_int = audioTrackCount(in);
    } catch(...) {}
}
CK_DLL_SFUN(video_extract_audio) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto track = GET_NEXT_INT(ARGS);
        Input in;
        if(track >= 0 && track <= 2147483647 && in.open(source) &&
           extractAudio(in, dest, static_cast<int>(track))) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_convert_audio) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto track = GET_NEXT_INT(ARGS);
        Input in;
        if(track >= 0 && track <= 2147483647 && in.open(source) &&
           convertAudio(in, dest, static_cast<int>(track))) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_transcode_av) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto w = GET_NEXT_INT(ARGS);
        auto h = GET_NEXT_INT(ARGS);
        auto mode = GET_NEXT_STRING_SAFE(ARGS);
        auto quality = GET_NEXT_INT(ARGS);
        auto kbps = GET_NEXT_INT(ARGS);
        if(w >= 1 && w <= 8192 && h >= 1 && h <= 8192 && quality >= 0 && quality <= 51 &&
           kbps >= 32 && kbps <= 256 && (mode == "h264" || mode == "vp9") &&
           transcode(source, dest, static_cast<int>(w), static_cast<int>(h), mode,
                     static_cast<int>(quality), static_cast<int>(kbps))) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_trim) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto start = GET_NEXT_FLOAT(ARGS);
        auto end = GET_NEXT_FLOAT(ARGS);
        if(trim(source, dest, start, end)) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_concat) {
    RETURN->v_int = 0;
    try {
        auto first = GET_NEXT_STRING_SAFE(ARGS);
        auto second = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        if(concatenate(first, second, dest)) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_trim_frames) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto first = GET_NEXT_INT(ARGS);
        auto count = GET_NEXT_INT(ARGS);
        if(first >= 0 && first <= 2147483647 && count >= 1 && count <= 100000 &&
           transcode(source, dest, 0, 0, "ffv1", 0, 0, first, count))
            RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_trim_av) {
    RETURN->v_int = 0;
    try {
        auto source = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto first = GET_NEXT_INT(ARGS);
        auto count = GET_NEXT_INT(ARGS);
        if(first >= 0 && first <= 2147483647 && count >= 1 && count <= 100000 &&
           transcode(source, dest, 0, 0, "ffv1", 0, 0, first, count, true))
            RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_concat_normalized) {
    RETURN->v_int = 0;
    try {
        auto first = GET_NEXT_STRING_SAFE(ARGS);
        auto secondClip = GET_NEXT_STRING_SAFE(ARGS);
        auto dest = GET_NEXT_STRING_SAFE(ARGS);
        auto w = GET_NEXT_INT(ARGS);
        auto h = GET_NEXT_INT(ARGS);
        auto fps = GET_NEXT_INT(ARGS);
        if(w >= 1 && w <= 8192 && h >= 1 && h <= 8192 && fps >= 1 && fps <= 120 &&
           concatNormalized(first, secondClip, dest, static_cast<int>(w),
                            static_cast<int>(h), static_cast<int>(fps))) RETURN->v_int = 1;
    } catch(...) {}
}
CK_DLL_SFUN(video_concat_av) {
    RETURN->v_int=0;
    try {
        auto first=GET_NEXT_STRING_SAFE(ARGS);
        auto secondClip=GET_NEXT_STRING_SAFE(ARGS);
        auto dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        auto rate=GET_NEXT_INT(ARGS), channels=GET_NEXT_INT(ARGS);
        if(w>=1 && w<=8192 && h>=1 && h<=8192 && fps>=1 && fps<=120 &&
           rate>=8000 && rate<=96000 && channels>=1 && channels<=2 &&
           concatNormalized(first,secondClip,dest,w,h,fps,rate,channels)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_crossfade) {
    RETURN->v_int=0;
    try {
        auto first=GET_NEXT_STRING_SAFE(ARGS);
        auto secondClip=GET_NEXT_STRING_SAFE(ARGS);
        auto dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        auto rate=GET_NEXT_INT(ARGS), channels=GET_NEXT_INT(ARGS);
        auto duration=GET_NEXT_FLOAT(ARGS);
        if(w>=1 && w<=8192 && h>=1 && h<=8192 && fps>=1 && fps<=120 &&
           rate>=8000 && rate<=96000 && channels>=1 && channels<=2 &&
           std::isfinite(duration) && duration>0 && duration*fps<=1200 &&
           std::llround(duration*fps)>=2 &&
           concatNormalized(first,secondClip,dest,w,h,fps,rate,channels,
                            static_cast<int>(std::llround(duration*fps)))) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_transition) {
    RETURN->v_int=0;
    try {
        auto first=GET_NEXT_STRING_SAFE(ARGS);
        auto secondClip=GET_NEXT_STRING_SAFE(ARGS);
        auto dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        auto rate=GET_NEXT_INT(ARGS), channels=GET_NEXT_INT(ARGS);
        auto duration=GET_NEXT_FLOAT(ARGS);
        auto visual=GET_NEXT_STRING_SAFE(ARGS);
        auto audioCurve=GET_NEXT_STRING_SAFE(ARGS);
        if(w>=1 && w<=8192 && h>=1 && h<=8192 && fps>=1 && fps<=120 &&
           rate>=8000 && rate<=96000 && channels>=1 && channels<=2 &&
           std::isfinite(duration) && duration>0 && duration*fps<=1200 &&
           std::llround(duration*fps)>=2 &&
           concatNormalized(first,secondClip,dest,w,h,fps,rate,channels,
                            static_cast<int>(std::llround(duration*fps)),visual,audioCurve)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_overlay) {
    RETURN->v_int=0;
    try {
        auto base=GET_NEXT_STRING_SAFE(ARGS), layer=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        auto x=GET_NEXT_INT(ARGS), y=GET_NEXT_INT(ARGS), lw=GET_NEXT_INT(ARGS), lh=GET_NEXT_INT(ARGS);
        auto opacity=GET_NEXT_FLOAT(ARGS);
        auto rate=GET_NEXT_INT(ARGS), channels=GET_NEXT_INT(ARGS);
        if(w>=1 && w<=8192 && h>=1 && h<=8192 && fps>=1 && fps<=120 &&
           x>=0 && x<=8192 && y>=0 && y<=8192 && lw>=1 && lw<=8192 && lh>=1 && lh<=8192 &&
           rate>=8000 && rate<=96000 && channels>=1 && channels<=2 &&
           overlayVideo(base,layer,dest,w,h,fps,x,y,lw,lh,opacity,rate,channels)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_composite) {
    RETURN->v_int=0;
    try {
        auto base=GET_NEXT_STRING_SAFE(ARGS), layer=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        auto x=GET_NEXT_INT(ARGS), y=GET_NEXT_INT(ARGS), lw=GET_NEXT_INT(ARGS), lh=GET_NEXT_INT(ARGS);
        auto opacity=GET_NEXT_FLOAT(ARGS);
        auto rate=GET_NEXT_INT(ARGS), channels=GET_NEXT_INT(ARGS);
        auto key=GET_NEXT_INT(ARGS);
        auto tolerance=GET_NEXT_FLOAT(ARGS), softness=GET_NEXT_FLOAT(ARGS);
        auto baseGain=GET_NEXT_FLOAT(ARGS), layerGain=GET_NEXT_FLOAT(ARGS);
        if(key>=-1 && key<=0xffffff && w>=1 && w<=8192 && h>=1 && h<=8192 && fps>=1 && fps<=120 &&
           x>=0 && x<=8192 && y>=0 && y<=8192 && lw>=1 && lw<=8192 && lh>=1 && lh<=8192 &&
           rate>=8000 && rate<=96000 && channels>=1 && channels<=2 &&
           overlayVideo(base,layer,dest,w,h,fps,x,y,lw,lh,opacity,rate,channels,static_cast<int>(key),tolerance,softness,baseGain,layerGain)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_watermark) {
    RETURN->v_int=0;
    try {
        auto base=GET_NEXT_STRING_SAFE(ARGS), layer=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        auto x=GET_NEXT_INT(ARGS), y=GET_NEXT_INT(ARGS), lw=GET_NEXT_INT(ARGS), lh=GET_NEXT_INT(ARGS);
        auto opacity=GET_NEXT_FLOAT(ARGS);
        auto rate=GET_NEXT_INT(ARGS), channels=GET_NEXT_INT(ARGS);
        if(w>=1 && w<=8192 && h>=1 && h<=8192 && fps>=1 && fps<=120 &&
           x>=0 && x<=8192 && y>=0 && y<=8192 && lw>=1 && lw<=8192 && lh>=1 && lh<=8192 &&
           rate>=8000 && rate<=96000 && channels>=1 && channels<=2 &&
           overlayVideo(base,layer,dest,w,h,fps,x,y,lw,lh,opacity,rate,channels,-1,0,0,1,0,true)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_multilayer) {
    RETURN->v_int=0;
    try {
        auto base=GET_NEXT_STRING_SAFE(ARGS), manifest=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        auto rate=GET_NEXT_INT(ARGS), channels=GET_NEXT_INT(ARGS);
        std::vector<std::unique_ptr<CompositeLayer>> layers;
        if(w>=1 && w<=8192 && h>=1 && h<=8192 && fps>=1 && fps<=120 &&
           rate>=8000 && rate<=96000 && channels>=1 && channels<=2 &&
           readCompositeLayers(manifest,layers) &&
           multilayerVideo(base,dest,w,h,fps,rate,channels,layers)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_title) {
    RETURN->v_int=0;
    try {
        auto source=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        auto content=GET_NEXT_STRING_SAFE(ARGS), font=GET_NEXT_STRING_SAFE(ARGS);
        auto size=GET_NEXT_INT(ARGS), rgb=GET_NEXT_INT(ARGS);
        auto x=GET_NEXT_INT(ARGS), y=GET_NEXT_INT(ARGS);
        auto begin=GET_NEXT_FLOAT(ARGS), end=GET_NEXT_FLOAT(ARGS);
        if(size<1 || size>512 || rgb<0 || rgb>0xffffff || x<0 || y<0 ||
           x>8191 || y>8191 || !std::isfinite(begin) || !std::isfinite(end) ||
           begin<0 || end<=begin) return;
        Input in;
        if(!in.open(source)) return;
        auto *v=in.ctx->streams[in.video];
        int w=v->codecpar->width,h=v->codecpar->height;
        AVRational guessed=av_guess_frame_rate(in.ctx,v,nullptr);
        double raw=av_q2d(guessed);
        if(w<1 || w>8192 || h<1 || h>8192 || x>=w || y>=h ||
           !std::isfinite(raw) || raw<1 || raw>120) return;
        int fps=static_cast<int>(std::llround(raw));
        int rate=48000,channels=2;
        for(unsigned i=0;i<in.ctx->nb_streams;++i) {
            auto *p=in.ctx->streams[i]->codecpar;
            if(p->codec_type==AVMEDIA_TYPE_AUDIO) {
                if(p->sample_rate>=8000 && p->sample_rate<=96000) rate=p->sample_rate;
                if(p->ch_layout.nb_channels==1) channels=1;
                break;
            }
        }
        std::vector<std::unique_ptr<CompositeLayer>> layers;
        auto l=std::unique_ptr<CompositeLayer>(new CompositeLayer);
        l->still=l->title=true; l->path=content; l->font=font;
        l->fontSize=static_cast<int>(size); l->rgb=static_cast<int>(rgb);
        l->x=static_cast<int>(x); l->y=static_cast<int>(y);
        l->w=w-l->x; l->h=h-l->y; l->begin=begin; l->end=end;
        layers.push_back(std::move(l));
        if(multilayerVideo(source,dest,w,h,fps,rate,channels,layers)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_SFUN(video_burn_subtitles) {
    RETURN->v_int=0;
    try {
        auto source=GET_NEXT_STRING_SAFE(ARGS), captions=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        auto font=GET_NEXT_STRING_SAFE(ARGS);
        auto size=GET_NEXT_INT(ARGS), rgb=GET_NEXT_INT(ARGS);
        auto margin=GET_NEXT_INT(ARGS);
        if(size<1 || size>512 || rgb<0 || rgb>0xffffff || margin<0 || margin>8191) return;
        Input in;
        if(!in.open(source)) return;
        auto *v=in.ctx->streams[in.video];
        int w=v->codecpar->width,h=v->codecpar->height;
        AVRational guessed=av_guess_frame_rate(in.ctx,v,nullptr);
        double raw=av_q2d(guessed);
        if(w<1 || w>8192 || h<1 || h>8192 || margin>=h ||
           !std::isfinite(raw) || raw<1 || raw>120) return;
        int fps=static_cast<int>(std::llround(raw));
        int rate=48000,channels=2;
        for(unsigned i=0;i<in.ctx->nb_streams;++i) {
            auto *p=in.ctx->streams[i]->codecpar;
            if(p->codec_type==AVMEDIA_TYPE_AUDIO) {
                if(p->sample_rate>=8000 && p->sample_rate<=96000) rate=p->sample_rate;
                if(p->ch_layout.nb_channels==1) channels=1;
                break;
            }
        }
        std::vector<std::unique_ptr<CompositeLayer>> layers;
        auto l=std::unique_ptr<CompositeLayer>(new CompositeLayer);
        l->still=l->captions=true; l->font=font;
        if(!subtitles::load(captions,l->cues)) return;
        l->fontSize=static_cast<int>(size); l->rgb=static_cast<int>(rgb);
        l->w=w; l->h=h-static_cast<int>(margin);
        layers.push_back(std::move(l));
        if(multilayerVideo(source,dest,w,h,fps,rate,channels,layers)) RETURN->v_int=1;
    } catch(...) {}
}

CK_DLL_SFUN(video_animate_overlay) {
    RETURN->v_int=0;
    try {
        auto source=GET_NEXT_STRING_SAFE(ARGS), image=GET_NEXT_STRING_SAFE(ARGS);
        auto manifest=GET_NEXT_STRING_SAFE(ARGS), dest=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS), h=GET_NEXT_INT(ARGS), fps=GET_NEXT_INT(ARGS);
        if(w<1 || w>8192 || h<1 || h>8192 || fps<1 || fps>120) return;
        Input in,mark;
        if(!in.open(source) || !mark.open(image)) return;
        auto *p=mark.ctx->streams[mark.video]->codecpar;
        if(p->width<1 || p->width>8192 || p->height<1 || p->height>8192) return;
        int rate=48000,channels=2;
        for(unsigned i=0;i<in.ctx->nb_streams;++i) {
            auto *a=in.ctx->streams[i]->codecpar;
            if(a->codec_type==AVMEDIA_TYPE_AUDIO) {
                if(a->sample_rate>=8000 && a->sample_rate<=96000) rate=a->sample_rate;
                if(a->ch_layout.nb_channels==1) channels=1;
                break;
            }
        }
        std::vector<std::unique_ptr<CompositeLayer>> layers;
        auto l=std::unique_ptr<CompositeLayer>(new CompositeLayer);
        l->still=true; l->path=image; l->w=p->width; l->h=p->height;
        l->animation=std::make_unique<OverlayAnimation>();
        if(!l->animation->load(manifest)) return;
        layers.push_back(std::move(l));
        if(multilayerVideo(source,dest,static_cast<int>(w),static_cast<int>(h),
                           static_cast<int>(fps),rate,channels,layers)) RETURN->v_int=1;
    } catch(...) {}
}


namespace {
struct VideoClipState {
    std::unique_ptr<OverlayReader> reader;
    int64_t index=0;
    std::array<double,4> controls{{1,1,1,0}};
    void reset() { controls={{1,1,1,0}}; }
    bool open(const std::string &path) {
        auto next=std::make_unique<OverlayReader>();
        if(!next->open(path)) return false;
        reader=std::move(next); index=0; reset();
        return true;
    }
    int advance() {
        if(!reader) return -1;
        if(reader->eof) return 0;
        // Read ahead before committing the new current frame.
        Frame future;
        if(!future.p || !reader->read(future.p)) {
            // Decoder state after an error cannot be retried safely.
            reader.reset(); index=0; return -1;
        }
        std::swap(reader->current.p,reader->next.p);
        std::swap(reader->next.p,future.p);
        ++index;
        return 1;
    }
    bool save(const std::string &path,int w,int h) {
        if(!reader || w<1 || h<1 || w>8192 || h>8192 ||
           static_cast<int64_t>(w)*h>16777216) return false;
        Frame image;
        if(!image.p) return false;
        image.p->width=w; image.p->height=h; image.p->format=AV_PIX_FMT_BGR0;
        if(av_frame_get_buffer(image.p,32)<0 || !reader->render(image.p)) return false;
        for(int y=0;y<h;++y) {
            auto *row=image.p->data[0]+static_cast<size_t>(y)*image.p->linesize[0];
            for(int x=0;x<w;++x) for(int c=0;c<3;++c) {
                double v=row[x*4+c]*controls[2-c]+255*controls[3];
                row[x*4+c]=static_cast<uint8_t>(std::lround(std::max(0.0,std::min(255.0,v))));
            }
        }
        return writeFramePPM(image.p,path);
    }
};
t_CKUINT videoClipOffset=0;
VideoClipState *clipState(Chuck_Object *self,CK_DL_API API) {
    return reinterpret_cast<VideoClipState *>(static_cast<uintptr_t>(OBJ_MEMBER_UINT(self,videoClipOffset)));
}
double clipNaN() { return std::numeric_limits<double>::quiet_NaN(); }
double clipGet(VideoClipState *p,size_t i) { return p?p->controls[i]:clipNaN(); }
double clipSet(VideoClipState *p,size_t i,double v) {
    if(!p || !std::isfinite(v) || (i<3 && (v<0 || v>4)) ||
       (i==3 && (v< -1 || v>1))) return clipNaN();
    return p->controls[i]=v;
}
}
CK_DLL_CTOR(video_clip_ctor) {
    static_assert(sizeof(t_CKUINT)>=sizeof(uintptr_t),"ChucK integer must hold native pointer");
    OBJ_MEMBER_UINT(SELF,videoClipOffset)=0;
    OBJ_MEMBER_UINT(SELF,videoClipOffset)=reinterpret_cast<uintptr_t>(new(std::nothrow) VideoClipState);
}
CK_DLL_DTOR(video_clip_dtor) {
    delete clipState(SELF,API);
    OBJ_MEMBER_UINT(SELF,videoClipOffset)=0;
}
CK_DLL_MFUN(video_clip_load) {
    RETURN->v_int=0;
    try {
        auto *p=clipState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->open(path)) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(video_clip_loaded) {
    auto *p=clipState(SELF,API);
    RETURN->v_int=p && p->reader;
}
CK_DLL_MFUN(video_clip_next) {
    RETURN->v_int=-1;
    try { if(auto *p=clipState(SELF,API)) RETURN->v_int=p->advance(); } catch(...) {}
}
CK_DLL_MFUN(video_clip_index) {
    auto *p=clipState(SELF,API);
    RETURN->v_int=p && p->reader?p->index:-1;
}
CK_DLL_MFUN(video_clip_time) {
    auto *p=clipState(SELF,API);
    RETURN->v_float=-1;
    if(p && p->reader) {
        auto &r=*p->reader;
        RETURN->v_float=(r.current.p->best_effort_timestamp-r.origin)*av_q2d(r.stream->time_base);
    }
}
CK_DLL_MFUN(video_clip_width) {
    auto *p=clipState(SELF,API);
    RETURN->v_int=p && p->reader?p->reader->current.p->width:0;
}
CK_DLL_MFUN(video_clip_height) {
    auto *p=clipState(SELF,API);
    RETURN->v_int=p && p->reader?p->reader->current.p->height:0;
}
CK_DLL_MFUN(video_clip_eof) {
    auto *p=clipState(SELF,API);
    RETURN->v_int=p && p->reader && p->reader->eof;
}
CK_DLL_MFUN(video_clip_save) {
    RETURN->v_int=0;
    try {
        auto *p=clipState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        if(p && p->reader && p->save(path,p->reader->current.p->width,p->reader->current.p->height))
            RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(video_clip_save_scaled) {
    RETURN->v_int=0;
    try {
        auto *p=clipState(SELF,API);
        auto path=GET_NEXT_STRING_SAFE(ARGS);
        auto w=GET_NEXT_INT(ARGS),h=GET_NEXT_INT(ARGS);
        if(p && w>=1 && w<=8192 && h>=1 && h<=8192 &&
           p->save(path,static_cast<int>(w),static_cast<int>(h))) RETURN->v_int=1;
    } catch(...) {}
}
CK_DLL_MFUN(video_clip_reset) {
    if(auto *p=clipState(SELF,API)) p->reset();
}
CK_DLL_MFUN(video_clip_clear) {
    if(auto *p=clipState(SELF,API)) { p->reader.reset(); p->index=0; p->reset(); }
}
CK_DLL_MFUN(video_clip_get_red) { RETURN->v_float=clipGet(clipState(SELF,API),0); }
CK_DLL_MFUN(video_clip_set_red) { RETURN->v_float=clipSet(clipState(SELF,API),0,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(video_clip_get_green) { RETURN->v_float=clipGet(clipState(SELF,API),1); }
CK_DLL_MFUN(video_clip_set_green) { RETURN->v_float=clipSet(clipState(SELF,API),1,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(video_clip_get_blue) { RETURN->v_float=clipGet(clipState(SELF,API),2); }
CK_DLL_MFUN(video_clip_set_blue) { RETURN->v_float=clipSet(clipState(SELF,API),2,GET_NEXT_FLOAT(ARGS)); }
CK_DLL_MFUN(video_clip_get_brightness) { RETURN->v_float=clipGet(clipState(SELF,API),3); }
CK_DLL_MFUN(video_clip_set_brightness) { RETURN->v_float=clipSet(clipState(SELF,API),3,GET_NEXT_FLOAT(ARGS)); }

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
    QUERY->add_sfun(QUERY, video_audio_tracks, "int", "audioTracks");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_sfun(QUERY, video_extract_audio, "int", "extractAudio");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "track");
    QUERY->add_sfun(QUERY, video_convert_audio, "int", "convertAudio");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "track");
    QUERY->add_sfun(QUERY, video_transcode_av, "int", "transcodeAV");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "width");
    QUERY->add_arg(QUERY, "int", "height");
    QUERY->add_arg(QUERY, "string", "codec");
    QUERY->add_arg(QUERY, "int", "quality");
    QUERY->add_arg(QUERY, "int", "audioKbps");
    QUERY->add_sfun(QUERY, video_trim, "int", "trim");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "float", "start");
    QUERY->add_arg(QUERY, "float", "end");
    QUERY->add_sfun(QUERY, video_concat, "int", "concat");
    QUERY->add_arg(QUERY, "string", "first");
    QUERY->add_arg(QUERY, "string", "secondClip");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_sfun(QUERY, video_trim_frames, "int", "trimFrames");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "firstFrame");
    QUERY->add_arg(QUERY, "int", "frameCount");
    QUERY->add_sfun(QUERY, video_trim_av, "int", "trimAV");
    QUERY->add_arg(QUERY, "string", "source");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "firstFrame");
    QUERY->add_arg(QUERY, "int", "frameCount");
    QUERY->add_sfun(QUERY, video_concat_normalized, "int", "concatNormalized");
    QUERY->add_arg(QUERY, "string", "firstClip");
    QUERY->add_arg(QUERY, "string", "secondClip");
    QUERY->add_arg(QUERY, "string", "destination");
    QUERY->add_arg(QUERY, "int", "width");
    QUERY->add_arg(QUERY, "int", "height");
    QUERY->add_arg(QUERY, "int", "fps");
    QUERY->add_sfun(QUERY,video_concat_av,"int","concatAV");
    QUERY->add_arg(QUERY,"string","firstClip");
    QUERY->add_arg(QUERY,"string","secondClip");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_sfun(QUERY,video_crossfade,"int","crossfade");
    QUERY->add_arg(QUERY,"string","firstClip");
    QUERY->add_arg(QUERY,"string","secondClip");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"float","fadeSeconds");
    QUERY->add_sfun(QUERY,video_transition,"int","transition");
    QUERY->add_arg(QUERY,"string","firstClip");
    QUERY->add_arg(QUERY,"string","secondClip");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"float","fadeSeconds");
    QUERY->add_arg(QUERY,"string","visualMode");
    QUERY->add_arg(QUERY,"string","audioCurve");
    QUERY->add_sfun(QUERY,video_overlay,"int","overlay");
    QUERY->add_arg(QUERY,"string","baseClip");
    QUERY->add_arg(QUERY,"string","layerClip");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","layerWidth");
    QUERY->add_arg(QUERY,"int","layerHeight");
    QUERY->add_arg(QUERY,"float","opacity");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_sfun(QUERY,video_composite,"int","composite");
    QUERY->add_arg(QUERY,"string","baseClip");
    QUERY->add_arg(QUERY,"string","layerClip");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","layerWidth");
    QUERY->add_arg(QUERY,"int","layerHeight");
    QUERY->add_arg(QUERY,"float","opacity");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_arg(QUERY,"int","keyRGB");
    QUERY->add_arg(QUERY,"float","tolerance");
    QUERY->add_arg(QUERY,"float","softness");
    QUERY->add_arg(QUERY,"float","baseGain");
    QUERY->add_arg(QUERY,"float","layerGain");
    QUERY->add_sfun(QUERY,video_watermark,"int","watermark");
    QUERY->add_arg(QUERY,"string","baseClip");
    QUERY->add_arg(QUERY,"string","imageFile");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"int","layerWidth");
    QUERY->add_arg(QUERY,"int","layerHeight");
    QUERY->add_arg(QUERY,"float","opacity");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_sfun(QUERY,video_multilayer,"int","multilayer");
    QUERY->add_arg(QUERY,"string","baseClip");
    QUERY->add_arg(QUERY,"string","manifest");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    QUERY->add_arg(QUERY,"int","sampleRate");
    QUERY->add_arg(QUERY,"int","channels");
    QUERY->add_sfun(QUERY,video_title,"int","title");
    QUERY->add_arg(QUERY,"string","source");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"string","content");
    QUERY->add_arg(QUERY,"string","font");
    QUERY->add_arg(QUERY,"int","fontSize");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"int","x");
    QUERY->add_arg(QUERY,"int","y");
    QUERY->add_arg(QUERY,"float","beginSeconds");
    QUERY->add_arg(QUERY,"float","endSeconds");
    QUERY->add_sfun(QUERY,video_burn_subtitles,"int","burnSubtitles");
    QUERY->add_arg(QUERY,"string","source");
    QUERY->add_arg(QUERY,"string","captions");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"string","font");
    QUERY->add_arg(QUERY,"int","fontSize");
    QUERY->add_arg(QUERY,"int","rgb");
    QUERY->add_arg(QUERY,"int","bottomMargin");
    QUERY->add_sfun(QUERY,video_animate_overlay,"int","animateOverlay");
    QUERY->add_arg(QUERY,"string","source");
    QUERY->add_arg(QUERY,"string","imageFile");
    QUERY->add_arg(QUERY,"string","animationFile");
    QUERY->add_arg(QUERY,"string","destination");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_arg(QUERY,"int","fps");
    if(!QUERY->end_class(QUERY)) return FALSE;
    QUERY->begin_class(QUERY,"VideoClip","Object");
    videoClipOffset=QUERY->add_mvar(QUERY,"int","@videoClipNative",FALSE);
    if(videoClipOffset==CK_INVALID_OFFSET) return FALSE;
    QUERY->add_ctor(QUERY,video_clip_ctor);
    QUERY->add_dtor(QUERY,video_clip_dtor);
    QUERY->add_mfun(QUERY,video_clip_load,"int","load");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,video_clip_loaded,"int","loaded");
    QUERY->add_mfun(QUERY,video_clip_next,"int","next");
    QUERY->add_mfun(QUERY,video_clip_index,"int","frameIndex");
    QUERY->add_mfun(QUERY,video_clip_time,"float","timeSeconds");
    QUERY->add_mfun(QUERY,video_clip_width,"int","width");
    QUERY->add_mfun(QUERY,video_clip_height,"int","height");
    QUERY->add_mfun(QUERY,video_clip_eof,"int","eof");
    QUERY->add_mfun(QUERY,video_clip_save,"int","saveFrame");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_mfun(QUERY,video_clip_save_scaled,"int","saveFrame");
    QUERY->add_arg(QUERY,"string","path");
    QUERY->add_arg(QUERY,"int","width");
    QUERY->add_arg(QUERY,"int","height");
    QUERY->add_mfun(QUERY,video_clip_reset,"void","reset");
    QUERY->add_mfun(QUERY,video_clip_clear,"void","clear");
    QUERY->add_mfun(QUERY,video_clip_get_red,"float","red");
    QUERY->add_mfun(QUERY,video_clip_set_red,"float","red");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,video_clip_get_green,"float","green");
    QUERY->add_mfun(QUERY,video_clip_set_green,"float","green");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,video_clip_get_blue,"float","blue");
    QUERY->add_mfun(QUERY,video_clip_set_blue,"float","blue");
    QUERY->add_arg(QUERY,"float","value");
    QUERY->add_mfun(QUERY,video_clip_get_brightness,"float","brightness");
    QUERY->add_mfun(QUERY,video_clip_set_brightness,"float","brightness");
    QUERY->add_arg(QUERY,"float","value");
    return QUERY->end_class(QUERY);
}
