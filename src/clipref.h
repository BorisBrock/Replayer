#ifndef CLIPREF_H
#define CLIPREF_H

#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

#include "timeformat.h"

// A clip shown by the player: a whole video file, or a part of one cut out
// with the split dialog. Clips are passed around as key() strings, which for
// a part append a media fragment to the path: "/clips/GX010123.MP4#t=12.5,20.25".
struct ClipRef {
    QString path;
    double  start = 0.0;  // seconds from the start of the file
    double  end   = -1.0; // -1: the end of the file

    bool isPart() const { return end >= 0.0; }

    QString key() const {
        if (!isPart()) return path;
        return QString("%1#t=%2,%3").arg(path, QString::number(start, 'f', 6), QString::number(end, 'f', 6));
    }

    static ClipRef fromKey(const QString& key) {
        static const QRegularExpression fragment(R"(#t=(\d+(?:\.\d*)?),(\d+(?:\.\d*)?)$)");
        const QRegularExpressionMatch match = fragment.match(key);
        if (!match.hasMatch()) return {key};
        return {key.left(match.capturedStart()), match.captured(1).toDouble(), match.captured(2).toDouble()};
    }

    // Where a part starts, to the second: "00:12"
    QString startText() const { return formatTime(start).section('.', 0, 0); }

    // "GX010123.MP4", or "GX010123.MP4 (00:12)" for a part
    QString displayName() const {
        const QString name = QFileInfo(path).fileName();
        return isPart() ? QString("%1 (%2)").arg(name, startText()) : name;
    }
};

#endif
