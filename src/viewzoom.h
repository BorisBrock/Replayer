#ifndef VIEWZOOM_H
#define VIEWZOOM_H

#include <QPointF>
#include <algorithm>

// Which part of the video is shown: a zoom factor and the visible centre in
// normalised display coordinates (0..1, y down, after rotation). Normalised
// coordinates make one zoom apply equally to clips of any resolution.
struct ViewZoom {
    static constexpr double kMaxZoom = 8.0;

    double  zoom   = 1.0;
    QPointF center = {0.5, 0.5};

    bool isZoomed() const { return zoom > 1.0001; }

    // Zoom by factor while keeping point (normalised) at the same place on screen.
    void zoomAt(double factor, QPointF point) {
        const double newZoom = std::clamp(zoom * factor, 1.0, kMaxZoom);
        center = point - (point - center) * (zoom / newZoom);
        zoom = newZoom;
        clamp();
    }

    void setZoom(double newZoom) { zoomAt(newZoom / zoom, center); }

    void setCenter(QPointF point) {
        center = point;
        clamp();
    }

    void panBy(QPointF delta) { setCenter(center + delta); }

    // Keep the visible region inside the video.
    void clamp() {
        const double half = 0.5 / zoom;
        center.setX(std::clamp(center.x(), half, 1.0 - half));
        center.setY(std::clamp(center.y(), half, 1.0 - half));
    }

    bool operator==(const ViewZoom& other) const {
        return zoom == other.zoom && center == other.center;
    }
};

#endif
