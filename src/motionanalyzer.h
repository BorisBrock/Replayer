#ifndef MOTIONANALYZER_H
#define MOTIONANALYZER_H

#include <QList>
#include <QString>
#include <atomic>
#include <functional>
#include <utility>

// Finds the jumps in a long recording from a static camera: the parts with
// motion in them, separated by quiet stretches of an empty scene.
namespace MotionAnalyzer {

struct Sample {
    double time   = 0.0; // seconds from the start of the file
    double motion = 0.0; // mean change from the previous sample, 0..255
};

// Decodes about four frames a second (keyframes only, where they are that
// frequent) at low quality and measures how much each differs from the one
// before. Slow for long clips, so run it on a worker thread; progress gets
// 0..1. Returns nothing on failure or when cancel is set.
QList<Sample> analyze(const QString& path, const std::atomic<bool>& cancel,
                      const std::function<void(double)>& progress);

// The parts with motion, each with some lead-in and run-out, as (start, end)
// in seconds. Empty when nothing stands out, e.g. a handheld camera.
QList<std::pair<double, double>> suggestParts(const QList<Sample>& samples, double duration);

} // namespace MotionAnalyzer

#endif
