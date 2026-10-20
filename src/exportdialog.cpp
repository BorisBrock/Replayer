#include "exportdialog.h"
#include "annotations.h"
#include "clipref.h"
#include "clipview.h"
#include "playercontroller.h"
#include "rangebar.h"
#include "theme.h"
#include "timeformat.h"
#include "transportbar.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
#include <iterator>

namespace {

struct FormatInfo {
    ExportFormat format;
    const char*  name;
    const char*  label;
    const char*  hint;
    const char*  filter;
};

const FormatInfo kFormats[] = {
    {ExportFormat::Png, "PNG", "PNG image", "Lossless still of the frame shown, for the sharpest picture.",
     "PNG Images (*.png)"},
    {ExportFormat::Jpeg, "JPEG", "JPEG image", "Compressed still of the frame shown: a much smaller file, for sharing.",
     "JPEG Images (*.jpg *.jpeg)"},
    {ExportFormat::Mp4, "MP4", "MP4 video", "H.264 video of the part between the markers. Plays almost anywhere.",
     "MP4 Videos (*.mp4)"},
    {ExportFormat::Gif, "GIF", "GIF animation", "Animation of the part between the markers, for chats and the web. "
     "Best kept short and small.", "GIF Animations (*.gif)"},
};

const FormatInfo& infoOf(ExportFormat format) {
    for (const FormatInfo& info : kFormats) {
        if (info.format == format) return info;
    }
    return kFormats[0];
}

QString settingsKey(const QString& name, ExportFormat format) {
    return QString("export/%1/%2").arg(name, formatSuffix(format));
}

int evenDown(double value) {
    return std::max(2, static_cast<int>(value) / 2 * 2);
}

QString fileSize(qint64 bytes) {
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(std::max<qint64>(1, bytes / 1024));
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace

ExportDialog::ExportDialog(ClipView* source, QWidget* parent)
    : QDialog(parent), m_source(source), m_path(source->path()) {
    const ClipRef clip = ClipRef::fromKey(m_path);
    setWindowTitle(QString("Export %1").arg(clip.displayName()));
    // Smaller screens, or large scaling, get what fits; the settings scroll
    const QRect screenArea = (parent ? parent : source)->screen()->availableGeometry();
    resize(std::min(1180, screenArea.width() * 95 / 100), std::min(760, screenArea.height() * 90 / 100));
    m_pool.setMaxThreadCount(1);

    const VideoInfo& info = source->controller()->videoInfo();
    m_displaySize = source->displaySize();
    m_rotation    = info.rotation;
    if (info.fps > 0.0) m_sourceFps = info.fps;

    auto* layout = new QHBoxLayout(this);
    layout->setSpacing(16);

    // Left: the player, and the part to export
    auto* left = new QVBoxLayout();
    left->setSpacing(10);
    m_view = new ClipView(this);
    m_view->setContextMenuPolicy(Qt::PreventContextMenu); // no export from the export dialog
    left->addWidget(m_view, 1);
    m_rangeBar = new RangeBar(this);
    left->addWidget(m_rangeBar);

    auto* row = new QHBoxLayout();
    m_rangeRow = new QWidget(this);
    auto* rangeButtons = new QHBoxLayout(m_rangeRow);
    rangeButtons->setContentsMargins(0, 0, 0, 0);
    auto addButton = [&](const QString& text, const QString& toolTip) {
        auto* button = new QPushButton(text, m_rangeRow);
        button->setToolTip(toolTip);
        button->setFocusPolicy(Qt::NoFocus); // keep Space for play/pause
        rangeButtons->addWidget(button);
        return button;
    };
    QPushButton* setStart = addButton("Set Start", "Start the export at the current frame (I)");
    QPushButton* setEnd   = addButton("Set End", "End the export at the current frame (O)");
    QPushButton* wholeClip = addButton("Whole Clip", "Export the clip from start to end");
    row->addWidget(m_rangeRow);
    row->addSpacing(12);
    m_rangeHint = new QLabel(this);
    m_rangeHint->setObjectName("MutedLabel");
    row->addWidget(m_rangeHint, 1);
    left->addLayout(row);
    layout->addLayout(left, 1);

    // Right: the settings, which scroll when the dialog is too low for
    // them, and below them the progress and the buttons
    auto* panel = new QWidget(this);
    panel->setFixedWidth(350);
    auto* side = new QVBoxLayout(panel);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(6);
    auto* scroll = new QScrollArea(panel);
    scroll->setObjectName("SidePanel"); // the side panel's look
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* settingsPanel = new QWidget(scroll);
    auto* right = new QVBoxLayout(settingsPanel);
    right->setContentsMargins(0, 0, 10, 0); // room for the scroll bar
    right->setSpacing(6);
    scroll->setWidget(settingsPanel);
    side->addWidget(scroll, 1);
    layout->addWidget(panel);

    auto addCard = [&](const QString& title, QWidget** container) {
        auto* box = new QWidget(panel);
        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setSpacing(6);
        auto* label = new QLabel(title, box);
        label->setObjectName("TitleLabel");
        boxLayout->addWidget(label);
        auto* group = new QGroupBox(box);
        boxLayout->addWidget(group);
        right->addWidget(box);
        if (container) *container = box;
        return group;
    };

    // Format
    QGroupBox* formatCard = addCard("Format", nullptr);
    auto* formatLayout = new QVBoxLayout(formatCard);
    auto* formatGrid = new QGridLayout();
    formatGrid->setSpacing(6);
    m_formatGroup = new QButtonGroup(this);
    for (int i = 0; i < static_cast<int>(std::size(kFormats)); i++) {
        auto* button = new QToolButton(formatCard);
        button->setText(kFormats[i].label);
        button->setCheckable(true);
        button->setToolTip(kFormats[i].hint);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setMinimumHeight(36);
        button->setFocusPolicy(Qt::NoFocus);
        m_formatGroup->addButton(button, static_cast<int>(kFormats[i].format));
        formatGrid->addWidget(button, i / 2, i % 2);
    }
    formatLayout->addLayout(formatGrid);
    m_formatHint = new QLabel(formatCard);
    m_formatHint->setObjectName("MutedLabel");
    m_formatHint->setWordWrap(true);
    formatLayout->addWidget(m_formatHint);

    // Picture
    QGroupBox* pictureCard = addCard("Picture", nullptr);
    auto* pictureForm = new QFormLayout(pictureCard);
    pictureForm->setRowWrapPolicy(QFormLayout::DontWrapRows);
    pictureForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_annotations = new QCheckBox("Drawings and measurements", pictureCard);
    pictureForm->addRow(m_annotations);
    m_timeStamp = new QComboBox(pictureCard);
    m_timeStamp->addItem("None", static_cast<int>(TimeStamp::Off));
    m_timeStamp->addItem("Position in the clip", static_cast<int>(TimeStamp::ClipTime));
    m_timeStamp->addItem("Time from the start", static_cast<int>(TimeStamp::Elapsed));
    m_timeStamp->setToolTip("Burns the time into the bottom left corner");
    pictureForm->addRow("Time stamp", m_timeStamp);
    m_size = new QComboBox(pictureCard);
    pictureForm->addRow("Size", m_size);
    m_qualityRow = new QWidget(pictureCard);
    auto* qualityLayout = new QHBoxLayout(m_qualityRow);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    m_quality = new QSlider(Qt::Horizontal, m_qualityRow);
    m_quality->setRange(1, 100);
    m_qualityValue = new QLabel(m_qualityRow);
    m_qualityValue->setMinimumWidth(m_qualityValue->fontMetrics().horizontalAdvance("100") + 4);
    m_qualityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    qualityLayout->addWidget(m_quality, 1);
    qualityLayout->addWidget(m_qualityValue);
    pictureForm->addRow("Quality", m_qualityRow);

    // Motion (video formats)
    QGroupBox* motionCard = addCard("Motion", &m_motionCard);
    auto* motionForm = new QFormLayout(motionCard);
    motionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_speed = new QComboBox(motionCard);
    m_speed->addItem("Real time", 1);
    for (int slower : {2, 4, 8, 10, 16, 20}) m_speed->addItem(QString("1/%1× (slow motion)").arg(slower), slower);
    m_speed->setToolTip("Slows the export down, like the player's speed setting");
    motionForm->addRow("Speed", m_speed);
    m_frameRate = new QComboBox(motionCard);
    motionForm->addRow("Frame rate", m_frameRate);
    m_loop = new QCheckBox("Play over and over", motionCard);
    motionForm->addRow(m_loop);
    m_dither = new QCheckBox("Dither for smoother colours", motionCard);
    m_dither->setToolTip("Hides banding in skies and shadows; makes the file a bit larger");
    motionForm->addRow(m_dither);

    // What comes out, and how it went
    m_summary = new QLabel(panel);
    m_summary->setWordWrap(true);
    right->addSpacing(6);
    right->addWidget(m_summary);
    m_warning = new QLabel(panel);
    m_warning->setObjectName("MutedLabel");
    m_warning->setWordWrap(true);
    right->addWidget(m_warning);
    right->addStretch();
    m_progress = new QProgressBar(panel);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    m_progress->hide();
    side->addWidget(m_progress);
    m_status = new QLabel(panel);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    side->addWidget(m_status);
    m_showButton = new QPushButton("Show in Folder", panel);
    m_showButton->hide();
    side->addWidget(m_showButton, 0, Qt::AlignLeft);

    auto* buttons = new QDialogButtonBox(panel);
    m_exportButton = new QPushButton("Export...", buttons);
    m_exportButton->setObjectName("PrimaryButton");
    m_exportButton->setDefault(true);
    buttons->addButton(m_exportButton, QDialogButtonBox::ActionRole);
    buttons->addButton(new QPushButton("Close", buttons), QDialogButtonBox::RejectRole);
    side->addSpacing(8);
    side->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_exportButton, &QPushButton::clicked, this, [this] {
        if (m_exporting) stopExport();
        else startExport();
    });
    connect(m_showButton, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastFile).absolutePath()));
    });
    m_settingsWidgets = {m_rangeBar, m_rangeRow, formatCard, pictureCard, motionCard};

    // Restore the last choices; each change is remembered right away.
    QSettings settings;
    const int saved = settings.value("export/format", static_cast<int>(ExportFormat::Png)).toInt();
    if (QAbstractButton* button = m_formatGroup->button(saved)) button->setChecked(true);
    else m_formatGroup->buttons().first()->setChecked(true);
    AnnotationDoc* doc = source->annotations();
    const bool hasDrawings = doc && !doc->items().isEmpty();
    m_annotations->setEnabled(hasDrawings);
    m_annotations->setChecked(hasDrawings && settings.value("export/annotations", true).toBool());
    if (!hasDrawings) m_annotations->setToolTip("This clip has no drawings or measurements");
    m_timeStamp->setCurrentIndex(m_timeStamp->findData(settings.value("export/timeStamp", 0).toInt()));
    m_speed->setCurrentIndex(std::max(0, m_speed->findData(settings.value("export/speed", 1).toInt())));
    m_loop->setChecked(settings.value("export/loop", true).toBool());
    m_dither->setChecked(settings.value("export/dither", true).toBool());

    connect(m_formatGroup, &QButtonGroup::idClicked, this, [this](int id) {
        QSettings().setValue("export/format", id);
        updateControls();
    });
    connect(m_annotations, &QCheckBox::toggled, this, [this, doc](bool on) {
        if (m_annotations->isEnabled()) QSettings().setValue("export/annotations", on);
        m_view->setAnnotations(on ? doc : nullptr);
    });
    connect(m_timeStamp, &QComboBox::activated, this, [this] {
        QSettings().setValue("export/timeStamp", m_timeStamp->currentData());
    });
    connect(m_size, &QComboBox::activated, this, [this] {
        QSettings().setValue(settingsKey("size", format()), m_size->currentData());
        updateSummary();
    });
    connect(m_quality, &QSlider::valueChanged, this, [this](int value) {
        m_qualityValue->setText(QString::number(value));
        if (format() == ExportFormat::Jpeg || format() == ExportFormat::Mp4)
            QSettings().setValue(settingsKey("quality", format()), value);
    });
    connect(m_speed, &QComboBox::activated, this, [this] {
        QSettings().setValue("export/speed", m_speed->currentData());
        updateSummary();
    });
    connect(m_frameRate, &QComboBox::activated, this, [this] {
        QSettings().setValue(settingsKey("fps", format()), m_frameRate->currentData());
        updateSummary();
    });
    connect(m_loop, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("export/loop", on); });
    connect(m_dither, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("export/dither", on); });

    // The player and the range
    PlayerController* c = m_view->controller();
    connect(c, &PlayerController::videoInfoReady, this, [this](const VideoInfo& info) {
        if (info.fps > 0.0) m_sourceFps = info.fps;
        m_rangeBar->setDuration(info.duration, info.fps);
    });
    connect(c, &PlayerController::positionChanged, this, [this, c](double position) {
        m_rangeBar->setPosition(position);
        // Looping plays just the part to export
        const double halfFrame = 0.5 / m_sourceFps;
        if (m_rangeLoop && c->isPlaying() && isVideoFormat(format()) && position > m_rangeBar->end() + halfFrame)
            c->seek(m_rangeBar->start());
        if (!isVideoFormat(format())) updateSummary();
    });
    connect(m_view->transportBar(), &TransportBar::loopToggled, this, [this, c](bool on) {
        m_rangeLoop = on;
        c->setLoop(on);
    });
    connect(m_view, &ClipView::errorOccurred, m_status, &QLabel::setText);
    connect(m_rangeBar, &RangeBar::seekRequested, c, &PlayerController::seek);
    connect(m_rangeBar, &RangeBar::rangeChanged, this, &ExportDialog::updateSummary);
    connect(setStart, &QPushButton::clicked, m_rangeBar, &RangeBar::setStartAtPosition);
    connect(setEnd, &QPushButton::clicked, m_rangeBar, &RangeBar::setEndAtPosition);
    connect(wholeClip, &QPushButton::clicked, m_rangeBar, &RangeBar::selectAll);

    // The main window's keys, which don't reach a dialog
    addShortcut(Qt::Key_Space, [c] { c->togglePlayPause(); });
    addShortcut(Qt::Key_Period, [c] { c->stepForward(); });
    addShortcut(Qt::Key_Comma, [c] { c->stepBackward(); });
    addShortcut(Qt::Key_Right, [c] { c->seekRelative(1.0); });
    addShortcut(Qt::Key_Left, [c] { c->seekRelative(-1.0); });
    addShortcut(QKeySequence("Shift+Right"), [c] { c->seekRelative(5.0); });
    addShortcut(QKeySequence("Shift+Left"), [c] { c->seekRelative(-5.0); });
    addShortcut(Qt::Key_I, [this] { if (isVideoFormat(format())) m_rangeBar->setStartAtPosition(); });
    addShortcut(Qt::Key_O, [this] { if (isVideoFormat(format())) m_rangeBar->setEndAtPosition(); });

    m_view->setAnnotations(m_annotations->isChecked() ? doc : nullptr);
    m_view->open(m_path, source->controller()->position());
    updateControls();
}

ExportDialog::~ExportDialog() {
    m_cancel = true;
    m_pool.waitForDone();
}

void ExportDialog::addShortcut(const QKeySequence& key, const std::function<void()>& fn) {
    auto* shortcut = new QShortcut(key, this);
    connect(shortcut, &QShortcut::activated, this, fn);
}

void ExportDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // Laid out by now: reserve the height of the longest format hint, which a
    // word-wrapped label doesn't always get by itself (larger fonts), and
    // which keeps the cards below from jumping when the format changes
    const int width = m_formatHint->contentsRect().width();
    int height = 0;
    for (const FormatInfo& info : kFormats) {
        height = std::max(height, m_formatHint->fontMetrics()
                                      .boundingRect(QRect(0, 0, width, 10000), Qt::TextWordWrap, info.hint)
                                      .height());
    }
    m_formatHint->setMinimumHeight(height);
}

void ExportDialog::done(int result) {
    if (m_exporting) {
        if (QMessageBox::question(this, "Export", "Stop the export? The part already written is deleted.")
            != QMessageBox::Yes) {
            return;
        }
        stopExport();
        m_pool.waitForDone();
    }
    QDialog::done(result);
}

ExportFormat ExportDialog::format() const {
    return static_cast<ExportFormat>(m_formatGroup->checkedId());
}

QSize ExportDialog::outputSize() const {
    const double longSide = std::max(m_displaySize.width(), m_displaySize.height());
    const int target = m_size->currentData().toInt();
    const double scale = target > 0 && longSide > 0.0 ? target / longSide : 1.0;
    const double w = m_displaySize.width() * scale, h = m_displaySize.height() * scale;
    // Video encoders want even sides
    if (isVideoFormat(format())) return {evenDown(std::lround(w)), evenDown(std::lround(h))};
    return {std::max(1, static_cast<int>(std::lround(w))), std::max(1, static_cast<int>(std::lround(h)))};
}

ExportSettings ExportDialog::settings() const {
    ExportSettings s;
    s.format    = format();
    s.size      = outputSize();
    s.rotation  = m_rotation;
    s.timeStamp = static_cast<TimeStamp>(m_timeStamp->currentData().toInt());
    s.quality   = m_quality->value();
    s.clip      = m_path;
    s.start     = m_rangeBar->start();
    s.end       = m_rangeBar->end();
    s.speed     = 1.0 / m_speed->currentData().toInt();
    const double fps = m_frameRate->currentData().toDouble();
    s.fps       = fps > 0.0 ? fps : m_sourceFps;
    s.loop      = m_loop->isChecked();
    s.dither    = m_dither->isChecked();
    return s;
}

void ExportDialog::fillSizes() {
    // The clip's own size, and common smaller ones by the longer side
    m_size->clear();
    const double w = m_displaySize.width(), h = m_displaySize.height();
    const double longSide = std::max(w, h);
    if (longSide <= 0.0) {
        m_size->addItem("Original", 0);
        return;
    }
    m_size->addItem(QString("Original (%1 × %2)").arg(std::lround(w)).arg(std::lround(h)), 0);
    for (int target : {3840, 2560, 1920, 1280, 960, 640, 480, 320}) {
        if (target >= longSide) continue;
        const double scale = target / longSide;
        m_size->addItem(QString("%1 × %2").arg(evenDown(std::lround(w * scale))).arg(evenDown(std::lround(h * scale))),
                        target);
    }
    const int fallback = format() == ExportFormat::Gif ? 640 : 0;
    const int saved = QSettings().value(settingsKey("size", format()), fallback).toInt();
    // Not offered for this clip: the largest that is no larger, else the original
    int index = 0;
    for (int i = m_size->count() - 1; i > 0; i--) {
        if (saved > 0 && m_size->itemData(i).toInt() <= saved) index = i;
    }
    m_size->setCurrentIndex(index);
}

void ExportDialog::fillFrameRates() {
    m_frameRate->clear();
    QList<double> rates;
    double fallback = 0.0;
    if (format() == ExportFormat::Gif) {
        // GIF timing is in hundredths of a second, and browsers slow down
        // anything faster than 50 fps.
        rates = {50, 25, 20, 15, 10};
        fallback = 20;
    } else {
        m_frameRate->addItem(QString("Same as the clip (%1 fps)").arg(m_sourceFps, 0, 'f', m_sourceFps == std::round(m_sourceFps) ? 0 : 2), 0.0);
        rates = {120, 60, 50, 30, 25, 24};
    }
    for (double rate : rates) {
        if (format() != ExportFormat::Gif && std::abs(rate - m_sourceFps) < 0.05) continue;
        m_frameRate->addItem(QString("%1 fps").arg(rate), rate);
    }
    const double saved = QSettings().value(settingsKey("fps", format()), fallback).toDouble();
    const int index = m_frameRate->findData(saved);
    m_frameRate->setCurrentIndex(std::max(0, index));
}

void ExportDialog::updateControls() {
    const ExportFormat f = format();
    const bool video = isVideoFormat(f);
    m_formatHint->setText(infoOf(f).hint);
    m_rangeBar->setVisible(video);
    m_rangeRow->setVisible(video);
    m_motionCard->setVisible(video);
    m_loop->setVisible(f == ExportFormat::Gif);
    m_dither->setVisible(f == ExportFormat::Gif);
    const bool hasQuality = f == ExportFormat::Jpeg || f == ExportFormat::Mp4;
    m_qualityRow->setEnabled(hasQuality);
    m_qualityRow->setToolTip(hasQuality ? "Higher: a better picture and a larger file"
                                        : "PNG is lossless, GIF has a palette of 256 colours");
    {
        const QSignalBlocker blocker(m_quality);
        m_quality->setValue(hasQuality ? QSettings().value(settingsKey("quality", f), f == ExportFormat::Jpeg ? 92 : 70).toInt()
                                       : 100);
        m_qualityValue->setText(hasQuality ? QString::number(m_quality->value()) : QString());
    }
    fillSizes();
    fillFrameRates();
    m_exportButton->setText("Export...");
    updateSummary();
}

void ExportDialog::updateSummary() {
    const ExportSettings s = settings();
    const QString size = QString("%1 × %2").arg(s.size.width()).arg(s.size.height());
    m_warning->clear();
    if (!isVideoFormat(s.format)) {
        const double position = m_view->controller()->position();
        m_rangeHint->clear();
        m_summary->setText(QString("%1 %2 of the frame at %3")
                               .arg(size, infoOf(s.format).name, formatTime(position)));
        return;
    }

    const double length = s.end - s.start + 1.0 / m_sourceFps;
    m_rangeHint->setText(QString("%1 to %2").arg(formatTime(s.start), formatTime(s.end)));
    const int frames = ClipExporter::frameCount(s, m_sourceFps);
    const double seconds = frames / s.fps;
    m_summary->setText(QString("%1 %2, %3 fps, %4 s, %5 frames")
                           .arg(size, infoOf(s.format).name)
                           .arg(s.fps, 0, 'f', s.fps == std::round(s.fps) ? 0 : 2)
                           .arg(seconds, 0, 'f', 1)
                           .arg(frames));

    // How the clip's frames map to the export's
    QStringList notes;
    const double perSource = s.fps / (m_sourceFps * s.speed); // output frames per clip frame
    if (perSource > 1.05) {
        notes << QString("Each frame of the clip is shown %1 times: slow motion looks choppier than "
                         "with footage of a higher frame rate.").arg(perSource, 0, 'f', perSource < 10 ? 1 : 0);
    } else if (perSource < 0.95) {
        const long nth = std::lround(1.0 / perSource);
        const char* suffix = nth % 100 / 10 == 1 ? "th" : nth % 10 == 2 ? "nd" : nth % 10 == 3 ? "rd" : "th";
        notes << QString("Uses about every %1%2 frame of the clip.").arg(nth).arg(suffix);
    }
    if (s.speed < 1.0) {
        notes << QString("%1 s of the clip become %2 s.").arg(length, 0, 'f', 1).arg(seconds, 0, 'f', 1);
    }
    if (s.format == ExportFormat::Gif
        && static_cast<double>(frames) * s.size.width() * s.size.height() > 200e6) {
        notes << "This makes a large GIF. A smaller size, a lower frame rate, a shorter part or "
                 "MP4 keeps it manageable.";
    }
    m_warning->setText(notes.join("\n"));
}

void ExportDialog::startExport() {
    ExportSettings s = settings();
    const ClipRef clip = ClipRef::fromKey(m_path);
    const QFileInfo info(clip.path);
    const FramePtr frame = m_view->currentFrame();
    const double position = m_view->controller()->position();
    if (!isVideoFormat(s.format) && !frame) return;

    // e.g. "clip_00-12.345.mp4", with the time of the (first) frame in the
    // file, by default in the folder of the last export
    const double time = clip.start + (isVideoFormat(s.format) ? s.start : position);
    const QString name = QString("%1_%2.%3").arg(info.completeBaseName(), formatTime(time).replace(':', '-'),
                                                 formatSuffix(s.format));
    QString folder = QSettings().value("export/folder").toString();
    if (folder.isEmpty() || !QDir(folder).exists()) folder = info.absolutePath();
    QString file = QFileDialog::getSaveFileName(this, "Export", QDir(folder).filePath(name), infoOf(s.format).filter);
    if (file.isEmpty()) return;
    const QString suffix = QFileInfo(file).suffix().toLower();
    const bool known = suffix == formatSuffix(s.format) || (s.format == ExportFormat::Jpeg && suffix == "jpeg");
    if (!known) file += "." + formatSuffix(s.format);
    QSettings().setValue("export/folder", QFileInfo(file).absolutePath());
    s.file = file;
    if (m_annotations->isChecked() && m_source) s.overlay = m_source->renderAnnotations(s.size);

    m_showButton->hide();
    m_lastFile = file;
    if (!isVideoFormat(s.format)) {
        QString error;
        if (ClipExporter::saveImage(frame, position, s, &error)) onExportDone(true, file, {});
        else onExportDone(false, file, error);
        return;
    }

    m_view->controller()->pause();
    m_exporting = true;
    m_cancel = false;
    for (QWidget* w : m_settingsWidgets) w->setEnabled(false);
    m_exportButton->setText("Stop");
    m_progress->setValue(0);
    m_progress->show();
    m_status->setText("Exporting...");

    m_pool.start([this, s] {
        int permille = -1;
        QString error;
        const bool ok = ClipExporter::exportVideo(s, m_cancel, [&](double done) {
            const int now = static_cast<int>(done * 1000.0);
            if (now == permille) return;
            permille = now;
            QMetaObject::invokeMethod(this, [this, now] {
                if (!m_exporting) return;
                m_progress->setValue(now);
                m_status->setText(QString("Exporting... %1%").arg(now / 10));
            }, Qt::QueuedConnection);
        }, &error);
        QMetaObject::invokeMethod(this, [this, ok, file = s.file, error] { onExportDone(ok, file, error); },
                                  Qt::QueuedConnection);
    });
}

void ExportDialog::stopExport() {
    m_cancel = true;
}

void ExportDialog::onExportDone(bool ok, const QString& file, const QString& error) {
    m_exporting = false;
    for (QWidget* w : m_settingsWidgets) w->setEnabled(true);
    updateControls(); // re-enables what the format allows
    m_progress->hide();
    const QString name = QFileInfo(file).fileName();
    if (ok) {
        m_status->setText(QString("Saved %1 (%2)").arg(name, fileSize(QFileInfo(file).size())));
        m_showButton->show();
    } else if (error.isEmpty()) {
        m_status->setText("Export stopped");
    } else {
        m_status->setText(QString("Could not export %1: %2").arg(name, error));
    }
}
