#ifndef DECODER_H
#define DECODER_H

#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

#include "framequeue.h"
#include "videoframe.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

// Decoder runs in its own thread, decoding as fast as the bounded FrameQueue
// allows. Timing and display-rate control are handled by PlayerController.
class Decoder : public QThread {
    Q_OBJECT

public:
    explicit Decoder(QObject* parent = nullptr);
    ~Decoder() override;

    // Must be called before open(). The queue must remain valid until close().
    void setQueue(FrameQueue* queue);

    // Opens filename, or just the part of it from partStart to partEnd
    // (seconds; -1: to the end of the file). Timestamps and the duration are
    // then relative to partStart, and the stream ends at partEnd.
    bool open(const QString& filename, double partStart = 0.0, double partEnd = -1.0);
    void close();

    // Seek to timestamp (seconds since stream start). Frames decoded after the
    // seek are tagged with serial. Decoding is frame accurate: frames before
    // the target are decoded but not queued.
    void seek(double timestamp, int serial);

    // Reads just the container header; thread-safe. Returns -1 on failure.
    static double probeDuration(const QString& filename);

    // The hardware decoder to use for codec, if any (not with
    // REPLAYER_HWDEC=none). Sets device to a new reference to it, shared by
    // all decoders, which the caller unrefs.
    static const AVCodecHWConfig* hardwareConfig(const AVCodec* codec, AVBufferRef** device);

    bool isOpen() const { return m_fmtCtx != nullptr; }
    const VideoInfo& info() const { return m_info; }

signals:
    void errorOccurred(const QString& error);

protected:
    void run() override;

private:
    bool initHWContext(const AVCodec* codec);
    bool openCodec(const AVCodec* codec);
    double framePts(const AVFrame* frame);
    FramePtr toDisplayFrame(AVFrame* frame);
    static AVPixelFormat selectPixelFormat(AVCodecContext* ctx, const AVPixelFormat* formats);

    AVFormatContext* m_fmtCtx      = nullptr;
    AVCodecContext*  m_codecCtx    = nullptr;
    AVBufferRef*     m_hwDeviceCtx = nullptr;          // reference to a shared device
    AVPixelFormat    m_hwPixFmt    = AV_PIX_FMT_NONE; // NONE: software decoding
    SwsContext*      m_swsCtx      = nullptr;

    int       m_videoStream = -1;
    double    m_timeBase    = 0.0; // seconds per stream tick
    double    m_startTime   = 0.0; // stream start (plus the part's start) in seconds
    double    m_partStart   = 0.0; // where the part opened starts in the file
    bool      m_partEnds    = false; // frames from m_info.duration on are not played
    double    m_lastPts     = 0.0;
    VideoInfo m_info;

    // Shared with the decoder thread; guarded by m_mutex unless atomic.
    QMutex            m_mutex;
    QWaitCondition    m_wake;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_seekPending{false};
    double            m_seekTarget = 0.0;
    int               m_seekSerial = 0;

    FrameQueue* m_queue = nullptr;
};

#endif
