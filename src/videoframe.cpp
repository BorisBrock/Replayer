#include "videoframe.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

FramePtr makeFramePtr(AVFrame* frame) {
    return FramePtr(frame, [](AVFrame* f) { av_frame_free(&f); });
}

bool isRenderablePixelFormat(int format) {
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_P010LE:
        return true;
    default:
        return false;
    }
}

QImage makeThumbnail(const AVFrame* frame, int maxWidth) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return {};
    double aspect = static_cast<double>(frame->width) / frame->height;
    if (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0)
        aspect *= av_q2d(frame->sample_aspect_ratio);
    const int w = std::min(maxWidth, frame->width);
    const int h = std::max(1, static_cast<int>(w / aspect));

    SwsContext* sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                     w, h, AV_PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return {};
    QImage image(w, h, QImage::Format_RGB32);
    uint8_t* dst[] = {image.bits()};
    const int dstStride[] = {static_cast<int>(image.bytesPerLine())};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    sws_freeContext(sws);
    return image;
}

QImage frameToImage(const AVFrame* frame, QSize size) {
    if (!frame || frame->width <= 0 || frame->height <= 0) return {};
    int w = frame->width;
    if (frame->sample_aspect_ratio.num > 0 && frame->sample_aspect_ratio.den > 0)
        w = std::max(1, static_cast<int>(std::lround(w * av_q2d(frame->sample_aspect_ratio))));
    int h = frame->height;
    if (!size.isEmpty()) {
        w = size.width();
        h = size.height();
    }

    SwsContext* sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                     w, h, AV_PIX_FMT_RGB32, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!sws) return {};
    // Same choice of matrix as the renderer's shader: untagged HD is BT.709.
    int colorspace = frame->colorspace;
    if (colorspace == AVCOL_SPC_UNSPECIFIED || colorspace == AVCOL_SPC_RESERVED || colorspace == AVCOL_SPC_RGB)
        colorspace = frame->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
    const bool fullRange = frame->color_range == AVCOL_RANGE_JPEG || frame->format == AV_PIX_FMT_YUVJ420P
                        || frame->format == AV_PIX_FMT_YUVJ422P || frame->format == AV_PIX_FMT_YUVJ444P;
    sws_setColorspaceDetails(sws, sws_getCoefficients(colorspace), fullRange ? 1 : 0,
                             sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);

    QImage image(w, h, QImage::Format_RGB32);
    uint8_t* dst[] = {image.bits()};
    const int dstStride[] = {static_cast<int>(image.bytesPerLine())};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
    sws_freeContext(sws);
    return image;
}
