/*!
 * \file   meshfillsublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Static terrain-fill sublayer for the 2D mesh.
 *
 *         Plan: RENDERING_OUTPUT_SUBLAYERS_PLAN.md §3 (SWMM2DResultsLayer
 *         default mix — MeshFill is the bottom static layer underneath
 *         the dynamic depth ramp / contours / vectors).
 *
 *         Static — isDynamic() == false. Terrain elevation does not
 *         change with the animation period; the host dispatch skips it.
 *
 *         Style bag v1 (3 properties):
 *           - fillColor        (QColor — flat fall-back fill colour)
 *           - hillshadeStrength (double 0..1 — shading darkness factor)
 *           - useElevationRamp (bool — when true, the renderer reads
 *                                 vertex-Z and remaps through a built-in
 *                                 elevation ramp; when false, fillColor
 *                                 is used uniformly)
 *
 *         Slice S5.1.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHFILLSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHFILLSUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class MeshFillStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor fillColor         READ fillColor         WRITE setFillColor         NOTIFY styleChanged)
    Q_PROPERTY(double hillshadeStrength READ hillshadeStrength WRITE setHillshadeStrength NOTIFY styleChanged)
    Q_PROPERTY(bool   useElevationRamp  READ useElevationRamp  WRITE setUseElevationRamp  NOTIFY styleChanged)
    // Slice US (mesh) — terrain fill is classified by bed elevation through
    // the shared ClassificationScheme (Continuous = smooth elevation ramp,
    // Classified = discrete bands). useElevationRamp stays the on/off gate.
    Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classification READ scheme WRITE setScheme NOTIFY styleChanged)

    Q_CLASSINFO("group:fillColor",         "Fill")
    Q_CLASSINFO("group:hillshadeStrength", "Shading")
    Q_CLASSINFO("group:useElevationRamp",  "Fill")
    Q_CLASSINFO("group:classification",    "Classification")

public:
    explicit MeshFillStyle(QObject *parent = nullptr);

    [[nodiscard]] QColor fillColor() const         { return m_fillColor; }
    [[nodiscard]] double hillshadeStrength() const { return m_hillshadeStrength; }
    [[nodiscard]] bool   useElevationRamp() const  { return m_useElevationRamp; }

    void setFillColor(const QColor &v);
    void setHillshadeStrength(double v);
    void setUseElevationRamp(bool v);

    /*! The embedded elevation classification scheme (mode, ramp, class
     *  count, range). Consumed by the mesh QSG renderer's fill pass. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &s);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QColor m_fillColor          = QColor(190, 180, 150);
    double m_hillshadeStrength  = 0.5;
    bool   m_useElevationRamp   = true;
    ClassificationScheme m_scheme;
};

class MeshFillSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit MeshFillSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return FillKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Mesh Terrain"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return false; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] MeshFillStyle *fillStyle() const { return m_style; }

private:
    QString        m_id;
    bool           m_visible = true;
    qreal          m_opacity = 1.0;
    MeshFillStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
