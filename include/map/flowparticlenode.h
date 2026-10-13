// GPL-3.0-or-later
#pragma once
#include "render/flowparticles.h"
#include <QSGGeometryNode>
#include <QColor>
#include <functional>

struct FlowParticleAppearance {
    QPointF anchor;
    QRectF viewport; // invalid means no culling; include stroke margin in scene units
    QColor color=Qt::white;
    double opacity=1;
    bool fade=true,taper=true;
    quint64 revision=0;
    std::function<QColor(double)> colorForSpeed;
};

// Joined history ribbons. Completed simulation frames update geometry;
// presentation interpolation changes only uniforms between those frames.
class FlowParticleNode : public QSGGeometryNode {
public:
    FlowParticleNode();
    void sync(std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame>,
              double interpolation,double scale,double trailSeconds=1,double width=1.5,
              double headWidth=2,double fadeSeconds=.5,quint64 trackedId=0,
              const FlowParticleAppearance &appearance={});
    int uploads() const { return m_uploads; }
    double geometryBuildMs() const { return m_geometryBuildMs; }
private:
    std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> m_frame;
    double m_scale=0;
    quint64 m_trackedId=0;
    QPointF m_anchor;
    QRectF m_viewport;
    QColor m_color;
    quint64 m_appearanceRevision=0;
    int m_uploads=0;
    double m_geometryBuildMs=0;
};
