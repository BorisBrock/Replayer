#include "clipsplits.h"

#include <QFileInfo>
#include <QSettings>
#include <algorithm>

namespace {

// "splits/GX010123.MP4 1234567890"; the settings escape what keys can't hold.
QString settingsKey(const QString& path) {
    const QFileInfo info(path);
    return QString("splits/%1 %2").arg(info.fileName()).arg(info.size());
}

} // namespace

namespace ClipSplits {

// Stored as "start,end" pairs in seconds.
QList<ClipRef> parts(const QString& path) {
    QList<ClipRef> result;
    for (const QString& range : QSettings().value(settingsKey(path)).toStringList()) {
        const QStringList bounds = range.split(',');
        if (bounds.size() != 2) continue;
        bool okStart = false, okEnd = false;
        const double start = bounds[0].toDouble(&okStart);
        const double end   = bounds[1].toDouble(&okEnd);
        if (okStart && okEnd && start >= 0.0 && end > start) result << ClipRef{path, start, end};
    }
    std::sort(result.begin(), result.end(),
              [](const ClipRef& a, const ClipRef& b) { return a.start < b.start; });
    return result;
}

void setParts(const QString& path, const QList<ClipRef>& parts) {
    QSettings settings;
    if (parts.isEmpty()) {
        settings.remove(settingsKey(path));
        return;
    }
    QStringList ranges;
    for (const ClipRef& part : parts) {
        ranges << QString("%1,%2").arg(QString::number(part.start, 'f', 6), QString::number(part.end, 'f', 6));
    }
    settings.setValue(settingsKey(path), ranges);
}

} // namespace ClipSplits
