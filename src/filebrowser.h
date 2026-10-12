#ifndef FILEBROWSER_H
#define FILEBROWSER_H

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QThreadPool>
#include <QWidget>

class QFileSystemWatcher;
class QLabel;
class QListWidget;

// Lists the video files of one folder. Selecting entries (mouse or
// keyboard; Ctrl/Shift for several) emits selectionChanged(). The list
// follows changes on disk, e.g. clips being copied in from a camera.
// Clip durations are read in the background and shown next to the names.
class FileBrowser : public QWidget {
    Q_OBJECT

public:
    explicit FileBrowser(QWidget* parent = nullptr);
    ~FileBrowser() override;

    static bool isVideoFile(const QString& path);
    static QStringList nameFilters(); // "*.mp4", "*.MP4", ...

    bool setFolder(const QString& folder);
    QString folder() const { return m_folder; }

    // Selected files in list order, and the one last clicked
    QStringList selectedFiles() const;
    QString currentFile() const;

    // Select files without emitting selectionChanged(); absent ones are skipped.
    void selectFiles(const QStringList& paths, const QString& current = {});
    void deselectFile(const QString& path);
    // Move to the next/previous clip, selecting only that one.
    void selectNext();
    void selectPrevious();

signals:
    void openFolderClicked();
    void selectionChanged();

private:
    void refresh();
    void scheduleSelectionChanged();
    int rowOf(const QString& path) const;
    void updateDuration(int row);
    void onDurationProbed(const QString& path, const QDateTime& modified, qint64 size, double duration);

    // A probed duration stays valid while the file is unchanged.
    struct Probe {
        QDateTime modified;
        qint64    size     = -1;
        double    duration = -1.0; // -1: unreadable
    };
    QHash<QString, Probe> m_durations;
    QSet<QString>         m_probing;
    QThreadPool           m_probePool;

    bool                 m_changePending = false;
    QLabel*              m_folderLabel;
    QListWidget*         m_list;
    QFileSystemWatcher*  m_watcher;
    QString              m_folder;
};

#endif
