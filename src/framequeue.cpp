#include "framequeue.h"

#include <algorithm>

bool FrameQueue::push(Frame frame) {
    QMutexLocker locker(&m_mutex);
    while (static_cast<int>(m_queue.size()) >= m_capacity && !m_closed
           && frame.serial == m_serial) {
        m_notFull.wait(&m_mutex);
    }
    if (m_closed) return false;
    if (frame.serial == m_serial) {
        m_queue.push_back(std::move(frame));
    }
    return true;
}

void FrameQueue::setCapacity(int capacity) {
    QMutexLocker locker(&m_mutex);
    m_capacity = std::max(2, capacity);
    m_notFull.wakeAll();
}

bool FrameQueue::popUpTo(double targetPts, Frame& out) {
    QMutexLocker locker(&m_mutex);
    bool found = false;
    while (!m_queue.empty() && m_queue.front().pts <= targetPts) {
        out = std::move(m_queue.front());
        m_queue.pop_front();
        found = true;
    }
    if (found) m_notFull.wakeAll();
    return found;
}

bool FrameQueue::popNext(Frame& out) {
    QMutexLocker locker(&m_mutex);
    if (m_queue.empty()) return false;
    out = std::move(m_queue.front());
    m_queue.pop_front();
    m_notFull.wakeAll();
    return true;
}

void FrameQueue::flush(int serial) {
    QMutexLocker locker(&m_mutex);
    m_queue.clear();
    m_serial = serial;
    m_eos = false;
    m_notFull.wakeAll();
}

void FrameQueue::setEndOfStream(int serial) {
    QMutexLocker locker(&m_mutex);
    if (serial == m_serial) m_eos = true;
}

bool FrameQueue::atEnd() const {
    QMutexLocker locker(&m_mutex);
    return m_eos && m_queue.empty();
}

void FrameQueue::close() {
    QMutexLocker locker(&m_mutex);
    m_closed = true;
    m_notFull.wakeAll();
}

void FrameQueue::reopen() {
    QMutexLocker locker(&m_mutex);
    m_closed = false;
    m_eos = false;
    m_serial = 0;
    m_queue.clear();
}
