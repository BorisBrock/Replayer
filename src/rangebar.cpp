#include "rangebar.h"
#include "theme.h"
#include "timeformat.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kMargin    = 12.0; // room for the marker flags at both ends
constexpr double kFlagSize  = 8.0;
constexpr double kGrabRange = 7.0;  // pixels around a marker that pick it up

} // namespace

RangeBar::RangeBar(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(sizeHint().height());
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void RangeBar::setDuration(double duration, double fps) {
    m_duration = std::max(0.0, duration);
    if (fps > 0.0) m_fps = fps;
    selectAll();
}

void RangeBar::setPosition(double position) {
    m_position = position;
    update();
}

double RangeBar::lastFrame() const {
    return std::max(0.0, snap(m_duration - 1.0 / m_fps));
}

void RangeBar::setStart(double time) {
    m_start = std::clamp(snap(time), 0.0, std::max(0.0, m_end - 1.0 / m_fps));
    update();
    emit rangeChanged();
}

void RangeBar::setEnd(double time) {
    m_end = std::clamp(snap(time), std::min(lastFrame(), m_start + 1.0 / m_fps), lastFrame());
    update();
    emit rangeChanged();
}

void RangeBar::selectAll() {
    m_start = 0.0;
    m_end = lastFrame();
    update();
    emit rangeChanged();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QRectF RangeBar::trackRect() const {
    return QRectF(kMargin, kFlagSize + 6.0, width() - 2.0 * kMargin, height() - kFlagSize - 12.0);
}

double RangeBar::xOf(double time) const {
    const QRectF r = trackRect();
    return m_duration > 0.0 ? r.left() + time / m_duration * r.width() : r.left();
}

double RangeBar::timeAt(double x) const {
    const QRectF r = trackRect();
    return std::clamp((x - r.left()) / r.width(), 0.0, 1.0) * m_duration;
}

double RangeBar::snap(double time) const {
    return std::clamp(std::round(time * m_fps) / m_fps, 0.0, m_duration);
}

RangeBar::Handle RangeBar::handleAt(double x) const {
    const double toStart = std::abs(xOf(m_start) - x);
    const double toEnd   = std::abs(xOf(m_end) - x);
    if (std::min(toStart, toEnd) > kGrabRange) return Handle::None;
    // Markers on top of each other: take the one that can move that way
    if (toStart == toEnd) return x < xOf(m_start) ? Handle::Start : Handle::End;
    return toStart < toEnd ? Handle::Start : Handle::End;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void RangeBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();
    const QColor text   = palette().color(QPalette::Text);
    const QColor muted  = palette().color(QPalette::PlaceholderText);
    const QColor accent = Theme::accent();

    QPainterPath clip;
    clip.addRoundedRect(track, 6, 6);
    p.fillPath(clip, palette().color(QPalette::Base));
    if (m_duration <= 0.0) return;

    // The range in the accent colour, the length written on it
    const double x0 = xOf(m_start), x1 = xOf(m_end + 1.0 / m_fps);
    const QRectF range(QPointF(x0, track.top()), QPointF(std::min(x1, track.right()), track.bottom()));
    p.save();
    p.setClipPath(clip);
    QColor fill = accent;
    fill.setAlphaF(0.4);
    p.fillRect(range, fill);
    p.restore();
    const QString length = formatTime(m_end - m_start + 1.0 / m_fps);
    if (QFontMetrics(font()).horizontalAdvance(length) + 12 < range.width()) {
        p.setPen(text);
        p.drawText(range, Qt::AlignCenter, length);
    }

    // Playhead
    const double x = xOf(m_position);
    p.setPen(QPen(muted, 2.0, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(x, track.top() - 3.0), QPointF(x, track.bottom() + 3.0));

    // Markers: a line through the track with a flag above it, pointing into
    // the range
    for (const bool isStart : {true, false}) {
        const double mx = isStart ? x0 : xOf(m_end);
        const bool active = m_dragging == (isStart ? Handle::Start : Handle::End);
        const QColor color = active ? text : accent;
        p.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(mx, track.top() - 2.0), QPointF(mx, track.bottom()));
        const double dir = isStart ? 1.0 : -1.0;
        QPainterPath flag;
        flag.moveTo(mx, 1.0);
        flag.lineTo(mx + dir * kFlagSize * 1.3, 1.0 + kFlagSize / 2);
        flag.lineTo(mx, 1.0 + kFlagSize);
        flag.closeSubpath();
        p.fillPath(flag, color);
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void RangeBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_duration <= 0.0) return;
    const double x = event->position().x();
    m_dragging = handleAt(x);
    if (m_dragging == Handle::None) m_dragging = Handle::Playhead;
    switch (m_dragging) {
    case Handle::Start: emit seekRequested(m_start); break;
    case Handle::End:   emit seekRequested(m_end); break;
    default:            emit seekRequested(snap(timeAt(x))); break;
    }
    update();
}

void RangeBar::mouseMoveEvent(QMouseEvent* event) {
    const double x = event->position().x();
    const double time = snap(timeAt(x));
    switch (m_dragging) {
    case Handle::Start:
        if (time == m_start) return;
        setStart(time);
        emit seekRequested(m_start); // show the frame it is on
        return;
    case Handle::End:
        if (time == m_end) return;
        setEnd(time);
        emit seekRequested(m_end);
        return;
    case Handle::Playhead:
        emit seekRequested(time);
        return;
    case Handle::None:
        break;
    }
    const Handle handle = handleAt(x);
    setCursor(handle != Handle::None ? Qt::SizeHorCursor : Qt::PointingHandCursor);
    setToolTip(handle == Handle::Start ? QString("Start at %1, drag to move").arg(formatTime(m_start))
             : handle == Handle::End   ? QString("End at %1, drag to move").arg(formatTime(m_end))
                                       : QString("Click to seek"));
}

void RangeBar::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = Handle::None;
    update();
}
