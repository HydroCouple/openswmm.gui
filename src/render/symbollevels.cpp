/*!
 * \file   symbollevels.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Symbol Levels paint-order algorithm (Slice Z.11).
 */

#include "render/symbollevels.h"

#include <algorithm>

namespace OpenSWMM::Render
{

int symbolLayerLevel(const SymbolLayer &layer)
{
    const QString key = QString::fromLatin1(kSymbolLevelPropKey);
    const auto it = layer.props.constFind(key);
    if (it == layer.props.constEnd())
        return 0;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    return ok ? v : 0;
}

void setSymbolLayerLevel(SymbolLayer &layer, int level)
{
    layer.props[QString::fromLatin1(kSymbolLevelPropKey)] = level;
}

QVector<PaintStep>
computeSymbolLevelOrder(const QVector<SymbolStyle> &features, bool enabled)
{
    QVector<PaintStep> steps;

    // Estimate total step count for a single reserve.
    int total = 0;
    for (const SymbolStyle &s : features)
        total += s.layers.size();
    steps.reserve(total);

    if (!enabled) {
        // Legacy order: per-feature, bottom-up symbol layers.
        for (int f = 0; f < features.size(); ++f) {
            const int n = features[f].layers.size();
            for (int sl = 0; sl < n; ++sl)
                steps.append({f, sl});
        }
        return steps;
    }

    // Level-major order. Build (level, featureIdx, symbolLayerIdx)
    // tuples, then sort by level then feature then symbol-layer for
    // stable ordering.
    struct Entry { int level; int featureIdx; int slIdx; };
    QVector<Entry> entries;
    entries.reserve(total);
    for (int f = 0; f < features.size(); ++f) {
        const auto &layers = features[f].layers;
        for (int sl = 0; sl < layers.size(); ++sl) {
            entries.append({ symbolLayerLevel(layers[sl]), f, sl });
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &a, const Entry &b) {
                         if (a.level != b.level) return a.level < b.level;
                         if (a.featureIdx != b.featureIdx) return a.featureIdx < b.featureIdx;
                         return a.slIdx < b.slIdx;
                     });
    for (const Entry &e : entries)
        steps.append({ e.featureIdx, e.slIdx });
    return steps;
}

} // namespace OpenSWMM::Render
