#include "mainwindow.h"
#include "annotations.h"
#include "clipgrid.h"
#include "clipview.h"
#include "filebrowser.h"
#include "infopanel.h"
#include "jumpsliderstyle.h"
#include "playercontroller.h"
#include "theme.h"
#include "transportbar.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

// More tiles get too small to be useful, and each one runs its own decoder.
constexpr int kMaxClips = 6;

// Comparison bar button ids of the overlay modes, next to the arrangements
constexpr int kBlendId = 10;
constexpr int kSplitId = 11;

const char* const kRepoUrl = "https://github.com/BorisBrock/Replayer";
const char* const kHelpUrl = "https://github.com/BorisBrock/Replayer/blob/main/docs/HELP.md";

QString appTitle() {
    return QString("%1 %2").arg(QApplication::applicationName(), QApplication::applicationVersion());
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(appTitle());
    resize(1440, 810);
    setAcceptDrops(true);

    m_fileBrowser = new FileBrowser(this);
    m_infoPanel   = new InfoPanel(this);

    // The panel scrolls when the window is too low for all of its cards.
    m_sidePanel = new QScrollArea(this);
    m_sidePanel->setObjectName("SidePanel");
    m_sidePanel->setWidget(m_infoPanel);
    m_sidePanel->setWidgetResizable(true);
    m_sidePanel->setFrameShape(QFrame::NoFrame);
    m_sidePanel->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Clip list | comparison bar + clip(s) | info and settings
    auto* center       = new QWidget(this);
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);
    m_grid = new ClipGrid(center);
    m_compareBar = createCompareBar();
    centerLayout->addWidget(m_compareBar);
    centerLayout->addWidget(m_grid, 1);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_fileBrowser);
    m_splitter->addWidget(center);
    m_splitter->addWidget(m_sidePanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setCollapsible(1, false);
    m_splitter->setSizes({240, 960, 240});
    setCentralWidget(m_splitter);

    createActions();

    connect(m_fileBrowser, &FileBrowser::openFolderClicked, this, &MainWindow::onOpenFolder);
    connect(m_fileBrowser, &FileBrowser::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_infoPanel, &InfoPanel::speedMultiplierChanged, this, [this](double speed) {
        m_speed = speed;
        forEachController([speed](PlayerController* c) { c->setSpeedMultiplier(speed); });
    });
    connect(m_infoPanel, &InfoPanel::zoomChanged, this, [this](double zoom) {
        ViewZoom z = m_zoom;
        z.setZoom(zoom);
        setZoom(z);
    });
    connect(m_infoPanel, &InfoPanel::zoomCenterRequested, this, [this](QPointF center) {
        ViewZoom z = m_zoom;
        z.setCenter(center);
        setZoom(z);
    });
    connect(m_infoPanel, &InfoPanel::zoomAtRequested, this, [this](double factor, QPointF point) {
        ViewZoom z = m_zoom;
        z.zoomAt(factor, point);
        setZoom(z);
    });
    connect(m_infoPanel, &InfoPanel::zoomResetRequested, this, [this] { setZoom(ViewZoom()); });
    connect(m_infoPanel, &InfoPanel::drawStyleChanged, this, [this](const DrawStyle& style) {
        for (ClipView* view : m_views) view->setDrawStyle(style);
    });
    connect(m_infoPanel, &InfoPanel::undoRequested, this, onCurrentAnnotations(&AnnotationDoc::undo));
    connect(m_infoPanel, &InfoPanel::redoRequested, this, onCurrentAnnotations(&AnnotationDoc::redo));
    connect(m_infoPanel, &InfoPanel::clearDrawingsRequested, this, onCurrentAnnotations(&AnnotationDoc::clear));

    // The navigator thumbnail follows the current clip, a few times a second.
    m_thumbnailTimer = new QTimer(this);
    m_thumbnailTimer->setSingleShot(true);
    m_thumbnailTimer->setInterval(200);
    connect(m_thumbnailTimer, &QTimer::timeout, this, &MainWindow::updateThumbnail);

    // Start with an empty player so the window doesn't look broken
    m_views << createView();
    m_grid->setViews(m_views);
    m_compareBar->hide();
}

QWidget* MainWindow::createCompareBar() {
    auto* bar = new QWidget(this);
    bar->setObjectName("CompareBar");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(2);

    m_compareLabel = new QLabel(bar);
    m_compareLabel->setObjectName("MutedLabel");
    layout->addWidget(m_compareLabel);
    layout->addSpacing(12);

    auto addButton = [&](Theme::Icon icon, const QString& toolTip) {
        auto* button = new QToolButton(bar);
        button->setIcon(Theme::icon(icon));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(toolTip);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        return button;
    };

    m_playAllButton = addButton(Theme::Icon::Play, "Play / pause all clips together (Space)");
    connect(m_playAllButton, &QToolButton::clicked, this, [this] { setPlayIntent(!anyPlaying()); });

    // Works in milliseconds of the current clip, like the transport bars.
    m_syncSlider = new QSlider(Qt::Horizontal, bar);
    auto* sliderStyle = new JumpSliderStyle();
    sliderStyle->setParent(m_syncSlider);
    m_syncSlider->setStyle(sliderStyle);
    m_syncSlider->setPageStep(1000);
    m_syncSlider->setFocusPolicy(Qt::NoFocus);
    m_syncSlider->setToolTip("Seek all clips together, keeping their offsets to each other");
    connect(m_syncSlider, &QSlider::valueChanged, this, &MainWindow::syncSeek);
    connect(m_syncSlider, &QSlider::sliderPressed, this, [this] { m_syncOffsetsValid = false; });
    connect(m_syncSlider, &QSlider::sliderReleased, this, [this] { m_syncOffsetsValid = false; });
    layout->addSpacing(6);
    layout->addWidget(m_syncSlider, 1);
    layout->addSpacing(12);

    // Tile arrangement (remembered across sessions), or one of the overlay
    // modes for exactly two clips
    auto* group = new QButtonGroup(bar);
    m_arrangementGroup = group;
    const struct {
        ClipGrid::Arrangement arrangement;
        Theme::Icon icon;
        const char* toolTip;
    } modes[] = {
        {ClipGrid::Arrangement::Auto, Theme::Icon::LayoutGrid, "Grid: arrange automatically"},
        {ClipGrid::Arrangement::SideBySide, Theme::Icon::LayoutSideBySide, "Side by side"},
        {ClipGrid::Arrangement::Stacked, Theme::Icon::LayoutStacked, "Stacked"},
    };
    const int saved = QSettings().value("compare/arrangement", 0).toInt();
    for (const auto& mode : modes) {
        QToolButton* button = addButton(mode.icon, mode.toolTip);
        button->setCheckable(true);
        const int id = static_cast<int>(mode.arrangement);
        group->addButton(button, id);
        button->setChecked(id == saved);
    }
    layout->addSpacing(8);
    m_blendButton = addButton(Theme::Icon::GhostBlend,
                              "Ghost: blend the second clip over the first (G)\n"
                              "Drag the line to change the mix, double-click it for 50/50");
    m_splitButton = addButton(Theme::Icon::GhostSplit,
                              "Ghost: first clip before the line, second after it (G)\n"
                              "Drag the line to move it; click here again to turn it");
    for (auto [button, id] : {std::pair{m_blendButton, kBlendId}, std::pair{m_splitButton, kSplitId}}) {
        button->setCheckable(true);
        group->addButton(button, id);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == kBlendId) {
            setOverlay(OverlayMode::Blend);
        } else if (id == kSplitId) {
            // Clicked again: the line turns between upright and across.
            if (m_overlay == OverlayMode::Split) m_horizontalSplit = !m_horizontalSplit;
            setOverlay(OverlayMode::Split);
        } else {
            setOverlay(OverlayMode::None);
            m_grid->setArrangement(static_cast<ClipGrid::Arrangement>(id));
            QSettings().setValue("compare/arrangement", id);
        }
    });
    m_grid->setArrangement(static_cast<ClipGrid::Arrangement>(saved));
    return bar;
}

void MainWindow::createActions() {
    // Actions are also added to the window itself so their shortcuts keep
    // working while the menu bar is hidden in full-screen mode.
    auto addAction = [this](QMenu* menu, const QString& text, const QList<QKeySequence>& keys,
                            auto slot) {
        QAction* action = menu->addAction(text);
        action->setShortcuts(keys);
        connect(action, &QAction::triggered, this, slot);
        QMainWindow::addAction(action);
        return action;
    };
    using Command = void (*)(PlayerController*);
    auto all = [this](Command fn) { return [this, fn] { forEachController(fn); }; };
    auto pauseAll = [this](Command fn) {
        return [this, fn] {
            m_playIntent = false;
            forEachController(fn);
        };
    };

    QMenu* fileMenu = menuBar()->addMenu("&File");
    addAction(fileMenu, "Open &Folder...", {QKeySequence("Ctrl+O")}, [this] { onOpenFolder(); });
    addAction(fileMenu, "Open F&ile...", {QKeySequence("Ctrl+Shift+O")}, [this] { onOpenFile(); });
    fileMenu->addSeparator();
    addAction(fileMenu, "&Quit", {QKeySequence("Ctrl+Q")}, [this] { close(); });

    // Drawings of the current clip
    QMenu* editMenu = menuBar()->addMenu("&Edit");
    addAction(editMenu, "&Undo Drawing", {QKeySequence("Ctrl+Z")}, onCurrentAnnotations(&AnnotationDoc::undo));
    addAction(editMenu, "&Redo Drawing", {QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")},
              onCurrentAnnotations(&AnnotationDoc::redo));
    addAction(editMenu, "&Clear Drawings", {QKeySequence("Ctrl+Delete")},
              onCurrentAnnotations(&AnnotationDoc::clear));

    // When comparing, these act on all clips at once.
    QMenu* playMenu = menuBar()->addMenu("&Playback");
    addAction(playMenu, "&Play / Pause", {Qt::Key_Space}, [this] { setPlayIntent(!anyPlaying()); });
    addAction(playMenu, "&Stop", {Qt::Key_S}, pauseAll([](PlayerController* c) { c->stop(); }));
    // The checkable action is the single source of truth for looping; the
    // transport bar buttons just mirror and toggle it.
    m_loopAction = addAction(playMenu, "&Loop Clip", {Qt::Key_L}, [] {});
    m_loopAction->setCheckable(true);
    connect(m_loopAction, &QAction::toggled, this, [this](bool on) {
        for (ClipView* view : m_views) {
            view->controller()->setLoop(on);
            view->transportBar()->setLoop(on);
        }
    });
    playMenu->addSeparator();
    addAction(playMenu, "&Next Frame", {Qt::Key_Period}, pauseAll([](PlayerController* c) { c->stepForward(); }));
    addAction(playMenu, "P&revious Frame", {Qt::Key_Comma}, pauseAll([](PlayerController* c) { c->stepBackward(); }));
    playMenu->addSeparator();
    addAction(playMenu, "Forward 1 s", {Qt::Key_Right}, all([](PlayerController* c) { c->seekRelative(1.0); }));
    addAction(playMenu, "Back 1 s", {Qt::Key_Left}, all([](PlayerController* c) { c->seekRelative(-1.0); }));
    addAction(playMenu, "Forward 5 s", {QKeySequence("Shift+Right")}, all([](PlayerController* c) { c->seekRelative(5.0); }));
    addAction(playMenu, "Back 5 s", {QKeySequence("Shift+Left")}, all([](PlayerController* c) { c->seekRelative(-5.0); }));
    playMenu->addSeparator();
    addAction(playMenu, "Ne&xt Clip", {Qt::Key_Down, Qt::Key_PageDown}, [this] { m_fileBrowser->selectNext(); });
    addAction(playMenu, "Previous &Clip", {Qt::Key_Up, Qt::Key_PageUp}, [this] { m_fileBrowser->selectPrevious(); });

    QMenu* viewMenu = menuBar()->addMenu("&View");
    addAction(viewMenu, "&Full Screen", {Qt::Key_F11, Qt::Key_F}, [this] { toggleFullScreen(); });
    viewMenu->addSeparator();
    auto zoomBy = [this](double factor) {
        return [this, factor] {
            ViewZoom z = m_zoom;
            z.zoomAt(factor, z.center);
            setZoom(z);
        };
    };
    addAction(viewMenu, "Zoom &In", {Qt::Key_Plus, Qt::Key_Equal}, zoomBy(1.25));
    addAction(viewMenu, "Zoom &Out", {Qt::Key_Minus}, zoomBy(1.0 / 1.25));
    addAction(viewMenu, "&Reset Zoom", {Qt::Key_0}, [this] { setZoom(ViewZoom()); });
    viewMenu->addSeparator();
    // Two clips: tiles -> blend -> split -> tiles
    addAction(viewMenu, "&Ghost Overlay", {Qt::Key_G}, [this] {
        if (m_views.size() != 2) return;
        setOverlay(m_overlay == OverlayMode::None    ? OverlayMode::Blend
                   : m_overlay == OverlayMode::Blend ? OverlayMode::Split
                                                     : OverlayMode::None);
    });
    viewMenu->addSeparator();
    createUnitsMenu(viewMenu->addMenu("&Units"));
    createAccentMenu(viewMenu->addMenu("&Accent Color"));

    QMenu* helpMenu = menuBar()->addMenu("&Help");
    addAction(helpMenu, "&User Guide", {QKeySequence(QKeySequence::HelpContents)},
              [] { QDesktopServices::openUrl(QUrl(kHelpUrl)); });
    addAction(helpMenu, "&GitHub Repository", {}, [] { QDesktopServices::openUrl(QUrl(kRepoUrl)); });
    helpMenu->addSeparator();
    addAction(helpMenu, "&About Replayer...", {}, [this] { showAbout(); });
    QAction* escape = new QAction(this);
    escape->setShortcut(Qt::Key_Escape);
    connect(escape, &QAction::triggered, this, [this] {
        // Leave full screen first; then put the drawing tool away.
        if (isFullScreen()) exitFullScreen();
        else m_infoPanel->setDrawTool(DrawTool::None);
    });
    QMainWindow::addAction(escape);
}

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle("About " + QApplication::applicationName());
    box.setIconPixmap(QApplication::windowIcon().pixmap(96));
    box.setTextFormat(Qt::RichText);
    box.setText(QString("<h2>%1</h2>Version %2").arg(QApplication::applicationName(), QApplication::applicationVersion()));
    box.setInformativeText(QString("Video player for high-frame-rate action cam footage<br><br><a href=\"%1\">%1</a>").arg(kRepoUrl));
    box.setTextInteractionFlags(Qt::TextBrowserInteraction); // clickable link
    box.exec();
}

// Speeds in km/h or mph, remembered between sessions
void MainWindow::createUnitsMenu(QMenu* menu) {
    auto* group = new QActionGroup(this);
    const auto saved = static_cast<UnitSystem>(QSettings().value("measure/units", 0).toInt());
    const std::pair<UnitSystem, const char*> choices[] = {
        {UnitSystem::Metric, "&Metric (km/h)"},
        {UnitSystem::Imperial, "&Imperial (mph)"},
    };
    for (const auto& [units, text] : choices) {
        QAction* action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(units == saved);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, units] {
            QSettings().setValue("measure/units", static_cast<int>(units));
            m_infoPanel->setUnitSystem(units);
        });
    }
    m_infoPanel->setUnitSystem(saved);
}

void MainWindow::createAccentMenu(QMenu* menu) {
    auto* group = new QActionGroup(this);
    auto saveAccent = [](const QColor& color) {
        Theme::setAccent(color);
        QSettings().setValue("appearance/accent", color.name());
    };

    for (const Theme::Preset& preset : Theme::accentPresets()) {
        QAction* action = menu->addAction(Theme::swatch(preset.color), preset.name);
        action->setCheckable(true);
        action->setData(preset.color);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [=] { saveAccent(preset.color); });
    }
    menu->addSeparator();
    QAction* custom = menu->addAction("&Custom...");
    custom->setCheckable(true);
    group->addAction(custom);
    connect(custom, &QAction::triggered, this, [=] {
        const QColor color = QColorDialog::getColor(Theme::accent(), this, "Accent Color");
        if (color.isValid()) saveAccent(color);
    });

    // Tick the entry matching the current accent (custom colours, or a
    // cancelled colour dialog, land here too).
    connect(menu, &QMenu::aboutToShow, this, [=] {
        QAction* match = custom;
        for (QAction* action : group->actions()) {
            if (action != custom && action->data().value<QColor>() == Theme::accent()) match = action;
        }
        match->setChecked(true);
        custom->setIcon(match == custom ? Theme::swatch(Theme::accent()) : QIcon());
    });
}

ClipView* MainWindow::createView() {
    auto* view = new ClipView(m_grid);
    PlayerController* c = view->controller();
    c->setSpeedMultiplier(m_speed);
    c->setLoop(m_loopAction->isChecked());
    view->transportBar()->setLoop(m_loopAction->isChecked());
    view->setDrawStyle(m_infoPanel->drawStyle());

    connect(view, &ClipView::activated, this, [this, view] {
        if (m_views.size() < 2 || view->path() == m_currentClip) return;
        setCurrentClip(view->path());
        m_fileBrowser->selectFiles(m_fileBrowser->selectedFiles(), view->path());
        saveSession();
    });
    connect(view, &ClipView::closeRequested, this, [this, view] { m_fileBrowser->deselectFile(view->path()); });
    connect(view, &ClipView::doubleClicked, this, &MainWindow::toggleFullScreen);
    connect(view, &ClipView::playRequested, this, [this](bool play) { m_playIntent = play; });
    connect(view, &ClipView::videoInfoReady, this, [this, view] {
        if (view->path() == m_currentClip) setCurrentClip(m_currentClip);
    });
    connect(view, &ClipView::errorOccurred, this, [this, view](const QString& error) {
        statusBar()->showMessage(QString("%1: %2").arg(QFileInfo(view->path()).fileName(), error), 8000);
    });
    connect(view->transportBar(), &TransportBar::loopToggled, m_loopAction, &QAction::setChecked);

    view->setZoom(m_zoom);
    connect(view, &ClipView::zoomAtRequested, this, [this](double factor, QPointF point) {
        ViewZoom z = m_zoom;
        z.zoomAt(factor, point);
        setZoom(z);
    });
    connect(view, &ClipView::panRequested, this, [this](QPointF delta) {
        ViewZoom z = m_zoom;
        z.panBy(delta);
        setZoom(z);
    });
    connect(c, &PlayerController::frameReady, this, [this, view] {
        if (view->path() == m_currentClip && !m_thumbnailTimer->isActive()) m_thumbnailTimer->start();
    });
    connect(c, &PlayerController::playbackStateChanged, this, &MainWindow::updatePlayAllButton);
    connect(c, &PlayerController::positionChanged, this, [this, view] {
        if (view->path() == m_currentClip) updateSyncSlider();
    });
    return view;
}

void MainWindow::onSelectionChanged() {
    const QStringList paths = m_fileBrowser->selectedFiles();
    if (!paths.isEmpty()) showClips(paths, m_fileBrowser->currentFile());
}

void MainWindow::showClips(QStringList paths, const QString& current) {
    if (paths.size() > kMaxClips) {
        statusBar()->showMessage(QString("Comparing the first %1 of %2 selected clips")
                                     .arg(kMaxClips).arg(paths.size()), 8000);
        paths = paths.mid(0, kMaxClips);
    } else {
        statusBar()->clearMessage();
    }
    const bool compare = paths.size() > 1;

    // Views of clips that stay selected are kept as they are. Views of
    // deselected clips are reused for newly selected ones, which keeps their
    // decoder and GL context warm (fast switching); the rest are removed.
    QList<ClipView*> spare;
    for (ClipView* view : m_views) {
        if (!paths.contains(view->path())) {
            rememberPosition(view);
            spare << view;
        }
    }
    QList<ClipView*> views;
    for (const QString& path : paths) {
        ClipView* view = nullptr;
        for (ClipView* v : m_views) {
            if (v->path() == path) view = v;
        }
        if (!view) {
            view = spare.isEmpty() ? createView() : spare.takeFirst();
            view->setAnnotations(annotationsFor(path));
            if (view->open(path, m_positions.value(path, 0.0)) && m_playIntent)
                view->controller()->play();
        }
        views << view;
    }
    qDeleteAll(spare);

    m_views = views;
    m_syncOffsets.clear();
    m_syncOffsetsValid = false;
    for (ClipView* view : m_views) view->setCompact(compare);
    m_grid->setViews(m_views);
    m_compareBar->setVisible(compare);
    m_compareLabel->setText(QString("Comparing %1 clips").arg(m_views.size()));
    applyOverlay();

    setCurrentClip(paths.contains(current) ? current : paths.first());
    setWindowTitle(compare ? QString("Comparing %1 clips - %2").arg(m_views.size()).arg(appTitle())
                           : QString("%1 - %2").arg(QFileInfo(paths.first()).fileName(), appTitle()));
    updatePlayAllButton();
    saveSession();
}

void MainWindow::setCurrentClip(const QString& path) {
    const bool changed = path != m_currentClip;
    m_currentClip = path;
    if (changed) updateThumbnail();
    m_infoPanel->setAnnotations(currentAnnotations());
    for (ClipView* view : m_views) {
        view->setCurrent(m_views.size() > 1 && view->path() == path);
        if (view->path() != path) continue;
        if (view->controller()->isOpen())
            m_infoPanel->setVideoInfo(QFileInfo(path).fileName(), view->controller()->videoInfo());
        else
            m_infoPanel->clearVideoInfo();
    }
    updateSyncSlider();
}

void MainWindow::rememberPosition(ClipView* view) {
    if (!view->path().isEmpty() && view->controller()->isOpen())
        m_positions.insert(view->path(), view->controller()->position());
}

void MainWindow::saveSession() {
    QSettings settings;
    QStringList clips;
    for (ClipView* view : m_views) clips << view->path();
    settings.setValue("session/clips", clips);
    settings.setValue("session/current", m_currentClip);
}

void MainWindow::restoreSession() {
    const QSettings settings;
    const QString folder = settings.value("session/folder").toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) return;

    // Show where we left off, but don't start playing on launch.
    m_playIntent = false;
    m_fileBrowser->setFolder(folder);
    QStringList clips;
    for (const QString& clip : settings.value("session/clips").toStringList()) {
        if (QFileInfo(clip).isFile() && QFileInfo(clip).absolutePath() == m_fileBrowser->folder())
            clips << clip;
    }
    if (clips.isEmpty()) {
        m_fileBrowser->selectNext(); // first clip
        return;
    }
    const QString current = settings.value("session/current").toString();
    m_fileBrowser->selectFiles(clips, current);
    showClips(clips, current);
}

void MainWindow::open(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir()) {
        m_fileBrowser->setFolder(info.absoluteFilePath());
        QSettings().setValue("session/folder", m_fileBrowser->folder());
        m_fileBrowser->selectNext(); // first clip
    } else if (info.isFile()) {
        if (m_fileBrowser->folder() != info.absolutePath()) {
            m_fileBrowser->setFolder(info.absolutePath());
            QSettings().setValue("session/folder", m_fileBrowser->folder());
        }
        m_fileBrowser->selectFiles({info.absoluteFilePath()});
        showClips({info.absoluteFilePath()}, info.absoluteFilePath());
    } else {
        statusBar()->showMessage(QString("Not found: %1").arg(QDir::toNativeSeparators(path)), 8000);
    }
}

void MainWindow::forEachController(const std::function<void(PlayerController*)>& fn) {
    for (ClipView* view : m_views) {
        if (view->controller()->isOpen()) fn(view->controller());
    }
}

bool MainWindow::anyPlaying() const {
    for (ClipView* view : m_views) {
        if (view->controller()->isPlaying()) return true;
    }
    return false;
}

void MainWindow::setPlayIntent(bool play) {
    m_playIntent = play;
    // Started back to back, so the clips stay in step.
    forEachController([play](PlayerController* c) { play ? c->play() : c->pause(); });
}

void MainWindow::setOverlay(OverlayMode mode) {
    m_overlay = mode;
    applyOverlay();
}

void MainWindow::applyOverlay() {
    const bool pair = m_views.size() == 2;
    const OverlayMode mode = pair ? m_overlay : OverlayMode::None;
    const bool on = mode != OverlayMode::None;
    for (qsizetype i = 0; i < m_views.size(); i++) {
        m_views[i]->setGhost(on && i == 0 ? m_views[1] : nullptr, mode, m_horizontalSplit);
        m_views[i]->setVideoHidden(on && i == 1);
    }
    m_grid->setOverlay(on);

    m_blendButton->setVisible(pair);
    m_splitButton->setVisible(pair);
    m_splitButton->setIcon(Theme::icon(m_horizontalSplit ? Theme::Icon::GhostSplitHorizontal
                                                         : Theme::Icon::GhostSplit));
    const int id = mode == OverlayMode::Blend ? kBlendId
                 : mode == OverlayMode::Split ? kSplitId
                                              : static_cast<int>(m_grid->arrangement());
    if (QAbstractButton* button = m_arrangementGroup->button(id)) button->setChecked(true);
}

void MainWindow::setZoom(ViewZoom zoom) {
    zoom.clamp();
    m_zoom = zoom;
    for (ClipView* view : m_views) view->setZoom(zoom);
    m_infoPanel->setZoom(zoom);
}

AnnotationDoc* MainWindow::annotationsFor(const QString& path) {
    AnnotationDoc*& doc = m_annotations[path];
    if (!doc) {
        doc = new AnnotationDoc(this);
        connect(doc, &AnnotationDoc::changed, this, [this, doc] {
            if (doc == currentAnnotations()) m_infoPanel->setAnnotations(doc);
        });
    }
    return doc;
}

AnnotationDoc* MainWindow::currentAnnotations() const {
    return m_currentClip.isEmpty() ? nullptr : m_annotations.value(m_currentClip);
}

std::function<void()> MainWindow::onCurrentAnnotations(void (AnnotationDoc::*fn)()) {
    return [this, fn] {
        if (AnnotationDoc* doc = currentAnnotations()) (doc->*fn)();
    };
}

void MainWindow::updateThumbnail() {
    for (ClipView* view : m_views) {
        if (view->path() != m_currentClip) continue;
        const FramePtr frame = view->currentFrame();
        QImage image = makeThumbnail(frame.get(), 320);
        const int rotation = view->controller()->videoInfo().rotation;
        if (!image.isNull() && rotation != 0) image = image.transformed(QTransform().rotate(rotation));
        m_infoPanel->setThumbnail(image);
    }
}

void MainWindow::updatePlayAllButton() {
    m_playAllButton->setIcon(Theme::icon(anyPlaying() ? Theme::Icon::Pause : Theme::Icon::Play));
}

PlayerController* MainWindow::currentController() const {
    for (ClipView* view : m_views) {
        if (view->path() == m_currentClip && view->controller()->isOpen()) return view->controller();
    }
    return nullptr;
}

void MainWindow::captureSyncOffsets() {
    m_syncOffsets.clear();
    m_syncOffsetsValid = false;
    PlayerController* current = currentController();
    if (!current) return;

    const double origin = current->position();
    m_syncMin = 0.0;
    m_syncMax = current->videoInfo().duration;
    for (ClipView* view : m_views) {
        PlayerController* c = view->controller();
        if (!c->isOpen()) continue;
        const double offset = c->position() - origin;
        m_syncOffsets.append({c, offset});
        m_syncMin = std::max(m_syncMin, -offset);
        m_syncMax = std::min(m_syncMax, c->videoInfo().duration - offset);
    }
    m_syncOffsetsValid = true;
}

void MainWindow::syncSeek(int ms) {
    // Offsets are captured afresh for each wheel step or click, and once per
    // drag, so adjustments made in a single clip in between are picked up.
    // (A click on the groove changes the value before the slider is down.)
    if (!m_syncOffsetsValid || !m_syncSlider->isSliderDown()) captureSyncOffsets();
    if (m_syncOffsets.isEmpty()) return;

    // Stop where the first clip reaches its start or end; letting that clip
    // stand still while the others move on would lose its offset.
    const double position = std::clamp(ms / 1000.0, m_syncMin, std::max(m_syncMin, m_syncMax));
    const int clampedMs = static_cast<int>(std::lround(position * 1000.0));
    if (clampedMs != ms) {
        const QSignalBlocker blocker(m_syncSlider);
        m_syncSlider->setValue(clampedMs);
    }
    for (const auto& [controller, offset] : m_syncOffsets) controller->seek(position + offset);
}

void MainWindow::updateSyncSlider() {
    PlayerController* current = currentController();
    m_syncSlider->setEnabled(current != nullptr);
    if (m_syncSlider->isSliderDown()) return; // don't fight the user while they drag
    const QSignalBlocker blocker(m_syncSlider);
    m_syncSlider->setRange(0, current ? static_cast<int>(current->videoInfo().duration * 1000.0) : 0);
    m_syncSlider->setValue(current ? static_cast<int>(current->position() * 1000.0) : 0);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& url : event->mimeData()->urls()) {
        const QFileInfo info(url.toLocalFile());
        if (info.isDir() || FileBrowser::isVideoFile(info.filePath())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        const QFileInfo info(url.toLocalFile());
        if (info.isDir() || FileBrowser::isVideoFile(info.filePath())) {
            open(info.absoluteFilePath());
            return;
        }
    }
}

void MainWindow::onOpenFolder() {
    const QString folder = QFileDialog::getExistingDirectory(this, "Open Folder", m_fileBrowser->folder());
    if (!folder.isEmpty()) open(folder);
}

void MainWindow::onOpenFile() {
    const QString filter = QString("Video Files (%1);;All Files (*)").arg(FileBrowser::nameFilters().join(' '));
    const QString file = QFileDialog::getOpenFileName(this, "Open Video File", m_fileBrowser->folder(), filter);
    if (!file.isEmpty()) open(file);
}

void MainWindow::toggleFullScreen() {
    const bool fullScreen = !isFullScreen();
    menuBar()->setVisible(!fullScreen);
    statusBar()->setVisible(!fullScreen);
    m_fileBrowser->setVisible(!fullScreen);
    m_sidePanel->setVisible(!fullScreen);
    setWindowState(windowState() ^ Qt::WindowFullScreen);
}

void MainWindow::exitFullScreen() {
    if (isFullScreen()) toggleFullScreen();
}
