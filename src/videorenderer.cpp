#include "videorenderer.h"
#include "timeformat.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <cmath>
#include <QOpenGLContext>
#include <algorithm>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

// GL 3.0 / ARB_texture_rg enums; not every platform header declares them.
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_R16
#define GL_R16 0x822A
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RG16
#define GL_RG16 0x822C
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

namespace {

const char* const kVertexShader =
    "attribute vec2 position;\n"
    "attribute vec2 texCoord;\n"
    "uniform mat4 transform;\n"
    "varying vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = transform * vec4(position, 0.0, 1.0);\n"
    "    v_texCoord = texCoord;\n"
    "}\n";

// Planar formats use one texture per plane; semi-planar (NV12/P010) store
// interleaved chroma in the .rg channels of the second texture.
const char* const kFragmentShader =
    "#ifdef GL_ES\n"
    "precision highp float;\n"
    "#endif\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D texY;\n"
    "uniform sampler2D texU;\n"
    "uniform sampler2D texV;\n"
    "uniform int semiPlanar;\n"
    "uniform float sampleScale;\n"
    "uniform mat4 colorMatrix;\n"
    "uniform float opacity;\n"
    "void main() {\n"
    "    vec3 yuv;\n"
    "    yuv.x = texture2D(texY, v_texCoord).r;\n"
    "    if (semiPlanar != 0)\n"
    "        yuv.yz = texture2D(texU, v_texCoord).rg;\n"
    "    else\n"
    "        yuv.yz = vec2(texture2D(texU, v_texCoord).r, texture2D(texV, v_texCoord).r);\n"
    "    gl_FragColor = vec4((colorMatrix * vec4(yuv * sampleScale, 1.0)).rgb, opacity);\n"
    "}\n";

// Fullscreen quad: x, y, s, t
const float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,
};

bool isFullRangeFormat(int format) {
    return format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_YUVJ422P
        || format == AV_PIX_FMT_YUVJ444P;
}

// Maps normalized (y, u, v, 1) to (r, g, b, 1) for the frame's colour space.
QMatrix4x4 yuvToRgbMatrix(const AVFrame* frame) {
    float kr, kb;
    switch (frame->colorspace) {
    case AVCOL_SPC_BT709:
        kr = 0.2126f; kb = 0.0722f; break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        kr = 0.2627f; kb = 0.0593f; break;
    case AVCOL_SPC_SMPTE240M:
        kr = 0.212f;  kb = 0.087f;  break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_FCC:
        kr = 0.299f;  kb = 0.114f;  break;
    default:
        // Untagged: HD and larger is BT.709 in practice, SD is BT.601.
        if (frame->height >= 720) { kr = 0.2126f; kb = 0.0722f; }
        else                      { kr = 0.299f;  kb = 0.114f;  }
        break;
    }
    const float kg = 1.0f - kr - kb;

    const bool  full = frame->color_range == AVCOL_RANGE_JPEG || isFullRangeFormat(frame->format);
    const float ys = full ? 1.0f : 255.0f / 219.0f;
    const float yo = full ? 0.0f : 16.0f / 255.0f;
    const float cs = full ? 1.0f : 255.0f / 224.0f;
    const float co = 128.0f / 255.0f;

    const float rv = 2.0f * (1.0f - kr) * cs;
    const float gu = -2.0f * kb * (1.0f - kb) / kg * cs;
    const float gv = -2.0f * kr * (1.0f - kr) / kg * cs;
    const float bu = 2.0f * (1.0f - kb) * cs;

    return QMatrix4x4(ys, 0.0f, rv,   -(ys * yo + rv * co),
                      ys, gu,   gv,   -(ys * yo + (gu + gv) * co),
                      ys, bu,   0.0f, -(ys * yo + bu * co),
                      0.0f, 0.0f, 0.0f, 1.0f);
}

int ceilShift(int value, int shift) {
    return (value + (1 << shift) - 1) >> shift;
}

// How close to the overlay's line (in widget pixels) a press grabs it
constexpr double kSplitGrab = 8.0;
constexpr double kSplitHandleRadius = 11.0;

bool isMarker(DrawTool tool) {
    return tool == DrawTool::MarkA || tool == DrawTool::MarkB;
}

QPointF clampToVideo(QPointF p) {
    return QPointF(std::clamp(p.x(), 0.0, 1.0), std::clamp(p.y(), 0.0, 1.0));
}

// e.g. "1.25 m", "38.2 cm", "112 m"
QString formatLength(double value, const QString& unit) {
    const int decimals = value >= 100.0 ? 0 : value >= 10.0 ? 1 : 2;
    return QString("%1 %2").arg(value, 0, 'f', decimals).arg(unit).trimmed();
}

// Size of a label for text, in the label font (which it selects into p)
QSizeF labelSize(QPainter& p, const QString& text) {
    QFont font = p.font();
    font.setBold(true);
    p.setFont(font);
    const QFontMetricsF fm(font);
    return QSizeF(fm.horizontalAdvance(text) + 12.0, fm.height() + 6.0);
}

// White text in a dark box outlined in the drawing's colour
void paintLabel(QPainter& p, const QRectF& rect, const QColor& color, const QString& text) {
    p.setPen(QPen(color, 1.5));
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRoundedRect(rect, 5, 5);
    p.setPen(Qt::white);
    p.drawText(rect, Qt::AlignCenter, text);
}

} // namespace

VideoRenderer::VideoRenderer(QWidget* parent)
    : QOpenGLWidget(parent),
      m_vbo(QOpenGLBuffer::VertexBuffer) {
    setMinimumSize(320, 180);
}

VideoRenderer::~VideoRenderer() {
    releaseGL();
    // ~QOpenGLWidget destroys the context after this, which must no longer
    // call back into this half-destroyed object.
    if (context()) disconnect(context(), nullptr, this, nullptr);
}

void VideoRenderer::setFrame(const FramePtr& frame) {
    m_main.frame = frame;
    m_main.dirty = true;
    update();
}

void VideoRenderer::clearFrame() {
    m_main.frame.reset();
    update();
}

void VideoRenderer::setRotation(int degrees) {
    m_main.rotation = ((degrees % 360) + 360) % 360;
    update();
}

void VideoRenderer::setGhostFrame(const FramePtr& frame, int rotation) {
    m_ghost.frame    = frame;
    m_ghost.dirty    = true;
    m_ghost.rotation = ((rotation % 360) + 360) % 360;
    if (m_overlay != OverlayMode::None) update();
}

void VideoRenderer::setOverlayMode(OverlayMode mode, bool horizontalSplit) {
    horizontalSplit = mode == OverlayMode::Split && horizontalSplit;
    // A line turned across starts in the middle, too.
    if ((m_overlay == OverlayMode::None && mode != OverlayMode::None) || horizontalSplit != m_horizontalSplit)
        m_split = 0.5;
    m_overlay         = mode;
    m_horizontalSplit = horizontalSplit;
    m_splitDragging   = false;
    m_splitHovered    = false;
    // Hovering the line changes the cursor
    setMouseTracking(mode != OverlayMode::None);
    updateCursor();
    update();
}

void VideoRenderer::setZoom(const ViewZoom& zoom) {
    if (zoom == m_zoom) return;
    m_zoom = zoom;
    updateCursor();
    update();
}

void VideoRenderer::setAnnotations(AnnotationDoc* doc) {
    if (doc == m_annotations) return;
    if (m_annotations) disconnect(m_annotations, nullptr, this, nullptr);
    m_annotations = doc;
    if (doc) connect(doc, &AnnotationDoc::changed, this, qOverload<>(&QWidget::update));
    m_drawing = false;
    update();
}

void VideoRenderer::setDrawStyle(const DrawStyle& style) {
    if (style.tool != m_drawStyle.tool) m_drawing = false;
    m_drawStyle = style;
    updateCursor();
    update();
}

void VideoRenderer::updateCursor() {
    if (m_splitDragging || m_splitHovered) setCursor(m_horizontalSplit ? Qt::SplitVCursor : Qt::SplitHCursor);
    else if (m_dragging) setCursor(Qt::ClosedHandCursor);
    else if (m_drawStyle.tool != DrawTool::None) setCursor(Qt::CrossCursor);
    else if (m_zoom.isZoomed()) setCursor(Qt::OpenHandCursor);
    else unsetCursor();
}

void VideoRenderer::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    // On the overlay's line: back to the middle.
    if (onSplitLine(event->position())) {
        m_split = 0.5;
        update();
        return;
    }
    // While drawing, quick clicks are strokes, not a full-screen toggle.
    if (m_drawStyle.tool == DrawTool::None) emit doubleClicked();
}

void VideoRenderer::mousePressEvent(QMouseEvent* event) {
    const bool tool = m_drawStyle.tool != DrawTool::None;
    if (event->button() == Qt::LeftButton && onSplitLine(event->position())) {
        // The line comes first, even with a drawing tool picked.
        m_splitDragging = true;
        setSplitAt(event->position());
        updateCursor();
    } else if (event->button() == Qt::LeftButton && tool && m_main.frame && m_annotations) {
        const QPointF p = clampToVideo(toVideo(event->position()));
        m_draft = Annotation();
        m_draft.tool   = m_drawStyle.tool;
        m_draft.color  = m_drawStyle.color;
        m_draft.width  = m_drawStyle.width;
        m_draft.points = {p, p};
        if (m_draft.tool == DrawTool::Freehand || isMarker(m_draft.tool)) m_draft.points = {p};
        m_drawing = true;
        update();
    } else if (m_main.frame && m_zoom.isZoomed()
               && ((event->button() == Qt::LeftButton && !tool) || event->button() == Qt::MiddleButton)) {
        // Drag to pan, when there is something to pan. While a drawing tool
        // has the left button, the middle one pans.
        m_dragging = true;
        m_dragPos = event->position();
        updateCursor();
    }
    QOpenGLWidget::mousePressEvent(event);
}

void VideoRenderer::mouseMoveEvent(QMouseEvent* event) {
    if (m_splitDragging) {
        setSplitAt(event->position());
    } else if (m_drawing) {
        const QPointF p = clampToVideo(toVideo(event->position()));
        if (m_draft.tool == DrawTool::Freehand) {
            // Skip points closer than a couple of pixels to keep strokes light
            if (QLineF(toWidget(m_draft.points.last()), event->position()).length() >= 2.0) m_draft.points << p;
        } else {
            m_draft.points.last() = p;
        }
        update();
    } else if (m_dragging) {
        // Moving the mouse right moves the picture right: the view goes left.
        const QSizeF scale = videoScale();
        const QPointF delta = event->position() - m_dragPos;
        m_dragPos = event->position();
        emit panRequested(QPointF(-delta.x() / (scale.width() * width() * m_zoom.zoom),
                                  -delta.y() / (scale.height() * height() * m_zoom.zoom)));
    } else if (event->buttons() == Qt::NoButton) {
        const bool hovered = onSplitLine(event->position());
        if (hovered != m_splitHovered) {
            m_splitHovered = hovered;
            updateCursor();
        }
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void VideoRenderer::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_splitDragging) {
        m_splitDragging = false;
        m_splitHovered  = onSplitLine(event->position());
        updateCursor();
        update(); // hides the mix label
    } else if (event->button() == Qt::LeftButton && m_drawing) {
        m_drawing = false;
        update();
        // Ignore plain clicks: they would draw nothing visible.
        const QPointF a = toWidget(m_draft.points.first());
        const QPointF b = toWidget(m_draft.points.last());
        const bool visible = isMarker(m_draft.tool)                ? true
                           : m_draft.tool == DrawTool::Freehand ? m_draft.points.size() > 1
                                                                : QLineF(a, b).length() >= 3.0;
        if (visible) emit annotationDrawn(m_draft);
    } else if (m_dragging && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        m_dragging = false;
        updateCursor();
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void VideoRenderer::wheelEvent(QWheelEvent* event) {
    if (!m_main.frame || event->angleDelta().y() == 0) return QOpenGLWidget::wheelEvent(event);
    // 1.25x per wheel notch, around the point under the cursor
    const double factor = std::pow(1.25, event->angleDelta().y() / 120.0);
    emit zoomAtRequested(factor, toVideo(event->position()));
    event->accept();
}

// Normalised video coordinates -> widget position (inverse of toVideo()).
QPointF VideoRenderer::toWidget(QPointF videoPos) const {
    return videoToWidget().map(videoPos);
}

QSizeF VideoRenderer::videoArea() const {
    const QSizeF scale = videoScale();
    return QSizeF(width() * scale.width(), height() * scale.height());
}

QRectF VideoRenderer::videoRect() const {
    const QSizeF area = videoArea();
    return QRectF(QPointF((width() - area.width()) / 2, (height() - area.height()) / 2), area);
}

// The line sits in the letterboxed area, which zooming doesn't change, so it
// stays in view however far the picture is zoomed in.
bool VideoRenderer::onSplitLine(QPointF widgetPos) const {
    if (!overlayShown()) return false;
    const QRectF area = videoRect();
    if (!area.adjusted(-kSplitGrab, -kSplitGrab, kSplitGrab, kSplitGrab).contains(widgetPos)) return false;
    const double distance = m_horizontalSplit ? widgetPos.y() - (area.top() + m_split * area.height())
                                              : widgetPos.x() - (area.left() + m_split * area.width());
    // The round handle in the middle is easier to hit than the line.
    const QPointF handle = m_horizontalSplit ? QPointF(area.center().x(), area.top() + m_split * area.height())
                                             : QPointF(area.left() + m_split * area.width(), area.center().y());
    return std::abs(distance) <= kSplitGrab
        || QLineF(widgetPos, handle).length() <= kSplitHandleRadius + kSplitGrab / 2;
}

void VideoRenderer::setSplitAt(QPointF widgetPos) {
    const QRectF area = videoRect();
    const double split = m_horizontalSplit ? (widgetPos.y() - area.top()) / std::max(1.0, area.height())
                                           : (widgetPos.x() - area.left()) / std::max(1.0, area.width());
    m_split = std::clamp(split, 0.0, 1.0);
    update();
}

// The zoom centre sits in the middle of the widget, magnified by the zoom.
QTransform VideoRenderer::videoToWidget() const {
    const QSizeF area = videoArea();
    QTransform t;
    t.translate(width() * 0.5, height() * 0.5);
    t.scale(area.width() * m_zoom.zoom, area.height() * m_zoom.zoom);
    t.translate(-m_zoom.center.x(), -m_zoom.center.y());
    return t;
}

double VideoRenderer::pixelLength(QPointF a, QPointF b) const {
    const QSizeF size = displaySize();
    return std::hypot((b.x() - a.x()) * size.width(), (b.y() - a.y()) * size.height());
}

// Widget position -> normalised video coordinates of the content shown there.
QPointF VideoRenderer::toVideo(QPointF widgetPos) const {
    const QSizeF scale = videoScale();
    const double ndcX = 2.0 * widgetPos.x() / std::max(1, width()) - 1.0;
    const double ndcY = 1.0 - 2.0 * widgetPos.y() / std::max(1, height());
    return QPointF(m_zoom.center.x() + ndcX / (2.0 * m_zoom.zoom * scale.width()),
                   m_zoom.center.y() - ndcY / (2.0 * m_zoom.zoom * scale.height()));
}

void VideoRenderer::initializeGL() {
    initializeOpenGLFunctions();

    // The context is recreated when the widget moves to another top-level
    // window; drop our GL objects with it. initializeGL() runs again after.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
            &VideoRenderer::releaseGL, Qt::UniqueConnection);

    m_program = new QOpenGLShaderProgram();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
        || !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)
        || !m_program->link()) {
        qWarning("VideoRenderer: shader setup failed: %s", qPrintable(m_program->log()));
        delete m_program;
        m_program = nullptr;
        return;
    }

    m_program->bind();
    m_program->setUniformValue("texY", 0);
    m_program->setUniformValue("texU", 1);
    m_program->setUniformValue("texV", 2);
    m_program->setUniformValue("opacity", 1.0f);
    m_program->release();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(kQuad, sizeof(kQuad));
    m_vbo.release();

    // re-upload after a context change
    m_main.dirty  = true;
    m_ghost.dirty = true;
}

void VideoRenderer::releaseGL() {
    if (!m_program) return;
    makeCurrent();
    for (Layer* layer : {&m_main, &m_ghost}) {
        glDeleteTextures(3, layer->textures);
        std::fill(std::begin(layer->textures), std::end(layer->textures), 0u);
        std::fill(std::begin(layer->texSize), std::end(layer->texSize), QSize());
        std::fill(std::begin(layer->texFormat), std::end(layer->texFormat), 0);
    }
    m_vbo.destroy();
    delete m_program;
    m_program = nullptr;
    doneCurrent();
}

bool VideoRenderer::uploadFrame(Layer& layer) {
    const AVFrame* f = layer.frame.get();
    const auto format = static_cast<AVPixelFormat>(f->format);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
    if (!desc || !isRenderablePixelFormat(format)) return false;

    const int  planes         = av_pix_fmt_count_planes(format);
    const int  depth          = desc->comp[0].depth;
    const int  bytesPerSample = depth > 8 ? 2 : 1;
    layer.semiPlanar = planes == 2;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < planes; i++) {
        const bool   chroma   = i > 0;
        const int    w        = chroma ? ceilShift(f->width, desc->log2_chroma_w) : f->width;
        const int    h        = chroma ? ceilShift(f->height, desc->log2_chroma_h) : f->height;
        const int    channels = (chroma && layer.semiPlanar) ? 2 : 1;
        const GLint  internal = bytesPerSample == 2 ? (channels == 2 ? GL_RG16 : GL_R16)
                                                    : (channels == 2 ? GL_RG8 : GL_R8);
        const GLenum glFormat = channels == 2 ? GL_RG : GL_RED;
        const GLenum glType   = bytesPerSample == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

        glActiveTexture(GL_TEXTURE0 + i);
        if (!layer.textures[i]) {
            glGenTextures(1, &layer.textures[i]);
            glBindTexture(GL_TEXTURE_2D, layer.textures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, layer.textures[i]);
        }

        // linesize includes decoder padding; tell GL the real row stride.
        glPixelStorei(GL_UNPACK_ROW_LENGTH, f->linesize[i] / (channels * bytesPerSample));
        if (layer.texSize[i] != QSize(w, h) || layer.texFormat[i] != internal) {
            glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, glFormat, glType, f->data[i]);
            layer.texSize[i]   = QSize(w, h);
            layer.texFormat[i] = internal;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, glFormat, glType, f->data[i]);
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // 16-bit textures normalize by 65535. P010 stores samples in the high
    // bits (already normalized); yuv420p10 stores them in the low bits.
    layer.sampleScale = (bytesPerSample == 2 && desc->comp[0].shift == 0)
        ? 65535.0f / static_cast<float>((1 << depth) - 1)
        : 1.0f;
    layer.colorMatrix = yuvToRgbMatrix(f);
    return true;
}

QSizeF VideoRenderer::displaySize(const Layer& layer) const {
    if (!layer.frame) return QSizeF(1.0, 1.0);
    double vw = layer.frame->width;
    double vh = std::max(1, layer.frame->height);
    const AVRational sar = layer.frame->sample_aspect_ratio;
    if (sar.num > 0 && sar.den > 0) vw *= av_q2d(sar);
    if (layer.rotation == 90 || layer.rotation == 270) std::swap(vw, vh);
    return QSizeF(vw, vh);
}

// Size of the letterboxed video area relative to the widget (1 = full
// width/height), for the frame's display aspect ratio after rotation.
QSizeF VideoRenderer::videoScale(const Layer& layer) const {
    if (!layer.frame) return QSizeF(1.0, 1.0);
    const QSizeF size = displaySize(layer);
    const double videoAspect  = size.width() / size.height();
    const double widgetAspect = static_cast<double>(width()) / std::max(1, height());
    if (videoAspect > widgetAspect) return QSizeF(1.0, widgetAspect / videoAspect);
    return QSizeF(videoAspect / widgetAspect, 1.0);
}

// Rotates the quad per the stream's display matrix, zooms into the region
// around the zoom centre, and letterboxes it so the video keeps its aspect
// ratio inside the widget.
QMatrix4x4 VideoRenderer::vertexTransform(const Layer& layer) const {
    const QSizeF scale = videoScale(layer);
    const auto zoom = static_cast<float>(m_zoom.zoom);
    QMatrix4x4 m;
    m.scale(static_cast<float>(scale.width()) * zoom, static_cast<float>(scale.height()) * zoom);
    // Quad spans -1..1, so normalised centre c sits at 2c - 1 (y flipped)
    m.translate(static_cast<float>(1.0 - 2.0 * m_zoom.center.x()),
                static_cast<float>(2.0 * m_zoom.center.y() - 1.0));
    m.rotate(static_cast<float>(-layer.rotation), 0.0f, 0.0f, 1.0f);
    return m;
}

void VideoRenderer::paintGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_main.frame || !m_program) return;
    for (Layer* layer : {&m_main, &m_ghost}) {
        if (!layer->frame || !layer->dirty) continue;
        if (layer == &m_ghost && m_overlay == OverlayMode::None) continue; // not needed yet
        if (!uploadFrame(*layer)) {
            if (layer == &m_main) return;
            layer->frame.reset();
        }
        layer->dirty = false;
    }

    m_program->bind();
    m_vbo.bind();
    m_program->enableAttributeArray("position");
    m_program->enableAttributeArray("texCoord");
    m_program->setAttributeBuffer("position", GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->setAttributeBuffer("texCoord", GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    // A zoomed picture stays within the video's letterboxed area. GL counts
    // device pixels from the bottom left.
    const QSizeF scale = videoScale();
    const double dpr = devicePixelRatioF();
    const int fw = static_cast<int>(std::lround(width() * dpr));
    const int fh = static_cast<int>(std::lround(height() * dpr));
    const int w  = static_cast<int>(std::lround(width() * scale.width() * dpr));
    const int h  = static_cast<int>(std::lround(height() * scale.height() * dpr));
    const QRect area((fw - w) / 2, (fh - h) / 2, w, h);

    glEnable(GL_SCISSOR_TEST);
    if (overlayShown() && m_overlay == OverlayMode::Split) {
        // The first clip before the line, the ghost after it
        QRect first = area, second = area;
        if (m_horizontalSplit) {
            const int top = static_cast<int>(std::lround(m_split * h));
            first.setTop(area.top() + h - top);
            second.setHeight(h - top);
        } else {
            const int left = static_cast<int>(std::lround(m_split * w));
            first.setWidth(left);
            second.setLeft(area.left() + left);
        }
        drawLayer(m_main, first, 1.0f);
        drawLayer(m_ghost, second, 1.0f);
    } else {
        drawLayer(m_main, area, 1.0f);
        if (overlayShown()) {
            // Keep the destination alpha opaque, or the window would show
            // through the widget.
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            drawLayer(m_ghost, area, static_cast<float>(m_split));
            glDisable(GL_BLEND);
        }
    }
    glDisable(GL_SCISSOR_TEST);

    m_program->disableAttributeArray("position");
    m_program->disableAttributeArray("texCoord");
    m_vbo.release();
    m_program->release();
    glActiveTexture(GL_TEXTURE0);

    const bool annotations = m_drawing || (m_annotations && !m_annotations->items().isEmpty());
    if (!annotations && !overlayShown()) return;
    QPainter p(this);
    if (annotations) paintAnnotations(p, videoToWidget(), videoRect(), true);
    if (overlayShown()) paintSplitLine(p);
}

// Expects the program, quad and scissor test set up by paintGL().
void VideoRenderer::drawLayer(const Layer& layer, const QRect& scissor, float opacity) {
    if (scissor.isEmpty()) return;
    for (int i = 0; i < 3; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, layer.textures[i]);
    }
    m_program->setUniformValue("transform", vertexTransform(layer));
    m_program->setUniformValue("colorMatrix", layer.colorMatrix);
    m_program->setUniformValue("sampleScale", layer.sampleScale);
    m_program->setUniformValue("semiPlanar", static_cast<GLint>(layer.semiPlanar ? 1 : 0));
    m_program->setUniformValue("opacity", opacity);
    glScissor(scissor.x(), scissor.y(), scissor.width(), scissor.height());
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
}

// A white line across the video with a round handle in its middle, and while
// blending is dragged, the mix beside the handle
void VideoRenderer::paintSplitLine(QPainter& p) const {
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipping(false);
    const QRectF area = videoRect();
    QLineF line;
    if (m_horizontalSplit) {
        const double y = area.top() + m_split * area.height();
        line = QLineF(area.left(), y, area.right(), y);
    } else {
        const double x = area.left() + m_split * area.width();
        line = QLineF(x, area.top(), x, area.bottom());
    }
    const QPointF handle = line.center();
    const bool active = m_splitDragging || m_splitHovered;

    // A soft shadow keeps the line visible on bright footage.
    p.setPen(QPen(QColor(0, 0, 0, 90), 4.0, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(line);
    p.setPen(QPen(QColor(255, 255, 255, active ? 255 : 220), 1.5, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(line);

    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(QColor(0, 0, 0, active ? 200 : 150));
    p.drawEllipse(handle, kSplitHandleRadius, kSplitHandleRadius);
    // Two small arrows: which way the handle moves
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    for (int side : {-1, 1}) {
        QPolygonF arrow({QPointF(side * 7.0, 0.0), QPointF(side * 3.0, -3.5), QPointF(side * 3.0, 3.5)});
        if (m_horizontalSplit) arrow = QTransform().rotate(90).map(arrow);
        p.drawPolygon(arrow.translated(handle));
    }

    if (m_overlay == OverlayMode::Blend && m_splitDragging) {
        const int ghost = static_cast<int>(std::lround(m_split * 100.0));
        const QString text = QString("%1 % | %2 %").arg(100 - ghost).arg(ghost);
        const QSizeF box = labelSize(p, text);
        QRectF rect(handle + QPointF(kSplitHandleRadius + 8.0, -box.height() / 2), box);
        if (rect.right() > area.right()) rect.moveRight(handle.x() - kSplitHandleRadius - 8.0);
        paintLabel(p, rect, Qt::white, text);
    }
}

QImage VideoRenderer::renderImage(const FramePtr& frame) const {
    QImage image = frameToImage(frame.get());
    if (image.isNull()) return {};
    if (m_main.rotation != 0) image = image.transformed(QTransform().rotate(m_main.rotation));
    if (overlayShown()) {
        // The ghost as on screen, scaled to this frame (the clips are expected
        // to come from the same camera)
        QImage ghost = frameToImage(m_ghost.frame.get());
        if (!ghost.isNull()) {
            if (m_ghost.rotation != 0) ghost = ghost.transformed(QTransform().rotate(m_ghost.rotation));
            ghost = ghost.scaled(image.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter p(&image);
            const QRectF target(QPointF((image.width() - ghost.width()) / 2.0, (image.height() - ghost.height()) / 2.0),
                                ghost.size());
            if (m_overlay == OverlayMode::Blend) {
                p.setOpacity(m_split);
            } else if (m_horizontalSplit) {
                p.setClipRect(QRectF(0, m_split * image.height(), image.width(), image.height()));
            } else {
                p.setClipRect(QRectF(m_split * image.width(), 0, image.width(), image.height()));
            }
            p.drawImage(target, ghost);
        }
    }
    const QImage drawings = renderAnnotations(image.size());
    if (!drawings.isNull()) QPainter(&image).drawImage(0, 0, drawings);
    return image;
}

QImage VideoRenderer::renderAnnotations(QSize size) const {
    if (!m_annotations || m_annotations->items().isEmpty() || size.isEmpty()) return {};
    // Lay the drawings out as on screen without zoom, then scale that to
    // size, so line widths and labels keep their size relative to the frame.
    const QSizeF area = videoArea();
    if (area.isEmpty()) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setFont(font());
    p.scale(size.width() / area.width(), size.height() / area.height());
    paintAnnotations(p, QTransform::fromScale(area.width(), area.height()), QRectF(QPointF(), area), false);
    return image;
}

void VideoRenderer::paintAnnotations(QPainter& p, const QTransform& toView, const QRectF& clip,
                                     bool withDraft) const {
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(clip);

    // A new reference or marker replaces the old one: hide that while drawing
    // it, and measure against the new one once it is done, not before.
    const bool drawing = withDraft && m_drawing;
    const Annotation* reference = m_annotations ? m_annotations->reference() : nullptr;
    const bool replacing = drawing && (m_draft.tool == DrawTool::Reference || isMarker(m_draft.tool));
    const Annotation* replaced = replacing && m_annotations ? m_annotations->find(m_draft.tool) : nullptr;
    auto shown = [this, drawing](DrawTool tool) -> const Annotation* {
        if (drawing && m_draft.tool == tool) return &m_draft;
        return m_annotations ? m_annotations->find(tool) : nullptr;
    };

    // The way from A to B, under everything else
    const Annotation* markA = shown(DrawTool::MarkA);
    const Annotation* markB = shown(DrawTool::MarkB);
    if (markA && markB) {
        p.setPen(QPen(QColor(255, 255, 255, 200), 1.5, Qt::DashLine));
        p.drawLine(toView.map(markA->points.first()), toView.map(markB->points.first()));
    }

    if (m_annotations) {
        for (const Annotation& a : m_annotations->items()) {
            if (&a == replaced) continue;
            paintAnnotation(p, toView, a, reference);
        }
    }
    if (drawing) paintAnnotation(p, toView, m_draft, reference);
}

void VideoRenderer::paintAnnotation(QPainter& p, const QTransform& toView, const Annotation& a,
                                    const Annotation* reference) const {
    if (a.points.isEmpty()) return;
    p.setPen(QPen(a.color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const QPointF start = toView.map(a.points.first());
    const QPointF end   = toView.map(a.points.last());
    const QLineF  line(start, end);

    switch (a.tool) {
    case DrawTool::None:
        break;
    case DrawTool::Line:
        p.drawLine(line);
        break;
    case DrawTool::Arrow: {
        if (line.length() < 1.0) break;
        // The head grows with the line width; the shaft stops inside it so
        // its round cap doesn't poke out at the tip.
        const double  head   = 8.0 + 3.0 * a.width;
        const QPointF back   = (start - end) / line.length();
        const QPointF normal(-back.y(), back.x());
        const QPointF base   = end + back * head;
        p.drawLine(start, end + back * std::min(head * 0.5, line.length()));
        p.setPen(Qt::NoPen);
        p.setBrush(a.color);
        p.drawPolygon(QPolygonF({end, base + normal * head * 0.5, base - normal * head * 0.5}));
        break;
    }
    case DrawTool::Freehand: {
        QPainterPath path(start);
        for (qsizetype i = 1; i < a.points.size(); i++) path.lineTo(toView.map(a.points[i]));
        p.drawPath(path);
        break;
    }
    case DrawTool::Circle:
        p.drawEllipse(start, line.length(), line.length());
        break;
    case DrawTool::Reference:
    case DrawTool::Measure: {
        // A line with end ticks, and its length on a label beside the middle
        p.drawLine(line);
        QPointF normal(0.0, -1.0); // unit, pointing up the screen
        if (line.length() >= 1.0) {
            normal = QPointF(line.dy(), -line.dx()) / line.length();
            if (normal.y() > 0.0) normal = -normal;
            const double tick = 5.0 + a.width;
            for (const QPointF& point : {start, end}) p.drawLine(point - normal * tick, point + normal * tick);
        }

        const double pixels = pixelLength(a.points.first(), a.points.last());
        QString text;
        if (a.tool == DrawTool::Reference) {
            text = a.length > 0.0 ? QString("Ref %1").arg(formatLength(a.length, a.unit)) : QString("Reference");
        } else if (reference && reference->length > 0.0) {
            const double refPixels = pixelLength(reference->points.first(), reference->points.last());
            text = formatLength(refPixels > 0.0 ? pixels / refPixels * reference->length : 0.0, reference->unit);
        } else {
            // No reference yet: video pixels are better than nothing.
            text = QString("%1 px").arg(std::lround(pixels));
        }

        const QSizeF box = labelSize(p, text);
        // Far enough along the normal that the box clears the line
        const double reach = std::abs(normal.x()) * box.width() / 2 + std::abs(normal.y()) * box.height() / 2;
        const QPointF shift = normal * (reach + a.width / 2 + 4.0);
        QRectF rect(line.center() + shift - QPointF(box.width() / 2, box.height() / 2), box);
        // Flip to the other side where the video's edge would cut it off
        if (!p.clipBoundingRect().contains(rect)) rect.translate(-2.0 * shift);
        paintLabel(p, rect, a.color, text);
        break;
    }
    case DrawTool::MarkA:
    case DrawTool::MarkB: {
        // A ring with cross hairs that leave the marked point itself free,
        // and the letter and time of the frame beside it
        const double radius = 6.0 + a.width;
        p.drawEllipse(start, radius, radius);
        for (const QPointF& dir : {QPointF(1, 0), QPointF(0, 1)}) {
            p.drawLine(start + dir * radius * 0.4, start + dir * radius * 1.6);
            p.drawLine(start - dir * radius * 0.4, start - dir * radius * 1.6);
        }
        QString text = a.tool == DrawTool::MarkA ? "A" : "B";
        if (&a != &m_draft) text += "  " + formatTime(a.time);
        const QSizeF box = labelSize(p, text);
        QRectF rect(start + QPointF(radius, -radius - box.height()), box);
        if (!p.clipBoundingRect().contains(rect)) rect.moveTop(start.y() + radius);
        if (!p.clipBoundingRect().contains(rect)) rect.moveRight(start.x() - radius);
        paintLabel(p, rect, a.color, text);
        break;
    }
    }
}
