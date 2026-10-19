#ifndef INFOPANEL_H
#define INFOPANEL_H

#include <QMap>
#include <QWidget>
#include <optional>

#include "annotations.h"
#include "videoframe.h"
#include "viewzoom.h"

class QAction;
class QLabel;
class QSlider;
class QToolButton;
class ZoomNavigator;

// Right-hand panel: properties of the current clip, and the settings shared
// by all clips (slow motion, zoom, annotation tools).
class InfoPanel : public QWidget {
    Q_OBJECT

public:
    explicit InfoPanel(QWidget* parent = nullptr);

    const DrawStyle& drawStyle() const { return m_drawStyle; }

public slots:
    void setVideoInfo(const QString& fileName, const VideoInfo& info);
    void clearVideoInfo();
    void setSpeedMultiplier(double multiplier);
    void setZoom(const ViewZoom& zoom);
    void setZoomLocked(bool locked);
    void setThumbnail(const QImage& image);
    void setDrawTool(DrawTool tool);
    // Undo/redo/clear, reference and speed of the current clip (nullptr: none)
    void setAnnotations(const AnnotationDoc* doc);
    void setUnitSystem(UnitSystem units);

signals:
    void speedMultiplierChanged(double multiplier);
    void zoomChanged(double zoom);                     // slider
    void zoomCenterRequested(QPointF center);          // navigator click/drag
    void zoomAtRequested(double factor, QPointF point); // navigator wheel
    void zoomResetRequested();
    void zoomLockToggled(bool locked);              // off: each clip zooms on its own
    void drawStyleChanged(const DrawStyle& style);
    void undoRequested();
    void redoRequested();
    void clearAllRequested();
    void clearDrawingsRequested();
    void clearMeasurementsRequested();

private:
    void updateSpeedLabel();
    void updateMarkerSpeed();
    QWidget* createAnnotationGroup();
    QToolButton* createToolButton(QWidget* parent, DrawTool tool, const QString& toolTip);

    QLabel* m_fileLabel;
    QLabel* m_codecLabel;
    QLabel* m_resolutionLabel;
    QLabel* m_fpsLabel;
    QLabel* m_durationLabel;

    QSlider* m_speedSlider;
    QLabel*  m_speedLabel;

    ZoomNavigator* m_navigator;
    QSlider*       m_zoomSlider;
    QLabel*        m_zoomLabel;
    QToolButton*   m_zoomLockButton;

    DrawStyle                    m_drawStyle;
    QMap<DrawTool, QToolButton*> m_toolButtons;
    QToolButton*                 m_undoButton;
    QToolButton*                 m_redoButton;
    QToolButton*                 m_clearButton;
    QAction*                     m_clearDrawingsAction;
    QAction*                     m_clearMeasurementsAction;
    QLabel*                      m_widthLabel;
    QLabel*                      m_referenceLabel;
    QLabel*                      m_markerSpeedLabel;
    QLabel*                      m_markerDetailLabel;

    // Speed between the markers A and B of the current clip, or why there is none
    std::optional<SpeedMeasurement> m_markerSpeed;
    QString                         m_markerHint;
    UnitSystem                      m_units = UnitSystem::Metric;

    double m_originalFps = 0.0;
};

#endif
