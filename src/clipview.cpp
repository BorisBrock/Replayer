#include "clipview.h"
#include "playercontroller.h"
#include "theme.h"
#include "timeformat.h"
#include "transportbar.h"
#include "videorenderer.h"

#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

ClipView::ClipView(QWidget* parent) : QFrame(parent) {
    setObjectName("ClipTile");

    m_controller    = new PlayerController(this);
    m_videoRenderer = new VideoRenderer(this);
    m_transportBar  = new TransportBar(this);

    m_header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(10, 4, 4, 2);
    m_titleLabel = new QLabel(m_header);
    m_titleLabel->setObjectName("TileTitle");
    m_titleLabel->setTextFormat(Qt::PlainText);
    auto* closeButton = new QToolButton(m_header);
    closeButton->setIcon(Theme::icon(Theme::Icon::Close));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setToolTip("Remove from comparison");
    closeButton->setFocusPolicy(Qt::NoFocus);
    connect(closeButton, &QToolButton::clicked, this, &ClipView::closeRequested);
    headerLayout->addWidget(m_titleLabel, 1);
    headerLayout->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->addWidget(m_header);
    layout->addWidget(m_videoRenderer, 1);
    layout->addWidget(m_transportBar);

    // This tile's own controls act on this clip only.
    auto* c = m_controller;
    connect(m_transportBar, &TransportBar::playPauseClicked, this, [this, c] {
        emit playRequested(!c->isPlaying());
        c->togglePlayPause();
    });
    connect(m_transportBar, &TransportBar::stopClicked, this, [this, c] { emit playRequested(false); c->stop(); });
    connect(m_transportBar, &TransportBar::stepBackwardClicked, this, [this, c] { emit playRequested(false); c->stepBackward(); });
    connect(m_transportBar, &TransportBar::stepForwardClicked, this, [this, c] { emit playRequested(false); c->stepForward(); });
    connect(m_transportBar, &TransportBar::seekRequested, c, &PlayerController::seek);

    connect(c, &PlayerController::frameReady, this, [this](const FramePtr& frame) {
        // The previous clip's last frame stays up until the new clip's first
        // frame arrives; switch the rotation together with the picture.
        if (m_pendingRotation >= 0) {
            m_videoRenderer->setRotation(m_pendingRotation);
            m_pendingRotation = -1;
        }
        m_videoRenderer->setFrame(frame);
    });
    connect(c, &PlayerController::positionChanged, m_transportBar, &TransportBar::setPosition);
    connect(c, &PlayerController::playbackStateChanged, m_transportBar, &TransportBar::setPlaybackState);
    connect(c, &PlayerController::videoInfoReady, this, [this](const VideoInfo& info) {
        m_transportBar->setVideoInfo(info);
        m_pendingRotation = info.rotation;
        emit videoInfoReady(info);
    });
    connect(c, &PlayerController::errorOccurred, this, &ClipView::errorOccurred);

    connect(m_videoRenderer, &VideoRenderer::doubleClicked, this, &ClipView::doubleClicked);
    connect(m_videoRenderer, &VideoRenderer::zoomAtRequested, this, &ClipView::zoomAtRequested);
    connect(m_videoRenderer, &VideoRenderer::panRequested, this, &ClipView::panRequested);
    connect(m_videoRenderer, &VideoRenderer::annotationDrawn, this, &ClipView::addAnnotation);
    // Clicks on child widgets (video, seek bar, buttons) don't reach
    // mousePressEvent(), so watch every descendant.
    for (QWidget* child : findChildren<QWidget*>()) child->installEventFilter(this);

    setCompact(false);
}

ClipView::~ClipView() = default;

bool ClipView::open(const QString& path, double position) {
    m_path = path;
    m_titleLabel->setText(QFileInfo(path).fileName());
    m_titleLabel->setToolTip(path);
    if (!m_controller->openFile(path)) {
        m_videoRenderer->clearFrame();
        m_transportBar->clear();
        return false;
    }
    // Resume where this clip was left, unless that was the very end.
    const VideoInfo& info = m_controller->videoInfo();
    if (position > 0.0 && position < info.duration - 2.0 / info.fps) m_controller->seek(position);
    return true;
}

void ClipView::setCompact(bool compact) {
    m_header->setVisible(compact);
    m_transportBar->setCompact(compact);
    // Tiles share the space: let the video shrink rather than push the
    // tile's controls out of view.
    m_videoRenderer->setMinimumSize(compact ? QSize(160, 90) : QSize(320, 180));
    // Several decoders at once: hold fewer frames each to bound memory.
    m_controller->setFrameQueueCapacity(compact ? 8 : 16);
    layout()->setContentsMargins(compact ? QMargins(2, 2, 2, 2) : QMargins());
    setStyleProperty("compact", compact);
}

void ClipView::setCurrent(bool current) {
    setStyleProperty("current", current);
}

FramePtr ClipView::currentFrame() const {
    return m_videoRenderer->frame();
}

void ClipView::setZoom(const ViewZoom& zoom) {
    m_videoRenderer->setZoom(zoom);
}

void ClipView::setAnnotations(AnnotationDoc* doc) {
    m_annotations = doc;
    m_videoRenderer->setAnnotations(doc);
}

void ClipView::setDrawStyle(const DrawStyle& style) {
    m_videoRenderer->setDrawStyle(style);
}

void ClipView::setGhost(ClipView* ghost, OverlayMode mode, bool horizontalSplit) {
    if (ghost != m_ghost) {
        disconnect(m_ghostConnection);
        m_ghost = ghost;
        if (ghost) {
            // The ghost's own tile has set the rotation by the time this runs
            // (it connected first).
            m_ghostConnection = connect(ghost->m_controller, &PlayerController::frameReady, this,
                                        [this, ghost](const FramePtr& frame) {
                                            m_videoRenderer->setGhostFrame(frame, ghost->m_videoRenderer->rotation());
                                        });
            m_videoRenderer->setGhostFrame(ghost->currentFrame(), ghost->m_videoRenderer->rotation());
        } else {
            m_videoRenderer->setGhostFrame(nullptr, 0);
        }
    }
    m_videoRenderer->setOverlayMode(ghost ? mode : OverlayMode::None, horizontalSplit);
}

void ClipView::setVideoHidden(bool hidden) {
    m_videoRenderer->setVisible(!hidden);
    // Without the video, the tile takes no more height than its controls.
    setSizePolicy(QSizePolicy::Preferred, hidden ? QSizePolicy::Maximum : QSizePolicy::Preferred);
}

void ClipView::addAnnotation(Annotation annotation) {
    if (!m_annotations) return;
    m_annotations->setDisplaySize(m_videoRenderer->displaySize());
    // Speed markers remember the frame they were set on.
    if (annotation.tool == DrawTool::MarkA || annotation.tool == DrawTool::MarkB)
        annotation.time = m_controller->position();
    if (annotation.tool == DrawTool::Reference) {
        // Ask for the real length, e.g. "1.25 m" or "80 cm" (unit: m if none)
        const Annotation* previous = m_annotations->reference();
        const QString suggestion = previous ? QString("%1 %2").arg(previous->length).arg(previous->unit)
                                            : QString("1 m");
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, "Reference Length", "Real length of the reference line (e.g. 1.25 m):",
            QLineEdit::Normal, suggestion, &ok);
        if (!ok) return;
        static const QRegularExpression pattern(R"(^\s*(\d+(?:[.,]\d*)?|[.,]\d+)\s*(.*?)\s*$)");
        const QRegularExpressionMatch match = pattern.match(text);
        const double length = match.hasMatch() ? QString(match.captured(1)).replace(',', '.').toDouble() : 0.0;
        if (length <= 0.0) {
            emit errorOccurred(QString("Not a length: \"%1\"").arg(text));
            return;
        }
        annotation.length = length;
        annotation.unit   = match.captured(2).isEmpty() ? QString("m") : match.captured(2);
    }
    m_annotations->add(annotation);
}

void ClipView::setStyleProperty(const char* name, bool value) {
    if (property(name).toBool() == value && property(name).isValid()) return;
    setProperty(name, value);
    // Re-evaluate [property="..."] stylesheet selectors
    style()->unpolish(this);
    style()->polish(this);
}

bool ClipView::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) emit activated();
    return QFrame::eventFilter(watched, event);
}

void ClipView::mousePressEvent(QMouseEvent* event) {
    emit activated();
    QFrame::mousePressEvent(event);
}

void ClipView::contextMenuEvent(QContextMenuEvent* event) {
    // Keep the frame under the cursor, even if playback moves on meanwhile
    const FramePtr frame = currentFrame();
    const double position = m_controller->position();
    QMenu menu(this);
    QAction* exportAction = menu.addAction("Export frame as .png...");
    exportAction->setEnabled(frame != nullptr);
    connect(exportAction, &QAction::triggered, this, [this, frame, position] { exportFrame(frame, position); });
    menu.exec(event->globalPos());
}

void ClipView::exportFrame(const FramePtr& frame, double position) {
    const QImage image = m_videoRenderer->renderImage(frame);
    if (image.isNull()) {
        emit errorOccurred("Could not convert the frame");
        return;
    }
    // e.g. "clip_00-12.345.png" next to the clip
    const QFileInfo clip(m_path);
    const QString name = QString("%1_%2.png").arg(clip.completeBaseName(), formatTime(position).replace(':', '-'));
    const QString file = QFileDialog::getSaveFileName(this, "Export Frame", clip.dir().filePath(name),
                                                      "PNG Images (*.png)");
    if (file.isEmpty()) return;
    if (!image.save(file, "PNG"))
        emit errorOccurred(QString("Could not save %1").arg(QDir::toNativeSeparators(file)));
}
