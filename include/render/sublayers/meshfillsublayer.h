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

public:
    /*! Which per-cell quantity drives the fill colour.
     *
     *  Declared as an enum (rather than a free-text attribute key) so the
     *  generic Q_PROPERTY style editor renders it as a combo, the same way
     *  dashPattern and MeshNodeStyle::shape already work.
     *
     *  Everything after Elevation mirrors mesh::cellParamSpecs() in registry
     *  order; attributeKey() below is the single mapping point. The Gw*
     *  members are the pending 2D groundwater set — selectable, but they
     *  carry no stored value yet, so the mesh renders as noDataColor and the
     *  legend says so. Persisted by *key*, not by ordinal, so this list can
     *  be reordered without breaking saved styles. */
    enum class CellAttribute : int
    {
        Elevation = 0,   ///< bed elevation — the historic default
        Mannings,        ///< "mannings"
        InitDepth,       ///< "initDepth"
        GwKs,            ///< "gw.Ks"      (engine support pending)
        GwZs,            ///< "gw.zs"      (engine support pending)
        GwThetaS,        ///< "gw.thetaS"  (engine support pending)
        GwHu0,           ///< "gw.hu0"     (engine support pending)
        GwHg0,           ///< "gw.hg0"     (engine support pending)
    };
    Q_ENUM(CellAttribute)

private:
    Q_PROPERTY(QColor fillColor         READ fillColor         WRITE setFillColor         NOTIFY styleChanged)
    Q_PROPERTY(double hillshadeStrength READ hillshadeStrength WRITE setHillshadeStrength NOTIFY styleChanged)
    Q_PROPERTY(bool   useElevationRamp  READ useElevationRamp  WRITE setUseElevationRamp  NOTIFY styleChanged)
    // Slice US (mesh) — terrain fill is classified by bed elevation through
    // the shared ClassificationScheme (Continuous = smooth elevation ramp,
    // Classified = discrete bands). useElevationRamp stays the on/off gate.
    Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classification READ scheme WRITE setScheme NOTIFY styleChanged)
    Q_PROPERTY(OpenSWMM::Render::MeshFillStyle::CellAttribute colorByAttribute
               READ colorByAttribute WRITE setColorByAttribute NOTIFY styleChanged)
    // Colour for cells whose selected attribute is unset (NaN). Every gw.*
    // key is 100% NaN until the engine grows a soil column, so this is what
    // the whole mesh renders as when one of them is selected.
    Q_PROPERTY(QColor  noDataColor      READ noDataColor      WRITE setNoDataColor      NOTIFY styleChanged)

    Q_CLASSINFO("group:fillColor",         "Fill")
    Q_CLASSINFO("group:hillshadeStrength", "Shading")
    Q_CLASSINFO("group:useElevationRamp",  "Fill")
    Q_CLASSINFO("group:colorByAttribute",  "Fill")
    Q_CLASSINFO("group:noDataColor",       "Fill")
    Q_CLASSINFO("group:classification",    "Classification")

public:
    /*! mesh::cellParamSpecs() key for \p a. CellAttribute::Elevation — the
     *  only colour source that is not a registry key — maps to "elevation". */
    [[nodiscard]] static QByteArray attributeKey(CellAttribute a);
    /*! Inverse of attributeKey(); unknown keys fall back to Elevation. */
    [[nodiscard]] static CellAttribute attributeFromKey(const QByteArray &key);

    explicit MeshFillStyle(QObject *parent = nullptr);

    [[nodiscard]] QColor fillColor() const         { return m_fillColor; }
    [[nodiscard]] double hillshadeStrength() const { return m_hillshadeStrength; }
    /*! On/off gate for graduated fill. Historic name: it now gates the ramp
     *  for *whatever* attribute colorByAttribute selects, not just elevation.
     *  Not renamed because the key ships in .swmm-style.json files and in
     *  projectserializer.cpp. */
    [[nodiscard]] bool   useElevationRamp() const  { return m_useElevationRamp; }
    [[nodiscard]] CellAttribute colorByAttribute() const { return m_colorByAttribute; }
    [[nodiscard]] QColor  noDataColor() const      { return m_noDataColor; }

    /*! mesh::cellParamSpecs() key for the current selection — what the
     *  renderer and the layer's cellAttributeValues() take. */
    [[nodiscard]] QByteArray colorByAttributeKey() const
    { return attributeKey(m_colorByAttribute); }

    /*! True when the fill is driven by bed elevation (the default). */
    [[nodiscard]] bool colorsByElevation() const
    { return m_colorByAttribute == CellAttribute::Elevation; }

    void setFillColor(const QColor &v);
    void setHillshadeStrength(double v);
    void setUseElevationRamp(bool v);
    void setColorByAttribute(CellAttribute v);
    void setNoDataColor(const QColor &v);

    /*! The embedded elevation classification scheme (mode, ramp, class
     *  count, range). Consumed by the mesh QSG renderer's fill pass. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &s);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QColor  m_fillColor          = QColor(190, 180, 150);
    double  m_hillshadeStrength  = 0.5;
    bool    m_useElevationRamp   = true;
    CellAttribute m_colorByAttribute = CellAttribute::Elevation;
    QColor  m_noDataColor        = QColor(205, 205, 205, 120);
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

    /*! \brief Whether the currently selected attribute has any non-NaN value
     *  in the host mesh. Pushed by SWMM2DMeshLayer (the model) so the const,
     *  context-free legendSymbolItems() can say "no data" instead of showing
     *  a plausible-looking uniform swatch. */
    void setAttributeHasData(bool hasData);
    [[nodiscard]] bool attributeHasData() const { return m_attributeHasData; }

    /*! \brief Project depth-unit label ("m" / "ft"), used to suffix the
     *  legend title for length-valued attributes. Pushed by the layer. */
    void setDepthUnitLabel(const QString &l) { m_depthUnitLabel = l; }

private:
    QString        m_id;
    bool           m_visible = true;
    qreal          m_opacity = 1.0;
    MeshFillStyle *m_style;
    bool           m_attributeHasData = true;
    QString        m_depthUnitLabel;
};

} // namespace OpenSWMM::Render

#endif
