#ifndef IDLEPICKER_H
#define IDLEPICKER_H

#include <QVector>

// Shared idle-rotation helpers used by all three animation engines and the
// SayingPool. Pure functions so tests never wait on real timers.
namespace IdlePicker {

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
    if (r >= 1.0) r = 0.999999999;
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
    if (r >= 1.0) r = 0.999999;
    return 1000 + static_cast<int>(r * 3000);
}

} // namespace IdlePicker

#endif // IDLEPICKER_H
