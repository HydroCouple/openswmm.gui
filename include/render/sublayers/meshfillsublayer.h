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
#include <QVector>

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
        // Per-cell infiltration (GUI plan §3.5(5)). The METHOD is categorical
        // and colours through categoryColorForValue(); the parameters are
        // ordinary graduated attributes. Values resolve through
        // mesh::resolveInfil, so a cell inheriting from its region tag paints
        // its region's numbers — which is the point: an assignment can be
        // checked visually before a run.
        InfilMethod,     ///< "infil.method"  (categorical)
        InfilF0,         ///< "infil.f0"
        InfilFmin,       ///< "infil.fmin"
        InfilDecay,      ///< "infil.decay"
        InfilDryTime,    ///< "infil.dryTime"
        InfilFmax,       ///< "infil.Fmax"
        InfilSuction,    ///< "infil.suction"
        InfilKs,         ///< "infil.Ks"
        InfilIMD,        ///< "infil.IMD"
        InfilCN,         ///< "infil.CN"
        InfilRate,       ///< "infil.rate"
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
    // Palette used when colorByAttribute names a CATEGORICAL attribute (the
    // infiltration method). A ClassificationScheme has only Continuous and
    // Classified modes, both numeric — binning a seven-value enumeration at
    // 0.5/1.5/… is a lie about the data and leaks into the legend labels, the
    // same reasoning that gave the edge BC colours their own properties.
    Q_PROPERTY(QString categoryPalette   READ categoryPalette  WRITE setCategoryPalette  NOTIFY styleChanged)

    Q_CLASSINFO("group:fillColor",         "Fill")
    Q_CLASSINFO("group:hillshadeStrength", "Shading")
    Q_CLASSINFO("group:useElevationRamp",  "Fill")
    Q_CLASSINFO("group:colorByAttribute",  "Fill")
    Q_CLASSINFO("group:noDataColor",       "Fill")
    Q_CLASSINFO("group:categoryPalette",   "Fill")
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
    [[nodiscard]] QString categoryPalette() const  { return m_categoryPalette; }

    /*! mesh::cellParamSpecs() key for the current selection — what the
     *  renderer and the layer's cellAttributeValues() take. */
    [[nodiscard]] QByteArray colorByAttributeKey() const
    { return attributeKey(m_colorByAttribute); }

    /*! True when the fill is driven by bed elevation (the default). */
    [[nodiscard]] bool colorsByElevation() const
    { return m_colorByAttribute == CellAttribute::Elevation; }

    /*! True when the selected attribute is CATEGORICAL — i.e. its
     *  mesh::CellParamSpec is Kind::Enum. The renderer then colours each cell
     *  by categoryColorForValue() instead of running the value through the
     *  ClassificationScheme, and the legend emits one row per present value. */
    [[nodiscard]] bool colorsByCategory() const;

    /*! \brief Colour for enumeration value \p v of the current categorical
     *  attribute. \p v is the enum's own integer (mesh::InfilMethod::None is
     *  -1), offset internally by the spec's first value so the palette index
     *  starts at 0. Wraps; out-of-range values fold back into the palette. */
    [[nodiscard]] QColor categoryColorForValue(int v) const;

    void setFillColor(const QColor &v);
    void setHillshadeStrength(double v);
    void setUseElevationRamp(bool v);
    void setColorByAttribute(CellAttribute v);
    void setNoDataColor(const QColor &v);
    void setCategoryPalette(const QString &v);

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
    QString m_categoryPalette    = QStringLiteral("Tab10");
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

    /*! \brief Enumeration values of the selected CATEGORICAL attribute that
     *  actually occur in the host mesh, ascending.
     *
     *  Pushed by SWMM2DMeshLayer (the model) so the const, context-free
     *  legendSymbolItems() can emit one row per method actually in use rather
     *  than seven rows of which five are usually dead — the same treatment
     *  MeshEdgeSublayer::setBcTypesPresent() gives the BC ring. Empty for a
     *  non-categorical attribute. */
    void setCategoriesPresent(const QVector<int> &values);
    [[nodiscard]] const QVector<int> &categoriesPresent() const
    { return m_categoriesPresent; }

private:
    QString        m_id;
    bool           m_visible = true;
    qreal          m_opacity = 1.0;
    MeshFillStyle *m_style;
    bool           m_attributeHasData = true;
    QString        m_depthUnitLabel;
    QVector<int>   m_categoriesPresent;
};

} // namespace OpenSWMM::Render

#endif
