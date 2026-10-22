/**
 * @file      meshfillsublayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Static terrain-fill sublayer for the 2D mesh.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    enum class CellAttribute : int {
        Elevation = 0, ///< bed elevation — the historic default
        Mannings,      ///< "mannings"
        InitDepth,     ///< "initDepth"
        // Per-cell infiltration (GUI plan §3.5(5)). The METHOD is categorical
        // and colours through categoryColorForValue(); the parameters are
        // ordinary graduated attributes. Values resolve through
        // mesh::resolveInfil, so a cell inheriting from its region tag paints
        // its region's numbers — which is the point: an assignment can be
        // checked visually before a run.
        InfilMethod,  ///< "infil.method"  (categorical)
        InfilF0,      ///< "infil.f0"
        InfilFmin,    ///< "infil.fmin"
        InfilDecay,   ///< "infil.decay"
        InfilDryTime, ///< "infil.dryTime"
        InfilFmax,    ///< "infil.Fmax"
        InfilSuction, ///< "infil.suction"
        InfilKs,      ///< "infil.Ks"
        InfilIMD,     ///< "infil.IMD"
        InfilCN,      ///< "infil.CN"
        InfilRate,    ///< "infil.rate"
        GwKs,         ///< "gw.Ks"      (engine support pending)
        GwZs,         ///< "gw.zs"      (engine support pending)
        GwThetaS,     ///< "gw.thetaS"  (engine support pending)
        GwHu0,        ///< "gw.hu0"     (engine support pending)
        GwHg0,        ///< "gw.hg0"     (engine support pending)
        InfilOwner,
        InfilConflict,
    };
    Q_ENUM(CellAttribute)

private:
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY styleChanged)
    Q_PROPERTY(double hillshadeStrength READ hillshadeStrength WRITE setHillshadeStrength NOTIFY
                   styleChanged)
    Q_PROPERTY(
        bool useElevationRamp READ useElevationRamp WRITE setUseElevationRamp NOTIFY styleChanged)
    // Slice US (mesh) — terrain fill is classified by bed elevation through
    // the shared ClassificationScheme (Continuous = smooth elevation ramp,
    // Classified = discrete bands). useElevationRamp stays the on/off gate.
    Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classification READ scheme WRITE setScheme
                   NOTIFY styleChanged)
    Q_PROPERTY(OpenSWMM::Render::MeshFillStyle::CellAttribute colorByAttribute READ colorByAttribute
                   WRITE setColorByAttribute NOTIFY styleChanged)
    // Colour for cells whose selected attribute is unset (NaN). Every gw.*
    // key is 100% NaN until the engine grows a soil column, so this is what
    // the whole mesh renders as when one of them is selected.
    Q_PROPERTY(QColor noDataColor READ noDataColor WRITE setNoDataColor NOTIFY styleChanged)
    // Palette used when colorByAttribute names a CATEGORICAL attribute (the
    // infiltration method). A ClassificationScheme has only Continuous and
    // Classified modes, both numeric — binning a seven-value enumeration at
    // 0.5/1.5/… is a lie about the data and leaks into the legend labels, the
    // same reasoning that gave the edge BC colours their own properties.
    Q_PROPERTY(
        QString categoryPalette READ categoryPalette WRITE setCategoryPalette NOTIFY styleChanged)

    Q_CLASSINFO("group:fillColor", "Fill")
    Q_CLASSINFO("group:hillshadeStrength", "Shading")
    Q_CLASSINFO("group:useElevationRamp", "Fill")
    Q_CLASSINFO("group:colorByAttribute", "Fill")
    Q_CLASSINFO("group:noDataColor", "Fill")
    Q_CLASSINFO("group:categoryPalette", "Fill")
    Q_CLASSINFO("group:classification", "Classification")

public:
    /*! mesh::cellParamSpecs() key for \p a. CellAttribute::Elevation — the
     *  only colour source that is not a registry key — maps to "elevation". */
    /** @param a Attribute enum; unknown integers map to elevation.
     * @return Independent exact registry key (case-sensitive UTF-8 bytes). */
    [[nodiscard]] static QByteArray attributeKey(CellAttribute a);
    /*! Inverse of attributeKey(); unknown keys fall back to Elevation. */
    /** @param key Exact case-sensitive registry token, without trimming.
     * @return Matched enum, or Elevation for any unknown/empty token. */
    [[nodiscard]] static CellAttribute attributeFromKey(const QByteArray &key);

    /** @brief Construct terrain styling with a Continuous Terrain ramp and five stored classes.
     * @param parent Optional QObject owner; destroys this style with itself.
     * Ordinary reads/edits occur on the owning Qt thread. */
    explicit MeshFillStyle(QObject *parent = nullptr);

    /** @brief Read the flat terrain/fallback fill color.
     * @return Stored value, initially RGB(190,180,150); may be invalid after setter input. */
    [[nodiscard]] QColor fillColor() const { return m_fillColor; }
    /** @brief Read the hillshade darkness multiplier.
     * @return Stored value, initially 0.5; finite setter inputs are clamped to [0,1]. */
    [[nodiscard]] double hillshadeStrength() const { return m_hillshadeStrength; }
    /*! On/off gate for graduated fill. Historic name: it now gates the ramp
     *  for *whatever* attribute colorByAttribute selects, not just elevation.
     *  Not renamed because the key ships in .swmm-style.json files and in
     *  projectserializer.cpp. */
    /** @return Stored ramp/category gate, initially true; not restricted to elevation. */
    [[nodiscard]] bool useElevationRamp() const { return m_useElevationRamp; }
    /** @brief Read the selected per-cell source enum.
     * @return Stored enum, initially Elevation; setter input is not validated. */
    [[nodiscard]] CellAttribute colorByAttribute() const { return m_colorByAttribute; }
    /** @brief Read the unavailable-attribute color.
     * @return Stored value, initially RGBA(205,205,205,120). */
    [[nodiscard]] QColor noDataColor() const { return m_noDataColor; }
    /** @brief Read the categorical palette name.
     * @return Stored token, initially Tab10; unknown names are retained until lookup. */
    [[nodiscard]] QString categoryPalette() const { return m_categoryPalette; }

    /*! mesh::cellParamSpecs() key for the current selection — what the
     *  renderer and the layer's cellAttributeValues() take. */
    /** @return Independent registry key for the selection; an unknown enum maps to elevation. */
    [[nodiscard]] QByteArray colorByAttributeKey() const
    {
        return attributeKey(m_colorByAttribute);
    }

    /*! True when the fill is driven by bed elevation (the default). */
    /** @return True only for the exact Elevation enum; an invalid enum is false even
     * though attributeKey() later falls back to the elevation key. */
    [[nodiscard]] bool colorsByElevation() const
    {
        return m_colorByAttribute == CellAttribute::Elevation;
    }

    /*! True when the selected attribute is CATEGORICAL — i.e. its
     *  mesh::CellParamSpec is Kind::Enum. The renderer then colours each cell
     *  by categoryColorForValue() instead of running the value through the
     *  ClassificationScheme, and the legend emits one row per present value. */
    /** @return True only when the selected non-Elevation key resolves to a CellParamSpec
     * whose Kind is Enum. This queries metadata without examining mesh values. */
    [[nodiscard]] bool colorsByCategory() const;

    /*! \brief Colour for enumeration value \p v of the current categorical
     *  attribute. \p v is the enum's own integer (mesh::InfilMethod::None is
     *  -1), offset internally by the spec's first value so the palette index
     *  starts at 0. Wraps; out-of-range values fold back into the palette. */
    /** @param v Raw enum integer, shifted by the selected registry spec's rounded
     * minimum before palette wrapping; supply a value whose subtraction fits int.
     * @return Palette entry with authored alpha, or fillColor if the palette is empty.
     * Unknown palette names follow CategoricalPalette::byName() fallback rules.
     * The selected attribute need not actually be categorical; no value validation occurs. */
    [[nodiscard]] QColor categoryColorForValue(int v) const;

    /** @brief Set the flat fallback color and notify on QColor inequality.
     * @param v Color copied verbatim, including invalid values. */
    void setFillColor(const QColor &v);
    /** @brief Clamp shading strength and notify on a changed value.
     * @param v Finite multiplier clamped to [0,1]; NaN is not sanitized.
     * Uses qFuzzyCompare(current+1, input+1) before emitting styleChanged. */
    void setHillshadeStrength(double v);
    /** @brief Enable or disable attribute-driven ramp/category coloring.
     * @param v New Boolean; exact changes emit styleChanged without resetting the source. */
    void setUseElevationRamp(bool v);
    /** @brief Select a per-cell quantity and disable a pinned custom range on change.
     * @param v Enum stored without validation. Equal input is a no-op. On change,
     * useCustomRange is cleared while endpoints, ramp and other scheme settings remain;
     * styleChanged is emitted. This does not read values from the host mesh. */
    void setColorByAttribute(CellAttribute v);
    /** @brief Set the unavailable-attribute color and notify on inequality.
     * @param v Color copied verbatim, including invalid values. */
    void setNoDataColor(const QColor &v);
    /** @brief Store a categorical palette token and notify on an exact string change.
     * @param v Token copied without validation; unknown names fall back during palette lookup. */
    void setCategoryPalette(const QString &v);

    /*! The embedded elevation classification scheme (mode, ramp, class
     *  count, range). Consumed by the mesh QSG renderer's fill pass. */
    /** @return Borrowed owned numeric scheme for the selected source, replaced by
     * setScheme()/fromJson(); categorical coloring uses its separate palette instead. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy a complete numeric scheme and notify on inequality.
     * @param s Source copied without validation; ClassificationScheme equality
     * determines whether it replaces the current scheme. Other style fields are retained. */
    void setScheme(const ClassificationScheme &s);

    /** @brief Serialize terrain colors, shading, ramp gate, palette and classification.
     * @return Independent object; source is persisted by stable key, colors as ARGB.
     * Invalid source enum values serialize as elevation through attributeKey(). */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read partial terrain styling and emit styleChanged unconditionally.
     * @param j Borrowed object; absent/mistyped scalar fields retain current values.
     * Invalid/empty color text is ignored; finite shading clamps to [0,1]. A nonempty
     * source key maps through attributeFromKey(), so unknown keys select Elevation;
     * empty keys retain the source. Nonempty palette tokens are retained verbatim.
     * A present classification key replaces the scheme via its reader, including a
     * nonobject as an empty object. Unlike setColorByAttribute(), this path does not
     * clear the custom-range flag merely because the source changes. */
    void fromJson(const QJsonObject &j) override;

private:
    QColor m_fillColor = QColor(190, 180, 150);
    double m_hillshadeStrength = 0.5;
    bool m_useElevationRamp = true;
    CellAttribute m_colorByAttribute = CellAttribute::Elevation;
    QColor m_noDataColor = QColor(205, 205, 205, 120);
    QString m_categoryPalette = QStringLiteral("Tab10");
    ClassificationScheme m_scheme;
};

class MeshFillSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a visible, opacity-one static pass and its owned style.
     * @param id_ Stable identifier moved into storage without checking uniqueness.
     * @param parent Optional QObject owner; destroys this pass and child style.
     * Style changes forward to invalidated(); access occurs on the owning Qt thread. */
    explicit MeshFillSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify the scene pass kind.
     * @return FillKind. */
    Kind kind() const override { return FillKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time ID without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Mesh Terrain label. */
    QString displayName() const override { return tr("Mesh Terrain"); }

    /** @brief Read local visibility.
     * @return Stored flag, initially true; host visibility is not consulted. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace visibility and emit invalidated() on an exact Boolean change.
     * @param v New visibility; no synchronous geometry construction occurs. */
    void setVisible(bool v) override;
    /** @brief Read local opacity.
     * @return Stored multiplier, initially one. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Replace clamped opacity and emit invalidated() on a change.
     * @param o Finite multiplier clamped to [0,1]; NaN is not explicitly rejected.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments. */
    void setOpacity(qreal o) override;

    /** @brief Report that this pass does not depend on the animation period.
     * @return False; hosts can still invalidate it after geometry/style changes. */
    bool isDynamic() const override { return false; }

    /** @brief Borrow the owned property bag through its base interface.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    SublayerStyle *style() override { return m_style; }

    /** @brief Build a context-free legend preview from style and host metadata.
     * @return Categorical mode returns one row per host-supplied present value using registry
     * labels and category colors. Otherwise returns one flat fill preview; a non-
     * elevation source flagged as unavailable uses noDataColor and a no-data label.
     * Does not expand numeric classification or sample the elevation ramp. Visibility
     * is ignored; rows carry the current id and opacity. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve an existing node while the host renderer builds actual geometry.
     * @param existing Borrowed node returned unchanged, including null.
     * @param ctx Unused rendering context.
     * @return existing without allocation or ownership transfer. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    [[nodiscard]] MeshFillStyle *fillStyle() const { return m_style; }

    /*! \brief Whether the currently selected attribute has any non-NaN value
     *  in the host mesh. Pushed by SWMM2DMeshLayer (the model) so the const,
     *  context-free legendSymbolItems() can say "no data" instead of showing
     *  a plausible-looking uniform swatch. */
    /** @param hasData Host-computed availability flag; copied without inspecting cells.
     * A Boolean change emits invalidated(); equal input is ignored. */
    void setAttributeHasData(bool hasData);
    /** @brief Read the last host-provided data-availability flag.
     * @return Stored flag, initially true; not computed from source values here. */
    [[nodiscard]] bool attributeHasData() const { return m_attributeHasData; }

    /*! \brief Project depth-unit label ("m" / "ft"), used to suffix the
     *  legend title for length-valued attributes. Pushed by the layer. */
    /** @param l Project depth-unit suffix copied verbatim; no validation, conversion
     * or invalidated() signal occurs. Host must arrange repaint if needed. */
    void setDepthUnitLabel(const QString &l) { m_depthUnitLabel = l; }

    /*! \brief Enumeration values of the selected CATEGORICAL attribute that
     *  actually occur in the host mesh, ascending.
     *
     *  Pushed by SWMM2DMeshLayer (the model) so the const, context-free
     *  legendSymbolItems() can emit one row per method actually in use rather
     *  than seven rows of which five are usually dead — the same treatment
     *  MeshEdgeSublayer::setBcTypesPresent() gives the BC ring. Empty for a
     *  non-categorical attribute. */
    /** @param values Host-provided enum values copied in supplied order without sorting,
     * deduplication or validation. A vector change emits invalidated(). Empty input
     * causes the categorical legend path to fall through to the generic swatch. */
    void setCategoriesPresent(const QVector<int> &values);
    /** @return Borrowed stored values, replaced by setCategoriesPresent(); caller supplies
     * the advertised ascending/unique order because this class does not enforce it. */
    [[nodiscard]] const QVector<int> &categoriesPresent() const { return m_categoriesPresent; }

private:
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    MeshFillStyle *m_style;
    bool m_attributeHasData = true;
    QString m_depthUnitLabel;
    QVector<int> m_categoriesPresent;
};

} // namespace OpenSWMM::Render

#endif
