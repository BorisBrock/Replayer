#include "trimslider.h"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kGrab        = 6.0;  // pixels either side of a marker
constexpr double kHalfHeight  = 12.0; // markers reach above and below the handle
constexpr double kSerif       = 5.0;  // the bracket's feet, pointing into the clip
const QColor kMarker("#e4e6eb");
const QColor kDimmed(20, 21, 24, 170); // over the track outside the markers

} // namespace

TrimSlider::TrimSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
    setMouseTracking(true); // cursor feedback over the markers
}

void TrimSlider::setTrim(int startMs, int endMs) {
    if (startMs == m_start && endMs == m_end) return;
    m_start = startMs;
    m_end   = endMs;
    update();
}

double TrimSlider::xOf(int ms) const {
    // Where the handle's centre is for that value
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
    const double span  = groove.width() - handle.width();
    const double range = maximum() - minimum();
    const double fraction = range > 0 ? (ms - minimum()) / range : 0.0;
    return groove.x() + handle.width() / 2.0 + fraction * span;
}

int TrimSlider::valueAt(double x) const {
    const double left  = xOf(minimum());
    const double right = xOf(maximum());
    const double fraction = right > left ? std::clamp((x - left) / (right - left), 0.0, 1.0) : 0.0;
    double ms = minimum() + fraction * (maximum() - minimum());
    if (m_fps > 0.0) ms = std::round(ms / 1000.0 * m_fps) / m_fps * 1000.0;
    return std::clamp(static_cast<int>(std::lround(ms)), minimum(), maximum());
}

TrimSlider::Marker TrimSlider::markerAt(const QPointF& pos) const {
    if (!markersShown()) return None;
    const double startDistance = std::abs(pos.x() - xOf(m_start));
    const double endDistance   = std::abs(pos.x() - xOf(m_end));
    if (std::min(startDistance, endDistance) > kGrab) return None;

    // Above and below the track, the markers are reachable even under the
    // handle; on the track, the handle wins.
    const double cy = height() / 2.0;
    const bool onTrack = std::abs(pos.y() - cy) < kHalfHeight - kSerif;
    if (onTrack) {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
        if (handle.contains(pos.toPoint())) return None;
    }
    // Both under the pointer (very short clip): the one on the pointer's side
    if (startDistance == endDistance) return pos.x() < xOf(m_start) ? Start : End;
    return startDistance < endDistance ? Start : End;
}

void TrimSlider::moveMarker(Marker marker, int ms) {
    // Keep at least two frames between the markers.
    const int gap = m_fps > 0.0 ? static_cast<int>(std::ceil(2000.0 / m_fps)) : 1;
    int start = m_start;
    int end   = m_end;
    if (marker == Start) start = std::clamp(ms, minimum(), std::max(minimum(), end - gap));
    else end = std::clamp(ms, std::min(maximum(), start + gap), maximum());
    if (start == m_start && end == m_end) return;
    setTrim(start, end);
    emit trimRequested(start, end, marker == Start ? start : end);
}

void TrimSlider::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    // The slider first, then what is cut off and the markers, then the handle
    // again on top. (The stylesheet draws the coloured part of the track
    // only together with the handle.)
    opt.subControls = QStyle::SC_SliderGroove | QStyle::SC_SliderHandle;
    style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);

    if (markersShown()) {
        p.setRenderHint(QPainter::Antialiasing);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const double cy = height() / 2.0;
        const double startX = xOf(m_start);
        const double endX   = xOf(m_end);

        // The track is 6 px high and rounded; cover a little more.
        const QRectF track(groove.left(), cy - 4.0, groove.width(), 8.0);
        p.setPen(Qt::NoPen);
        p.setBrush(kDimmed);
        if (m_start > minimum()) p.drawRect(QRectF(QPointF(track.left(), track.top()), QPointF(startX, track.bottom())));
        if (m_end < maximum()) p.drawRect(QRectF(QPointF(endX, track.top()), QPointF(track.right(), track.bottom())));

        // Brackets: [ at the start, ] at the end
        auto bracket = [&](double x, double inward) {
            QPainterPath path;
            path.moveTo(x + inward * kSerif, cy - kHalfHeight);
            path.lineTo(x, cy - kHalfHeight);
            path.lineTo(x, cy + kHalfHeight);
            path.lineTo(x + inward * kSerif, cy + kHalfHeight);
            p.strokePath(path, QPen(kMarker, 2.0, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        };
        bracket(startX, 1.0);
        bracket(endX, -1.0);
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    opt.subControls = QStyle::SC_SliderHandle;
    style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);
}

void TrimSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = markerAt(event->position());
        if (m_dragging != None) {
            event->accept();
            return;
        }
    }
    QSlider::mousePressEvent(event);
}

void TrimSlider::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging != None) {
        moveMarker(m_dragging, valueAt(event->position().x()));
        event->accept();
        return;
    }
    if (event->buttons() == Qt::NoButton) {
        if (markerAt(event->position()) != None) setCursor(Qt::SizeHorCursor);
        else unsetCursor();
    }
    QSlider::mouseMoveEvent(event);
}

void TrimSlider::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragging != None && event->button() == Qt::LeftButton) {
        m_dragging = None;
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

void TrimSlider::contextMenuEvent(QContextMenuEvent* event) {
    if (!markersShown()) return;
    const int ms = valueAt(event->pos().x());
    QMenu menu(this);
    QAction* start = menu.addAction("Set Start Marker Here");
    QAction* end   = menu.addAction("Set End Marker Here");
    menu.addSeparator();
    QAction* reset = menu.addAction("Reset Markers");
    start->setEnabled(ms < m_end);
    end->setEnabled(ms > m_start);
    reset->setEnabled(m_start > minimum() || m_end < maximum());
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == start) moveMarker(Start, ms);
    else if (chosen == end) moveMarker(End, ms);
    else if (chosen == reset) emit trimResetRequested();
}
