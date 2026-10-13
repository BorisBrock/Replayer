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
#include "videoframe.h"
#include "viewzoom.h"

// Displays decoded YUV frames. Planes are uploaded as-is and converted to RGB
// in a fragment shader, which keeps the per-frame CPU cost to a memcpy.
// Drawings are painted on top with QPainter.
class VideoRenderer : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit VideoRenderer(QWidget* parent = nullptr);
    ~VideoRenderer() override;

    const ViewZoom& zoom() const { return m_zoom; }
    FramePtr frame() const { return m_frame; } // the frame on screen
    QSizeF displaySize() const; // video size in pixels after aspect ratio and rotation
    // frame at full size, rotated, with this clip's drawings as they look
    // unzoomed; null if there is no frame
    QImage renderImage(const FramePtr& frame) const;

public slots:
    void setFrame(const FramePtr& frame);
    void clearFrame();
    void setRotation(int degrees);
    void setZoom(const ViewZoom& zoom);
    void setAnnotations(AnnotationDoc* doc); // not owned
    void setDrawStyle(const DrawStyle& style);

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
    bool uploadFrame();
    QMatrix4x4 vertexTransform() const;
    QSizeF videoScale() const; // letterboxed video size as a fraction of the widget
    QPointF toVideo(QPointF widgetPos) const;
    QPointF toWidget(QPointF videoPos) const;
    double pixelLength(QPointF a, QPointF b) const; // in video pixels
    QSizeF videoArea() const; // letterboxed video size in widget pixels, unzoomed
    QTransform videoToWidget() const;
    // Drawings, mapped by toView into a view clipped to clip
    void paintAnnotations(QPainter& p, const QTransform& toView, const QRectF& clip, bool withDraft) const;
    void paintAnnotation(QPainter& p, const QTransform& toView, const Annotation& a,
                         const Annotation* reference) const;
    void updateCursor();
    void releaseGL();

    FramePtr m_frame;
    bool     m_frameDirty = false;
    int      m_rotation   = 0;
    ViewZoom m_zoom;
    bool     m_dragging   = false;
    QPointF  m_dragPos;

    QPointer<AnnotationDoc> m_annotations;
    DrawStyle  m_drawStyle;
    bool       m_drawing = false;
    Annotation m_draft; // while drawing

    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLBuffer m_vbo;
    GLuint m_textures[3] = {};
    QSize  m_texSize[3];
    GLint  m_texFormat[3] = {};

    // Shader parameters derived from the current frame
    QMatrix4x4 m_colorMatrix;
    float      m_sampleScale = 1.0f;
    bool       m_semiPlanar  = false;
};

#endif
