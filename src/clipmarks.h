#ifndef CLIPMARKS_H
#define CLIPMARKS_H

#include <QColor>
#include <QList>
#include <QString>
#include <utility>

// What the user noted about a clip in the list: hidden (nothing of interest),
// a colour label (e.g. a great jump) and the start and end markers on its
// seek bar. Clips are ClipRef keys; like the
// splits (see ClipSplits), the marks are kept in the settings and recognise
// a file by its name and size, so they stay when the folder is copied.
namespace ClipMarks {

struct Label {
    QString id;   // stored in the settings: "red"
    QString name; // shown in menus: "Red"
    QColor  color;
};
QList<Label> labels();

bool isHidden(const QString& clip);
void setHidden(const QString& clip, bool hidden);

// The label's colour; invalid when the clip has none.
QColor color(const QString& clip);
// An empty id removes the label.
void setLabel(const QString& clip, const QString& id);

// Start and end marker, in seconds from the clip's start; end is -1 when
// the markers are at the clip's ends. setTrim(clip, 0, -1) removes them.
std::pair<double, double> trim(const QString& clip);
void setTrim(const QString& clip, double start, double end);

} // namespace ClipMarks

#endif
