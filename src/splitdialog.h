#ifndef SPLITDIALOG_H
#define SPLITDIALOG_H

#include <QDialog>
#include <QThreadPool>
#include <atomic>

#include "clipref.h"

class ClipView;
class MarkerBar;
class QLabel;
class QPushButton;

// Splits a long recording (several jumps filmed in one go) into parts that
// the clip list then shows like separate clips. The user sets markers while
// watching the clip, or lets motion detection suggest them.
class SplitDialog : public QDialog {
    Q_OBJECT

public:
    SplitDialog(const QString& path, const QList<ClipRef>& parts, QWidget* parent = nullptr);
    ~SplitDialog() override;

    // The parts to keep; empty when the clip isn't split (any more).
    QList<ClipRef> parts() const;

protected:
    void done(int result) override;

private:
    void addShortcut(const QKeySequence& key, const std::function<void()>& fn);
    void suggestMarkers();
    void stopAnalysis();
    void onAnalysisDone(const QList<std::pair<double, double>>& activity,
                        const QList<std::pair<double, double>>& parts);
    void updateSummary();

    QString      m_path;
    ClipView*    m_view;
    MarkerBar*   m_markerBar;
    QLabel*      m_summary;
    QPushButton* m_suggestButton;

    QThreadPool       m_pool;
    std::atomic<bool> m_cancel{false};
    bool              m_analyzing = false;
};

#endif
