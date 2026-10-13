#include "annotations.h"

#include <QHash>
#include <algorithm>
#include <cmath>

double metersPerUnit(const QString& unit) {
    static const QHash<QString, double> units = {
        {"m", 1.0},        {"meter", 1.0},   {"meters", 1.0},  {"metre", 1.0},   {"metres", 1.0},
        {"cm", 0.01},      {"mm", 0.001},    {"km", 1000.0},
        {"in", 0.0254},    {"\"", 0.0254},   {"inch", 0.0254}, {"inches", 0.0254},
        {"ft", 0.3048},    {"'", 0.3048},    {"foot", 0.3048}, {"feet", 0.3048},
        {"yd", 0.9144},    {"yard", 0.9144}, {"yards", 0.9144},
        {"mi", 1609.344},  {"mile", 1609.344}, {"miles", 1609.344},
    };
    return units.value(unit.trimmed().toLower(), 0.0);
}

bool isMeasurement(DrawTool tool) {
    return tool == DrawTool::Reference || tool == DrawTool::Measure || tool == DrawTool::MarkA ||
           tool == DrawTool::MarkB;
}

std::optional<double> SpeedMeasurement::speed(UnitSystem units) const {
    const double meters = metersPerUnit(unit);
    if (meters <= 0.0 || seconds <= 0.0) return std::nullopt;
    const double metersPerSecond = distance * meters / seconds;
    return units == UnitSystem::Metric ? metersPerSecond * 3.6 : metersPerSecond / 0.44704;
}

const Annotation* AnnotationDoc::find(DrawTool tool) const {
    for (const Annotation& a : m_items) {
        if (a.tool == tool) return &a;
    }
    return nullptr;
}

std::optional<SpeedMeasurement> AnnotationDoc::speed() const {
    const Annotation* reference = this->reference();
    const Annotation* a = find(DrawTool::MarkA);
    const Annotation* b = find(DrawTool::MarkB);
    if (!reference || reference->length <= 0.0 || !a || !b || m_displaySize.isEmpty()) return std::nullopt;
    auto pixels = [this](QPointF p, QPointF q) {
        return std::hypot((q.x() - p.x()) * m_displaySize.width(), (q.y() - p.y()) * m_displaySize.height());
    };
    const double refPixels = pixels(reference->points.first(), reference->points.last());
    const double seconds   = std::abs(b->time - a->time);
    if (refPixels <= 0.0 || seconds <= 0.0) return std::nullopt;
    SpeedMeasurement result;
    result.distance = pixels(a->points.first(), b->points.first()) / refPixels * reference->length;
    result.unit     = reference->unit;
    result.seconds  = seconds;
    return result;
}

void AnnotationDoc::add(const Annotation& annotation) {
    QList<Annotation> items = m_items;
    const DrawTool tool = annotation.tool;
    if (tool == DrawTool::Reference || tool == DrawTool::MarkA || tool == DrawTool::MarkB) {
        items.erase(std::remove_if(items.begin(), items.end(), [tool](const Annotation& a) { return a.tool == tool; }),
                    items.end());
    }
    items << annotation;
    commit(items);
}

bool AnnotationDoc::hasDrawings() const {
    return std::any_of(m_items.cbegin(), m_items.cend(), [](const Annotation& a) { return !isMeasurement(a.tool); });
}

bool AnnotationDoc::hasMeasurements() const {
    return std::any_of(m_items.cbegin(), m_items.cend(), [](const Annotation& a) { return isMeasurement(a.tool); });
}

void AnnotationDoc::clear() {
    if (!m_items.isEmpty()) commit({});
}

void AnnotationDoc::clearDrawings() {
    removeWhere(false);
}

void AnnotationDoc::clearMeasurements() {
    removeWhere(true);
}

void AnnotationDoc::removeWhere(bool measurements) {
    QList<Annotation> items = m_items;
    items.erase(std::remove_if(items.begin(), items.end(),
                               [measurements](const Annotation& a) { return isMeasurement(a.tool) == measurements; }),
                items.end());
    if (items.size() != m_items.size()) commit(items);
}

// Snapshots are cheap: a clip holds a handful of shapes.
void AnnotationDoc::commit(QList<Annotation> items) {
    m_undo << m_items;
    m_redo.clear();
    m_items = std::move(items);
    emit changed();
}

void AnnotationDoc::undo() {
    if (m_undo.isEmpty()) return;
    m_redo << m_items;
    m_items = m_undo.takeLast();
    emit changed();
}

void AnnotationDoc::redo() {
    if (m_redo.isEmpty()) return;
    m_undo << m_items;
    m_items = m_redo.takeLast();
    emit changed();
}
