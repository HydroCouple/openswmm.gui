/*!
 * \file   binsampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Frame-sampler implementation (Slice Z.7).
 */

#include "render/binsampler.h"

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

QVector<int> sampledFrameIndices(int frameCount, double sampleRate)
{
    if (frameCount <= 0)
        return {};

    const double clamped = std::clamp(sampleRate, 0.0, 1.0);
    // Always include at least one frame so the sample is non-empty.
    int k = static_cast<int>(std::ceil(clamped * frameCount));
    if (k < 1) k = 1;
    if (k > frameCount) k = frameCount;

    QVector<int> indices;
    indices.reserve(k);
    if (k == 1) {
        indices.append(0);
        return indices;
    }
    // Spread k indices across [0, frameCount-1] inclusive. Use double
    // arithmetic so the first and last frames are always sampled when
    // k >= 2.
    const double stride = (frameCount - 1) / static_cast<double>(k - 1);
    for (int i = 0; i < k; ++i) {
        int idx = static_cast<int>(std::round(i * stride));
        if (idx < 0) idx = 0;
        if (idx > frameCount - 1) idx = frameCount - 1;
        indices.append(idx);
    }
    return indices;
}

QVector<double>
sampleBreaksAcrossFrames(const IntervalBinner &binner,
                          const QVector<QVector<double>> &perFrameValues,
                          double sampleRate)
{
    if (perFrameValues.isEmpty())
        return {};

    const QVector<int> idx = sampledFrameIndices(perFrameValues.size(),
                                                  sampleRate);

    // Flatten selected frames into one sample vector.
    QVector<double> flat;
    {
        // Estimate total size so we don't repeatedly reallocate.
        int total = 0;
        for (int i : idx)
            if (i >= 0 && i < perFrameValues.size())
                total += perFrameValues[i].size();
        flat.reserve(total);
    }
    for (int i : idx) {
        if (i < 0 || i >= perFrameValues.size())
            continue;
        const QVector<double> &frame = perFrameValues[i];
        for (double v : frame)
            flat.append(v);
    }

    return binner.computeBreaks(flat);
}

} // namespace OpenSWMM::Render
