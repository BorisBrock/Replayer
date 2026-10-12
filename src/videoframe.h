#ifndef VIDEOFRAME_H
#define VIDEOFRAME_H

#include <QImage>
#include <QString>
#include <memory>

struct AVFrame;

// Reference-counted decoded frame in system memory. Frames are kept in their
// native YUV layout; colour conversion happens on the GPU (see VideoRenderer).
using FramePtr = std::shared_ptr<AVFrame>;

// Takes ownership of frame (released with av_frame_free).
FramePtr makeFramePtr(AVFrame* frame);

// True if VideoRenderer can upload this AVPixelFormat directly. Anything else
// is converted by the decoder first.
bool isRenderablePixelFormat(int format);

// Small RGB copy of a frame (at most maxWidth wide, display aspect kept)
QImage makeThumbnail(const AVFrame* frame, int maxWidth);

struct VideoInfo {
    QString codec;
    QString hwAccel;      // empty when decoding in software
    int     width    = 0;
    int     height   = 0;
    double  fps      = 0.0;
    double  duration = 0.0;
    int     rotation = 0; // clockwise degrees: 0, 90, 180 or 270
};

#endif
