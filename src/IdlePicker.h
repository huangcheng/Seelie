#ifndef IDLEPICKER_H
#define IDLEPICKER_H

#include <QVector>
#include <algorithm>

// Shared idle-rotation helpers used by all three animation engines and the
// SayingPool. Pure functions so tests never wait on real timers.
namespace IdlePicker {

// Upper clamp for `r` (uniform in [0,1)): shared by both helpers so the
// magic number lives in exactly one place. Kept below 1.0 on purpose —
// callers feed raw [0,1) draws, and we never want r == 1.
constexpr double kMaxR = 0.999999999;

// Weighted pick from `weights`, skipping index `exclude` (pass -1 for no
// exclusion). `r` is uniform in [0,1). Returns -1 when no candidate has
// positive weight.
inline int pickWeighted(const QVector<int> &weights, int exclude, double r)
{
    int total = 0;
    for (int i = 0; i < weights.size(); ++i)
        if (i != exclude) total += weights.at(i);
    if (total <= 0) return -1;
    if (r < 0.0) r = 0.0;
    if (r >= 1.0) r = kMaxR;
    const int roll = static_cast<int>(r * total);
    int cumulative = 0;
    for (int i = 0; i < weights.size(); ++i) {
        if (i == exclude) continue;
        cumulative += weights.at(i);
        if (roll < cumulative) return i;
    }
    return -1;
}

// Uniform idle gap in [1000, 4000] ms. `r` uniform in [0,1), clamped.
inline int idleTimeoutMs(double r)
{
    if (r < 0.0) r = 0.0;
    if (r >= 1.0) r = kMaxR;
    return 1000 + std::min(static_cast<int>(r * 3001), 3000);
}

} // namespace IdlePicker

#endif // IDLEPICKER_H
