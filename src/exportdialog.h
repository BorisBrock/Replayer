#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QThreadPool>
#include <atomic>

#include "clipexporter.h"

class ClipView;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QWidget;
class RangeBar;

// Saves the clip's frame as an image, or a part of it as an MP4 video or a
// GIF animation, with or without the drawings. The dialog has a player of
// its own to pick the frame or set the start and end of the part.
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    // source: the clip's view in the main window, for its drawings as they
    // look there
    explicit ExportDialog(ClipView* source, QWidget* parent = nullptr);
    ~ExportDialog() override;

protected:
    void done(int result) override;

private:
    void addShortcut(const QKeySequence& key, const std::function<void()>& fn);
    ExportFormat format() const;
    QSize outputSize() const;
    ExportSettings settings() const; // without the file
    void fillSizes();
    void fillFrameRates();
    void updateControls();  // after the format changed
    void updateSummary();
    void startExport();
    void stopExport();
    void onExportDone(bool ok, const QString& file, const QString& error);

    QPointer<ClipView> m_source;
    QString      m_path;          // ClipRef key
    QSizeF       m_displaySize;   // of the video, after rotation
    int          m_rotation  = 0;
    double       m_sourceFps = 30.0;

    ClipView*     m_view;
    RangeBar*     m_rangeBar;
    QWidget*      m_rangeRow;
    QLabel*       m_rangeHint;
    QButtonGroup* m_formatGroup;
    QLabel*       m_formatHint;
    QCheckBox*    m_annotations;
    QComboBox*    m_timeStamp;
    QComboBox*    m_size;
    QWidget*      m_qualityRow;
    QSlider*      m_quality;
    QLabel*       m_qualityValue;
    QWidget*      m_motionCard;
    QComboBox*    m_speed;
    QComboBox*    m_frameRate;
    QCheckBox*    m_loop;
    QCheckBox*    m_dither;
    QLabel*       m_summary;
    QLabel*       m_warning;
    QProgressBar* m_progress;
    QLabel*       m_status;
    QPushButton*  m_showButton;
    QPushButton*  m_exportButton;
    QList<QWidget*> m_settingsWidgets; // disabled while exporting

    QString           m_lastFile;
    bool              m_rangeLoop = false;
    QThreadPool       m_pool;
    std::atomic<bool> m_cancel{false};
    bool              m_exporting = false;
};

#endif
