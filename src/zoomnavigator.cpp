#include "zoomnavigator.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

ZoomNavigator::ZoomNavigator(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateHeight();
    setCursor(Qt::PointingHandCursor);
    setToolTip("Click or drag to move the zoomed region; scroll to zoom");
}

void ZoomNavigator::setImage(const QImage& image) {
    m_image = image;
    if (!image.isNull()) m_aspect = static_cast<double>(image.width()) / image.height();
    updateHeight();
    update();
}

void ZoomNavigator::setAspectRatio(double aspect) {
    if (aspect > 0.0) m_aspect = aspect;
    updateHeight();
    update();
}

// The frame fills the panel's width; portrait clips are capped in height.
void ZoomNavigator::updateHeight() {
    const int h = std::clamp(static_cast<int>(std::lround(width() / m_aspect)) + 2, 60, 220);
    if (h != height()) setFixedHeight(h);
}

void ZoomNavigator::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateHeight();
}

void ZoomNavigator::setZoom(const ViewZoom& zoom) {
    m_zoom = zoom;
    update();
}

QRectF ZoomNavigator::frameRect() const {
    const QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
    QSizeF size(area.width(), area.width() / m_aspect);
    if (size.height() > area.height()) size = QSizeF(area.height() * m_aspect, area.height());
    QRectF r(QPointF(), size);
    r.moveCenter(area.center());
    return r;
}

QPointF ZoomNavigator::toNormalized(QPointF pos) const {
    const QRectF r = frameRect();
    return QPointF((pos.x() - r.left()) / r.width(), (pos.y() - r.top()) / r.height());
}

void ZoomNavigator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF frame = frameRect();

    QPainterPath clip;
    clip.addRoundedRect(frame, 6, 6);
    p.setClipPath(clip);
    if (m_image.isNull()) p.fillRect(frame, Qt::black);
    else p.drawImage(frame, m_image);

    // Region currently shown in the players
    const double size = 1.0 / m_zoom.zoom;
    const QRectF view(frame.left() + (m_zoom.center.x() - size / 2) * frame.width(),
                      frame.top() + (m_zoom.center.y() - size / 2) * frame.height(),
                      size * frame.width(), size * frame.height());
    if (m_zoom.isZoomed()) {
        QPainterPath outside;
        outside.addRect(frame);
        outside.addRect(view);
        p.fillPath(outside, QColor(0, 0, 0, 150));
    }
    p.setClipping(false);
    p.setPen(QPen(palette().color(QPalette::Highlight), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(view.adjusted(1, 1, -1, -1), 4, 4);
}

void ZoomNavigator::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) emit centerRequested(toNormalized(event->position()));
}

void ZoomNavigator::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) emit centerRequested(toNormalized(event->position()));
}

void ZoomNavigator::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() == 0) return;
    emit zoomAtRequested(std::pow(1.25, event->angleDelta().y() / 120.0), toNormalized(event->position()));
    event->accept();
}
