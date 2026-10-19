#include "clipexporter.h"
#include "clipref.h"
#include "decoder.h"
#include "framequeue.h"
#include "timeformat.h"

#include <QFile>
#include <QPainter>
#include <QThread>
#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace {

QString avErrorString(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// Speeds like 0.25 read better as fractions: "1/4×"
QString speedText(double speed) {
    if (speed >= 1.0) return QString("%1×").arg(speed);
    return QString("1/%1×").arg(std::lround(1.0 / speed));
}

void paintTimeStamp(QPainter& p, const QSize& size, const QString& text) {
    // Scales with the picture, so it reads the same in a GIF and in 4K
    QFont font;
    font.setStyleHint(QFont::Monospace);
    font.setFamily("monospace");
    font.setPixelSize(std::max(10, std::min(size.width(), size.height()) / 22));
    font.setBold(true);
    p.setFont(font);
    const QFontMetricsF metrics(font);
    const double pad = metrics.height() * 0.35;
    const QSizeF box(metrics.horizontalAdvance(text) + 2 * pad, metrics.height() + pad);
    const double margin = metrics.height() * 0.6;
    const QRectF rect(QPointF(margin, size.height() - margin - box.height()), box);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRoundedRect(rect, pad, pad);
    p.setPen(Qt::white);
    p.drawText(rect, Qt::AlignCenter, text);
}

// ---------------------------------------------------------------------------
// GIF palette: median cut over a 5-bit-per-channel colour histogram
// ---------------------------------------------------------------------------

constexpr int kCells = 1 << 15;

inline int cellOf(QRgb c) {
    return (qRed(c) >> 3) << 10 | (qGreen(c) >> 3) << 5 | qBlue(c) >> 3;
}

inline int cellChannel(int cell, int channel) { // 0: red, 1: green, 2: blue
    return cell >> (10 - 5 * channel) & 31;
}

void addToHistogram(std::vector<uint32_t>& histogram, const QImage& image, uint32_t weight) {
    const bool alpha = image.hasAlphaChannel();
    for (int y = 0; y < image.height(); y++) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x++) {
            if (alpha && qAlpha(line[x]) < 128) continue; // only the drawn parts
            histogram[cellOf(line[x])] += weight;
        }
    }
}

struct Palette {
    std::vector<QRgb>    colors; // at most 255: the encoder needs a free entry for transparency
    std::vector<uint8_t> lookup; // per histogram cell: the nearest colour
};

Palette buildPalette(const std::vector<uint32_t>& histogram, size_t maxColors) {
    struct Box {
        std::vector<int> cells;
        uint64_t count = 0;
        int      axis  = 0;     // the channel with the widest spread
        int      range = 0;
    };
    auto measure = [&](Box& box) {
        int low[3] = {31, 31, 31}, high[3] = {0, 0, 0};
        box.count = 0;
        for (int cell : box.cells) {
            box.count += histogram[cell];
            for (int c = 0; c < 3; c++) {
                low[c]  = std::min(low[c], cellChannel(cell, c));
                high[c] = std::max(high[c], cellChannel(cell, c));
            }
        }
        box.range = -1;
        for (int c = 0; c < 3; c++) {
            if (high[c] - low[c] > box.range) {
                box.range = high[c] - low[c];
                box.axis = c;
            }
        }
    };

    std::vector<Box> boxes(1);
    for (int cell = 0; cell < kCells; cell++) {
        if (histogram[cell]) boxes[0].cells.push_back(cell);
    }
    if (boxes[0].cells.empty()) boxes[0].cells.push_back(0);
    measure(boxes[0]);

    // Split the box that covers the most pixels over the widest range, at
    // its weighted median, until there are enough colours.
    while (boxes.size() < maxColors) {
        int best = -1;
        double bestScore = 0.0;
        for (size_t i = 0; i < boxes.size(); i++) {
            if (boxes[i].cells.size() < 2) continue;
            const double score = static_cast<double>(boxes[i].count) * boxes[i].range;
            if (best < 0 || score > bestScore) {
                best = static_cast<int>(i);
                bestScore = score;
            }
        }
        if (best < 0) break; // every colour has an entry of its own
        Box& box = boxes[best];
        const int axis = box.axis;
        std::sort(box.cells.begin(), box.cells.end(),
                  [axis](int a, int b) { return cellChannel(a, axis) < cellChannel(b, axis); });
        uint64_t sum = 0;
        size_t split = 1;
        for (; split < box.cells.size(); split++) {
            sum += histogram[box.cells[split - 1]];
            if (sum * 2 >= box.count) break;
        }
        split = std::clamp<size_t>(split, 1, box.cells.size() - 1);
        Box other;
        other.cells.assign(box.cells.begin() + static_cast<std::ptrdiff_t>(split), box.cells.end());
        box.cells.resize(split);
        measure(box);
        measure(other);
        boxes.push_back(std::move(other));
    }

    Palette palette;
    for (const Box& box : boxes) {
        double sum[3] = {}, weight = 0.0;
        for (int cell : box.cells) {
            const double w = std::max<uint32_t>(1, histogram[cell]);
            for (int c = 0; c < 3; c++) sum[c] += w * (cellChannel(cell, c) << 3 | 4);
            weight += w;
        }
        palette.colors.push_back(qRgb(static_cast<int>(sum[0] / weight), static_cast<int>(sum[1] / weight),
                                      static_cast<int>(sum[2] / weight)));
    }

    // Nearest colour for every cell, weighted roughly by how the eye sees it
    palette.lookup.resize(kCells);
    for (int cell = 0; cell < kCells; cell++) {
        const int r = cellChannel(cell, 0) << 3 | 4, g = cellChannel(cell, 1) << 3 | 4, b = cellChannel(cell, 2) << 3 | 4;
        int best = 0, bestDistance = INT_MAX;
        for (size_t i = 0; i < palette.colors.size(); i++) {
            const QRgb c = palette.colors[i];
            const int dr = qRed(c) - r, dg = qGreen(c) - g, db = qBlue(c) - b;
            const int distance = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<int>(i);
            }
        }
        palette.lookup[cell] = static_cast<uint8_t>(best);
    }
    return palette;
}

// Ordered dithering: the pattern stays put from frame to frame, so the parts
// of the picture that don't move stay the same and the GIF stays small.
constexpr int kBayer[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41}, {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

void quantize(const QImage& image, const Palette& palette, bool dither, AVFrame* out) {
    for (int y = 0; y < image.height(); y++) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        uint8_t* dst = out->data[0] + static_cast<ptrdiff_t>(y) * out->linesize[0];
        for (int x = 0; x < image.width(); x++) {
            int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            if (dither) {
                const int offset = (kBayer[y & 7][x & 7] - 32) / 4; // -8..7
                r = std::clamp(r + offset, 0, 255);
                g = std::clamp(g + offset, 0, 255);
                b = std::clamp(b + offset, 0, 255);
            }
            dst[x] = palette.lookup[(r >> 3) << 10 | (g >> 3) << 5 | b >> 3];
        }
    }
    auto* colors = reinterpret_cast<uint32_t*>(out->data[1]);
    for (int i = 0; i < 256; i++) {
        colors[i] = i < static_cast<int>(palette.colors.size()) ? (palette.colors[i] | 0xff000000u) : 0xff000000u;
    }
}

// ---------------------------------------------------------------------------
// Encoding and muxing
// ---------------------------------------------------------------------------

// H.264 encoders in order of preference: x264 where FFmpeg has it (Linux),
// else what the graphics card or the system offers (Windows builds of FFmpeg
// under the LGPL come without x264), and MPEG-4 Part 2 as a last resort.
const char* const kMp4Encoders[] = {
    "libx264", "h264_nvenc", "h264_qsv", "h264_amf", "h264_mf", "h264_videotoolbox", "libopenh264", "mpeg4",
};

AVPixelFormat pickPixelFormat(const AVCodec* codec) {
    const AVPixelFormat* formats = nullptr;
    int count = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* configs = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count) >= 0)
        formats = static_cast<const AVPixelFormat*>(configs);
#else
    formats = codec->pix_fmts;
    while (formats && formats[count] != AV_PIX_FMT_NONE) count++;
#endif
    if (!formats || count == 0) return AV_PIX_FMT_YUV420P;
    for (AVPixelFormat wanted : {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12}) {
        if (std::find(formats, formats + count, wanted) != formats + count) return wanted;
    }
    return formats[0];
}

class VideoWriter {
public:
    ~VideoWriter() { close(); }

    bool open(const ExportSettings& s, QString* error);
    // Encodes image (the size given to open()) as the frame at pts, in
    // 1/fps for MP4 and 1/100 s for GIF.
    bool write(const QImage& image, int64_t pts);
    bool finish(QString* error); // flushes the encoder and completes the file
    void close();
    QString error() const { return m_error; }
    void setPalette(Palette palette) { m_palette = std::move(palette); } // GIF only, before write()

private:
    bool openEncoder(const AVCodec* codec, const ExportSettings& s);
    bool send(AVFrame* frame); // nullptr flushes
    bool fail(const QString& what, int err) {
        m_error = QString("%1: %2").arg(what, avErrorString(err));
        return false;
    }

    AVFormatContext* m_fmt    = nullptr;
    AVCodecContext*  m_codec  = nullptr;
    AVStream*        m_stream = nullptr;
    AVFrame*         m_frame  = nullptr;
    AVPacket*        m_packet = nullptr;
    SwsContext*      m_sws    = nullptr;
    bool             m_gif    = false;
    bool             m_dither = true;
    Palette          m_palette;
    QString          m_error;
};

bool VideoWriter::openEncoder(const AVCodec* codec, const ExportSettings& s) {
    m_codec = avcodec_alloc_context3(codec);
    if (!m_codec) return false;
    m_codec->width  = s.size.width();
    m_codec->height = s.size.height();
    m_codec->sample_aspect_ratio = {1, 1};
    AVDictionary* options = nullptr;
    if (m_gif) {
        m_codec->pix_fmt   = AV_PIX_FMT_PAL8;
        m_codec->time_base = {1, 100}; // GIF delays are in hundredths of a second
    } else {
        const AVRational rate = av_d2q(s.fps, 65535); // MPEG-4 Part 2 allows no more
        m_codec->framerate = rate;
        m_codec->time_base = av_inv_q(rate);
        m_codec->pix_fmt   = pickPixelFormat(codec);
        m_codec->gop_size  = std::clamp(static_cast<int>(std::lround(s.fps * 2.0)), 1, 600);
        m_codec->color_primaries = AVCOL_PRI_BT709;
        m_codec->color_trc       = AVCOL_TRC_BT709;
        m_codec->colorspace      = AVCOL_SPC_BT709;
        m_codec->color_range     = AVCOL_RANGE_MPEG;
        const double quality = std::clamp(s.quality, 1, 100) / 100.0;
        if (QString::fromUtf8(codec->name) == "libx264") {
            // Constant quality: 100 -> CRF 14 (visually lossless), 50 -> 27
            av_dict_set_int(&options, "crf", std::lround(40.0 - 26.0 * quality), 0);
            av_dict_set(&options, "preset", "medium", 0);
        } else {
            // Bits per pixel and frame, about what x264 needs for the same look
            const double bitsPerPixel = 0.02 + 0.2 * quality;
            m_codec->bit_rate = static_cast<int64_t>(bitsPerPixel * s.size.width() * s.size.height()
                                                     * std::min(s.fps, 120.0));
        }
        if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER) m_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    const int ret = avcodec_open2(m_codec, codec, &options);
    av_dict_free(&options);
    if (ret < 0) {
        avcodec_free_context(&m_codec);
        return false;
    }
    return true;
}

bool VideoWriter::open(const ExportSettings& s, QString* error) {
    m_gif = s.format == ExportFormat::Gif;
    m_dither = s.dither;
    const QByteArray file = s.file.toUtf8();
    int ret = avformat_alloc_output_context2(&m_fmt, nullptr, m_gif ? "gif" : "mp4", file.constData());
    if (ret < 0 || !m_fmt) {
        *error = QString("Could not create the file: %1").arg(avErrorString(ret));
        return false;
    }

    if (m_gif) {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_GIF);
        if (!codec || !openEncoder(codec, s)) {
            *error = "No GIF encoder available";
            return false;
        }
    } else {
        for (const char* name : kMp4Encoders) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name);
            if (codec && openEncoder(codec, s)) break;
        }
        if (!m_codec) {
            *error = "No video encoder available";
            return false;
        }
    }

    m_stream = avformat_new_stream(m_fmt, nullptr);
    if (!m_stream || avcodec_parameters_from_context(m_stream->codecpar, m_codec) < 0) {
        *error = "Could not create the video stream";
        return false;
    }
    m_stream->time_base = m_codec->time_base;
    if (!m_gif) m_stream->avg_frame_rate = m_codec->framerate;

    ret = avio_open(&m_fmt->pb, file.constData(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        *error = QString("Could not write the file: %1").arg(avErrorString(ret));
        return false;
    }
    AVDictionary* options = nullptr;
    if (m_gif) av_dict_set(&options, "loop", s.loop ? "0" : "-1", 0);
    else av_dict_set(&options, "movflags", "+faststart", 0); // plays while still downloading
    ret = avformat_write_header(m_fmt, &options);
    av_dict_free(&options);
    if (ret < 0) {
        *error = QString("Could not write the file: %1").arg(avErrorString(ret));
        return false;
    }

    m_frame  = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_frame || !m_packet) {
        *error = "Out of memory";
        return false;
    }
    m_frame->format = m_codec->pix_fmt;
    m_frame->width  = m_codec->width;
    m_frame->height = m_codec->height;
    if (av_frame_get_buffer(m_frame, 0) < 0) {
        *error = "Out of memory";
        return false;
    }
    if (!m_gif) {
        m_sws = sws_getContext(m_codec->width, m_codec->height, AV_PIX_FMT_RGB32, m_codec->width,
                               m_codec->height, m_codec->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!m_sws) {
            *error = "Could not convert the frames for the encoder";
            return false;
        }
        const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(m_sws, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
    }
    return true;
}

bool VideoWriter::write(const QImage& image, int64_t pts) {
    const int ret = av_frame_make_writable(m_frame);
    if (ret < 0) return fail("Out of memory", ret);
    if (m_gif) {
        quantize(image, m_palette, m_dither, m_frame);
    } else {
        const uint8_t* src[] = {image.constBits()};
        const int srcStride[] = {static_cast<int>(image.bytesPerLine())};
        sws_scale(m_sws, src, srcStride, 0, image.height(), m_frame->data, m_frame->linesize);
    }
    m_frame->pts = pts;
    return send(m_frame);
}

bool VideoWriter::send(AVFrame* frame) {
    int ret = avcodec_send_frame(m_codec, frame);
    if (ret < 0 && ret != AVERROR_EOF) return fail("Encoding failed", ret);
    for (;;) {
        ret = avcodec_receive_packet(m_codec, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
        if (ret < 0) return fail("Encoding failed", ret);
        av_packet_rescale_ts(m_packet, m_codec->time_base, m_stream->time_base);
        m_packet->stream_index = m_stream->index;
        ret = av_interleaved_write_frame(m_fmt, m_packet);
        if (ret < 0) return fail("Could not write the file", ret);
    }
}

bool VideoWriter::finish(QString* error) {
    if (!send(nullptr)) {
        *error = m_error;
        return false;
    }
    const int ret = av_write_trailer(m_fmt);
    if (ret < 0) {
        *error = QString("Could not write the file: %1").arg(avErrorString(ret));
        return false;
    }
    return true;
}

void VideoWriter::close() {
    sws_freeContext(m_sws);
    m_sws = nullptr;
    av_packet_free(&m_packet);
    av_frame_free(&m_frame);
    avcodec_free_context(&m_codec);
    if (m_fmt) {
        if (m_fmt->pb) avio_closep(&m_fmt->pb);
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
    }
    m_stream = nullptr;
}

} // namespace

bool isVideoFormat(ExportFormat format) {
    return format == ExportFormat::Mp4 || format == ExportFormat::Gif;
}

QString formatSuffix(ExportFormat format) {
    switch (format) {
    case ExportFormat::Png:  return "png";
    case ExportFormat::Jpeg: return "jpg";
    case ExportFormat::Mp4:  return "mp4";
    case ExportFormat::Gif:  return "gif";
    }
    return {};
}

namespace ClipExporter {

int frameCount(const ExportSettings& s, double sourceFps) {
    // The last frame is shown for as long as any other
    const double length = std::max(0.0, s.end - s.start) + 1.0 / std::max(1.0, sourceFps);
    const double fps = s.fps > 0.0 ? s.fps : sourceFps;
    return std::max(1, static_cast<int>(std::lround(length / s.speed * fps)));
}

QImage composeFrame(const AVFrame* frame, double time, const ExportSettings& s) {
    // Scaled before rotating: a quarter turn swaps the sides
    const bool sideways = s.rotation == 90 || s.rotation == 270;
    const QSize unrotated = sideways ? s.size.transposed() : s.size;
    QImage image = frameToImage(frame, unrotated);
    if (image.isNull()) return {};
    if (s.rotation != 0) image = image.transformed(QTransform().rotate(s.rotation));
    if (s.overlay.isNull() && s.timeStamp == TimeStamp::Off) return image;

    QPainter p(&image);
    if (!s.overlay.isNull()) p.drawImage(QRect(QPoint(), image.size()), s.overlay);
    if (s.timeStamp != TimeStamp::Off) {
        QString text = formatTime(s.timeStamp == TimeStamp::Elapsed ? time - s.start : time);
        if (isVideoFormat(s.format) && s.speed != 1.0) text += "  " + speedText(s.speed);
        paintTimeStamp(p, image.size(), text);
    }
    return image;
}

bool saveImage(const FramePtr& frame, double time, const ExportSettings& s, QString* error) {
    const QImage image = composeFrame(frame.get(), time, s);
    if (image.isNull()) {
        *error = "Could not convert the frame";
        return false;
    }
    const bool jpeg = s.format == ExportFormat::Jpeg;
    if (!image.save(s.file, jpeg ? "JPEG" : "PNG", jpeg ? std::clamp(s.quality, 1, 100) : -1)) {
        *error = "Could not save the file";
        return false;
    }
    return true;
}

bool exportVideo(const ExportSettings& settings, const std::atomic<bool>& cancel,
                 const std::function<void(double)>& progress, QString* error) {
    error->clear();
    ExportSettings s = settings;
    const ClipRef clip = ClipRef::fromKey(s.clip);
    FrameQueue queue;
    queue.setCapacity(8);
    Decoder decoder;
    decoder.setQueue(&queue);
    QObject::connect(&decoder, &Decoder::errorOccurred, [error](const QString& e) { *error = e; });
    if (!decoder.open(clip.path, clip.start, clip.end)) {
        if (error->isEmpty()) *error = "Could not open the clip";
        return false;
    }
    const VideoInfo info = decoder.info();
    const double halfFrame = 0.5 / info.fps;
    if (s.fps <= 0.0) s.fps = info.fps;

    int serial = 0;
    auto seekTo = [&](double time) {
        queue.flush(++serial);
        decoder.seek(time, serial);
    };
    // The next decoded frame; false at the end of the clip or when cancelled
    auto nextFrame = [&](FrameQueue::Frame& out) {
        while (!cancel) {
            if (queue.popNext(out)) return true;
            if (queue.atEnd()) return false;
            QThread::msleep(1);
        }
        return false;
    };

    VideoWriter writer;
    bool ok = writer.open(s, error);
    auto abandon = [&] {
        writer.close();
        QFile::remove(s.file);
        return false;
    };
    if (!ok) return abandon();

    const int total = frameCount(s, info.fps);
    if (s.format == ExportFormat::Gif) {
        // One palette for the whole GIF, from frames spread over the range
        // and the drawings, which get extra weight to keep their colours
        std::vector<uint32_t> histogram(kCells, 0);
        const int samples = std::min(total, 8);
        for (int i = 0; i < samples && !cancel; i++) {
            seekTo(s.start + (s.end - s.start) * (i + 0.5) / samples);
            FrameQueue::Frame sample;
            if (!nextFrame(sample)) break;
            addToHistogram(histogram, composeFrame(sample.frame.get(), sample.pts, s), 1);
            progress(0.05 * (i + 1) / samples);
        }
        if (!s.overlay.isNull())
            addToHistogram(histogram, s.overlay.convertToFormat(QImage::Format_ARGB32), 16);
        writer.setPalette(buildPalette(histogram, 255));
    }
    if (cancel) return abandon();

    seekTo(s.start);
    FrameQueue::Frame current, next;
    if (!nextFrame(current)) {
        if (!cancel) *error = "No frames in the range";
        return abandon();
    }
    bool hasNext = nextFrame(next);
    QImage image;
    int source = 0, composed = -1; // which source frame is current, and which image shows
    const double base = s.format == ExportFormat::Gif ? 0.05 : 0.0;

    for (int k = 0; k < total; k++) {
        if (cancel) return abandon();
        // The frame on screen at this moment, as the player would show it
        const double time = s.start + k * s.speed / s.fps;
        while (hasNext && next.pts <= time + halfFrame) {
            current = std::move(next);
            hasNext = nextFrame(next);
            source++;
        }
        // In slow motion, a frame is shown for several in a row
        if (source != composed) {
            image = composeFrame(current.frame.get(), current.pts, s);
            composed = source;
            if (image.isNull()) {
                *error = "Could not convert a frame";
                return abandon();
            }
        }
        const int64_t pts = s.format == ExportFormat::Gif ? std::llround(k * 100.0 / s.fps) : k;
        if (!writer.write(image, pts)) {
            *error = writer.error();
            return abandon();
        }
        progress(base + (1.0 - base) * (k + 1) / total);
    }
    if (cancel || !writer.finish(error)) return abandon();
    writer.close();
    return true;
}

} // namespace ClipExporter
