#ifndef ANNOTATIONS_H
#define ANNOTATIONS_H

#include <QColor>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <optional>

// Drawing and measuring tools; None leaves the mouse to panning.
enum class DrawTool { None, Line, Arrow, Freehand, Circle, Reference, Measure, MarkA, MarkB };

// Reference, Measure and the speed markers; the other tools are plain drawings.
bool isMeasurement(DrawTool tool);

// How speeds are shown: km/h or mph.
enum class UnitSystem { Metric, Imperial };

// Tool, colour and line width used for new drawings (shared by all clips).
struct DrawStyle {
    DrawTool tool  = DrawTool::None;
    QColor   color = QColor("#ef4444");
    double   width = 3.0; // screen pixels
};

// One drawing on a clip. Points are in normalised display coordinates (like
// ViewZoom), so drawings follow zoom and pan and fit clips of any size.
struct Annotation {
    DrawTool       tool = DrawTool::Line;
    QColor         color;
    double         width = 3.0;
    QList<QPointF> points; // Line, Arrow, Reference, Measure: start, end
                           // Circle: centre, a point on the rim
                           // Freehand: the whole stroke
                           // MarkA, MarkB: the marked point

    // Reference only: the real length of the line, e.g. 1.25 "m"
    double  length = 0.0;
    QString unit;

    // MarkA, MarkB only: position in the clip (seconds) of the marked frame
    double time = 0.0;
};

// Distance and time between the speed markers A and B
struct SpeedMeasurement {
    double  distance = 0.0; // in unit, the reference's unit
    QString unit;
    double  seconds  = 0.0;

    // Speed in km/h or mph; std::nullopt if unit is not one we know.
    std::optional<double> speed(UnitSystem units) const;
};

// Meters per unit for "m", "cm", "ft", "in", ...; 0 if unknown.
double metersPerUnit(const QString& unit);

// The drawings of one clip, with undo and redo. A clip has at most one
// reference line; measuring lines show their length relative to it. Likewise
// at most one A and one B marker, for the speed between them.
class AnnotationDoc : public QObject {
    Q_OBJECT

public:
    explicit AnnotationDoc(QObject* parent = nullptr) : QObject(parent) {}

    const QList<Annotation>& items() const { return m_items; }
    const Annotation* find(DrawTool tool) const; // first of that tool, or nullptr
    const Annotation* reference() const { return find(DrawTool::Reference); }

    // Video size in pixels (see VideoRenderer::displaySize()); real lengths
    // depend on the aspect ratio.
    void setDisplaySize(QSizeF size) { m_displaySize = size; }
    // Needs a reference, both markers and two different frames.
    std::optional<SpeedMeasurement> speed() const;

    bool hasDrawings() const;
    bool hasMeasurements() const;

    void add(const Annotation& annotation); // a new reference or marker replaces the old one
    void clear();             // drawings and measurements
    void clearDrawings();
    void clearMeasurements();

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    void undo();
    void redo();

signals:
    void changed();

private:
    void commit(QList<Annotation> items);
    void removeWhere(bool measurements); // one undo step, if anything goes

    QSizeF                   m_displaySize;
    QList<Annotation>        m_items;
    QList<QList<Annotation>> m_undo;
    QList<QList<Annotation>> m_redo;
};

#endif
