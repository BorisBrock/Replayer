#ifndef TRANSPORTBAR_H
#define TRANSPORTBAR_H

#include <QWidget>

#include "theme.h"
#include "videoframe.h"

class QLabel;
class QSlider;
class QToolButton;

// Seek bar, playback buttons and time display shown below the video.
class TransportBar : public QWidget {
    Q_OBJECT

public:
    explicit TransportBar(QWidget* parent = nullptr);

    // Smaller icons and margins, for comparison tiles
    void setCompact(bool compact);

public slots:
    void setVideoInfo(const VideoInfo& info);
    void clear();
    void setPosition(double position);
    void setPlaybackState(bool playing);
    void setLoop(bool loop);

signals:
    void playPauseClicked();
    void stopClicked();
    void stepBackwardClicked();
    void stepForwardClicked();
    void seekRequested(double position);
    void loopToggled(bool loop);

private:
    QToolButton* addButton(Theme::Icon icon, const QString& toolTip);

    QSlider*     m_positionSlider;
    QToolButton* m_stepBackButton;
    QToolButton* m_playPauseButton;
    QToolButton* m_stopButton;
    QToolButton* m_stepForwardButton;
    QToolButton* m_loopButton;
    QLabel*      m_timeLabel;
    QLabel*      m_frameLabel;

    double m_duration = 0.0;
    double m_fps      = 0.0;
    bool   m_compact  = false;
};

#endif
