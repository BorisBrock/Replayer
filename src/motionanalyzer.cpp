#include "motionanalyzer.h"

#include "decoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr int    kThumbWidth      = 64;   // frames are compared at this width
constexpr double kSampleInterval  = 0.25; // seconds between compared frames
constexpr double kMaxKeyInterval  = 0.6;  // keyframes this close: decode only them
constexpr double kMinChunk        = 4.0;  // a worker decodes at least this much after a seek
constexpr int    kMaxWorkers      = 8;    // each holds its own decoded reference frames
constexpr int    kHardwareWorkers = 2;    // enough to keep the GPU's decoder busy
constexpr double kMergeGap        = 2.0;  // motion this close together is one jump
constexpr double kMinActive       = 0.5;  // shorter bursts are noise
constexpr double kLeadIn          = 1.5;  // kept before the motion starts
constexpr double kRunOut          = 1.0;  // kept after it ends

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const auto mid = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

// Average spacing of the stream's keyframes, from the container index
double keyframeInterval(AVStream* stream, double duration) {
    int keyframes = 0;
    const int entries = avformat_index_get_entries_count(stream);
    for (int i = 0; i < entries; i++) {
        const AVIndexEntry* entry = avformat_index_get_entry(stream, i);
        if (entry && (entry->flags & AVINDEX_KEYFRAME)) keyframes++;
    }
    return keyframes > 1 && duration > 0.0 ? duration / keyframes : HUGE_VAL;
}

// The sample slot a frame falls into. Every frame is compared from the first
// one of its slot, so workers decoding different chunks agree on which.
int64_t slotOf(double pts) {
    return static_cast<int64_t>(std::floor(pts / kSampleInterval + 1e-6));
}

// A frame shrunk to grey, without its mean brightness (auto exposure
// shouldn't count as motion)
struct Thumb {
    double             time = 0.0;
    std::vector<float> pixels;
};

// One demuxer and decoder, used by one worker thread. Frees everything it
// allocates, however it is left.
struct Reader {
    AVFormatContext* fmt      = nullptr;
    AVCodecContext*  codec    = nullptr;
    AVPacket*        packet   = av_packet_alloc();
    AVFrame*         frame    = av_frame_alloc();
    AVFrame*         download = av_frame_alloc(); // a hardware frame, in system memory
    SwsContext*      sws      = nullptr;
    AVBufferRef*     device   = nullptr;
    AVPixelFormat    hwFormat = AV_PIX_FMT_NONE;
    int              streamIndex = -1;
    double           timeBase  = 0.0;
    double           startTime = 0.0;
    double           duration  = 0.0; // 0 when unknown
    double           lastPts   = 0.0;
    bool             fresh     = true; // nothing read yet: no need to seek to the start

    ~Reader() {
        sws_freeContext(sws);
        av_frame_free(&download);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        av_buffer_unref(&device);
        avformat_close_input(&fmt);
    }

    // Decodes on the GPU when hardware is set and there is one, else on
    // threads CPU threads.
    bool open(const QString& path, bool hardware, int threads) {
        if (!packet || !frame || !download) return false;
        if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(fmt, nullptr) < 0) return false;

        const AVCodec* decoder = nullptr;
        streamIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if (streamIndex < 0 || !decoder) return false;
        for (unsigned int i = 0; i < fmt->nb_streams; i++) {
            if (static_cast<int>(i) != streamIndex) fmt->streams[i]->discard = AVDISCARD_ALL;
        }
        AVStream* stream = fmt->streams[streamIndex];
        timeBase  = av_q2d(stream->time_base);
        startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time * timeBase : 0.0;
        duration  = stream->duration != AV_NOPTS_VALUE ? stream->duration * timeBase : 0.0;
        if (duration <= 0.0 && fmt->duration != AV_NOPTS_VALUE)
            duration = fmt->duration / static_cast<double>(AV_TIME_BASE);

        if (hardware) {
            if (const AVCodecHWConfig* config = Decoder::hardwareConfig(decoder, &device))
                hwFormat = config->pix_fmt;
        }
        if (openCodec(decoder, stream, threads)) return true;
        if (hwFormat == AV_PIX_FMT_NONE) return false;
        avcodec_free_context(&codec);
        av_buffer_unref(&device);
        hwFormat = AV_PIX_FMT_NONE;
        return openCodec(decoder, stream, threads);
    }

    bool openCodec(const AVCodec* decoder, AVStream* stream, int threads) {
        codec = avcodec_alloc_context3(decoder);
        if (!codec || avcodec_parameters_to_context(codec, stream->codecpar) < 0) return false;
        codec->pkt_timebase = stream->time_base;
        if (hwFormat != AV_PIX_FMT_NONE) {
            codec->hw_device_ctx = av_buffer_ref(device);
            codec->opaque        = this;
            codec->get_format    = &Reader::selectFormat;
        } else {
            codec->thread_count = threads;
            codec->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
        }
        // Speed over picture quality: a blurry thumbnail shows motion just as well.
        codec->skip_loop_filter = AVDISCARD_ALL;
        codec->flags2 |= AV_CODEC_FLAG2_FAST;
        const bool keyframesOnly = keyframeInterval(stream, duration) <= kMaxKeyInterval;
        codec->skip_frame = keyframesOnly ? AVDISCARD_NONKEY : AVDISCARD_NONREF;
        return avcodec_open2(codec, decoder, nullptr) >= 0;
    }

    static AVPixelFormat selectFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
        const auto* self = static_cast<const Reader*>(ctx->opaque);
        for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; p++) {
            if (*p == self->hwFormat) return *p;
        }
        // Not for this hardware (e.g. an unsupported profile): decode in software
        for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; p++) {
            if (!(av_pix_fmt_desc_get(*p)->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
        }
        return AV_PIX_FMT_NONE;
    }

    // Whether the GPU really decodes the stream, from its first frame
    bool decodesInHardware() {
        if (hwFormat == AV_PIX_FMT_NONE) return false;
        fresh = false;
        bool hardware = false;
        bool decoded  = false;
        while (!decoded && av_read_frame(fmt, packet) >= 0) {
            if (packet->stream_index == streamIndex) avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (avcodec_receive_frame(codec, frame) >= 0) {
                decoded  = true;
                hardware = frame->hw_frames_ctx != nullptr;
                av_frame_unref(frame);
            }
        }
        return hardware;
    }

    // Adds the first frame of each sample slot from first to last (inclusive)
    // to thumbs, adding the seconds decoded to progress as it goes. Returns
    // false when seeking there fails.
    bool readSlots(int64_t first, int64_t last, const std::atomic<bool>& cancel,
                   std::vector<Thumb>& thumbs, std::atomic<int64_t>& progressMs) {
        if (!fresh || first > 0) {
            const double target = first * kSampleInterval + startTime;
            const auto ts = static_cast<int64_t>(std::floor(target / timeBase));
            if (av_seek_frame(fmt, streamIndex, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
            avcodec_flush_buffers(codec);
        }
        fresh = false;

        const double from = first * kSampleInterval;
        double reported = from;
        int64_t lastSlot = first - 1;
        bool done = false;

        auto receiveFrames = [&] {
            while (!done && !cancel && avcodec_receive_frame(codec, frame) >= 0) {
                int64_t ts = frame->best_effort_timestamp;
                if (ts == AV_NOPTS_VALUE) ts = frame->pts;
                const double pts = ts != AV_NOPTS_VALUE ? ts * timeBase - startTime : lastPts;
                lastPts = pts;
                const int64_t slot = slotOf(pts);
                if (slot > last) {
                    done = true;
                } else if (slot > lastSlot) {
                    lastSlot = slot;
                    addThumb(pts, thumbs);
                }
                if (pts > reported) {
                    progressMs += static_cast<int64_t>((pts - reported) * 1000.0);
                    reported = pts;
                }
                av_frame_unref(frame);
            }
        };

        while (!done && !cancel && av_read_frame(fmt, packet) >= 0) {
            if (packet->stream_index == streamIndex) avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            receiveFrames();
        }
        if (!done && !cancel) {
            avcodec_send_packet(codec, nullptr); // the end of the file: drain the decoder
            receiveFrames();
        }
        return true;
    }

    void addThumb(double pts, std::vector<Thumb>& thumbs) {
        AVFrame* f = frame;
        if (frame->hw_frames_ctx) {
            av_frame_unref(download);
            if (av_hwframe_transfer_data(download, frame, 0) < 0) return;
            f = download;
        }
        const int thumbHeight = std::max(8, static_cast<int>(std::lround(
            kThumbWidth * static_cast<double>(f->height) / std::max(1, f->width))));
        sws = sws_getCachedContext(sws, f->width, f->height, static_cast<AVPixelFormat>(f->format),
                                   kThumbWidth, thumbHeight, AV_PIX_FMT_GRAY8, SWS_AREA,
                                   nullptr, nullptr, nullptr);
        if (!sws) return;
        std::vector<uint8_t> gray(kThumbWidth * thumbHeight);
        uint8_t* dst[4] = {gray.data(), nullptr, nullptr, nullptr};
        int dstStride[4] = {kThumbWidth, 0, 0, 0};
        sws_scale(sws, f->data, f->linesize, 0, f->height, dst, dstStride);

        double mean = 0.0;
        for (uint8_t v : gray) mean += v;
        mean /= gray.size();
        Thumb thumb{pts, std::vector<float>(gray.size())};
        for (size_t i = 0; i < gray.size(); i++) thumb.pixels[i] = static_cast<float>(gray[i] - mean);
        thumbs.push_back(std::move(thumb));
    }
};

// Several workers, each with its own decoder, take turns at chunks of the
// clip: decoders scale poorly across cores, separate parts of a file don't.
// Returns nothing on failure (e.g. a file that can't seek) or when cancelled.
std::vector<Thumb> readThumbs(const QString& path, const std::atomic<bool>& cancel,
                              const std::function<void(double)>& progress) {
    auto first = std::make_unique<Reader>();
    const int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    if (!first->open(path, true, cores)) return {};
    const double duration = first->duration;
    if (duration <= 0.0) return {}; // can't divide it up

    const bool hardware = first->decodesInHardware();
    const int64_t slotCount = slotOf(duration) + 1;
    int workers = hardware ? kHardwareWorkers : std::min(cores, kMaxWorkers);
    // Two chunks per worker even out their speeds (motion is slower to decode)
    const int64_t minChunk = static_cast<int64_t>(std::ceil(kMinChunk / kSampleInterval));
    const int64_t chunk = std::max(minChunk, (slotCount + 2 * workers - 1) / (2 * workers));
    const int64_t chunks = (slotCount + chunk - 1) / chunk;
    workers = static_cast<int>(std::min<int64_t>(workers, chunks));
    const int threads = std::max(1, cores / workers);
    if (!hardware && (threads != cores || first->hwFormat != AV_PIX_FMT_NONE)) {
        first = std::make_unique<Reader>(); // in software, with the right number of threads
        if (!first->open(path, false, threads)) return {};
    }

    std::atomic<int64_t> nextChunk{0};
    std::atomic<int64_t> progressMs{0};
    std::atomic<int>     running{workers};
    std::atomic<bool>    failed{false};
    std::vector<std::vector<Thumb>> results(workers);
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; w++) {
        pool.emplace_back([&, w, reader = w == 0 ? std::move(first) : nullptr]() mutable {
            if (!reader) {
                reader = std::make_unique<Reader>();
                if (!reader->open(path, hardware, threads)) failed = true;
            }
            while (!failed && !cancel) {
                const int64_t c = nextChunk++;
                if (c >= chunks) break;
                const int64_t from = c * chunk;
                if (!reader->readSlots(from, std::min(slotCount, from + chunk) - 1, cancel, results[w], progressMs))
                    failed = true;
            }
            running--;
        });
    }
    while (running > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        progress(std::clamp(progressMs / (duration * 1000.0), 0.0, 1.0));
    }
    for (std::thread& t : pool) t.join();
    if (failed || cancel) return {};

    std::vector<Thumb> thumbs;
    for (std::vector<Thumb>& r : results) {
        thumbs.insert(thumbs.end(), std::make_move_iterator(r.begin()), std::make_move_iterator(r.end()));
    }
    std::sort(thumbs.begin(), thumbs.end(), [](const Thumb& a, const Thumb& b) { return a.time < b.time; });
    return thumbs;
}

// The whole clip with one decoder, from start to end, for files that can't
// be divided up
std::vector<Thumb> readThumbsInOrder(const QString& path, const std::atomic<bool>& cancel) {
    Reader reader;
    const int cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    if (!reader.open(path, false, cores)) return {};
    std::vector<Thumb> thumbs;
    std::atomic<int64_t> progressMs{0};
    reader.readSlots(0, INT64_MAX - 1, cancel, thumbs, progressMs);
    if (cancel) return {};
    return thumbs;
}

} // namespace

namespace MotionAnalyzer {

QList<Sample> analyze(const QString& path, const std::atomic<bool>& cancel,
                      const std::function<void(double)>& progress) {
    std::vector<Thumb> thumbs = readThumbs(path, cancel, progress);
    if (thumbs.empty() && !cancel) thumbs = readThumbsInOrder(path, cancel);
    if (cancel) return {};

    // How much each frame differs from the one before
    QList<Sample> samples;
    for (size_t i = 0; i < thumbs.size(); i++) {
        double motion = 0.0;
        const std::vector<float>& current = thumbs[i].pixels;
        if (i > 0 && thumbs[i - 1].pixels.size() == current.size()) {
            const std::vector<float>& previous = thumbs[i - 1].pixels;
            for (size_t p = 0; p < current.size(); p++) motion += std::abs(current[p] - previous[p]);
            motion /= current.size();
        }
        samples.append({thumbs[i].time, motion});
    }
    if (!samples.isEmpty()) progress(1.0);
    return samples;
}

QList<std::pair<double, double>> suggestParts(const QList<Sample>& samples, double duration) {
    if (samples.size() < 8 || duration <= 0.0) return {};

    // Median of three: a single odd frame (compression glitch) is no motion.
    // The first sample has nothing to compare with.
    std::vector<double> motion(samples.size(), 0.0);
    for (qsizetype i = 1; i < samples.size(); i++) {
        const double before = samples[std::max<qsizetype>(1, i - 1)].motion;
        const double after  = samples[std::min(samples.size() - 1, i + 1)].motion;
        motion[i] = median({before, samples[i].motion, after});
    }

    // Mostly the scene is empty: its sensor noise sets the threshold.
    const std::vector<double> values(motion.begin() + 1, motion.end());
    const double floor = median(values);
    std::vector<double> deviations;
    for (double v : values) deviations.push_back(std::abs(v - floor));
    const double spread = median(deviations);
    const double threshold = std::max({floor + 5.0 * spread, floor * 2.5, floor + 1.5});

    // Stretches of motion; a sample's change happened since the one before it.
    QList<std::pair<double, double>> active;
    for (qsizetype i = 1; i < samples.size(); i++) {
        if (motion[i] <= threshold) continue;
        const double from = samples[i - 1].time;
        const double to   = samples[i].time;
        if (!active.isEmpty() && from - active.last().second < kMergeGap) active.last().second = to;
        else active.append({from, to});
    }
    active.erase(std::remove_if(active.begin(), active.end(),
                                [](const auto& a) { return a.second - a.first < kMinActive; }),
                 active.end());

    double total = 0.0;
    for (const auto& [from, to] : active) total += to - from;
    if (active.isEmpty() || total > 0.8 * duration) return {};

    // Add lead-in and run-out, but never past halfway to the next jump.
    QList<std::pair<double, double>> parts;
    for (qsizetype i = 0; i < active.size(); i++) {
        const double lowest  = i > 0 ? (active[i - 1].second + active[i].first) / 2.0 : 0.0;
        const double highest = i + 1 < active.size() ? (active[i].second + active[i + 1].first) / 2.0 : duration;
        parts.append({std::max(lowest, active[i].first - kLeadIn), std::min(highest, active[i].second + kRunOut)});
    }
    return parts;
}

} // namespace MotionAnalyzer
