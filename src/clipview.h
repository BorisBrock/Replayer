#ifndef CLIPVIEW_H
#define CLIPVIEW_H

#include <QFrame>
#include <QPointer>

#include "annotations.h"
#include "overlay.h"
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

    // Opens a clip (a ClipRef key: a file, or a part of one) and resumes at
    // position (seconds from the clip's start; 0 = start).
    bool open(const QString& path, double position);
    QString path() const { return m_path; } // the ClipRef key

    PlayerController* controller() const { return m_controller; }
    TransportBar* transportBar() const { return m_transportBar; }
    FramePtr currentFrame() const;
    AnnotationDoc* annotations() const { return m_annotations; }
    // The video's size in pixels, after aspect ratio and rotation; empty
    // without a frame
    QSizeF displaySize() const;
    // The drawings as in this view, unzoomed, scaled to size (see VideoRenderer)
    QImage renderAnnotations(QSize size) const;
    // Opens the export dialog for this clip, at its current frame
    void showExportDialog();

    // Compact: tile look with a title header and smaller controls.
    void setCompact(bool compact);
    // Outlines the tile whose details the info panel shows.
    void setCurrent(bool current);
    void setZoom(const ViewZoom& zoom);
    // The drawings of the clip shown (not owned), and how new ones are drawn.
    void setAnnotations(AnnotationDoc* doc);
    void setDrawStyle(const DrawStyle& style);
    // Shows ghost's video on top of this clip's, as mode says; nullptr ends
    // it. See OverlayMode.
    void setGhost(ClipView* ghost, OverlayMode mode, bool horizontalSplit);
    // Leaves just the title and controls, for a clip shown as another
    // tile's ghost. Its player keeps running.
    void setVideoHidden(bool hidden);

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
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // Saves frame as it looks here, with the ghost over it
    void exportFrame(const FramePtr& frame, double position);
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
    QPointer<ClipView>      m_ghost;
    QMetaObject::Connection m_ghostConnection;
};

#endif
