/*!
 * \file   couplednodesublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SWMM-coupled vertex marker sublayer for the 2D mesh.
 *
 *         Static — isDynamic() == false. The coupled set follows the
 *         [2D_VERTEX_NODE_MAP] tags, not the animation period.
 *
 *         Carved out of MeshNodeStyle (which used to carry
 *         highlightTagged / taggedColor / taggedSizePx as a group on the
 *         Mesh Vertices sublayer) so coupled-node markers are a
 *         first-class sublayer: their own layer-tree row, styling tab, and
 *         — new — visibility independent of the mesh-vertex markers.
 *         Legacy styles migrate in SWMM2DMeshLayer::onSublayersJsonLoaded.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_COUPLEDNODESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_COUPLEDNODESUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class CoupledNodeStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor color        READ color        WRITE setColor        NOTIFY styleChanged)
    Q_PROPERTY(double markerSizePx READ markerSizePx WRITE setMarkerSizePx NOTIFY styleChanged)

    Q_CLASSINFO("group:color",        "Symbology")
    Q_CLASSINFO("group:markerSizePx", "Symbology")

public:
    explicit CoupledNodeStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] QColor color() const        { return m_color; }
    [[nodiscard]] double markerSizePx() const { return m_markerSizePx; }

    void setColor(const QColor &v) { if (m_color == v) return; m_color = v; setDirty(); }
    void setMarkerSizePx(double v);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    // Defaults mirror MeshNodeStyle's historic taggedColor / taggedSizePx so
    // migrated projects render identically.
    QColor m_color        = QColor(0xff, 0x8c, 0x00, 235);   // orange
    double m_markerSizePx = 5.0;
};

class CoupledNodeSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit CoupledNodeSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return MarkerKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Coupled Nodes"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return false; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] CoupledNodeStyle *coupledStyle() const { return m_style; }

private:
    QString           m_id;
    // Visible by default: the legacy behaviour (highlightTagged default true
    // with the mesh ctor turning vertices on) drew coupled markers on a
    // fresh mesh, and they are the primary 1D↔2D wiring cue.
    bool              m_visible = true;
    qreal             m_opacity = 1.0;
    CoupledNodeStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
