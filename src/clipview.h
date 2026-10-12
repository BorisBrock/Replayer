#ifndef CLIPVIEW_H
#define CLIPVIEW_H

#include <QFrame>

#include "annotations.h"
#include "videoframe.h"
#include "viewzoom.h"

class PlayerController;
class QLabel;
class QToolButton;
class TransportBar;
class VideoRenderer;

// One clip with its own player, video and transport controls. The main window
// shows one of these normally, and several in a grid when comparing clips.
class ClipView : public QFrame {
    Q_OBJECT

public:
    explicit ClipView(QWidget* parent = nullptr);
    ~ClipView() override;

    // Opens path and resumes at position (seconds; 0 = start).
    bool open(const QString& path, double position);
    QString path() const { return m_path; }

    PlayerController* controller() const { return m_controller; }
    TransportBar* transportBar() const { return m_transportBar; }
    FramePtr currentFrame() const;

    // Compact: tile look with a title header and smaller controls.
    void setCompact(bool compact);
    // Outlines the tile whose details the info panel shows.
    void setCurrent(bool current);
    void setZoom(const ViewZoom& zoom);
    // The drawings of the clip shown (not owned), and how new ones are drawn.
    void setAnnotations(AnnotationDoc* doc);
    void setDrawStyle(const DrawStyle& style);

signals:
    void activated();          // clicked anywhere on the tile
    void closeRequested();     // header close button
    void doubleClicked();      // on the video
    void zoomAtRequested(double factor, QPointF point); // wheel on the video
    void panRequested(QPointF delta);                   // drag on the video
    void playRequested(bool play); // the user pressed play/pause/stop/step here
    void videoInfoReady(const VideoInfo& info);
    void errorOccurred(const QString& error);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setStyleProperty(const char* name, bool value);
    void addAnnotation(Annotation annotation);

    PlayerController* m_controller;
    VideoRenderer*    m_videoRenderer;
    TransportBar*     m_transportBar;
    QWidget*          m_header;
    QLabel*           m_titleLabel;
    QString           m_path;
    AnnotationDoc*    m_annotations = nullptr;
    int               m_pendingRotation = -1;
};

#endif
