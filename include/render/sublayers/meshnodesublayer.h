/*!
 * \file   meshnodesublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Stylable mesh-vertex marker sublayer.
 *
 *         Static — isDynamic() == false. Vertex positions follow the mesh,
 *         not the animation period.
 *
 *         Replaces the previous boolean "showMeshNodes" toggle (no styling)
 *         with a property bag that exposes color, marker size, shape, and
 *         a distinct color for SWMM-coupled (tagged) vertices. Hidden by
 *         default to preserve the existing visual.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHNODESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHNODESUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class MeshNodeStyle : public SublayerStyle
{
    Q_OBJECT
public:
    enum MarkerShape { Circle = 0, Square, Triangle, Diamond };
    Q_ENUM(MarkerShape)

private:
    Q_PROPERTY(QColor      color           READ color           WRITE setColor           NOTIFY styleChanged)
    Q_PROPERTY(double      markerSizePx    READ markerSizePx    WRITE setMarkerSizePx    NOTIFY styleChanged)
    Q_PROPERTY(MarkerShape shape           READ shape           WRITE setShape           NOTIFY styleChanged)
    Q_PROPERTY(QColor      outlineColor    READ outlineColor    WRITE setOutlineColor    NOTIFY styleChanged)
    Q_PROPERTY(double      outlineWidthPx  READ outlineWidthPx  WRITE setOutlineWidthPx  NOTIFY styleChanged)
    // Tagged (SWMM-coupled) vertex styling moved to CoupledNodeStyle
    // (couplednodesublayer.h) — coupled markers are their own sublayer now.
    // Legacy "tagged*" JSON keys migrate in
    // SWMM2DMeshLayer::onSublayersJsonLoaded.

    Q_CLASSINFO("group:color",           "Symbology")
    Q_CLASSINFO("group:markerSizePx",    "Symbology")
    Q_CLASSINFO("group:shape",           "Symbology")
    Q_CLASSINFO("group:outlineColor",    "Outline")
    Q_CLASSINFO("group:outlineWidthPx",  "Outline")

public:
    explicit MeshNodeStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] QColor      color() const           { return m_color; }
    [[nodiscard]] double      markerSizePx() const    { return m_markerSizePx; }
    [[nodiscard]] MarkerShape shape() const           { return m_shape; }
    [[nodiscard]] QColor      outlineColor() const    { return m_outlineColor; }
    [[nodiscard]] double      outlineWidthPx() const  { return m_outlineWidthPx; }

    void setColor(const QColor &v)         { if (m_color == v) return; m_color = v; setDirty(); }
    void setMarkerSizePx(double v);
    void setShape(MarkerShape v)           { if (m_shape == v) return; m_shape = v; setDirty(); }
    void setOutlineColor(const QColor &v)  { if (m_outlineColor == v) return; m_outlineColor = v; setDirty(); }
    void setOutlineWidthPx(double v);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QColor      m_color           = QColor(40, 40, 40, 220);
    double      m_markerSizePx    = 3.0;
    MarkerShape m_shape           = Circle;
    QColor      m_outlineColor    = QColor(255, 255, 255, 220);
    double      m_outlineWidthPx  = 0.5;
};

class MeshNodeSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit MeshNodeSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return MarkerKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Mesh Vertices"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return false; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] MeshNodeStyle *nodeStyle() const { return m_style; }

private:
    QString        m_id;
    bool           m_visible = false; // matches historic showMeshNodes default
    qreal          m_opacity = 1.0;
    MeshNodeStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
