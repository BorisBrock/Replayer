#include "transportbar.h"
#include "jumpsliderstyle.h"
#include "timeformat.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

TransportBar::TransportBar(QWidget* parent) : QWidget(parent) {
    setObjectName("TransportBar");
    setAttribute(Qt::WA_StyledBackground); // let the stylesheet paint the bar
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 8);
    layout->setSpacing(4);

    // Position slider works in milliseconds.
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    auto* sliderStyle = new JumpSliderStyle();
    sliderStyle->setParent(m_positionSlider);
    m_positionSlider->setStyle(sliderStyle);
    m_positionSlider->setPageStep(1000);
    m_positionSlider->setFocusPolicy(Qt::NoFocus);
    connect(m_positionSlider, &QSlider::valueChanged, this, [this](int value) {
        emit seekRequested(value / 1000.0);
    });
    layout->addWidget(m_positionSlider);

    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    m_stepBackButton    = addButton(Theme::Icon::StepBackward, "Previous frame (,)");
    m_playPauseButton   = addButton(Theme::Icon::Play, "Play / Pause (Space)");
    m_stopButton        = addButton(Theme::Icon::Stop, "Stop and rewind (S)");
    m_stepForwardButton = addButton(Theme::Icon::StepForward, "Next frame (.)");
    m_loopButton        = addButton(Theme::Icon::Loop, "Loop clip (L)");
    m_loopButton->setCheckable(true);
    connect(m_stepBackButton, &QToolButton::clicked, this, &TransportBar::stepBackwardClicked);
    connect(m_playPauseButton, &QToolButton::clicked, this, &TransportBar::playPauseClicked);
    connect(m_stopButton, &QToolButton::clicked, this, &TransportBar::stopClicked);
    connect(m_stepForwardButton, &QToolButton::clicked, this, &TransportBar::stepForwardClicked);
    connect(m_loopButton, &QToolButton::toggled, this, &TransportBar::loopToggled);
    for (QToolButton* b : {m_stepBackButton, m_playPauseButton, m_stopButton,
                           m_stepForwardButton, m_loopButton}) {
        row->addWidget(b);
    }
    row->addSpacing(16);

    m_timeLabel  = new QLabel(this);
    m_frameLabel = new QLabel(this);
    // Fixed-width digits keep the labels from jittering during playback.
    QFont mono = m_timeLabel->font();
    mono.setFamily("monospace");
    mono.setStyleHint(QFont::Monospace);
    m_timeLabel->setFont(mono);
    m_frameLabel->setFont(mono);
    row->addWidget(m_timeLabel);
    row->addStretch();
    row->addSpacing(8);
    row->addWidget(m_frameLabel);
    layout->addLayout(row);

    setCompact(false);
    clear();
}

void TransportBar::setCompact(bool compact) {
    m_compact = compact;
    const int icon = compact ? 18 : 22;
    for (QToolButton* b : {m_stepBackButton, m_stopButton, m_stepForwardButton, m_loopButton}) {
        b->setIconSize(QSize(icon, icon));
    }
    m_playPauseButton->setIconSize(compact ? QSize(22, 22) : QSize(28, 28));
    if (compact) layout()->setContentsMargins(6, 0, 6, 4);
    else layout()->setContentsMargins(12, 6, 12, 8);
}

QToolButton* TransportBar::addButton(Theme::Icon icon, const QString& toolTip) {
    auto* button = new QToolButton(this);
    button->setIcon(Theme::icon(icon));
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

void TransportBar::setVideoInfo(const VideoInfo& info) {
    m_duration = info.duration;
    m_fps      = info.fps;
    {
        const QSignalBlocker blocker(m_positionSlider);
        m_positionSlider->setRange(0, static_cast<int>(info.duration * 1000.0));
        m_positionSlider->setValue(0);
    }
    setEnabled(true);
    setPosition(0.0);
}

void TransportBar::clear() {
    m_duration = 0.0;
    m_fps      = 0.0;
    {
        const QSignalBlocker blocker(m_positionSlider);
        m_positionSlider->setRange(0, 0);
    }
    m_timeLabel->setText(QString("%1 / %1").arg(formatTime(0.0)));
    m_frameLabel->setText(QString());
    setEnabled(false);
}

void TransportBar::setPosition(double position) {
    // Don't fight the user while they drag the handle.
    if (!m_positionSlider->isSliderDown()) {
        const QSignalBlocker blocker(m_positionSlider);
        m_positionSlider->setValue(static_cast<int>(position * 1000.0));
    }

    // Tiles are narrow, so they show the position only; the current clip's
    // duration is in the info panel.
    m_timeLabel->setText(m_compact ? formatTime(position)
                                   : QString("%1 / %2").arg(formatTime(position), formatTime(m_duration)));
    if (m_fps > 0.0) {
        const auto frame = std::llround(position * m_fps);
        const auto total = std::llround(m_duration * m_fps);
        m_frameLabel->setText(QString("Frame %1 / %2").arg(frame).arg(total));
    }
}

void TransportBar::setPlaybackState(bool playing) {
    m_playPauseButton->setIcon(Theme::icon(playing ? Theme::Icon::Pause : Theme::Icon::Play));
}

void TransportBar::setLoop(bool loop) {
    const QSignalBlocker blocker(m_loopButton);
    m_loopButton->setChecked(loop);
}
