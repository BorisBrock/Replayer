#ifndef VIDEORENDERER_H
#define VIDEORENDERER_H

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPointer>
#include <QSize>
#include <QTransform>

#include "annotations.h"
#include "overlay.h"
#include "videoframe.h"
#include "viewzoom.h"

// Displays decoded YUV frames. Planes are uploaded as-is and converted to RGB
// in a fragment shader, which keeps the per-frame CPU cost to a memcpy.
// Drawings are painted on top with QPainter. A second clip's frames (the
// ghost) can be blended over or split against the clip's own, see overlay.h.
class VideoRenderer : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit VideoRenderer(QWidget* parent = nullptr);
    ~VideoRenderer() override;

    const ViewZoom& zoom() const { return m_zoom; }
    FramePtr frame() const { return m_main.frame; } // the frame on screen
    int rotation() const { return m_main.rotation; }
    QSizeF displaySize() const { return displaySize(m_main); } // video size in pixels after aspect ratio and rotation
    // frame at full size, rotated, with the ghost over it as on screen and
    // this clip's drawings as they look unzoomed; null if there is no frame
    QImage renderImage(const FramePtr& frame) const;

public slots:
    void setFrame(const FramePtr& frame);
    void clearFrame();
    void setRotation(int degrees);
    void setZoom(const ViewZoom& zoom);
    void setAnnotations(AnnotationDoc* doc); // not owned
    void setDrawStyle(const DrawStyle& style);
    // The other clip's frame on screen, shown as setOverlayMode() says
    void setGhostFrame(const FramePtr& frame, int rotation);
    // Turning the overlay on puts the line in the middle; switching between
    // modes keeps it where it is. horizontalSplit: Split mode only, the line
    // runs across with the first clip above it.
    void setOverlayMode(OverlayMode mode, bool horizontalSplit);

signals:
    void doubleClicked();
    // Interaction on the video, in normalised video coordinates. The owner
    // applies them (zoom is shared between clips) and calls setZoom().
    void zoomAtRequested(double factor, QPointF point);
    void panRequested(QPointF delta);
    // A drawing made with the mouse, for the owner to add to the clip.
    void annotationDrawn(const Annotation& annotation);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // A frame and its GL textures: the clip's own, or the ghost's
    struct Layer {
        FramePtr frame;
        bool     dirty    = false;
        int      rotation = 0;
        GLuint   textures[3] = {};
        QSize    texSize[3];
        GLint    texFormat[3] = {};

        // Shader parameters derived from the frame
        QMatrix4x4 colorMatrix;
        float      sampleScale = 1.0f;
        bool       semiPlanar  = false;
    };

    bool uploadFrame(Layer& layer);
    void drawLayer(const Layer& layer, const QRect& scissor, float opacity);
    QSizeF displaySize(const Layer& layer) const;
    QMatrix4x4 vertexTransform(const Layer& layer) const;
    QSizeF videoScale() const { return videoScale(m_main); }
    QSizeF videoScale(const Layer& layer) const; // letterboxed video size as a fraction of the widget
    QPointF toVideo(QPointF widgetPos) const;
    QPointF toWidget(QPointF videoPos) const;
    double pixelLength(QPointF a, QPointF b) const; // in video pixels
    QSizeF videoArea() const; // letterboxed video size in widget pixels, unzoomed
    QRectF videoRect() const; // the same, centred in the widget
    QTransform videoToWidget() const;
    bool overlayShown() const { return m_overlay != OverlayMode::None && m_ghost.frame; }
    bool onSplitLine(QPointF widgetPos) const;
    void setSplitAt(QPointF widgetPos);
    void paintSplitLine(QPainter& p) const;
    // Drawings, mapped by toView into a view clipped to clip
    void paintAnnotations(QPainter& p, const QTransform& toView, const QRectF& clip, bool withDraft) const;
    void paintAnnotation(QPainter& p, const QTransform& toView, const Annotation& a,
                         const Annotation* reference) const;
    void updateCursor();
    void releaseGL();

    Layer    m_main;
    Layer    m_ghost;
    ViewZoom m_zoom;
    bool     m_dragging   = false;
    QPointF  m_dragPos;

    QPointer<AnnotationDoc> m_annotations;
    DrawStyle  m_drawStyle;
    bool       m_drawing = false;
    Annotation m_draft; // while drawing

    OverlayMode m_overlay         = OverlayMode::None;
    bool        m_horizontalSplit = false;
    double      m_split           = 0.5; // line position across the video, 0..1
    bool        m_splitDragging   = false;
    bool        m_splitHovered    = false;

    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLBuffer m_vbo;
};

#endif
