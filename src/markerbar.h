#ifndef MARKERBAR_H
#define MARKERBAR_H

#include <QList>
#include <QWidget>
#include <utility>

// Timeline of a clip being split (see SplitDialog). Markers cut the clip
// into parts; each part is kept or skipped. Click to seek, drag a marker to
// move it, double-click a part to keep or skip it, right-click for a menu.
// Optionally shows how much motion there is over time, as a faint curve.
class MarkerBar : public QWidget {
    Q_OBJECT

public:
    explicit MarkerBar(QWidget* parent = nullptr);

    void setDuration(double duration, double fps);
    void setPosition(double position);
    // Motion per time (any scale; drawn relative to the largest value)
    void setActivity(const QList<std::pair<double, double>>& activity);

    // The parts kept, as (start, end) in seconds; the rest is skipped.
    QList<std::pair<double, double>> keptParts() const;
    void setKeptParts(const QList<std::pair<double, double>>& parts);
    bool hasMarkers() const { return !m_markers.isEmpty(); }
    int partCount() const { return static_cast<int>(m_markers.size()) + 1; }
    int keptCount() const;

public slots:
    void addMarker(double time);
    void addMarkerAtPosition() { addMarker(m_position); }
    void removeSelectedMarker();
    void clear();

signals:
    void seekRequested(double time);
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    QSize sizeHint() const override { return {400, 64}; }

private:
    QRectF trackRect() const;
    double xOf(double time) const;
    double timeAt(double x) const;
    double snap(double time) const; // to the nearest frame
    int markerAt(double x) const;   // within grabbing distance, or -1
    int partAt(double time) const;
    void removeMarker(int index);
    void togglePart(int part);

    double m_duration = 0.0;
    double m_fps      = 30.0;
    double m_position = 0.0;
    QList<double> m_markers;  // sorted, inside (0, duration)
    QList<bool>   m_skipped;  // per part: m_markers.size() + 1 entries
    QList<std::pair<double, double>> m_activity;
    double m_activityMax = 0.0;
    int  m_selected = -1;     // marker index
    int  m_dragging = -1;     // marker index
    bool m_seeking  = false;  // dragging the playhead
};

#endif
