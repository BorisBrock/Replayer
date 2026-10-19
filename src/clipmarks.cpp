#include "clipmarks.h"

#include <QFileInfo>
#include <QSettings>

#include "clipref.h"

namespace {

// "marks/GX010123.MP4 1234567890" for a file, with " 12.500" appended for
// the part starting there; a group holding "hidden" and "label".
QString group(const QString& clip) {
    const ClipRef ref = ClipRef::fromKey(clip);
    const QFileInfo info(ref.path);
    QString key = QString("marks/%1 %2").arg(info.fileName()).arg(info.size());
    if (ref.isPart()) key += ' ' + QString::number(ref.start, 'f', 3);
    return key;
}

void setValue(const QString& clip, const QString& name, const QVariant& value) {
    QSettings settings;
    settings.beginGroup(group(clip));
    if (value.isValid()) settings.setValue(name, value);
    else settings.remove(name);
}

} // namespace

namespace ClipMarks {

// Bright enough to read as text on the dark list
QList<Label> labels() {
    return {
        {"red",    "Red",    QColor("#f87171")},
        {"orange", "Orange", QColor("#fb923c")},
        {"yellow", "Yellow", QColor("#facc15")},
        {"green",  "Green",  QColor("#4ade80")},
        {"blue",   "Blue",   QColor("#60a5fa")},
        {"purple", "Purple", QColor("#c084fc")},
    };
}

bool isHidden(const QString& clip) {
    return QSettings().value(group(clip) + "/hidden", false).toBool();
}

void setHidden(const QString& clip, bool hidden) {
    setValue(clip, "hidden", hidden ? QVariant(true) : QVariant());
}

QColor color(const QString& clip) {
    const QString id = QSettings().value(group(clip) + "/label").toString();
    if (id.isEmpty()) return {};
    for (const Label& label : labels()) {
        if (label.id == id) return label.color;
    }
    return {};
}

void setLabel(const QString& clip, const QString& id) {
    setValue(clip, "label", id.isEmpty() ? QVariant() : QVariant(id));
}

} // namespace ClipMarks
