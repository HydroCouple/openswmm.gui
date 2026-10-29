// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/constraintpreview.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh
{
ConstraintPreview makeConstraintPreview(const QVector<SimplificationFeature> &original,
                                       const ConstraintSimplificationResult &candidate,
                                       const std::function<bool()> &cancelled,
                                       int maximumVertices, int maximumFeatures)
{
    ConstraintPreview preview;
    if (!candidate.ok || original.size() != candidate.features.size() ||
        original.size() != candidate.changes.size()) return preview;
    maximumVertices = std::clamp(maximumVertices, 4, 250000);
    const int markerBudget = std::min(2000, maximumVertices / 10);
    const int pathBudget = maximumVertices - markerBudget;
    maximumFeatures = std::clamp(maximumFeatures, 1, std::min(5000, pathBudget / 4));
    preview.featuresTotal = original.size();
    QVector<int> selected;
    quint64 visited = 0;
    double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
    for (int i = 0; i < original.size(); ++i) {
        if (cancelled && cancelled()) { preview.cancelled = true; return preview; }
        preview.verticesBefore += original[i].xy.size();
        preview.verticesAfter += candidate.features[i].xy.size();
        preview.removedTotal += candidate.changes[i].removed;
        for (auto p : original[i].xy) {
            if ((++visited & 4095) == 0 && cancelled && cancelled()) { preview.cancelled = true; return preview; }
            x0 = std::min(x0, p.x()); y0 = std::min(y0, p.y());
            x1 = std::max(x1, p.x()); y1 = std::max(y1, p.y());
        }
        const auto &change = candidate.changes[i];
        if ((change.removed || change.rolledBack || change.budgetRetained) && selected.size() < maximumFeatures)
            selected.append(i);
    }
    for (int i = 0; selected.size() < maximumFeatures && i < original.size(); ++i)
        if (!candidate.changes[i].removed && !candidate.changes[i].rolledBack &&
            !candidate.changes[i].budgetRetained) selected.append(i);
    preview.displayReduced = selected.size() < original.size();
    if (std::isfinite(x0) && std::isfinite(y0)) preview.bounds = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    const int perPath = selected.isEmpty() ? pathBudget / 2
                                         : std::max(2, pathBudget / (2 * int(selected.size())));
    auto sample = [&](const QVector<QPointF> &points) {
        if (points.size() <= perPath) return points;
        QVector<QPointF> output;
        output.reserve(perPath);
        for (int k = 0; k < perPath; ++k)
            output.append(points[qsizetype((quint64(k) * quint64(points.size() - 1)) / (perPath - 1))]);
        return output;
    };
    int removedMarkers = 0;
    for (int i : selected) {
        const auto &input = original[i], &output = candidate.features[i];
        const auto &change = candidate.changes[i];
        ConstraintPreviewFeature feature;
        feature.id = input.id; feature.label = input.label.isEmpty() ? input.id : input.label;
        feature.original = sample(input.xy); feature.proposed = sample(output.xy);
        feature.verticesBefore = input.xy.size(); feature.verticesAfter = output.xy.size();
        feature.removedCount = change.removed;
        feature.closed = input.closed || (input.xy.size() > 1 && input.xy.first().x() == input.xy.last().x() && input.xy.first().y() == input.xy.last().y());
        feature.maxDeviation = change.maxDeviation; feature.maxZDeviation = change.maxZDeviation;
        feature.areaChange = change.areaChange; feature.rolledBack = change.rolledBack;
        feature.budgetRetained = change.budgetRetained;
        feature.displayReduced = feature.original.size() < input.xy.size() || feature.proposed.size() < output.xy.size();
        preview.displayReduced = preview.displayReduced || feature.displayReduced;
        if (change.removed && !change.sourceVertices.isEmpty() && removedMarkers < markerBudget) {
            int kept = 0;
            quint64 removed = 0;
            const int quota = std::min(100, markerBudget - removedMarkers);
            const quint64 stride = std::max<quint64>(1, (quint64(change.removed) + quota - 1) / quota);
            for (int k = 0; k < input.xy.size(); ++k) {
                if ((++visited & 4095) == 0 && cancelled && cancelled()) { preview.cancelled = true; return preview; }
                if (kept < change.sourceVertices.size() && change.sourceVertices[kept] == k) { ++kept; continue; }
                if (removed++ % stride == 0 && removedMarkers < markerBudget) {
                    feature.removed.append(input.xy[k]); ++removedMarkers;
                }
            }
        }
        if (quint64(feature.removed.size()) < feature.removedCount)
            feature.displayReduced = preview.displayReduced = true;
        preview.features.append(std::move(feature));
    }
    return preview;
}
}
