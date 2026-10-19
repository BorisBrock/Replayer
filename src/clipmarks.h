#ifndef CLIPMARKS_H
#define CLIPMARKS_H

#include <QColor>
#include <QList>
#include <QString>

// What the user noted about a clip in the list: hidden (nothing of interest)
// or a colour label (e.g. a great jump). Clips are ClipRef keys; like the
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

} // namespace ClipMarks

#endif
