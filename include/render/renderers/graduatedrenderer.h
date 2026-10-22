/**
 * @file      graduatedrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Renderer that classifies a numeric attribute into bins, each with its own colour.
 *
 *         GraduatedRenderer is what drives the per-attribute heatmap on
 *         SWMMResultsLayer (depth / head / flow) and SWMM2DResultsLayer
 *         (depth). It carries:
 *           - classifyAttribute  — which attribute drives binning
 *           - minValue / maxValue — the value range covered by the bins
 *           - binColors          — N QColors, one per bin
 *           - baseSymbol         — the SymbolStyle template; the renderer
 *                                  overrides "color" in every SymbolLayer
 *                                  with the per-bin colour at paint time.
 *
 *         The interval binner computes the configured classification method,
 *         while a color ramp and sparse overrides provide bin colors. Owning
 *         layers coordinate sampling, animation policy and repaint notifications.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2 + §J.4). Sub-phase 8.13.6.2 — concrete renderer class.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_GRADUATEDRENDERER_H
#define OPENSWMM_RENDER_GRADUATEDRENDERER_H

#include "render/attributesource.h"
#include "render/colorramp.h"
#include "render/ifeaturerenderer.h"
#include "render/intervalbinner.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QVector>

namespace OpenSWMM::Render
{

/*!
 * \class GraduatedRenderer
 * \brief Classifies a numeric attribute into bins and assigns a colour per bin.
 *
 *        Slice BB-α (2026-05-24): rewired from the placeholder
 *        `binColors[]` field to `RasterColorRamp m_ramp` + `IntervalBinner
 *        m_binner` + `QVector<double> m_lastBreaks`. The renderer samples
 *        the ramp at each bin's midpoint to obtain its colour; the binner
 *        does the value-to-bin lookup. `setBinColors` / `binColors` stay
 *        as a legacy compatibility surface — they read/write the same
 *        per-bin colour vector but no longer drive `symbolFor` directly
 *        (the ramp is the source of truth).
 *
 *        Legacy `.oswp` projects authored before BB-α used a `binColors`
 *        JSON array with no ramp + binner. `fromJson` honours that shape
 *        via a migration shim that synthesises a discrete ramp from the
 *        saved colours so existing projects keep their visuals.
 */
class GraduatedRenderer final : public IFeatureRenderer
{
public:
    /** @brief Construct five equal-interval bins with a viridis [0,1] ramp.
     * The template and attribute keys are empty. Color output is on; size/width output is off. */
    GraduatedRenderer() = default;
    /** @brief Destroy the stored style, binner, cached breaks and overrides. */
    ~GraduatedRenderer() override = default;

    /** @brief Read the exact attribute key driving color bins.
     *
     * Name of the attribute key (in QVariantMap attrs) to classify on.
     * Typical SWMM values: "depth", "head", "flow", "runoff".
     *
     * @return Owned key text; no lookup or classification is performed.
     */
    [[nodiscard]] QString classifyAttribute() const { return m_classifyAttribute; }
    /** @brief Replace the color-attribute key without clearing cached breaks.
     * @param name Exact key moved verbatim, including empty text. Call clearBreaks() when a new
     * data sample is needed. */
    void setClassifyAttribute(QString name) { m_classifyAttribute = std::move(name); }

    /** @brief Read the optional independent size/width attribute key.
     *
     * Independent attribute driving the size / width output axes.
     * Empty (the default) = follow classifyAttribute, preserving the
     * historical bin-mapped behaviour. When set to a different
     * attribute, sizes/widths are interpolated over that attribute's
     * own value range (sizeValueMin/Max — sampled by the owning
     * layer's rebuild) instead of the colour bin index.
     *
     * @return Owned text; empty follows classifyAttribute().
     */
    [[nodiscard]] QString sizeAttribute() const { return m_sizeAttribute; }
    /** @brief Replace the independent size/width key and discard its sampled range on change.
     * @param name Exact key moved verbatim. Equal text is a no-op; a changed key resets both size
     * value bounds to zero. */
    void setSizeAttribute(QString name)
    {
        if (name == m_sizeAttribute)
            return;
        m_sizeAttribute = std::move(name);
        // New attribute → the sampled value range is stale; invalidate so
        // the owning layer's next rebuild re-derives it from data.
        m_sizeValueMin = m_sizeValueMax = 0.0;
    }

    /** @brief Resolve the attribute used by size/width outputs.
     * @return Independent key when nonempty, otherwise the classification key.
     *
     * The attribute actually feeding the size/width axes.
     */
    [[nodiscard]] QString effectiveSizeAttribute() const
    {
        return m_sizeAttribute.isEmpty() ? m_classifyAttribute : m_sizeAttribute;
    }

    /** @brief Test whether size/width use a different attribute key.
     * @return True only for a nonempty size key unequal to the exact classification key.
     *
     * True when the size axes are driven by a different attribute than
     *  the colour classification.
     */
    [[nodiscard]] bool sizeAxisIndependent() const
    {
        return !m_sizeAttribute.isEmpty() && m_sizeAttribute != m_classifyAttribute;
    }

    /** @brief Read the sampled independent attribute lower bound.
     * @return Stored scalar in the independent attribute units, initially zero.
     *
     * Value range used to normalise the independent size attribute.
     *  Invalid (max <= min) until the owning layer samples it.
     */
    [[nodiscard]] double sizeValueMin() const { return m_sizeValueMin; }
    /** @brief Read the sampled independent attribute upper bound.
     * @return Stored scalar in the same units as sizeValueMin(), initially zero. */
    [[nodiscard]] double sizeValueMax() const { return m_sizeValueMax; }
    /** @brief Test ordering of the sampled independent range.
     * @return max > min using ordinary comparison; this predicate does not reject infinite bounds. */
    [[nodiscard]] bool sizeValueRangeValid() const { return m_sizeValueMax > m_sizeValueMin; }
    /** @brief Replace the sampled independent attribute bounds without validation.
     * @param mn Lower bound in independent attribute units.
     * @param mx Upper bound in the same units. No ordering/finiteness check, repaint notification,
     * or bin recomputation occurs. */
    void setSizeValueRange(double mn, double mx)
    {
        m_sizeValueMin = mn;
        m_sizeValueMax = mx;
    }

    /** @brief Read the static/dynamic attribute-source metadata.
     *
     * P2 — whether classifyAttribute() is a STATIC model/GIS field
     * (classified once) or a DYNAMIC SWMM output variable (sampled per
     * timestep). Default Static — pre-P2 renderers behave unchanged.
     *
     * @return Stored source kind; this renderer does not sample animation frames itself.
     */
    [[nodiscard]] AttributeSourceKind sourceKind() const { return m_sourceKind; }
    /** @brief Store source metadata for the owning layer.
     * @param k Kind stored without enum validation, reclassification, or notifications. */
    void setSourceKind(AttributeSourceKind k) { m_sourceKind = k; }

    /** @brief Read the owning layer's animation range policy.
     *
     * P2 — how the value range behaves over the time axis. The owning
     * results layer consumes this on each tick (re-bin / re-stretch).
     * Default FixedOverRun.
     *
     * @return Stored policy; the renderer does not aggregate frames on access.
     */
    [[nodiscard]] RangeMode rangeMode() const { return m_rangeMode; }
    /** @brief Store an animation range policy without reclassification.
     * @param m Policy copied without enum validation or notifications. */
    void setRangeMode(RangeMode m) { m_rangeMode = m; }

    /** @brief Read the ramp's lower classification bound.
     *
     * Convenience accessor for the ramp's value range. Equivalent
     * to ramp().minValue / .maxValue. Calls to setRange propagate
     * into the ramp.
     *
     * @return Stored scalar in classification-attribute units.
     */
    [[nodiscard]] double minValue() const { return m_ramp.minValue; }
    /** @brief Read the ramp's upper classification bound.
     * @return Stored scalar in the same units as minValue(). */
    [[nodiscard]] double maxValue() const { return m_ramp.maxValue; }
    /** @brief Replace the ramp bounds while retaining cached breaks and overrides.
     * @param minValue Finite lower bound in classification units.
     * @param maxValue Finite upper bound in the same units, expected at least minValue. Bounds are
     * stored without validation or normalization. */
    void setRange(double minValue, double maxValue);

    /** @brief Inspect the ramp used for bin midpoint colors.
     *
     * The colour ramp used to derive per-bin colours. Sampled at
     * each bin's midpoint (i + 0.5) / nBins.
     *
     * @return Borrowed ramp valid for this renderer's lifetime; setters/loading can replace it.
     */
    [[nodiscard]] const RasterColorRamp &ramp() const { return m_ramp; }
    /** @brief Replace the ramp, including its bounds, while retaining breaks and overrides.
     * @param ramp Payload moved without stop/range validation or repaint notification. */
    void setRamp(RasterColorRamp ramp);

    /** @brief Inspect the configured interval algorithm and bin count.
     *
     * The interval binner that maps attribute values → bin indices.
     *
     * @return Borrowed binner valid for this renderer's lifetime; later configuration edits can
     * change it.
     */
    [[nodiscard]] const IntervalBinner &binner() const { return m_binner; }
    /** @brief Replace the interval configuration and clear cached breaks.
     * @param b Binner moved into storage without an equality check. Even equivalent input clears
     * lastBreaks(); the next owning-layer pass must resample. */
    void setBinner(IntervalBinner b);

    /** @brief Inspect cached interior boundaries in classification units.
     *
     * The last set of interior break values computed by the binner.
     * Kept on the renderer so paint-time `binFor` does not need
     * to recompute breaks every frame.
     *
     * @return Borrowed vector, possibly empty or stale until the owning layer classifies. Loading
     * and classification can replace it.
     */
    [[nodiscard]] const QVector<double> &lastBreaks() const { return m_lastBreaks; }

    /** @brief Discard cached boundaries while retaining ramp bounds and configuration.
     *
     * Drops the cached breaks so the next classify pass (driven by the
     * owning layer's rebuild) re-derives them from data. The editor
     * calls this when the attribute / bin count / method changes so a
     * re-classify actually re-samples instead of reusing stale breaks.
     *
     */
    void clearBreaks() { m_lastBreaks.clear(); }

    /** @brief Classify a non-null renderer only when no cached breaks exist.
     *
     * Shared classify gate used by every owning layer's rebuild so the
     * model + results paths cannot drift. Classifies only when the
     * breaks are empty (a fresh renderer, or after clearBreaks()) and
     * a nonempty sample was supplied; otherwise leaves the
     * renderer untouched.
     *
     * @param[in,out] g Optional renderer; nullptr is a no-op.
     * @param samples Candidate values in classification units; empty input is a no-op. Nonempty
     * all-nonfinite input reaches autoClassify(), which leaves state unchanged.
     */
    static void classifyIfNeeded(GraduatedRenderer *g, const QVector<double> &samples)
    {
        if (g && g->lastBreaks().isEmpty() && !samples.isEmpty())
            g->autoClassify(samples);
    }

    /** @brief Sample every active bin color, including sparse overrides.
     *
     * Per-bin colours (derived view). Computed from the ramp +
     * binCount at request time; kept on the legacy surface so
     * callers that haven't migrated to ramp() still work.
     *
     * @return Owned color list in bin order; its size equals binCount().
     */
    [[nodiscard]] QList<QColor> binColors() const;
    /** @brief Migrate a legacy color list into midpoint ramp stops and its bin count.
     * @param colors Ordered colors copied into a new RGB ramp; empty input is a no-op. Existing
     * range bounds, method, cached breaks and sparse overrides remain, so overrides can still win
     * and callers may need to reclassify. */
    void setBinColors(QList<QColor> colors);
    /** @brief Read the configured number of bins.
     * @return Embedded binner count; manual breaks do not automatically synchronize this count. */
    [[nodiscard]] int binCount() const { return m_binner.binCount(); }

    /** @brief Inspect the template copied for each feature.
     *
     * The SymbolStyle template. At paint time the renderer overrides
     * the "color" property of every SymbolLayer in this template with
     * the appropriate per-bin colour.
     *
     * @return Borrowed style valid for this renderer's lifetime; mutations/loading can replace it.
     */
    [[nodiscard]] const SymbolStyle &baseSymbol() const { return m_baseSymbol; }
    /** @brief Replace the complete style template without validation or notifications.
     * @param s Style moved into storage; classification, output channels and overrides are
     * retained. */
    void setBaseSymbol(SymbolStyle s);

    /** @brief Recompute data bounds and interval breaks from a sample.
     *
     * Recomputes interval breaks + ramp range from a sample of
     * attribute values. NaNs and infinities are skipped. If the
     * sample is empty or all-NaN, the existing state is unchanged.
     *
     * @param samples Classification-unit values, inspected without mutation. Nonfinite values do
     * not influence bounds; no finite values leaves all state unchanged. Constant finite data
     * expands bounds by abs(value)*1e-9, or 1e-9 at zero, before storing binner-computed breaks.
     * Overrides and independent size bounds remain.
     */
    void autoClassify(const QVector<double> &samples);

    /** @brief Map a scalar to a bin color using cached breaks or an equal-interval fallback.
     *
     * Returns the colour for one raw attribute value.
     * Hides nonfinite and below-range values; high values saturate.
     * Cached boundaries determine bins after classification.
     *
     * @param v Value in classification units, with finite ordered ramp bounds.
     * @return Transparent for nonfinite/below-minimum values or no bins; final bin color at/above
     * maximum. Otherwise cached break lookup selects the bin, or an equal-interval calculation is
     * used when no breaks exist. Sparse overrides apply.
     */
    [[nodiscard]] QColor colorForValue(double v) const;

    /** @brief Read a bin override or sample the ramp at its normalized midpoint.
     *
     * Returns the colour for one already-computed bin index.
     * Sampled from the ramp at midpoint (i + 0.5) / nBins.
     *
     * @param bin Index clamped into the configured bin range.
     * @return Stored override when present, otherwise midpoint color; transparent when no bins
     * exist.
     */
    [[nodiscard]] QColor colorForBin(int bin) const;

    /** @brief Read whether per-feature marker-size mapping is enabled.
     *
     * Slice BI Phase 8.13.43-α — Graduated tab's "Output axes"
     * toggle for size/width. When `outputSizeEnabled = true`,
     * `symbolFor` writes a bin-index-mapped size into the symbol's
     * `props["size"]` in addition to (or instead
     * of) the colour. Disabled by default — preserves prior
     * colour-only behaviour.
     *
     * @return Stored toggle; sizeForBin()/sizeForValue() themselves ignore this flag.
     */
    [[nodiscard]] bool outputSizeEnabled() const { return m_outputSizeEnabled; }
    /** @brief Enable or disable writes to existing marker size slots.
     * @param on Toggle stored without changing width/color channels or issuing notifications. */
    void setOutputSizeEnabled(bool on) { m_outputSizeEnabled = on; }

    /** @brief Read whether per-feature bin colors replace existing color slots.
     * @return Stored toggle; underflow/nonfinite classification values are hidden even when color
     * output is off. */
    [[nodiscard]] bool outputColorEnabled() const { return m_outputColorEnabled; }
    /** @brief Enable or disable per-feature color replacement.
     * @param on Toggle copied without changing binning or repaint notifications. */
    void setOutputColorEnabled(bool on) { m_outputColorEnabled = on; }

    /** @brief Read the low marker-size output endpoint.
     * @return Stored logical-pixel size, initially two. */
    [[nodiscard]] double outputSizeMin() const { return m_outputSizeMin; }
    /** @brief Read the high marker-size output endpoint.
     * @return Stored logical-pixel size, initially fourteen. */
    [[nodiscard]] double outputSizeMax() const { return m_outputSizeMax; }
    /** @brief Replace marker-size output endpoints without validation.
     * @param minPx Low logical-pixel endpoint.
     * @param maxPx High logical-pixel endpoint; reversed, negative or nonfinite values are not
     * rejected. No channel is enabled automatically. */
    void setOutputSizeRange(double minPx, double maxPx);

    /** @brief Interpolate marker size across bin indices, independent of the enable toggle.
     *
     * Returns the bin-index-mapped size regardless of
     * outputSizeEnabled. Linear interpolation between
     * outputSizeMin and outputSizeMax over [0, nBins-1].
     * Clamps out-of-range bin indices; a single bin uses the output midpoint.
     *
     * @param bin Index clamped into [0,binCount()-1].
     * @return Logical-pixel size, interpolated from the first to last bin endpoints; a single bin
     * uses their midpoint and no bins returns outputSizeMin().
     */
    [[nodiscard]] double sizeForBin(int bin) const;

    /** @brief Interpolate marker size using the independent sampled attribute range.
     *
     * Value-interpolated size for the independent size axis: linear
     * map of \p v (clamped) from [sizeValueMin, sizeValueMax] onto
     * [outputSizeMin, outputSizeMax]. Falls back to outputSizeMin
     * when the value range is invalid or \p v is non-finite.
     *
     * @param v Scalar in sizeValueMin()/sizeValueMax() units; configured endpoints must be finite.
     * @return Logical-pixel size with input position clamped to [0,1]; outputSizeMin() for
     * nonfinite input or a nonincreasing value range. The enable toggle is ignored.
     */
    [[nodiscard]] double sizeForValue(double v) const;

    /** @brief Read whether per-feature line-width mapping is enabled.
     *
     * VS.4 — independent line-WIDTH output axis. Marker "size" and
     * line "width" are distinct visual channels (nodes vs links) with
     * different sensible px ranges, so they get separate toggles and
     * ranges. When enabled, `symbolFor` writes a bin-mapped width into
     * each SymbolLayer's `props["width"]`. Disabled by default.
     *
     * Back-compat: projects authored before VS.4 drove width through
     * the size axis (which wrote both "size" and "width"). `fromJson`
     * migrates those so their line widths are preserved.
     *
     * @return Stored toggle; widthForBin()/widthForValue() themselves ignore it.
     */
    [[nodiscard]] bool outputWidthEnabled() const { return m_outputWidthEnabled; }
    /** @brief Enable or disable writes to existing line-width slots.
     * @param on Toggle copied without changing marker-size mapping or notifications. */
    void setOutputWidthEnabled(bool on) { m_outputWidthEnabled = on; }

    /** @brief Read the low line-width output endpoint.
     * @return Stored logical-pixel width, initially 0.5. */
    [[nodiscard]] double outputWidthMin() const { return m_outputWidthMin; }
    /** @brief Read the high line-width output endpoint.
     * @return Stored logical-pixel width, initially six. */
    [[nodiscard]] double outputWidthMax() const { return m_outputWidthMax; }
    /** @brief Replace line-width output endpoints without validation.
     * @param minPx Low logical-pixel endpoint.
     * @param maxPx High logical-pixel endpoint; no finiteness/order/nonnegative check occurs and
     * the width channel is not enabled automatically. */
    void setOutputWidthRange(double minPx, double maxPx);

    /** @brief Interpolate line width across clamped bin indices.
     *
     * Bin-mapped line width, mirroring sizeForBin over the width
     * range. Clamps bin indices; a single bin uses the output midpoint.
     *
     * @param bin Index clamped into the configured bins.
     * @return Logical-pixel width; one bin uses the output midpoint and no bins returns
     * outputWidthMin(). The enable toggle is ignored.
     */
    [[nodiscard]] double widthForBin(int bin) const;

    /** @brief Interpolate line width over the independent size-attribute range.
     * @param v Scalar in sizeValueMin()/sizeValueMax() units; configured endpoints must be finite.
     * @return Logical-pixel width with clamped position; outputWidthMin() for nonfinite input or a
     * nonincreasing range. The enable toggle is ignored.
     *
     * Value-interpolated width, mirroring sizeForValue over the width
     *  px range.
     */
    [[nodiscard]] double widthForValue(double v) const;

    // IFeatureRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token graduated. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("graduated"); }
    /** @brief Copy the template and apply enabled color, marker-size and line-width channels.
     * @param f Feature identity, unused by this renderer.
     * @param attrs Exact-key attribute values converted with QVariant::toDouble().
     * Missing/unconvertible classification values retain template color; convertible
     * nonfinite/below-minimum values force transparent color slots and opacity zero.
     * @return Owned style. Size/width use classification bins or a convertible independent
     * attribute; an invalid independent range falls back to the classification bin. Only existing
     * property slots are overwritten, and no stored template or expression is modified. */
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    /** @brief Build one reference row for each configured bin.
     * @return Owned rows with numeric ranges/labels, index keys and bin colors. Cached boundaries
     * are used only when their count is binCount()-1; otherwise equal intervals are shown.
     * Size/width mappings and output-color disabling are not applied to the legend template. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize renderer configuration, cached breaks, output channels and overrides.
     * @return Owned snapshot including independent bounds only when an independent key is stored.
     * No bounds/break validation occurs before serialization. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Load configuration and migrate legacy bin colors and shared size/width settings.
     * @param j Renderer object whose id is ignored. Missing ramp/binner retain current values;
     * keys, independent range, cached breaks, base symbol and overrides are replaced. Cached break
     * entries use Qt numeric conversion without order/finite checks. Absent color/size toggle/range
     * fields retain current values. Without outputWidthEnabled, width follows the loaded size
     * channel/range for legacy compatibility. Override keys must be nonnegative integers with valid
     * colors. No notification is emitted. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy all configuration and cached state into an independent renderer.
     * @return Sole ownership of the deep value snapshot, including dormant overrides. */
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

    // ── Per-class editing (Slice BB Phase 8.6.16) ──────────────────────
    // classKey is the bin index as a string ("0", "1", …). Only colour
    // is editable for graduated — size/width are global on this renderer.
    // Overrides persist through ramp changes but become "dormant" if the
    // bin count drops below the override index (they re-emerge if the
    // bin count grows again, modelling undo-friendly user intent).
    /** @brief Advertise sparse per-bin color editing.
     * @param kind Requested capability.
     * @return True only for Color; other and unknown kinds return false. */
    [[nodiscard]] bool supportsClassEdit(ClassEditKind kind) const override
    {
        return kind == ClassEditKind::Color;
    }
    /** @brief Read a sparse override without sampling the ramp.
     * @param classKey Nonnegative bin key parsed with QString::toInt(); current bin count is not
     * checked.
     * @return Stored override or invalid QColor for absent/unparseable/negative keys, including
     * dormant above-count entries. */
    [[nodiscard]] QColor colorForClass(const QString &classKey) const override;
    /** @brief Store or remove a sparse color override without checking active bin count.
     * @param classKey Nonnegative integer key parsed with QString::toInt(); invalid/negative keys
     * are ignored.
     * @param color Valid color to copy; invalid removes the override. Above-count keys remain
     * dormant until their bin is used, and no notification is emitted. */
    void setColorForClass(const QString &classKey, const QColor &color) override;
    /** @brief Drop every sparse bin-color override, including dormant entries. */
    void clearClassEditOverrides() override;

    /** @brief Inspect all stored sparse overrides without filtering by bin count.
     * @return Borrowed hash valid for this renderer's lifetime; override edits/loading can change
     * its contents.
     *
     * Inspect currently-stored per-bin colour overrides — primarily
     *         for unit tests + the property dialog's "Reset" affordance.
     */
    [[nodiscard]] const QHash<int, QColor> &binColorOverrides() const noexcept
    {
        return m_binColorOverrides;
    }

private:
    QString m_classifyAttribute;
    // Independent size/width attribute (empty = follow classifyAttribute)
    // + its sampled value range (invalid until the owning layer derives it).
    QString m_sizeAttribute;
    double m_sizeValueMin = 0.0;
    double m_sizeValueMax = 0.0;
    RasterColorRamp m_ramp = RasterColorRamp::viridis(0.0, 1.0);
    IntervalBinner m_binner; // EqualInterval, 5 bins by default
    QVector<double> m_lastBreaks;
    SymbolStyle m_baseSymbol;

    // Slice BI Phase 8.13.43-α — output-axis toggles.
    bool m_outputColorEnabled = true;
    bool m_outputSizeEnabled = false;
    double m_outputSizeMin = 2.0;
    double m_outputSizeMax = 14.0;

    // VS.4 — independent line-width output axis.
    bool m_outputWidthEnabled = false;
    double m_outputWidthMin = 0.5;
    double m_outputWidthMax = 6.0;

    // P2 — static/dynamic source + range-mode spine.
    AttributeSourceKind m_sourceKind = AttributeSourceKind::Static;
    RangeMode m_rangeMode = RangeMode::FixedOverRun;

    // Slice BB Phase 8.6.16 — sparse per-bin colour overrides.
    QHash<int, QColor> m_binColorOverrides;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_GRADUATEDRENDERER_H
