#ifndef FRAMEQUEUE_H
#define FRAMEQUEUE_H

#include <QMutex>
#include <QWaitCondition>
#include <deque>

#include "videoframe.h"

// Thread-safe bounded queue that decouples the decoder thread (producer)
// from the render timer (consumer).
//
// Every seek bumps a serial number. Frames carry the serial they were decoded
// under, and frames from an older serial are discarded on push, so a frame the
// decoder was holding while a seek arrived can never reach the screen.
class FrameQueue {
public:
    struct Frame {
        FramePtr frame;
        double   pts    = 0.0; // seconds since stream start
        int      serial = 0;
    };

    // Default maximum number of decoded frames held in memory at once. A 4K
    // frame is ~12 MB in NV12 (8-bit) and ~24 MB in P010 (10-bit), so this
    // caps the queue at ~200 MB, or ~400 MB for 10-bit footage.
    static constexpr int DEFAULT_CAPACITY = 16;

    // Changes the maximum; e.g. lowered when several clips play side by side.
    void setCapacity(int capacity);

    // Push a frame. Blocks when the queue is full. Returns false if the queue
    // has been closed (shutdown signal), in which case the caller should stop.
    bool push(Frame frame);

    // Pop all frames whose pts <= targetPts and return the most recent one in
    // out. Older frames are discarded (frame-drop for high-fps / high-speed).
    // Non-blocking. Returns false when no eligible frame exists yet.
    bool popUpTo(double targetPts, Frame& out);

    // Pop the oldest frame regardless of its pts. Non-blocking.
    bool popNext(Frame& out);

    // Discard all queued frames, start accepting frames of the given serial
    // only, and wake a blocked push().
    void flush(int serial);

    // Called by the decoder once the last frame of serial has been pushed.
    void setEndOfStream(int serial);

    // True when the decoder has reached the end and every frame was consumed.
    bool atEnd() const;

    // Mark the queue as closed; any blocked push() will return false.
    // Call this before waiting for the decoder thread to exit.
    void close();

    // Reset to the open/empty state for reuse (call after the decoder thread
    // has exited and before starting a new one).
    void reopen();

private:
    mutable QMutex    m_mutex;
    QWaitCondition    m_notFull;
    std::deque<Frame> m_queue;
    int               m_capacity = DEFAULT_CAPACITY;
    int               m_serial = 0;
    bool              m_closed = false;
    bool              m_eos    = false;
};

#endif // FRAMEQUEUE_H
