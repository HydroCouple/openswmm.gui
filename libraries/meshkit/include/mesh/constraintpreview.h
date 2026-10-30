// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "constraintsimplifier.h"
#include <QRectF>

namespace mesh
{
struct ConstraintPreviewFeature {
    QString id, label;
    QVector<QPointF> original, proposed, removed;
    quint64 verticesBefore = 0, verticesAfter = 0, removedCount = 0;
    double maxDeviation = 0, maxZDeviation = 0, areaChange = 0;
    bool closed = false, displayReduced = false, rolledBack = false, budgetRetained = false;
};
struct ConstraintPreview {
    QVector<ConstraintPreviewFeature> features;
    QRectF bounds;
    quint64 featuresTotal = 0, verticesBefore = 0, verticesAfter = 0, removedTotal = 0;
    bool displayReduced = false, cancelled = false;
    QString horizontalUnit, verticalUnit;
};
// Bounded DISPLAY data only. Audits and generation always use the full geometry.
// Changed features precede unchanged context; sampling never changes the result.
// The candidate and source mapping must have passed the preparation audit.
ConstraintPreview makeConstraintPreview(const QVector<SimplificationFeature> &original,
                                       const ConstraintSimplificationResult &candidate,
                                       const std::function<bool()> &cancelled = {},
                                       int maximumVertices = 50000, int maximumFeatures = 500);
}
