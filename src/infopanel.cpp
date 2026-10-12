#include "infopanel.h"
#include "theme.h"
#include "timeformat.h"
#include "zoomnavigator.h"

#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

// The zoom slider is exponential: equal steps multiply the zoom equally, so
// low zoom levels get as much travel as high ones.
constexpr int kZoomSteps = 300;

int zoomToSlider(double zoom) {
    return static_cast<int>(std::lround(std::log(zoom) / std::log(ViewZoom::kMaxZoom) * kZoomSteps));
}

double sliderToZoom(int value) {
    return std::pow(ViewZoom::kMaxZoom, static_cast<double>(value) / kZoomSteps);
}

} // namespace

InfoPanel::InfoPanel(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 0, 10, 10);

    auto* infoGroup  = new QGroupBox("VIDEO", this);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    m_fileLabel       = new QLabel(infoGroup);
    m_codecLabel      = new QLabel(infoGroup);
    m_resolutionLabel = new QLabel(infoGroup);
    m_fpsLabel        = new QLabel(infoGroup);
    m_durationLabel   = new QLabel(infoGroup);
    m_fileLabel->setObjectName("TitleLabel");
    infoLayout->setSpacing(6);
    for (QLabel* label : {m_fileLabel, m_codecLabel, m_resolutionLabel, m_fpsLabel, m_durationLabel}) {
        label->setWordWrap(true); // the panel can be narrow
        infoLayout->addWidget(label);
    }
    mainLayout->addWidget(infoGroup);

    // Speed slider selects a slow-motion factor: value / 100 = how many times
    // slower than real time (1.0x .. 20.0x). It applies to every clip, so
    // clips can be compared at the same speed.
    auto* speedGroup  = new QGroupBox("SLOW MOTION", this);
    auto* speedLayout = new QVBoxLayout(speedGroup);
    m_speedSlider = new QSlider(Qt::Horizontal, speedGroup);
    m_speedSlider->setRange(100, 2000);
    m_speedSlider->setValue(100);
    m_speedSlider->setFocusPolicy(Qt::NoFocus);
    m_speedLabel = new QLabel(speedGroup);
    m_speedLabel->setObjectName("MutedLabel");
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int value) {
        updateSpeedLabel();
        // factor 2 -> multiplier 0.5 -> half speed
        emit speedMultiplierChanged(100.0 / value);
    });
    speedLayout->addWidget(m_speedSlider);
    speedLayout->addWidget(m_speedLabel);
    mainLayout->addWidget(speedGroup);

    // Zoom and pan, shared by all clips (they usually come from one camera).
    auto* zoomGroup  = new QGroupBox("ZOOM", this);
    auto* zoomLayout = new QVBoxLayout(zoomGroup);
    m_navigator = new ZoomNavigator(zoomGroup);
    connect(m_navigator, &ZoomNavigator::centerRequested, this, &InfoPanel::zoomCenterRequested);
    connect(m_navigator, &ZoomNavigator::zoomAtRequested, this, &InfoPanel::zoomAtRequested);
    m_zoomSlider = new QSlider(Qt::Horizontal, zoomGroup);
    m_zoomSlider->setRange(0, kZoomSteps);
    m_zoomSlider->setFocusPolicy(Qt::NoFocus);
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        emit zoomChanged(sliderToZoom(value));
    });
    auto* zoomRow = new QHBoxLayout();
    m_zoomLabel = new QLabel(zoomGroup);
    m_zoomLabel->setObjectName("MutedLabel");
    auto* resetButton = new QPushButton("Reset", zoomGroup);
    resetButton->setToolTip("Show the whole frame (0)");
    resetButton->setFocusPolicy(Qt::NoFocus);
    connect(resetButton, &QPushButton::clicked, this, &InfoPanel::zoomResetRequested);
    zoomRow->addWidget(m_zoomLabel, 1);
    zoomRow->addWidget(resetButton);
    zoomLayout->addWidget(m_navigator);
    zoomLayout->addWidget(m_zoomSlider);
    zoomLayout->addLayout(zoomRow);
    mainLayout->addWidget(zoomGroup);
    setZoom(ViewZoom());

    mainLayout->addWidget(createDrawingGroup());
    mainLayout->addWidget(createMeasurementGroup());
    setAnnotations(nullptr);

    mainLayout->addStretch();
    setMinimumWidth(200);

    clearVideoInfo();
}

QToolButton* InfoPanel::createToolButton(QWidget* parent, DrawTool tool, const QString& toolTip) {
    static const QMap<DrawTool, Theme::Icon> icons = {
        {DrawTool::Line, Theme::Icon::Line},         {DrawTool::Arrow, Theme::Icon::Arrow},
        {DrawTool::Freehand, Theme::Icon::Freehand}, {DrawTool::Circle, Theme::Icon::Circle},
        {DrawTool::Reference, Theme::Icon::Ruler},   {DrawTool::Measure, Theme::Icon::Measure},
        {DrawTool::MarkA, Theme::Icon::MarkA},       {DrawTool::MarkB, Theme::Icon::MarkB},
    };
    auto* button = new QToolButton(parent);
    button->setIcon(Theme::icon(icons.value(tool)));
    button->setIconSize(QSize(20, 20));
    button->setToolTip(toolTip);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    // One tool at a time; clicking the active one again puts it away.
    connect(button, &QToolButton::clicked, this, [this, tool](bool checked) {
        setDrawTool(checked ? tool : DrawTool::None);
    });
    m_toolButtons.insert(tool, button);
    return button;
}

// Drawing tools, colour and line width. Drawings belong to a clip; undo,
// redo and clear act on the current one.
QWidget* InfoPanel::createDrawingGroup() {
    auto* group  = new QGroupBox("DRAWING", this);
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(6);

    auto* toolRow = new QHBoxLayout();
    toolRow->setSpacing(2);
    toolRow->addWidget(createToolButton(group, DrawTool::Line, "Line"));
    toolRow->addWidget(createToolButton(group, DrawTool::Arrow, "Arrow"));
    toolRow->addWidget(createToolButton(group, DrawTool::Freehand, "Freehand"));
    toolRow->addWidget(createToolButton(group, DrawTool::Circle, "Circle: drag from the centre"));
    toolRow->addStretch();
    layout->addLayout(toolRow);

    const QColor colors[] = {
        QColor("#ef4444"), QColor("#facc15"), QColor("#22c55e"),
        QColor("#3b82f6"), QColor("#ec4899"), QColor(Qt::white),
    };
    const char* const colorNames[] = {"Red", "Yellow", "Green", "Blue", "Pink", "White"};
    auto* colorRow   = new QHBoxLayout();
    auto* colorGroup = new QButtonGroup(group);
    colorRow->setSpacing(2);
    for (int i = 0; i < 6; i++) {
        auto* button = new QToolButton(group);
        button->setIcon(Theme::swatch(colors[i]));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(colorNames[i]);
        button->setCheckable(true);
        button->setChecked(colors[i] == m_drawStyle.color);
        button->setFocusPolicy(Qt::NoFocus);
        colorGroup->addButton(button);
        colorRow->addWidget(button);
        const QColor color = colors[i];
        connect(button, &QToolButton::clicked, this, [this, color] {
            m_drawStyle.color = color;
            emit drawStyleChanged(m_drawStyle);
        });
    }
    colorRow->addStretch();
    layout->addLayout(colorRow);

    auto* widthSlider = new QSlider(Qt::Horizontal, group);
    widthSlider->setRange(1, 12);
    widthSlider->setValue(static_cast<int>(m_drawStyle.width));
    widthSlider->setFocusPolicy(Qt::NoFocus);
    widthSlider->setToolTip("Line width");
    m_widthLabel = new QLabel(group);
    m_widthLabel->setObjectName("MutedLabel");
    m_widthLabel->setText(QString("Line width %1 px").arg(widthSlider->value()));
    connect(widthSlider, &QSlider::valueChanged, this, [this](int value) {
        m_widthLabel->setText(QString("Line width %1 px").arg(value));
        m_drawStyle.width = value;
        emit drawStyleChanged(m_drawStyle);
    });
    layout->addWidget(widthSlider);
    layout->addWidget(m_widthLabel);

    auto addButton = [group](QHBoxLayout* row, Theme::Icon icon, const QString& toolTip) {
        auto* button = new QToolButton(group);
        button->setIcon(Theme::icon(icon));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(toolTip);
        button->setFocusPolicy(Qt::NoFocus);
        row->addWidget(button);
        return button;
    };
    auto* editRow = new QHBoxLayout();
    editRow->setSpacing(2);
    m_undoButton  = addButton(editRow, Theme::Icon::Undo, "Undo (Ctrl+Z)");
    m_redoButton  = addButton(editRow, Theme::Icon::Redo, "Redo (Ctrl+Y)");
    editRow->addStretch();
    m_clearButton = addButton(editRow, Theme::Icon::Trash, "Remove all drawings and measurements from the current clip");
    connect(m_undoButton, &QToolButton::clicked, this, &InfoPanel::undoRequested);
    connect(m_redoButton, &QToolButton::clicked, this, &InfoPanel::redoRequested);
    connect(m_clearButton, &QToolButton::clicked, this, &InfoPanel::clearDrawingsRequested);
    layout->addLayout(editRow);
    return group;
}

// Measuring: a reference line of known length, then lines measured against it,
// and speed: the way between two markers set on different frames.
QWidget* InfoPanel::createMeasurementGroup() {
    auto* group  = new QGroupBox("MEASUREMENT", this);
    auto* layout = new QVBoxLayout(group);
    layout->setSpacing(6);
    auto* toolRow = new QHBoxLayout();
    toolRow->setSpacing(2);
    toolRow->addWidget(createToolButton(group, DrawTool::Reference,
                                        "Reference: draw along something of known length, then enter its real length"));
    toolRow->addWidget(createToolButton(group, DrawTool::Measure,
                                        "Measure: lines show their real length, relative to the reference"));
    toolRow->addSpacing(12);
    toolRow->addWidget(createToolButton(group, DrawTool::MarkA,
                                        "Speed marker A: click a point, e.g. the front tyre, on the first frame"));
    toolRow->addWidget(createToolButton(group, DrawTool::MarkB,
                                        "Speed marker B: click the same point on a later frame"));
    toolRow->addStretch();
    layout->addLayout(toolRow);
    m_referenceLabel = new QLabel(group);
    m_referenceLabel->setObjectName("MutedLabel");
    m_referenceLabel->setWordWrap(true);
    layout->addWidget(m_referenceLabel);
    m_markerSpeedLabel = new QLabel(group);
    m_markerSpeedLabel->setObjectName("TitleLabel");
    m_markerDetailLabel = new QLabel(group);
    m_markerDetailLabel->setObjectName("MutedLabel");
    m_markerDetailLabel->setWordWrap(true);
    layout->addWidget(m_markerSpeedLabel);
    layout->addWidget(m_markerDetailLabel);
    return group;
}

void InfoPanel::setDrawTool(DrawTool tool) {
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it) it.value()->setChecked(it.key() == tool);
    if (tool == m_drawStyle.tool) return;
    m_drawStyle.tool = tool;
    emit drawStyleChanged(m_drawStyle);
}

void InfoPanel::setAnnotations(const AnnotationDoc* doc) {
    m_undoButton->setEnabled(doc && doc->canUndo());
    m_redoButton->setEnabled(doc && doc->canRedo());
    m_clearButton->setEnabled(doc && !doc->items().isEmpty());
    const Annotation* reference = doc ? doc->reference() : nullptr;
    m_referenceLabel->setText(reference ? QString("Reference: %1 %2").arg(reference->length).arg(reference->unit)
                                        : QString("No reference yet: lengths are in video pixels"));

    m_markerSpeed = doc ? doc->speed() : std::nullopt;
    if (m_markerSpeed) m_markerHint.clear();
    else if (!doc || !doc->find(DrawTool::MarkA) || !doc->find(DrawTool::MarkB))
        m_markerHint = "Speed: set marker A, then marker B on a later frame";
    else if (!reference) m_markerHint = "Speed: draw a reference line first";
    else m_markerHint = "Speed: set A and B on different frames";
    updateMarkerSpeed();
}

void InfoPanel::setUnitSystem(UnitSystem units) {
    m_units = units;
    updateMarkerSpeed();
}

void InfoPanel::updateMarkerSpeed() {
    m_markerSpeedLabel->setVisible(m_markerSpeed.has_value());
    if (!m_markerSpeed) {
        m_markerDetailLabel->setText(m_markerHint);
        return;
    }
    const SpeedMeasurement& m = *m_markerSpeed;
    if (const std::optional<double> speed = m.speed(m_units)) {
        m_markerSpeedLabel->setText(QString("%1 %2").arg(*speed, 0, 'f', 1)
                                        .arg(m_units == UnitSystem::Metric ? "km/h" : "mph"));
    } else {
        // A unit we can't convert, e.g. "bike lengths"
        m_markerSpeedLabel->setText(QString("%1 %2/s").arg(m.distance / m.seconds, 0, 'f', 2).arg(m.unit));
    }
    QString detail = QString("%1 %2 in %3 s").arg(m.distance, 0, 'f', 2).arg(m.unit).arg(m.seconds, 0, 'f', 3);
    if (m_originalFps > 0.0) detail += QString(" (%1 frames)").arg(std::lround(m.seconds * m_originalFps));
    m_markerDetailLabel->setText(detail);
}

void InfoPanel::setVideoInfo(const QString& fileName, const VideoInfo& info) {
    m_originalFps = info.fps;

    m_fileLabel->setText(fileName);
    const QString decoder = info.hwAccel.isEmpty() ? QString("software") : info.hwAccel;
    m_codecLabel->setText(QString("Codec: %1 (%2)").arg(info.codec, decoder));
    QString resolution = QString("Resolution: %1x%2").arg(info.width).arg(info.height);
    if (info.rotation != 0) resolution += QString(", rotated %1°").arg(info.rotation);
    m_resolutionLabel->setText(resolution);
    m_fpsLabel->setText(QString("FPS: %1").arg(info.fps, 0, 'f', 2));
    if (info.width > 0 && info.height > 0) {
        const bool portrait = info.rotation == 90 || info.rotation == 270;
        m_navigator->setAspectRatio(portrait ? static_cast<double>(info.height) / info.width
                                             : static_cast<double>(info.width) / info.height);
    }
    m_durationLabel->setText(QString("Duration: %1").arg(formatTime(info.duration)));
    updateSpeedLabel();
    updateMarkerSpeed();
}

void InfoPanel::setZoom(const ViewZoom& zoom) {
    {
        const QSignalBlocker blocker(m_zoomSlider);
        m_zoomSlider->setValue(zoomToSlider(zoom.zoom));
    }
    m_zoomLabel->setText(QString("Zoom %1x").arg(zoom.zoom, 0, 'f', 1));
    m_navigator->setZoom(zoom);
}

void InfoPanel::setThumbnail(const QImage& image) {
    m_navigator->setImage(image);
}

void InfoPanel::clearVideoInfo() {
    m_originalFps = 0.0;
    m_fileLabel->setText("No clip selected");
    m_codecLabel->setText("Codec: -");
    m_resolutionLabel->setText("Resolution: -");
    m_fpsLabel->setText("FPS: -");
    m_durationLabel->setText("Duration: -");
    updateSpeedLabel();
    updateMarkerSpeed();
}

void InfoPanel::setSpeedMultiplier(double multiplier) {
    if (multiplier <= 0.0) return;
    // multiplier = 1/factor, so factor = 1/multiplier
    const int value = std::clamp(static_cast<int>(std::lround(100.0 / multiplier)),
                                 m_speedSlider->minimum(), m_speedSlider->maximum());
    const QSignalBlocker blocker(m_speedSlider);
    m_speedSlider->setValue(value);
    updateSpeedLabel();
}

void InfoPanel::updateSpeedLabel() {
    const double factor = m_speedSlider->value() / 100.0;
    QString text = QString("Factor %1x").arg(factor, 0, 'f', 1);
    if (m_originalFps > 0.0) {
        // Source frames shown per second of wall-clock time
        text += QString(", %1 FPS").arg(std::lround(m_originalFps / factor));
    }
    m_speedLabel->setText(text);
}
