#include "markerbar.h"
#include "theme.h"
#include "timeformat.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kMargin    = 10.0; // room for the marker heads at both ends
constexpr double kHeadSize  = 7.0;
constexpr double kGrabRange = 6.0;  // pixels around a marker that pick it up

} // namespace

MarkerBar::MarkerBar(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(sizeHint().height());
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    m_skipped = {false};
}

void MarkerBar::setDuration(double duration, double fps) {
    m_duration = std::max(0.0, duration);
    if (fps > 0.0) m_fps = fps;
    clear();
}

void MarkerBar::setPosition(double position) {
    m_position = position;
    update();
}

void MarkerBar::setActivity(const QList<std::pair<double, double>>& activity) {
    m_activity = activity;
    m_activityMax = 0.0;
    for (const auto& [time, value] : activity) m_activityMax = std::max(m_activityMax, value);
    update();
}

QList<std::pair<double, double>> MarkerBar::keptParts() const {
    QList<double> bounds = m_markers;
    bounds.prepend(0.0);
    bounds.append(m_duration);
    QList<std::pair<double, double>> parts;
    for (int i = 0; i + 1 < bounds.size(); i++) {
        if (!m_skipped[i]) parts.append({bounds[i], bounds[i + 1]});
    }
    return parts;
}

void MarkerBar::setKeptParts(const QList<std::pair<double, double>>& parts) {
    const double halfFrame = 0.5 / m_fps;
    m_markers.clear();
    for (const auto& [start, end] : parts) {
        for (double bound : {start, end}) {
            bound = snap(bound);
            if (bound < halfFrame || bound > m_duration - halfFrame) continue;
            const bool known = std::any_of(m_markers.begin(), m_markers.end(),
                                           [&](double m) { return std::abs(m - bound) < halfFrame; });
            if (!known) m_markers.append(bound);
        }
    }
    std::sort(m_markers.begin(), m_markers.end());

    // A part is kept if a kept range covers its middle.
    m_skipped.clear();
    for (int i = 0; i <= m_markers.size(); i++) {
        const double from = i > 0 ? m_markers[i - 1] : 0.0;
        const double to   = i < m_markers.size() ? m_markers[i] : m_duration;
        const double mid  = (from + to) / 2.0;
        const bool kept = std::any_of(parts.begin(), parts.end(), [&](const auto& part) {
            return part.first <= mid && mid <= part.second;
        });
        m_skipped.append(!kept);
    }
    m_selected = -1;
    update();
    emit changed();
}

int MarkerBar::keptCount() const {
    return static_cast<int>(std::count(m_skipped.begin(), m_skipped.end(), false));
}

void MarkerBar::addMarker(double time) {
    if (m_duration <= 0.0) return;
    const double halfFrame = 0.5 / m_fps;
    time = snap(time);
    if (time < halfFrame || time > m_duration - halfFrame) return; // no empty part
    const auto it = std::lower_bound(m_markers.begin(), m_markers.end(), time - halfFrame);
    if (it != m_markers.end() && *it < time + halfFrame) return; // already there
    const int index = static_cast<int>(it - m_markers.begin());
    m_markers.insert(index, time);
    // Both halves of the part cut keep its state.
    m_skipped.insert(index, m_skipped[index]);
    m_selected = index;
    update();
    emit changed();
}

void MarkerBar::removeSelectedMarker() {
    if (m_selected >= 0) removeMarker(m_selected);
}

void MarkerBar::removeMarker(int index) {
    if (index < 0 || index >= m_markers.size()) return;
    // The joined part is kept if either side was.
    m_skipped[index] = m_skipped[index] && m_skipped[index + 1];
    m_skipped.removeAt(index + 1);
    m_markers.removeAt(index);
    m_selected = -1;
    m_dragging = -1;
    update();
    emit changed();
}

void MarkerBar::clear() {
    m_markers.clear();
    m_skipped = {false};
    m_selected = -1;
    m_dragging = -1;
    update();
    emit changed();
}

void MarkerBar::togglePart(int part) {
    if (part < 0 || part >= m_skipped.size()) return;
    m_skipped[part] = !m_skipped[part];
    update();
    emit changed();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QRectF MarkerBar::trackRect() const {
    return QRectF(kMargin, kHeadSize + 6.0, width() - 2.0 * kMargin, height() - kHeadSize - 12.0);
}

double MarkerBar::xOf(double time) const {
    const QRectF r = trackRect();
    return m_duration > 0.0 ? r.left() + time / m_duration * r.width() : r.left();
}

double MarkerBar::timeAt(double x) const {
    const QRectF r = trackRect();
    return std::clamp((x - r.left()) / r.width(), 0.0, 1.0) * m_duration;
}

double MarkerBar::snap(double time) const {
    return std::clamp(std::round(time * m_fps) / m_fps, 0.0, m_duration);
}

int MarkerBar::markerAt(double x) const {
    int best = -1;
    double bestDistance = kGrabRange;
    for (int i = 0; i < m_markers.size(); i++) {
        const double distance = std::abs(xOf(m_markers[i]) - x);
        if (distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

int MarkerBar::partAt(double time) const {
    return static_cast<int>(std::upper_bound(m_markers.begin(), m_markers.end(), time) - m_markers.begin());
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void MarkerBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();
    const QColor text  = palette().color(QPalette::Text);
    QColor muted = palette().color(QPalette::PlaceholderText);
    const QColor accent = Theme::accent();

    QPainterPath clip;
    clip.addRoundedRect(track, 6, 6);
    p.fillPath(clip, palette().color(QPalette::Base));
    p.save();
    p.setClipPath(clip);

    // Parts: kept ones in alternating accent shades, skipped ones hatched
    QList<double> bounds = m_markers;
    bounds.prepend(0.0);
    bounds.append(m_duration);
    int number = 0;
    for (int i = 0; i + 1 < bounds.size() && m_duration > 0.0; i++) {
        const QRectF r(QPointF(xOf(bounds[i]), track.top()), QPointF(xOf(bounds[i + 1]), track.bottom()));
        QString label;
        if (m_skipped[i]) {
            QColor hatch = muted;
            hatch.setAlphaF(0.35);
            p.fillRect(r, QBrush(hatch, Qt::BDiagPattern));
            label = "skipped";
        } else {
            QColor fill = accent;
            fill.setAlphaF(++number % 2 ? 0.45 : 0.30);
            p.fillRect(r, fill);
            label = QString::number(number);
        }
        if (QFontMetrics(font()).horizontalAdvance(label) + 8 < r.width()) {
            p.setPen(m_skipped[i] ? muted : text);
            p.drawText(r, Qt::AlignCenter, label);
        }
    }

    // Motion over time, along the bottom of the track
    if (m_activity.size() > 1 && m_activityMax > 0.0 && m_duration > 0.0) {
        QPainterPath curve;
        curve.moveTo(xOf(m_activity.first().first), track.bottom());
        for (const auto& [time, value] : m_activity) {
            curve.lineTo(xOf(time), track.bottom() - value / m_activityMax * track.height() * 0.8);
        }
        curve.lineTo(xOf(m_activity.last().first), track.bottom());
        curve.closeSubpath();
        QColor fill = text;
        fill.setAlphaF(0.18);
        p.fillPath(curve, fill);
    }
    p.restore();

    // Playhead
    if (m_duration > 0.0) {
        const double x = xOf(m_position);
        p.setPen(QPen(text, 2.0, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(x, track.top() - 3.0), QPointF(x, track.bottom() + 3.0));
    }

    // Markers: a line through the track with a head above it
    for (int i = 0; i < m_markers.size(); i++) {
        const double x = xOf(m_markers[i]);
        const bool selected = i == m_selected;
        const QColor color = selected ? text : accent;
        p.setPen(QPen(color, selected ? 3.0 : 2.0, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(x, track.top()), QPointF(x, track.bottom()));
        QPainterPath head;
        head.moveTo(x - kHeadSize, 1.0);
        head.lineTo(x + kHeadSize, 1.0);
        head.lineTo(x, kHeadSize + 5.0);
        head.closeSubpath();
        p.fillPath(head, color);
    }

    if (hasFocus()) {
        muted.setAlphaF(0.5);
        p.setPen(QPen(muted, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(track.adjusted(-1, -1, 1, 1), 7, 7);
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void MarkerBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || m_duration <= 0.0) return;
    const double x = event->position().x();
    const int marker = markerAt(x);
    m_selected = marker;
    if (marker >= 0) {
        m_dragging = marker;
        emit seekRequested(m_markers[marker]);
    } else {
        m_seeking = true;
        emit seekRequested(snap(timeAt(x)));
    }
    update();
}

void MarkerBar::mouseMoveEvent(QMouseEvent* event) {
    const double x = event->position().x();
    if (m_dragging >= 0) {
        // A marker stays at least one frame away from its neighbours.
        const double frame = 1.0 / m_fps;
        const double low  = (m_dragging > 0 ? m_markers[m_dragging - 1] : 0.0) + frame;
        const double high = (m_dragging + 1 < m_markers.size() ? m_markers[m_dragging + 1] : m_duration) - frame;
        if (low > high) return;
        const double time = std::clamp(snap(timeAt(x)), low, high);
        if (time != m_markers[m_dragging]) {
            m_markers[m_dragging] = time;
            update();
            emit seekRequested(time); // show the frame it is on
            emit changed();
        }
        return;
    }
    if (m_seeking) {
        emit seekRequested(snap(timeAt(x)));
        return;
    }
    const int marker = markerAt(x);
    setCursor(marker >= 0 ? Qt::SizeHorCursor : Qt::PointingHandCursor);
    setToolTip(marker >= 0 ? QString("Marker at %1, drag to move").arg(formatTime(m_markers[marker]))
                           : "Click to seek, double-click a part to keep or skip it");
}

void MarkerBar::mouseReleaseEvent(QMouseEvent*) {
    m_dragging = -1;
    m_seeking = false;
}

void MarkerBar::mouseDoubleClickEvent(QMouseEvent* event) {
    const double x = event->position().x();
    if (event->button() != Qt::LeftButton || markerAt(x) >= 0) return;
    togglePart(partAt(timeAt(x)));
}

void MarkerBar::contextMenuEvent(QContextMenuEvent* event) {
    if (m_duration <= 0.0) return;
    const double x = event->pos().x();
    const double time = snap(timeAt(x));
    const int marker = markerAt(x);
    const int part = partAt(time);

    QMenu menu(this);
    if (marker >= 0) {
        menu.addAction("Remove Marker", this, [this, marker] { removeMarker(marker); });
    } else {
        menu.addAction(QString("Add Marker at %1").arg(formatTime(time)), this, [this, time] { addMarker(time); });
        menu.addAction(m_skipped[part] ? "Keep This Part" : "Skip This Part", this,
                       [this, part] { togglePart(part); });
    }
    menu.exec(event->globalPos());
}

void MarkerBar::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_selected >= 0) {
        removeSelectedMarker();
        return;
    }
    QWidget::keyPressEvent(event);
}
