// GPL-3.0-or-later
#pragma once
#include "render/flowstreakpaths.h"
#include "render/sublayers/flowstreaksublayer.h"
#include <QSGGeometryNode>

class FlowStreakNode : public QSGGeometryNode {
public:
    FlowStreakNode();
    void sync(std::shared_ptr<const OpenSWMM::Render::FlowStreakBatch>,
              const OpenSWMM::Render::FlowStreakStyle &, double phase,
              double opacity, QPointF anchor, double pixelsPerSceneUnit, double displaySeconds=0);
    int uploads() const { return m_uploads; }
private:
    std::shared_ptr<const OpenSWMM::Render::FlowStreakBatch> m_batch;
    QJsonObject m_style;
    QPointF m_anchor;
    double m_scale=0;
    int m_uploads=0;
};
