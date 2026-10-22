/**
 * @file      meshbcsublayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Boundary-condition indicator sublayer for the 2D mesh.
 *
 *         Static — isDynamic() == false. The BC ring depends on the mesh
 *         topology and the authored [2D_BOUNDARY_CONDITIONS] rows, not on
 *         the animation period.
 *
 *         Carved out of MeshEdgeStyle (which used to carry colorByBC + the
 *         per-type colours/widths) so BC indicators are a first-class
 *         sublayer: their own layer-tree row with visibility/opacity, their
 *         own styling tab, and — new — per-BC-type visibility. The
 *         sublayer's own isVisible() replaces the old colorByBC master
 *         toggle; legacy styles migrate in
 *         SWMM2DMeshLayer::onSublayersJsonLoaded.
 *
 *         Seven explicit per-type properties rather than a
 *         ClassificationScheme: the scheme's modes are numeric, and forcing
 *         a 7-value enum through manual breaks would put fake numeric edges
 *         in legend labels. The enum is frozen at seven
 *         (mesh::MeshBCTypes::Type), so there is no open-ended growth.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHBCSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHBCSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QString>

namespace OpenSWMM::Render
{

class MeshBcStyle : public SublayerStyle
{
    Q_OBJECT
    // Wall governs the mesh INTERIOR: when the sublayer is visible and
    // wallVisible is on, interior wireframe edges are recoloured with
    // wallColor (the legacy colorByBC behaviour). Wall has no width — Wall
    // edges ARE the wireframe and keep MeshEdgeStyle's line widths.
    Q_PROPERTY(bool wallVisible READ wallVisible WRITE setWallVisible NOTIFY styleChanged)
    Q_PROPERTY(QColor wallColor READ wallColor WRITE setWallColor NOTIFY styleChanged)
    Q_PROPERTY(bool normalFlowVisible READ normalFlowVisible WRITE setNormalFlowVisible NOTIFY
                   styleChanged)
    Q_PROPERTY(
        QColor normalFlowColor READ normalFlowColor WRITE setNormalFlowColor NOTIFY styleChanged)
    Q_PROPERTY(double normalFlowWidthPx READ normalFlowWidthPx WRITE setNormalFlowWidthPx NOTIFY
                   styleChanged)
    Q_PROPERTY(bool stageConstVisible READ stageConstVisible WRITE setStageConstVisible NOTIFY
                   styleChanged)
    Q_PROPERTY(
        QColor stageConstColor READ stageConstColor WRITE setStageConstColor NOTIFY styleChanged)
    Q_PROPERTY(double stageConstWidthPx READ stageConstWidthPx WRITE setStageConstWidthPx NOTIFY
                   styleChanged)
    Q_PROPERTY(bool stageTSVisible READ stageTSVisible WRITE setStageTSVisible NOTIFY styleChanged)
    Q_PROPERTY(QColor stageTSColor READ stageTSColor WRITE setStageTSColor NOTIFY styleChanged)
    Q_PROPERTY(
        double stageTSWidthPx READ stageTSWidthPx WRITE setStageTSWidthPx NOTIFY styleChanged)
    Q_PROPERTY(
        bool flowConstVisible READ flowConstVisible WRITE setFlowConstVisible NOTIFY styleChanged)
    Q_PROPERTY(
        QColor flowConstColor READ flowConstColor WRITE setFlowConstColor NOTIFY styleChanged)
    Q_PROPERTY(
        double flowConstWidthPx READ flowConstWidthPx WRITE setFlowConstWidthPx NOTIFY styleChanged)
    Q_PROPERTY(bool flowTSVisible READ flowTSVisible WRITE setFlowTSVisible NOTIFY styleChanged)
    Q_PROPERTY(QColor flowTSColor READ flowTSColor WRITE setFlowTSColor NOTIFY styleChanged)
    Q_PROPERTY(double flowTSWidthPx READ flowTSWidthPx WRITE setFlowTSWidthPx NOTIFY styleChanged)
    Q_PROPERTY(bool ratingCurveVisible READ ratingCurveVisible WRITE setRatingCurveVisible NOTIFY
                   styleChanged)
    Q_PROPERTY(
        QColor ratingCurveColor READ ratingCurveColor WRITE setRatingCurveColor NOTIFY styleChanged)
    Q_PROPERTY(double ratingCurveWidthPx READ ratingCurveWidthPx WRITE setRatingCurveWidthPx NOTIFY
                   styleChanged)

    Q_CLASSINFO("group:wallVisible", "Wall / interior")
    Q_CLASSINFO("group:wallColor", "Wall / interior")
    Q_CLASSINFO("group:normalFlowVisible", "Normal depth")
    Q_CLASSINFO("group:normalFlowColor", "Normal depth")
    Q_CLASSINFO("group:normalFlowWidthPx", "Normal depth")
    Q_CLASSINFO("group:stageConstVisible", "Stage (constant)")
    Q_CLASSINFO("group:stageConstColor", "Stage (constant)")
    Q_CLASSINFO("group:stageConstWidthPx", "Stage (constant)")
    Q_CLASSINFO("group:stageTSVisible", "Stage (series)")
    Q_CLASSINFO("group:stageTSColor", "Stage (series)")
    Q_CLASSINFO("group:stageTSWidthPx", "Stage (series)")
    Q_CLASSINFO("group:flowConstVisible", "Flow (constant)")
    Q_CLASSINFO("group:flowConstColor", "Flow (constant)")
    Q_CLASSINFO("group:flowConstWidthPx", "Flow (constant)")
    Q_CLASSINFO("group:flowTSVisible", "Flow (series)")
    Q_CLASSINFO("group:flowTSColor", "Flow (series)")
    Q_CLASSINFO("group:flowTSWidthPx", "Flow (series)")
    Q_CLASSINFO("group:ratingCurveVisible", "Rating curve")
    Q_CLASSINFO("group:ratingCurveColor", "Rating curve")
    Q_CLASSINFO("group:ratingCurveWidthPx", "Rating curve")

public:
    /*! Number of BC types (mesh::MeshBCTypes::Type). Kept as a plain
     *  constant so this render header does not pull in mesh/meshbctype.h;
     *  a static_assert in the .cpp locks the two together. */
    static constexpr int kBcTypeCount = 7;

    /** @brief Construct seven typed BC appearances, all individually visible and uncustomized.
     * @param parent Optional QObject owner; destroys this style with itself. Slot order
     * tracks MeshBCTypes (Wall, NormalFlow, StageConst, StageTS, FlowConst, FlowTS, RatingCurve).
     * Ordinary access occurs on the owning Qt thread. */
    explicit MeshBcStyle(QObject *parent = nullptr);

    /** @brief Read wall/interior visibility.
     * @return Slot 0 value, initially true; the master sublayer gate is separate. */
    [[nodiscard]] bool wallVisible() const { return m_typeVisible[0]; }
    /** @brief Read wall/interior color.
     * @return Slot 0 value, initially RGBA(0,0,0,130); the master sublayer gate is separate. */
    [[nodiscard]] QColor wallColor() const { return m_colors[0]; }
    /** @brief Read normal-flow boundary visibility.
     * @return Slot 1 value, initially true; the master sublayer gate is separate. */
    [[nodiscard]] bool normalFlowVisible() const { return m_typeVisible[1]; }
    /** @brief Read normal-flow boundary color.
     * @return Slot 1 value, initially RGBA(44,160,44,230); the master sublayer gate is separate. */
    [[nodiscard]] QColor normalFlowColor() const { return m_colors[1]; }
    /** @brief Read normal-flow boundary width in logical screen pixels.
     * @return Slot 1 value, initially 2; the master sublayer gate is separate. */
    [[nodiscard]] double normalFlowWidthPx() const { return m_widths[1]; }
    /** @brief Read constant-stage boundary visibility.
     * @return Slot 2 value, initially true; the master sublayer gate is separate. */
    [[nodiscard]] bool stageConstVisible() const { return m_typeVisible[2]; }
    /** @brief Read constant-stage boundary color.
     * @return Slot 2 value, initially RGBA(31,119,180,235); the master sublayer gate is separate. */
    [[nodiscard]] QColor stageConstColor() const { return m_colors[2]; }
    /** @brief Read constant-stage boundary width in logical screen pixels.
     * @return Slot 2 value, initially 2.4; the master sublayer gate is separate. */
    [[nodiscard]] double stageConstWidthPx() const { return m_widths[2]; }
    /** @brief Read time-series stage boundary visibility.
     * @return Slot 3 value, initially true; the master sublayer gate is separate. */
    [[nodiscard]] bool stageTSVisible() const { return m_typeVisible[3]; }
    /** @brief Read time-series stage boundary color.
     * @return Slot 3 value, initially RGBA(23,190,207,235); the master sublayer gate is separate. */
    [[nodiscard]] QColor stageTSColor() const { return m_colors[3]; }
    /** @brief Read time-series stage boundary width in logical screen pixels.
     * @return Slot 3 value, initially 2.4; the master sublayer gate is separate. */
    [[nodiscard]] double stageTSWidthPx() const { return m_widths[3]; }
    /** @brief Read constant-flow boundary visibility.
     * @return Slot 4 value, initially true; the master sublayer gate is separate. */
    [[nodiscard]] bool flowConstVisible() const { return m_typeVisible[4]; }
    /** @brief Read constant-flow boundary color.
     * @return Slot 4 value, initially RGBA(255,127,14,235); the master sublayer gate is separate. */
    [[nodiscard]] QColor flowConstColor() const { return m_colors[4]; }
    /** @brief Read constant-flow boundary width in logical screen pixels.
     * @return Slot 4 value, initially 2.4; the master sublayer gate is separate. */
    [[nodiscard]] double flowConstWidthPx() const { return m_widths[4]; }
    /** @brief Read time-series flow boundary visibility.
     * @return Slot 5 value, initially true; the master sublayer gate is separate. */
    [[nodiscard]] bool flowTSVisible() const { return m_typeVisible[5]; }
    /** @brief Read time-series flow boundary color.
     * @return Slot 5 value, initially RGBA(227,119,194,235); the master sublayer gate is separate. */
    [[nodiscard]] QColor flowTSColor() const { return m_colors[5]; }
    /** @brief Read time-series flow boundary width in logical screen pixels.
     * @return Slot 5 value, initially 2.4; the master sublayer gate is separate. */
    [[nodiscard]] double flowTSWidthPx() const { return m_widths[5]; }
    /** @brief Read rating-curve boundary visibility.
     * @return Slot 6 value, initially true; the master sublayer gate is separate. */
    [[nodiscard]] bool ratingCurveVisible() const { return m_typeVisible[6]; }
    /** @brief Read rating-curve boundary color.
     * @return Slot 6 value, initially RGBA(148,103,189,235); the master sublayer gate is separate. */
    [[nodiscard]] QColor ratingCurveColor() const { return m_colors[6]; }
    /** @brief Read rating-curve boundary width in logical screen pixels.
     * @return Slot 6 value, initially 2.4; the master sublayer gate is separate. */
    [[nodiscard]] double ratingCurveWidthPx() const { return m_widths[6]; }

    /** @brief Set wall/interior visibility through the indexed setter.
     * @param v New slot 0 value. Boolean inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setWallVisible(bool v) { setBcTypeVisible(0, v); }
    /** @brief Set wall/interior color through the indexed setter.
     * @param v New slot 0 value. Invalid QColor values are retained; QColor inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setWallColor(const QColor &v) { setBcColor(0, v); }
    /** @brief Set normal-flow boundary visibility through the indexed setter.
     * @param v New slot 1 value. Boolean inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setNormalFlowVisible(bool v) { setBcTypeVisible(1, v); }
    /** @brief Set normal-flow boundary color through the indexed setter.
     * @param v New slot 1 value. Invalid QColor values are retained; QColor inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setNormalFlowColor(const QColor &v) { setBcColor(1, v); }
    /** @brief Set normal-flow boundary width in logical screen pixels through the indexed setter.
     * @param v New slot 1 value. Finite values clamp to [0.1,20] via qBound; qFuzzyCompare(current+1, input+1)
     * suppresses unchanged values, otherwise styleChanged is emitted.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setNormalFlowWidthPx(double v) { setBcWidth(1, v); }
    /** @brief Set constant-stage boundary visibility through the indexed setter.
     * @param v New slot 2 value. Boolean inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setStageConstVisible(bool v) { setBcTypeVisible(2, v); }
    /** @brief Set constant-stage boundary color through the indexed setter.
     * @param v New slot 2 value. Invalid QColor values are retained; QColor inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setStageConstColor(const QColor &v) { setBcColor(2, v); }
    /** @brief Set constant-stage boundary width in logical screen pixels through the indexed setter.
     * @param v New slot 2 value. Finite values clamp to [0.1,20] via qBound; qFuzzyCompare(current+1, input+1)
     * suppresses unchanged values, otherwise styleChanged is emitted.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setStageConstWidthPx(double v) { setBcWidth(2, v); }
    /** @brief Set time-series stage boundary visibility through the indexed setter.
     * @param v New slot 3 value. Boolean inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setStageTSVisible(bool v) { setBcTypeVisible(3, v); }
    /** @brief Set time-series stage boundary color through the indexed setter.
     * @param v New slot 3 value. Invalid QColor values are retained; QColor inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setStageTSColor(const QColor &v) { setBcColor(3, v); }
    /** @brief Set time-series stage boundary width in logical screen pixels through the indexed setter.
     * @param v New slot 3 value. Finite values clamp to [0.1,20] via qBound; qFuzzyCompare(current+1, input+1)
     * suppresses unchanged values, otherwise styleChanged is emitted.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setStageTSWidthPx(double v) { setBcWidth(3, v); }
    /** @brief Set constant-flow boundary visibility through the indexed setter.
     * @param v New slot 4 value. Boolean inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setFlowConstVisible(bool v) { setBcTypeVisible(4, v); }
    /** @brief Set constant-flow boundary color through the indexed setter.
     * @param v New slot 4 value. Invalid QColor values are retained; QColor inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setFlowConstColor(const QColor &v) { setBcColor(4, v); }
    /** @brief Set constant-flow boundary width in logical screen pixels through the indexed setter.
     * @param v New slot 4 value. Finite values clamp to [0.1,20] via qBound; qFuzzyCompare(current+1, input+1)
     * suppresses unchanged values, otherwise styleChanged is emitted.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setFlowConstWidthPx(double v) { setBcWidth(4, v); }
    /** @brief Set time-series flow boundary visibility through the indexed setter.
     * @param v New slot 5 value. Boolean inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setFlowTSVisible(bool v) { setBcTypeVisible(5, v); }
    /** @brief Set time-series flow boundary color through the indexed setter.
     * @param v New slot 5 value. Invalid QColor values are retained; QColor inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setFlowTSColor(const QColor &v) { setBcColor(5, v); }
    /** @brief Set time-series flow boundary width in logical screen pixels through the indexed setter.
     * @param v New slot 5 value. Finite values clamp to [0.1,20] via qBound; qFuzzyCompare(current+1, input+1)
     * suppresses unchanged values, otherwise styleChanged is emitted.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setFlowTSWidthPx(double v) { setBcWidth(5, v); }
    /** @brief Set rating-curve boundary visibility through the indexed setter.
     * @param v New slot 6 value. Boolean inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setRatingCurveVisible(bool v) { setBcTypeVisible(6, v); }
    /** @brief Set rating-curve boundary color through the indexed setter.
     * @param v New slot 6 value. Invalid QColor values are retained; QColor inequality emits styleChanged.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setRatingCurveColor(const QColor &v) { setBcColor(6, v); }
    /** @brief Set rating-curve boundary width in logical screen pixels through the indexed setter.
     * @param v New slot 6 value. Finite values clamp to [0.1,20] via qBound; qFuzzyCompare(current+1, input+1)
     * suppresses unchanged values, otherwise styleChanged is emitted.
     * Does not automatically mark the style customized; the caller owns that policy. */
    void setRatingCurveWidthPx(double v) { setBcWidth(6, v); }

    /*! Colour for BC type index \p t (0..kBcTypeCount-1). Out-of-range
     *  indices fold onto Wall so a corrupt/forward-version BC value cannot
     *  read past the array. */
    /** @param t Raw BC index; values outside [0,7) read Wall slot zero.
     * @return Stored QColor including authored alpha; visibility is not applied. */
    [[nodiscard]] QColor bcColorForType(int t) const
    {
        return m_colors[(t >= 0 && t < kBcTypeCount) ? t : 0];
    }

    /*! Width in px for BC type index \p t, same folding rule. Index 0
     *  (Wall) is never consulted by the renderer — Wall edges are drawn by
     *  the wireframe pass at MeshEdgeStyle's widths. */
    /** @param t Raw BC index; values outside [0,7) read Wall slot zero.
     * @return Stored width in logical pixels. Wall starts at 0.35, but the actual
     * wireframe/Wall legend can use the host-provided width instead. */
    [[nodiscard]] double bcWidthForType(int t) const
    {
        return m_widths[(t >= 0 && t < kBcTypeCount) ? t : 0];
    }

    /*! Per-type visibility, same folding rule. All types default visible —
     *  the sublayer's own isVisible() is the master gate. */
    /** @param t Raw BC index; values outside [0,7) read Wall slot zero.
     * @return Stored per-type flag, independent of sublayer/master visibility. */
    [[nodiscard]] bool bcTypeVisible(int t) const
    {
        return m_typeVisible[(t >= 0 && t < kBcTypeCount) ? t : 0];
    }

    /*! Indexed setters. Public so callers that seed a whole style (prefs
     *  seeding, legacy migration) loop over the type enum rather than
     *  naming twenty typed setters. Out-of-range indices are ignored. */
    /** @brief Copy a BC color and emit styleChanged on a QColor change.
     * @param idx Slot in [0,7); invalid indices are ignored.
     * @param v Color copied verbatim, including invalid QColor. customized is unchanged. */
    void setBcColor(int idx, const QColor &v);
    /** @brief Set a bounded BC width and emit styleChanged on a fuzzy change.
     * @param idx Slot in [0,7), including normally unused Wall width; invalid indices are ignored.
     * @param v Finite logical-pixel width clamped to [0.1,20]. NaN is not explicitly
     * validated; supply finite input. Uses qFuzzyCompare(current+1, input+1). customized is unchanged. */
    void setBcWidth(int idx, double v);
    /** @brief Set a per-type flag and emit styleChanged on a Boolean change.
     * @param idx Slot in [0,7); invalid indices are ignored.
     * @param v Local flag; the sublayer master gate and customized flag are unchanged. */
    void setBcTypeVisible(int idx, bool v);

    /*! True once the user has edited this style directly (style panel) or
     *  a project carrying an explicit edit was loaded. While false, live
     *  changes to the Preferences BC defaults re-seed this style; once
     *  true, per-layer edits win and the defaults only apply to meshes
     *  opened later. Persisted in toJson/fromJson. */
    /** @return Stored customization policy flag, initially false; ordinary indexed/
     * typed edits do not set it automatically. */
    [[nodiscard]] bool isCustomized() const noexcept { return m_customized; }
    /** @brief Replace customization policy without emitting a style notification.
     * @param on New raw flag; the caller arranges any preference/repaint consequences. */
    void setCustomized(bool on) { m_customized = on; }

    /** @brief Serialize seven colors/visibility flags, six boundary widths and customization.
     * @return Independent JSON with ARGB colors. The unused Wall width is not persisted. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read partial BC appearance and emit styleChanged once unconditionally.
     * @param j Borrowed JSON; absent/mistyped scalar fields retain current values,
     * invalid color text is ignored, boundary widths clamp to [0.1,20]. Wall width is
     * not read. customized uses its previous value when absent/mistyped. Typed setters
     * are bypassed; unknown keys are ignored. */
    void fromJson(const QJsonObject &j) override;

    /*! Migration: seed colours/widths from a legacy MeshEdgeStyle JSON
     *  object (the pre-split "bc*"-prefixed keys, including the even older
     *  single "bcWidthPx"). The caller decides sublayer visibility from the
     *  legacy "colorByBC" flag — this only copies the styling. */
    /** @param j Borrowed old edge-style JSON. bcWidthPx first seeds all non-Wall widths
     * with [0.1,20] clamping (1.60 conversion fallback), then per-type bc* widths override
     * it and valid bc* colors replace slots. Recognized width keys or valid colors mark
     * customized true; no keys never clear an existing true flag. All per-type visibility
     * is reset to true, even for an empty object. Emits styleChanged unconditionally;
     * does not read the old colorByBC master switch or change sublayer visibility. */
    void seedFromLegacyEdgeJson(const QJsonObject &j);

private:
    QColor m_colors[kBcTypeCount];
    double m_widths[kBcTypeCount];
    bool m_typeVisible[kBcTypeCount];
    bool m_customized = false;
};

class MeshBcSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a hidden, opacity-one static BC pass with an owned style.
     * @param id_ Stable identifier moved into storage without checking uniqueness.
     * @param parent Optional QObject owner; destroys the pass and child style.
     * Style changes forward to invalidated(). Initially only Wall is in the present-type
     * set and its legend width is 0.35 px. Access occurs on the owning Qt thread. */
    explicit MeshBcSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify the scene pass kind.
     * @return LineKind. */
    Kind kind() const override { return LineKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time id without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Boundary Conditions label. */
    QString displayName() const override { return tr("Boundary Conditions"); }

    /** @brief Read the local master visibility gate.
     * @return Stored flag, initially false; per-type flags are separate. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace master visibility and emit invalidated() on a Boolean change.
     * @param v New flag; does not change individual type visibility. */
    void setVisible(bool v) override;
    /** @brief Read local opacity.
     * @return Stored multiplier, initially one. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Clamp opacity and emit invalidated() on a fuzzy change.
     * @param o Finite multiplier clamped to [0,1] via qBound. No explicit NaN check
     * occurs; qFuzzyCompare(current+1, input+1) suppresses unchanged values. */
    void setOpacity(qreal o) override;

    /** @brief Report that BC indicators are independent of animation period.
     * @return False; authored topology/style changes can still invalidate the pass. */
    bool isDynamic() const override { return false; }

    /** @brief Borrow the owned BC style through its base interface.
     * @return Child pointer valid until sublayer destruction; do not delete/reparent. */
    SublayerStyle *style() override { return m_style; }

    /** @brief Build rows for known types both present in the host and individually visible.
     * @return Rows in enum order with translated type labels, raw numeric class keys,
     * colors, widths and local opacity. Wall uses the host legend width, not slot-zero
     * style width. Master visibility is not checked here; no geometry is inspected. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Leave node construction to the host mesh renderer.
     * @param existing Borrowed existing node returned unchanged, including null.
     * @param ctx Unused rendering context.
     * @return existing without allocation or ownership transfer. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned BC style.
     * @return Child pointer valid until sublayer destruction; do not delete/reparent. */
    [[nodiscard]] MeshBcStyle *bcStyle() const { return m_style; }

    /*! BC type indices actually present in the host mesh. Pushed by
     *  SWMM2DMeshLayer whenever the BC vector changes, so
     *  legendSymbolItems() emits one row per *present* type instead of
     *  seven rows of which five are usually dead. Wall is always present. */
    /** @param types Host-provided set copied into storage with Wall (zero) always
     * inserted. Unknown indices are retained but skipped by the seven-type legend loop.
     * A set change emits invalidated(); no mesh data is queried. */
    void setBcTypesPresent(const QSet<int> &types);
    /** @return Borrowed stored set, initially {0}, replaced by setBcTypesPresent(). */
    [[nodiscard]] const QSet<int> &bcTypesPresent() const { return m_bcTypesPresent; }

    /*! Base wireframe width shown in the Wall legend row (Wall edges are
     *  drawn by the wireframe pass). Pushed by the layer alongside the
     *  present set. */
    /** @param px Finite logical-pixel wireframe width clamped to [0.1,20]; no explicit
     * NaN check. A qFuzzyCompare(current+1, input+1) change emits invalidated().
     * Only the Wall preview changes; no MeshEdgeStyle or slot-zero BC width is modified. */
    void setWallLegendWidthPx(double px);

private:
    QString m_id;
    // Hidden by default — mirrors the legacy colorByBC=false default, so a
    // fresh mesh renders byte-identically until the user opts in.
    bool m_visible = false;
    qreal m_opacity = 1.0;
    MeshBcStyle *m_style;
    QSet<int> m_bcTypesPresent{0}; ///< Wall is always present
    double m_wallLegendWidthPx = 0.35;
};

} // namespace OpenSWMM::Render

#endif
