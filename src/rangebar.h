#ifndef RANGEBAR_H
#define RANGEBAR_H

#include <QWidget>

// Timeline of a clip being exported (see ExportDialog), with a start and an
// end marker around the part to export. Click to seek, drag a marker to move
// it; the frames at both markers are part of the range.
class RangeBar : public QWidget {
    Q_OBJECT

public:
    explicit RangeBar(QWidget* parent = nullptr);

    void setDuration(double duration, double fps); // resets the range to the whole clip
    void setPosition(double position);
    double start() const { return m_start; }
    double end() const { return m_end; }
    double lastFrame() const; // where the end marker goes for the whole clip

public slots:
    void setStart(double time); // keeps at least one frame to the end marker
    void setEnd(double time);
    void setStartAtPosition() { setStart(m_position); }
    void setEndAtPosition() { setEnd(m_position); }
    void selectAll();

signals:
    void seekRequested(double time);
    void rangeChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return {400, 52}; }

private:
    enum class Handle { None, Start, End, Playhead };

    QRectF trackRect() const;
    double xOf(double time) const;
    double timeAt(double x) const;
    double snap(double time) const; // to the nearest frame
    Handle handleAt(double x) const;

    double m_duration = 0.0;
    double m_fps      = 30.0;
    double m_position = 0.0;
    double m_start    = 0.0;
    double m_end      = 0.0;
    Handle m_dragging = Handle::None;
};

#endif
