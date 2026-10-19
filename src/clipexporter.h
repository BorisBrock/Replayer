#ifndef CLIPEXPORTER_H
#define CLIPEXPORTER_H

#include <QImage>
#include <QSize>
#include <QString>
#include <atomic>
#include <functional>

#include "videoframe.h"

// What the export dialog saves: a still of one frame, or a range of the clip
// as a video or an animation.
enum class ExportFormat { Png, Jpeg, Mp4, Gif };

bool isVideoFormat(ExportFormat format);
QString formatSuffix(ExportFormat format); // "png", "jpg", "mp4", "gif"

// Burnt into the picture, in the bottom left corner
enum class TimeStamp { Off, ClipTime, Elapsed }; // Elapsed: since the start of the range

struct ExportSettings {
    ExportFormat format = ExportFormat::Png;
    QString      file;
    QSize        size;          // of the output, after rotation
    int          rotation = 0;  // the clip's, clockwise degrees
    QImage       overlay;       // the drawings at size, or null for none
    TimeStamp    timeStamp = TimeStamp::Off;
    int          quality = 90;  // 1..100: JPEG and MP4

    // MP4 and GIF: the range, as positions in the clip (seconds), both frames
    // included
    QString clip;               // ClipRef key
    double  start  = 0.0;
    double  end    = 0.0;
    double  speed  = 1.0;       // 0.25: four times slower than real time
    double  fps    = 0.0;       // of the output; 0: the clip's
    bool    loop   = true;      // GIF: play over and over
    bool    dither = true;      // GIF: smoother gradients, larger files
};

namespace ClipExporter {

// Frames in the output for a clip of sourceFps
int frameCount(const ExportSettings& settings, double sourceFps);

// frame as exported: scaled, rotated, with drawings and time stamp. time:
// the frame's position in the clip.
QImage composeFrame(const AVFrame* frame, double time, const ExportSettings& settings);

// Saves frame as PNG or JPEG.
bool saveImage(const FramePtr& frame, double time, const ExportSettings& settings, QString* error);

// Encodes the range as MP4 or GIF. Blocks: call from a worker thread.
// progress gets 0..1. On failure or cancel the file is removed and false
// returned, with error set (left empty when cancelled).
bool exportVideo(const ExportSettings& settings, const std::atomic<bool>& cancel,
                 const std::function<void(double)>& progress, QString* error);

} // namespace ClipExporter

#endif
