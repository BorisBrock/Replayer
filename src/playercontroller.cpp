#include "playercontroller.h"
#include "decoder.h"

#include <QGuiApplication>
#include <QScreen>
#include <algorithm>

PlayerController::PlayerController(QObject* parent)
    : QObject(parent),
      m_decoder(std::make_unique<Decoder>()) {
    m_decoder->setQueue(&m_frameQueue);
    connect(m_decoder.get(), &Decoder::errorOccurred, this, &PlayerController::errorOccurred);

    // Tick at twice the display refresh rate: frames reach the screen at most
    // half a refresh late, and popping an empty queue is nearly free.
    QScreen* screen = QGuiApplication::primaryScreen();
    const double refreshHz = (screen && screen->refreshRate() > 0.0) ? screen->refreshRate() : 60.0;

    m_renderTimer = new QTimer(this);
    m_renderTimer->setTimerType(Qt::PreciseTimer);
    m_renderTimer->setInterval(std::max(1, static_cast<int>(500.0 / refreshHz)));
    connect(m_renderTimer, &QTimer::timeout, this, &PlayerController::onRenderTimerTick);
}

PlayerController::~PlayerController() = default;

bool PlayerController::openFile(const QString& filename) {
    closeFile();
    if (!m_decoder->open(filename)) return false;

    m_info = m_decoder->info();
    m_awaitingFrame = true; // show the first frame even while paused
    m_renderTimer->start();
    emit videoInfoReady(m_info);
    emit positionChanged(0.0);
    return true;
}

void PlayerController::closeFile() {
    pause();
    m_renderTimer->stop();
    m_awaitingFrame = false;
    m_decoder->close();
    m_frameQueue.reopen();
    m_serial       = 0;
    m_position     = 0.0;
    m_clockBasePts = 0.0;
    m_info         = VideoInfo();
}

bool PlayerController::isOpen() const {
    return m_decoder->isOpen();
}

void PlayerController::play() {
    if (m_playing || !isOpen()) return;
    if (m_frameQueue.atEnd()) seek(0.0); // replay from the start

    m_playing = true;
    restartClock(m_position);
    m_renderTimer->start();
    emit playbackStateChanged(true);
}

void PlayerController::pause() {
    if (!m_playing) return;
    m_playing = false;
    if (!m_awaitingFrame) m_renderTimer->stop();
    emit playbackStateChanged(false);
}

void PlayerController::togglePlayPause() {
    if (m_playing) pause();
    else play();
}

void PlayerController::stop() {
    pause();
    seek(0.0);
}

void PlayerController::seek(double position) {
    if (!isOpen()) return;
    position = std::clamp(position, 0.0, std::max(0.0, m_info.duration));

    m_frameQueue.flush(++m_serial); // discard stale frames, wakes blocked push()
    m_decoder->seek(position, m_serial);

    m_position = position;
    restartClock(position);
    m_awaitingFrame = true;
    m_renderTimer->start();
    emit positionChanged(position);
}

void PlayerController::seekRelative(double delta) {
    seek(m_position + delta);
}

void PlayerController::stepForward() {
    if (!isOpen()) return;
    pause();
    if (m_awaitingFrame) return; // previous step/seek still in flight

    FrameQueue::Frame frame;
    if (m_frameQueue.popNext(frame)) {
        presentFrame(frame);
    } else if (!m_frameQueue.atEnd()) {
        m_awaitingFrame = true;
        m_renderTimer->start();
    }
}

void PlayerController::stepBackward() {
    if (!isOpen()) return;
    pause();
    // Decoding is frame accurate, so seeking one frame back lands exactly on
    // the previous frame.
    seek(m_position - 1.0 / m_info.fps);
}

void PlayerController::setSpeedMultiplier(double multiplier) {
    if (multiplier <= 0.0) return;
    // Rebase the clock so playback position is continuous across the change.
    if (m_playing) restartClock(clockPts());
    m_speedMultiplier = multiplier;
}

void PlayerController::setLoop(bool loop) {
    m_loop = loop;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void PlayerController::onRenderTimerTick() {
    FrameQueue::Frame frame;

    if (m_awaitingFrame) {
        // First frame after open/seek/step: show it as soon as it is decoded
        // and start the clock from it, so decoding latency causes no drops.
        if (m_frameQueue.popNext(frame)) {
            m_awaitingFrame = false;
            presentFrame(frame);
            restartClock(frame.pts);
        } else if (m_frameQueue.atEnd()) {
            m_awaitingFrame = false;
        }
        if (!m_awaitingFrame && !m_playing) m_renderTimer->stop();
        return;
    }

    if (!m_playing) {
        m_renderTimer->stop();
        return;
    }

    // Show the newest frame that is due; older ones are dropped (high-fps
    // footage at 1x on a 60 Hz screen, or a decoder that can't keep up).
    if (m_frameQueue.popUpTo(clockPts(), frame)) {
        presentFrame(frame);
    } else if (m_frameQueue.atEnd()) {
        if (m_loop) seek(0.0);
        else pause();
    }
}

void PlayerController::presentFrame(const FrameQueue::Frame& frame) {
    m_position = frame.pts;
    emit frameReady(frame.frame);
    emit positionChanged(frame.pts);
}

double PlayerController::clockPts() const {
    return m_clockBasePts + m_playClock.nsecsElapsed() * 1e-9 * m_speedMultiplier;
}

void PlayerController::restartClock(double pts) {
    m_clockBasePts = pts;
    m_playClock.restart();
}
