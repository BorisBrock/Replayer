#ifndef TRIMSLIDER_H
#define TRIMSLIDER_H

#include <QSlider>

// The transport bar's seek bar (in milliseconds), with a start and an end
// marker that cut off the beginning and end of the clip. The markers sit at
// the clip's ends until dragged; they snap to frames and never cross.
class TrimSlider : public QSlider {
    Q_OBJECT

public:
    explicit TrimSlider(QWidget* parent = nullptr);

    void setFrameRate(double fps) { m_fps = fps; }
    void setTrim(int startMs, int endMs);
    int trimStart() const { return m_start; }
    int trimEnd() const { return m_end; }

signals:
    // The user moved a marker; moved is the new time of the one dragged.
    void trimRequested(int startMs, int endMs, int movedMs);
    void trimResetRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    enum Marker { None, Start, End };

    double xOf(int ms) const;
    int valueAt(double x) const; // snapped to a frame
    Marker markerAt(const QPointF& pos) const;
    bool markersShown() const { return isEnabled() && maximum() > minimum(); }
    void moveMarker(Marker marker, int ms);

    double m_fps      = 0.0;
    int    m_start    = 0;
    int    m_end      = 0;
    Marker m_dragging = None;
};

#endif
