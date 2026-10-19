#include "decoder.h"

#include <QDebug>
#include <cmath>
#include <map>

extern "C" {
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace {

QString avErrorString(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// Clockwise rotation from the stream's display matrix (set by cameras that
// were mounted upside down or recorded in portrait).
int streamRotation(const AVStream* stream) {
    const uint8_t* matrix = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 29, 100)
    const AVPacketSideData* sd = av_packet_side_data_get(
        stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX);
    if (sd) matrix = sd->data;
#else
    matrix = av_stream_get_side_data(stream, AV_PKT_DATA_DISPLAYMATRIX, nullptr);
#endif
    if (!matrix) return 0;

    double theta = -av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix));
    if (std::isnan(theta)) return 0;
    int degrees = static_cast<int>(std::lround(theta / 90.0)) * 90 % 360;
    return degrees < 0 ? degrees + 360 : degrees;
}

// Hardware devices are shared by all decoders (one per comparison tile) and
// kept for the lifetime of the app: creating one takes ~100 ms (e.g. a CUDA
// context), which would make every clip switch and new tile sluggish.
// Failures are remembered too, so a missing driver is only probed once.
AVBufferRef* sharedHwDevice(AVHWDeviceType type) {
    static QMutex mutex;
    static std::map<AVHWDeviceType, AVBufferRef*> devices;
    QMutexLocker locker(&mutex);
    auto it = devices.find(type);
    if (it == devices.end()) {
        AVBufferRef* device = nullptr;
        if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) device = nullptr;
        it = devices.emplace(type, device).first;
    }
    return it->second ? av_buffer_ref(it->second) : nullptr;
}

} // namespace

Decoder::Decoder(QObject* parent) : QThread(parent) {
}

Decoder::~Decoder() {
    close();
}

void Decoder::setQueue(FrameQueue* queue) {
    m_queue = queue;
}

double Decoder::probeDuration(const QString& filename) {
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, filename.toUtf8().constData(), nullptr, nullptr) < 0) return -1.0;
    // MP4/MOV carry the duration in the header; other containers need a look at the streams.
    if (ctx->duration == AV_NOPTS_VALUE) avformat_find_stream_info(ctx, nullptr);
    const double duration = ctx->duration != AV_NOPTS_VALUE
        ? ctx->duration / static_cast<double>(AV_TIME_BASE) : -1.0;
    avformat_close_input(&ctx);
    return duration;
}

bool Decoder::open(const QString& filename, double partStart, double partEnd) {
    close();
    if (m_queue) m_queue->reopen();

    m_stop = false;
    m_seekPending = false;
    m_info = VideoInfo();
    m_lastPts = 0.0;

    int ret = avformat_open_input(&m_fmtCtx, filename.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        emit errorOccurred(QString("Failed to open file: %1").arg(avErrorString(ret)));
        return false;
    }

    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) {
        emit errorOccurred(QString("Failed to read stream info: %1").arg(avErrorString(ret)));
        close();
        return false;
    }

    const AVCodec* codec = nullptr;
    m_videoStream = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (m_videoStream < 0 || !codec) {
        emit errorOccurred(m_videoStream == AVERROR_DECODER_NOT_FOUND
                               ? "No decoder available for the video codec"
                               : "No video stream found");
        close();
        return false;
    }

    // Skip audio, timecode and telemetry (e.g. GoPro GPMF) packets entirely.
    for (unsigned int i = 0; i < m_fmtCtx->nb_streams; i++) {
        if (static_cast<int>(i) != m_videoStream)
            m_fmtCtx->streams[i]->discard = AVDISCARD_ALL;
    }

    AVStream* stream = m_fmtCtx->streams[m_videoStream];
    m_timeBase = av_q2d(stream->time_base);

    if (stream->start_time != AV_NOPTS_VALUE)
        m_startTime = stream->start_time * m_timeBase;
    else if (m_fmtCtx->start_time != AV_NOPTS_VALUE)
        m_startTime = m_fmtCtx->start_time / static_cast<double>(AV_TIME_BASE);
    else
        m_startTime = 0.0;

    if (stream->duration != AV_NOPTS_VALUE)
        m_info.duration = stream->duration * m_timeBase;
    else if (m_fmtCtx->duration != AV_NOPTS_VALUE)
        m_info.duration = m_fmtCtx->duration / static_cast<double>(AV_TIME_BASE);

    // A part of the file: shift the timeline so the part starts at 0, and
    // begin decoding there.
    const double fileDuration = m_info.duration;
    m_partStart = std::max(0.0, partStart);
    if (fileDuration > 0.0) m_partStart = std::min(m_partStart, fileDuration);
    m_partEnds = partEnd >= 0.0 && (fileDuration <= 0.0 || partEnd < fileDuration);
    if (m_partEnds) m_info.duration = std::max(0.0, partEnd - m_partStart);
    else if (fileDuration > 0.0) m_info.duration = fileDuration - m_partStart;
    m_startTime += m_partStart;
    if (m_partStart > 0.0) {
        m_seekTarget  = 0.0;
        m_seekSerial  = 0;
        m_seekPending = true;
    }

    m_info.fps = av_q2d(stream->avg_frame_rate);
    if (!(m_info.fps > 0.0)) m_info.fps = av_q2d(stream->r_frame_rate);
    if (!(m_info.fps > 0.0)) m_info.fps = 30.0;

    m_info.codec    = QString::fromUtf8(codec->name);
    m_info.width    = stream->codecpar->width;
    m_info.height   = stream->codecpar->height;
    m_info.rotation = streamRotation(stream);

    initHWContext(codec); // falls back to software decoding on failure

    if (!openCodec(codec)) {
        emit errorOccurred("Failed to open codec");
        close();
        return false;
    }

    start();
    return true;
}

bool Decoder::initHWContext(const AVCodec* codec) {
    // REPLAYER_HWDEC=none disables hardware decoding; any other value
    // (e.g. "vaapi", "cuda", "d3d11va", "videotoolbox") selects that device.
    const QString requested = qEnvironmentVariable("REPLAYER_HWDEC").toLower();
    if (requested == "none" || requested == "no") return false;

    auto usable = [&](const AVCodecHWConfig* config) {
        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) return false;
        if (!requested.isEmpty() && requested != "auto")
            return requested == QString::fromUtf8(av_hwdevice_get_type_name(config->device_type));
        // Vulkan decoding is still less mature than the native APIs
        return config->device_type != AV_HWDEVICE_TYPE_VULKAN;
    };
    auto use = [&](const AVCodecHWConfig* config) {
        m_hwPixFmt = config->pix_fmt;
        m_info.hwAccel = QString::fromUtf8(av_hwdevice_get_type_name(config->device_type));
        return true;
    };

    for (int i = 0; const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i); i++) {
        if (!usable(config)) continue;
        m_hwDeviceCtx = sharedHwDevice(config->device_type);
        if (m_hwDeviceCtx) return use(config);
    }
    return false;
}

bool Decoder::openCodec(const AVCodec* codec) {
    AVStream* stream = m_fmtCtx->streams[m_videoStream];

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) return false;

    if (avcodec_parameters_to_context(m_codecCtx, stream->codecpar) < 0) return false;
    m_codecCtx->pkt_timebase = stream->time_base;

    if (m_hwPixFmt != AV_PIX_FMT_NONE) {
        m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_codecCtx->opaque        = this;
        m_codecCtx->get_format    = &Decoder::selectPixelFormat;
    } else {
        // Software decoding of 4K HEVC needs every core it can get.
        m_codecCtx->thread_count = 0;
        m_codecCtx->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    int ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0 && m_hwPixFmt != AV_PIX_FMT_NONE) {
        qWarning() << "Hardware decoder failed to open, using software:" << avErrorString(ret);
        avcodec_free_context(&m_codecCtx);
        m_hwPixFmt = AV_PIX_FMT_NONE;
        m_info.hwAccel.clear();
        return openCodec(codec);
    }
    return ret >= 0;
}

AVPixelFormat Decoder::selectPixelFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
    const auto* self = static_cast<const Decoder*>(ctx->opaque);
    for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == self->m_hwPixFmt) return *p;
    }
    // The hardware can't handle this stream (e.g. an unsupported profile):
    // fall back to the first software format.
    for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; p++) {
        if (!(av_pix_fmt_desc_get(*p)->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
    }
    return AV_PIX_FMT_NONE;
}

void Decoder::close() {
    {
        QMutexLocker locker(&m_mutex);
        m_stop = true;
        m_wake.wakeAll();
    }
    if (m_queue) m_queue->close(); // unblock any blocked push()
    wait();

    avcodec_free_context(&m_codecCtx);
    avformat_close_input(&m_fmtCtx);
    av_buffer_unref(&m_hwDeviceCtx);
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_videoStream = -1;
}

void Decoder::seek(double timestamp, int serial) {
    QMutexLocker locker(&m_mutex);
    m_seekTarget  = timestamp;
    m_seekSerial  = serial;
    m_seekPending = true;
    m_wake.wakeAll();
}

void Decoder::run() {
    AVPacket* packet = av_packet_alloc();
    AVFrame*  frame  = av_frame_alloc();
    AVFrame*  held   = av_frame_alloc(); // last frame skipped during a seek

    int    serial    = 0;
    double skipUntil = -1.0;  // accurate seek: frames before this pts are not queued
    double heldPts   = 0.0;
    bool   draining  = false; // demuxer reached EOF, decoder is being flushed
    bool   finished  = false; // every frame of this serial has been queued
    const double halfFrame = 0.5 / m_info.fps;

    double seekTarget  = 0.0;
    double seekPos     = 0.0;   // where the demuxer was last asked to go
    double seekBackoff = 1.0;
    bool   byteSeeked  = false;
    bool   awaitKey    = false; // drop packets until a keyframe after a seek

    auto seekDemuxer = [&](double pos) {
        seekPos = pos;
        const auto ts = static_cast<int64_t>(std::llround((pos + m_startTime) / m_timeBase));
        av_seek_frame(m_fmtCtx, m_videoStream, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_codecCtx);
        awaitKey = true;
    };

    // Formats without an index (e.g. MPEG-TS) can land after the keyframe
    // that precedes the target. Retry from further back; returns false when
    // there is nowhere earlier to go.
    auto seekEarlier = [&]() -> bool {
        if (seekPos > -m_partStart) {
            seekDemuxer(std::max(-m_partStart, seekTarget - seekBackoff));
            seekBackoff *= 2.0;
            return true;
        }
        if (!byteSeeked && !(m_fmtCtx->iformat->flags & AVFMT_NO_BYTE_SEEK)) {
            byteSeeked = true;
            av_seek_frame(m_fmtCtx, -1, 0, AVSEEK_FLAG_BYTE);
            avcodec_flush_buffers(m_codecCtx);
            awaitKey = true;
            return true;
        }
        return false;
    };

    auto queueFrame = [&](AVFrame* f, double pts) {
        FramePtr out = toDisplayFrame(f);
        if (out && m_queue && !m_queue->push({std::move(out), pts, serial}))
            m_stop = true;
    };

    while (packet && frame && held && !m_stop) {
        if (m_seekPending) {
            {
                QMutexLocker locker(&m_mutex);
                seekTarget = m_seekTarget;
                serial = m_seekSerial;
                m_seekPending = false;
            }
            seekBackoff = 1.0;
            byteSeeked = false;
            seekDemuxer(seekTarget);
            av_frame_unref(held);
            skipUntil = seekTarget - halfFrame;
            draining = finished = false;
        }

        if (finished) {
            // Nothing left to decode; sleep until a seek or shutdown.
            QMutexLocker locker(&m_mutex);
            while (!m_stop && !m_seekPending) m_wake.wait(&m_mutex);
            continue;
        }

        if (!draining) {
            int ret = av_read_frame(m_fmtCtx, packet);
            if (ret < 0) {
                if (awaitKey && seekEarlier()) continue; // no keyframe before EOF
                // EOF (or a read error in a truncated file): flush out the
                // frames still buffered inside the decoder.
                awaitKey = false;
                draining = true;
                avcodec_send_packet(m_codecCtx, nullptr);
            } else {
                if (packet->stream_index != m_videoStream) {
                    av_packet_unref(packet);
                    continue;
                }
                if (awaitKey) {
                    // Start decoding cleanly at a keyframe, and only if it is
                    // not already past the seek target.
                    if (!(packet->flags & AV_PKT_FLAG_KEY)) {
                        av_packet_unref(packet);
                        continue;
                    }
                    awaitKey = false;
                    const int64_t keyTs = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
                    if (keyTs != AV_NOPTS_VALUE
                        && keyTs * m_timeBase - m_startTime > seekTarget + halfFrame
                        && seekEarlier()) {
                        av_packet_unref(packet);
                        continue;
                    }
                }
                avcodec_send_packet(m_codecCtx, packet);
                av_packet_unref(packet);
            }
        }

        while (!m_stop && !m_seekPending) {
            int ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN)) break;
            if (ret < 0) {
                if (draining) {
                    // Seeking past the last frame still shows the last frame.
                    if (skipUntil >= 0.0 && held->buf[0]) queueFrame(held, heldPts);
                    finished = true;
                    if (m_queue) m_queue->setEndOfStream(serial);
                }
                break;
            }

            const double pts = framePts(frame);
            if (m_partEnds && pts >= m_info.duration - halfFrame) {
                // The end of the part: as at the end of the file.
                av_frame_unref(frame);
                if (skipUntil >= 0.0 && held->buf[0]) queueFrame(held, heldPts);
                finished = true;
                if (m_queue) m_queue->setEndOfStream(serial);
                break;
            }
            if (pts < skipUntil) {
                av_frame_unref(held);
                av_frame_move_ref(held, frame);
                heldPts = pts;
                continue;
            }
            skipUntil = -1.0;
            av_frame_unref(held);
            queueFrame(frame, pts);
        }
    }

    av_frame_free(&held);
    av_frame_free(&frame);
    av_packet_free(&packet);
}

double Decoder::framePts(const AVFrame* frame) {
    int64_t ts = frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
    m_lastPts = ts != AV_NOPTS_VALUE ? ts * m_timeBase - m_startTime
                                     : m_lastPts + 1.0 / m_info.fps;
    return m_lastPts;
}

// Returns a system-memory frame VideoRenderer can upload, consuming frame.
FramePtr Decoder::toDisplayFrame(AVFrame* frame) {
    AVFrame* out = av_frame_alloc();
    if (!out) {
        av_frame_unref(frame);
        return {};
    }

    if (frame->hw_frames_ctx) {
        // Download from GPU memory; produces NV12 or P010.
        int ret = av_hwframe_transfer_data(out, frame, 0);
        if (ret >= 0) ret = av_frame_copy_props(out, frame);
        av_frame_unref(frame);
        if (ret < 0) {
            av_frame_free(&out);
            return {};
        }
    } else {
        av_frame_move_ref(out, frame);
    }

    if (isRenderablePixelFormat(out->format)) return makeFramePtr(out);

    // Uncommon pixel format: convert to 8-bit 4:2:0 once here.
    const auto srcFormat = static_cast<AVPixelFormat>(out->format);
    m_swsCtx = sws_getCachedContext(m_swsCtx, out->width, out->height, srcFormat,
                                    out->width, out->height, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
    AVFrame* conv = av_frame_alloc();
    if (!m_swsCtx || !conv) {
        av_frame_free(&conv);
        av_frame_free(&out);
        return {};
    }
    conv->format = AV_PIX_FMT_YUV420P;
    conv->width  = out->width;
    conv->height = out->height;
    if (av_frame_get_buffer(conv, 0) < 0) {
        av_frame_free(&conv);
        av_frame_free(&out);
        return {};
    }
    sws_scale(m_swsCtx, out->data, out->linesize, 0, out->height, conv->data, conv->linesize);
    av_frame_copy_props(conv, out);
    conv->color_range = AVCOL_RANGE_MPEG;
    if (av_pix_fmt_desc_get(srcFormat)->flags & AV_PIX_FMT_FLAG_RGB)
        conv->colorspace = AVCOL_SPC_SMPTE170M;
    av_frame_free(&out);
    return makeFramePtr(conv);
}
