#include "splitdialog.h"
#include "clipview.h"
#include "markerbar.h"
#include "motionanalyzer.h"
#include "playercontroller.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

SplitDialog::SplitDialog(const QString& path, const QList<ClipRef>& parts, QWidget* parent)
    : QDialog(parent), m_path(path) {
    setWindowTitle(QString("Split %1").arg(QFileInfo(path).fileName()));
    resize(1000, 720);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto* intro = new QLabel(
        "Set a marker between the jumps (M sets one at the current frame); every part then "
        "becomes a clip of its own in the clip list. Drag markers to move them, double-click "
        "a part to skip it, e.g. the walk back up.", this);
    intro->setObjectName("MutedLabel");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_view = new ClipView(this);
    layout->addWidget(m_view, 1);

    m_markerBar = new MarkerBar(this);
    layout->addWidget(m_markerBar);

    auto* row = new QHBoxLayout();
    auto addButton = [&](const QString& text, const QString& toolTip) {
        auto* button = new QPushButton(text, this);
        button->setToolTip(toolTip);
        button->setFocusPolicy(Qt::NoFocus); // keep Space for play/pause
        row->addWidget(button);
        return button;
    };
    QPushButton* addMarker = addButton("Add Marker", "Set a marker at the current frame (M)");
    addMarker->setIcon(Theme::icon(Theme::Icon::Cut));
    QPushButton* removeMarker = addButton("Remove Marker", "Remove the selected marker (Delete)");
    QPushButton* clearMarkers = addButton("Clear", "Remove all markers");
    row->addSpacing(16);
    m_suggestButton = addButton("Suggest Markers",
                                "Find the jumps by motion, for clips filmed from a fixed camera");
    row->addSpacing(16);
    m_summary = new QLabel(this);
    m_summary->setObjectName("MutedLabel");
    row->addWidget(m_summary, 1);
    layout->addLayout(row);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Apply");
    buttons->button(QDialogButtonBox::Ok)->setObjectName("PrimaryButton");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    PlayerController* c = m_view->controller();
    connect(c, &PlayerController::positionChanged, m_markerBar, &MarkerBar::setPosition);
    connect(c, &PlayerController::videoInfoReady, this, [this, parts](const VideoInfo& info) {
        m_markerBar->setDuration(info.duration, info.fps);
        QList<std::pair<double, double>> ranges;
        for (const ClipRef& part : parts) ranges.append({part.start, part.end});
        if (!ranges.isEmpty()) m_markerBar->setKeptParts(ranges);
    });
    connect(m_view, &ClipView::errorOccurred, m_summary, &QLabel::setText);
    connect(m_markerBar, &MarkerBar::seekRequested, c, &PlayerController::seek);
    connect(m_markerBar, &MarkerBar::changed, this, &SplitDialog::updateSummary);
    connect(addMarker, &QPushButton::clicked, m_markerBar, &MarkerBar::addMarkerAtPosition);
    connect(removeMarker, &QPushButton::clicked, m_markerBar, &MarkerBar::removeSelectedMarker);
    connect(clearMarkers, &QPushButton::clicked, m_markerBar, &MarkerBar::clear);
    connect(m_suggestButton, &QPushButton::clicked, this, &SplitDialog::suggestMarkers);

    // The main window's keys, which don't reach a dialog
    addShortcut(Qt::Key_Space, [c] { c->togglePlayPause(); });
    addShortcut(Qt::Key_S, [c] { c->stop(); });
    addShortcut(Qt::Key_Period, [c] { c->stepForward(); });
    addShortcut(Qt::Key_Comma, [c] { c->stepBackward(); });
    addShortcut(Qt::Key_Right, [c] { c->seekRelative(1.0); });
    addShortcut(Qt::Key_Left, [c] { c->seekRelative(-1.0); });
    addShortcut(QKeySequence("Shift+Right"), [c] { c->seekRelative(5.0); });
    addShortcut(QKeySequence("Shift+Left"), [c] { c->seekRelative(-5.0); });
    addShortcut(Qt::Key_M, [this] { m_markerBar->addMarkerAtPosition(); });
    addShortcut(Qt::Key_Delete, [this] { m_markerBar->removeSelectedMarker(); });

    m_view->open(path, 0.0);
    updateSummary();
}

SplitDialog::~SplitDialog() {
    m_cancel = true;
    m_pool.waitForDone();
}

void SplitDialog::addShortcut(const QKeySequence& key, const std::function<void()>& fn) {
    auto* shortcut = new QShortcut(key, this);
    connect(shortcut, &QShortcut::activated, this, fn);
}

QList<ClipRef> SplitDialog::parts() const {
    const QList<std::pair<double, double>> kept = m_markerBar->keptParts();
    // One part that is the whole clip: not split
    if (!m_markerBar->hasMarkers()) return {};
    QList<ClipRef> result;
    for (const auto& [start, end] : kept) result << ClipRef{m_path, start, end};
    return result;
}

void SplitDialog::done(int result) {
    stopAnalysis();
    if (result == Accepted && m_markerBar->hasMarkers() && m_markerBar->keptCount() == 0) {
        QMessageBox::information(this, "Split Clip", "Every part is skipped. Keep at least one part.");
        return;
    }
    QDialog::done(result);
}

void SplitDialog::updateSummary() {
    if (m_analyzing) return;
    if (!m_markerBar->hasMarkers()) {
        m_summary->setText("Not split");
        return;
    }
    const int kept = m_markerBar->keptCount();
    const int skipped = m_markerBar->partCount() - kept;
    QString text = kept == 1 ? QString("1 part") : QString("%1 parts").arg(kept);
    if (skipped > 0) text += QString(", %1 skipped").arg(skipped);
    m_summary->setText(text);
}

void SplitDialog::suggestMarkers() {
    if (m_analyzing) {
        stopAnalysis();
        return;
    }
    if (m_markerBar->hasMarkers()
        && QMessageBox::question(this, "Suggest Markers", "Replace the current markers with suggested ones?")
               != QMessageBox::Yes) {
        return;
    }
    m_pool.waitForDone(); // a stopped analysis winding down

    m_cancel = false;
    m_analyzing = true;
    m_suggestButton->setText("Stop");
    m_summary->setText("Looking for motion...");

    const QString path = m_path;
    const double duration = m_view->controller()->videoInfo().duration;
    m_pool.start([this, path, duration] {
        int percent = -1;
        const QList<MotionAnalyzer::Sample> samples = MotionAnalyzer::analyze(path, m_cancel, [&](double done) {
            const int now = static_cast<int>(done * 100.0);
            if (now == percent) return;
            percent = now;
            QMetaObject::invokeMethod(this, [this, now] {
                if (m_analyzing) m_summary->setText(QString("Looking for motion... %1%").arg(now));
            }, Qt::QueuedConnection);
        });
        if (m_cancel) return;

        QList<std::pair<double, double>> activity;
        for (const MotionAnalyzer::Sample& s : samples) activity.append({s.time, s.motion});
        const QList<std::pair<double, double>> parts = MotionAnalyzer::suggestParts(samples, duration);
        QMetaObject::invokeMethod(this, [this, activity, parts] { onAnalysisDone(activity, parts); },
                                  Qt::QueuedConnection);
    });
}

void SplitDialog::stopAnalysis() {
    if (!m_analyzing) return;
    m_cancel = true;
    m_analyzing = false;
    m_suggestButton->setText("Suggest Markers");
    updateSummary();
}

void SplitDialog::onAnalysisDone(const QList<std::pair<double, double>>& activity,
                                 const QList<std::pair<double, double>>& parts) {
    if (!m_analyzing) return;
    m_analyzing = false;
    m_suggestButton->setText("Suggest Markers");
    m_markerBar->setActivity(activity);
    if (activity.isEmpty()) {
        m_summary->setText("Could not read the clip");
    } else if (parts.isEmpty()) {
        m_summary->setText("No separate jumps found (is the camera on a tripod?)");
    } else {
        m_markerBar->setKeptParts(parts);
        m_summary->setText(QString("%1 suggested; check the markers before applying")
                               .arg(parts.size() == 1 ? QString("1 part") : QString("%1 parts").arg(parts.size())));
    }
}
