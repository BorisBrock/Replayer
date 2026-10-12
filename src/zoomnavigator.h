#ifndef ZOOMNAVIGATOR_H
#define ZOOMNAVIGATOR_H

#include <QImage>
#include <QWidget>

#include "viewzoom.h"

// Overview of the whole frame with the zoomed-in region outlined. Click or
// drag to move the region, use the wheel to zoom.
class ZoomNavigator : public QWidget {
    Q_OBJECT

public:
    explicit ZoomNavigator(QWidget* parent = nullptr);

    void setImage(const QImage& image); // thumbnail of the current clip
    void setAspectRatio(double aspect); // used while there is no image
    void setZoom(const ViewZoom& zoom);

signals:
    void centerRequested(QPointF center);
    void zoomAtRequested(double factor, QPointF point);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRectF frameRect() const; // where the frame is drawn
    QPointF toNormalized(QPointF pos) const;
    void updateHeight();

    QImage   m_image;
    double   m_aspect = 16.0 / 9.0;
    ViewZoom m_zoom;
};

#endif
