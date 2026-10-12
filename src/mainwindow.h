#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <functional>
#include <utility>

#include "viewzoom.h"

class AnnotationDoc;
class ClipGrid;
class ClipView;
class FileBrowser;
class InfoPanel;
class PlayerController;
class QLabel;
class QMenu;
class QScrollArea;
class QSlider;
class QSplitter;
class QTimer;
class QToolButton;

// Shows the clips selected in the file browser: one normally, several side
// by side in comparison mode. Each clip has its own controls; the keyboard
// and the comparison bar act on all of them together.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Opens a folder, or the folder of a file with that file selected.
    void open(const QString& path);

    // Reopens the folder and clips of the previous session, paused.
    void restoreSession();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpenFolder();
    void onOpenFile();
    void onSelectionChanged();
    void toggleFullScreen();
    void exitFullScreen();

private:
    void createActions();
    void createUnitsMenu(QMenu* menu);
    void createAccentMenu(QMenu* menu);
    QWidget* createCompareBar();

    void showClips(QStringList paths, const QString& current);
    ClipView* createView();
    void setCurrentClip(const QString& path);
    void rememberPosition(ClipView* view);
    void saveSession();

    // Keyboard and comparison-bar commands act on every visible clip.
    void forEachController(const std::function<void(PlayerController*)>& fn);
    bool anyPlaying() const;
    void setPlayIntent(bool play);
    void updatePlayAllButton();

    // The comparison bar's seek bar shows the current clip and moves all
    // clips by the same amount, keeping the offsets the user set up between
    // them (e.g. each clip cued to where the action starts).
    PlayerController* currentController() const;
    void captureSyncOffsets();
    void syncSeek(int ms);
    void updateSyncSlider();

    // Zoom is shared by all clips (they usually come from one static camera).
    void setZoom(ViewZoom zoom);
    void updateThumbnail();

    // Drawings are kept per clip (this session only); the tools are shared.
    AnnotationDoc* annotationsFor(const QString& path);
    AnnotationDoc* currentAnnotations() const;
    std::function<void()> onCurrentAnnotations(void (AnnotationDoc::*fn)());

    FileBrowser* m_fileBrowser;
    ClipGrid*    m_grid;
    InfoPanel*   m_infoPanel;
    QScrollArea* m_sidePanel;
    QSplitter*   m_splitter;
    QWidget*     m_compareBar;
    QLabel*      m_compareLabel;
    QToolButton* m_playAllButton;
    QSlider*     m_syncSlider;
    QAction*     m_loopAction = nullptr;

    // Each clip's offset to the current clip, captured when a seek with the
    // comparison bar starts, and the range the current clip can then move in
    // without any clip running past its start or end.
    QList<std::pair<PlayerController*, double>> m_syncOffsets;
    bool   m_syncOffsetsValid = false;
    double m_syncMin = 0.0;
    double m_syncMax = 0.0;

    QList<ClipView*>       m_views;
    QString                m_currentClip;       // shown in the info panel
    QHash<QString, double> m_positions;         // per clip, this session only
    QHash<QString, AnnotationDoc*> m_annotations;
    bool                   m_playIntent = true; // survives clip ends and switches
    double                 m_speed      = 1.0;
    ViewZoom               m_zoom;
    QTimer*                m_thumbnailTimer = nullptr;
};

#endif
