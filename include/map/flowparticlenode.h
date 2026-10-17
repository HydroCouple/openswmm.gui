// GPL-3.0-or-later
#pragma once
#include "render/flowparticles.h"
#include <QSGGeometryNode>
#include <QColor>
#include <functional>

using FlowParticleAppearance=OpenSWMM::Render::FlowParticleAppearance;
class QQuickWindow;

// Joined history ribbons. Completed simulation frames update geometry;
// presentation interpolation changes only uniforms between those frames.
class FlowParticleNode : public QSGGeometryNode {
public:
    explicit FlowParticleNode(QQuickWindow *window=nullptr);
    void sync(std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame>,
              double interpolation,double scale,double trailSeconds=1,double width=1.5,
              double headWidth=2,double fadeSeconds=.5,quint64 trackedId=0,
              const FlowParticleAppearance &appearance={});
    int uploads() const { return m_uploads; }
    int indexUploads() const { return 0; } // Packed strips have no index buffer.
    double geometryBuildMs() const { return m_geometryBuildMs; }
private:
    QQuickWindow *m_window=nullptr;
    std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> m_frame;
    OpenSWMM::Render::FlowParticleRibbon m_ribbon;
    double m_scale=0;
    quint64 m_trackedId=0;
    QPointF m_anchor;
    QRectF m_viewport;
    QColor m_color;
    quint64 m_appearanceRevision=0;
    int m_uploads=0;
    bool m_deferredView=false;
    double m_geometryBuildMs=0;
};
