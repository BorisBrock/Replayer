#include "motionanalyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace {

constexpr int    kThumbWidth      = 64;   // frames are compared at this width
constexpr double kSampleInterval  = 0.25; // seconds between compared frames
constexpr double kMaxKeyInterval  = 0.6;  // keyframes this close: decode only them
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

// Frees everything analyze() allocates, however it returns.
struct Context {
    AVFormatContext* fmt   = nullptr;
    AVCodecContext*  codec = nullptr;
    AVPacket*        packet = av_packet_alloc();
    AVFrame*         frame  = av_frame_alloc();
    SwsContext*      sws    = nullptr;

    ~Context() {
        sws_freeContext(sws);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&fmt);
    }
};

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

} // namespace

namespace MotionAnalyzer {

QList<Sample> analyze(const QString& path, const std::atomic<bool>& cancel,
                      const std::function<void(double)>& progress) {
    Context c;
    if (!c.packet || !c.frame) return {};
    if (avformat_open_input(&c.fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) return {};
    if (avformat_find_stream_info(c.fmt, nullptr) < 0) return {};

    const AVCodec* codec = nullptr;
    const int streamIndex = av_find_best_stream(c.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (streamIndex < 0 || !codec) return {};
    for (unsigned int i = 0; i < c.fmt->nb_streams; i++) {
        if (static_cast<int>(i) != streamIndex) c.fmt->streams[i]->discard = AVDISCARD_ALL;
    }
    AVStream* stream = c.fmt->streams[streamIndex];
    const double timeBase  = av_q2d(stream->time_base);
    const double startTime = stream->start_time != AV_NOPTS_VALUE ? stream->start_time * timeBase : 0.0;
    double duration = stream->duration != AV_NOPTS_VALUE ? stream->duration * timeBase : 0.0;
    if (duration <= 0.0 && c.fmt->duration != AV_NOPTS_VALUE)
        duration = c.fmt->duration / static_cast<double>(AV_TIME_BASE);

    c.codec = avcodec_alloc_context3(codec);
    if (!c.codec || avcodec_parameters_to_context(c.codec, stream->codecpar) < 0) return {};
    c.codec->pkt_timebase = stream->time_base;
    c.codec->thread_count = 0;
    c.codec->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
    // Speed over picture quality: a blurry thumbnail shows motion just as well.
    c.codec->skip_loop_filter = AVDISCARD_ALL;
    c.codec->flags2 |= AV_CODEC_FLAG2_FAST;
    const bool keyframesOnly = keyframeInterval(stream, duration) <= kMaxKeyInterval;
    c.codec->skip_frame = keyframesOnly ? AVDISCARD_NONKEY : AVDISCARD_NONREF;
    if (avcodec_open2(c.codec, codec, nullptr) < 0) return {};

    QList<Sample> samples;
    std::vector<double> previous, current;
    double nextSample = -HUGE_VAL;
    double lastPts = 0.0;

    // Shrinks the frame to grey, without its mean brightness (auto exposure
    // shouldn't count as motion), and compares it with the previous one.
    auto addSample = [&](AVFrame* f, double pts) {
        const int thumbHeight = std::max(8, static_cast<int>(std::lround(
            kThumbWidth * static_cast<double>(f->height) / std::max(1, f->width))));
        c.sws = sws_getCachedContext(c.sws, f->width, f->height, static_cast<AVPixelFormat>(f->format),
                                     kThumbWidth, thumbHeight, AV_PIX_FMT_GRAY8, SWS_AREA,
                                     nullptr, nullptr, nullptr);
        if (!c.sws) return;
        std::vector<uint8_t> gray(kThumbWidth * thumbHeight);
        uint8_t* dst[4] = {gray.data(), nullptr, nullptr, nullptr};
        int dstStride[4] = {kThumbWidth, 0, 0, 0};
        sws_scale(c.sws, f->data, f->linesize, 0, f->height, dst, dstStride);

        double mean = 0.0;
        for (uint8_t v : gray) mean += v;
        mean /= gray.size();
        current.resize(gray.size());
        for (size_t i = 0; i < gray.size(); i++) current[i] = gray[i] - mean;

        double motion = 0.0;
        if (previous.size() == current.size()) {
            for (size_t i = 0; i < current.size(); i++) motion += std::abs(current[i] - previous[i]);
            motion /= current.size();
        }
        samples.append({pts, motion});
        std::swap(previous, current);
        if (duration > 0.0) progress(std::clamp(pts / duration, 0.0, 1.0));
    };

    auto receiveFrames = [&] {
        while (!cancel && avcodec_receive_frame(c.codec, c.frame) >= 0) {
            int64_t ts = c.frame->best_effort_timestamp;
            if (ts == AV_NOPTS_VALUE) ts = c.frame->pts;
            const double pts = ts != AV_NOPTS_VALUE ? ts * timeBase - startTime : lastPts;
            lastPts = pts;
            if (keyframesOnly || pts >= nextSample) {
                nextSample = pts + kSampleInterval;
                addSample(c.frame, pts);
            }
            av_frame_unref(c.frame);
        }
    };

    while (!cancel && av_read_frame(c.fmt, c.packet) >= 0) {
        if (c.packet->stream_index == streamIndex) avcodec_send_packet(c.codec, c.packet);
        av_packet_unref(c.packet);
        receiveFrames();
    }
    avcodec_send_packet(c.codec, nullptr);
    receiveFrames();
    if (cancel) return {};
    progress(1.0);
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
