#ifndef CLIPSPLITS_H
#define CLIPSPLITS_H

#include <QList>
#include <QString>

#include "clipref.h"

// The parts a long recording was split into (see SplitDialog), kept in the
// settings. A file is recognised by its name and size, so its parts stay
// when the folder is copied, e.g. from the camera's memory card.
namespace ClipSplits {

// The parts of path in time order; empty when it isn't split.
QList<ClipRef> parts(const QString& path);
// Replaces the parts of path; an empty list removes the split.
void setParts(const QString& path, const QList<ClipRef>& parts);

} // namespace ClipSplits

#endif
