#ifndef TIMEFORMAT_H
#define TIMEFORMAT_H

#include <QString>
#include <algorithm>
#include <cmath>

// "mm:ss.zzz", or "h:mm:ss.zzz" from one hour on.
inline QString formatTime(double seconds) {
    const qint64 ms = std::llround(std::max(0.0, seconds) * 1000.0);
    const qint64 h = ms / 3'600'000;
    const qint64 m = ms / 60'000 % 60;
    const qint64 s = ms / 1000 % 60;
    const QString mmss = QString("%1:%2.%3")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(ms % 1000, 3, 10, QChar('0'));
    return h > 0 ? QString("%1:%2").arg(h).arg(mmss) : mmss;
}

#endif
