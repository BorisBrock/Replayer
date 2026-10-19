#ifndef PLAYERCONTROLLER_H
#define PLAYERCONTROLLER_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <memory>

#include "framequeue.h"
#include "videoframe.h"

class Decoder;

// PlayerController owns the FrameQueue, the render timer, and the playback
// clock. The Decoder thread pushes frames into the queue as fast as the queue
// allows; the render timer pops and displays them at the correct wall-clock
// rate, accounting for video fps, display refresh rate, and speed multiplier.
class PlayerController : public QObject {
    Q_OBJECT

public:
    explicit PlayerController(QObject* parent = nullptr);
    ~PlayerController() override;

    // Opens filename, or the part of it from start to end (see Decoder::open).
    bool openFile(const QString& filename, double start = 0.0, double end = -1.0);
    void closeFile();

    void play();
    void pause();
    void togglePlayPause();
    void stop(); // pause and rewind to the start
    void seek(double position);
    void seekRelative(double delta);
    void stepForward();
    void stepBackward();
    void setSpeedMultiplier(double multiplier);
    void setLoop(bool loop); // restart from the beginning at the end of the clip
    void setFrameQueueCapacity(int frames) { m_frameQueue.setCapacity(frames); }

    bool isOpen() const;
    bool loop() const { return m_loop; }
    bool isPlaying() const { return m_playing; }
    double position() const { return m_position; }
    double speedMultiplier() const { return m_speedMultiplier; }
    const VideoInfo& videoInfo() const { return m_info; }

signals:
    void frameReady(const FramePtr& frame);
    void positionChanged(double position);
    void playbackStateChanged(bool playing);
    void videoInfoReady(const VideoInfo& info);
    void errorOccurred(const QString& error);

private slots:
    void onRenderTimerTick();

private:
    void startSeek(double position);
    void presentFrame(const FrameQueue::Frame& frame);
    double clockPts() const;
    void restartClock(double pts);

    // Declared before m_decoder so it outlives the decoder thread.
    FrameQueue               m_frameQueue;
    std::unique_ptr<Decoder> m_decoder;
    QTimer*                  m_renderTimer = nullptr;
    QElapsedTimer            m_playClock;
    VideoInfo                m_info;

    bool   m_playing         = false;
    bool   m_loop            = false;
    bool   m_awaitingFrame   = false; // show the next frame that arrives (after open/seek/step)
    bool   m_seekInFlight    = false; // the decoder is still working towards a seek target
    double m_pendingSeek     = -1.0;  // seek requested while another was in flight, or -1
    int    m_serial          = 0;     // incremented on every seek, see FrameQueue
    double m_position        = 0.0;   // pts of the frame on screen
    double m_clockBasePts    = 0.0;   // pts at the moment m_playClock was last restarted
    double m_speedMultiplier = 1.0;
};

#endif
