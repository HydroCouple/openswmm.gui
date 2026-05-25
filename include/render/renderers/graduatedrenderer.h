/*!
 * \file   graduatedrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer that classifies a numeric attribute into bins, each with its own colour.
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
 *         v1 uses equal-interval binning (linear) only. Slice BB Phase
 *         8.6.2 will swap binColors[] for a proper ColorRamp + IntervalBinner
 *         (Quantile / StdDev / Jenks / Log binning). The public symbolFor /
 *         legendSymbolItems contract does not change at that swap.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2 + §J.4). Sub-phase 8.13.6.2 — concrete renderer class.
 */

#ifndef OPENSWMM_RENDER_GRADUATEDRENDERER_H
#define OPENSWMM_RENDER_GRADUATEDRENDERER_H

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
    GraduatedRenderer() = default;
    ~GraduatedRenderer() override = default;

    /*!
     * \brief Name of the attribute key (in QVariantMap attrs) to classify on.
     *        Typical SWMM values: "depth", "head", "flow", "runoff".
     */
    [[nodiscard]] QString classifyAttribute() const { return m_classifyAttribute; }
    void setClassifyAttribute(QString name) { m_classifyAttribute = std::move(name); }

    /*!
     * \brief Convenience accessor for the ramp's value range. Equivalent
     *        to ramp().minValue / .maxValue. Calls to setRange propagate
     *        into the ramp.
     */
    [[nodiscard]] double minValue() const { return m_ramp.minValue; }
    [[nodiscard]] double maxValue() const { return m_ramp.maxValue; }
    void setRange(double minValue, double maxValue);

    /*!
     * \brief The colour ramp used to derive per-bin colours. Sampled at
     *        each bin's midpoint (i + 0.5) / nBins.
     */
    [[nodiscard]] const RasterColorRamp &ramp() const { return m_ramp; }
    void setRamp(RasterColorRamp ramp);

    /*!
     * \brief The interval binner that maps attribute values → bin indices.
     */
    [[nodiscard]] const IntervalBinner &binner() const { return m_binner; }
    void setBinner(IntervalBinner b);

    /*!
     * \brief The last set of interior break values computed by the binner.
     *        Kept on the renderer so paint-time `binFor` does not need
     *        to recompute breaks every frame.
     */
    [[nodiscard]] const QVector<double> &lastBreaks() const { return m_lastBreaks; }

    /*!
     * \brief Per-bin colours (derived view). Computed from the ramp +
     *        binCount at request time; kept on the legacy surface so
     *        callers that haven't migrated to ramp() still work.
     */
    [[nodiscard]] QList<QColor> binColors() const;
    void setBinColors(QList<QColor> colors);
    [[nodiscard]] int binCount() const { return m_binner.binCount(); }

    /*!
     * \brief The SymbolStyle template. At paint time the renderer overrides
     *        the "color" property of every SymbolLayer in this template with
     *        the appropriate per-bin colour.
     */
    [[nodiscard]] const SymbolStyle &baseSymbol() const { return m_baseSymbol; }
    void setBaseSymbol(SymbolStyle s);

    /*!
     * \brief Recomputes interval breaks + ramp range from a sample of
     *        attribute values. NaNs and infinities are skipped. If the
     *        sample is empty or all-NaN, the existing state is unchanged.
     */
    void autoClassify(const QVector<double> &samples);

    /*!
     * \brief Returns the colour for one raw attribute value.
     *        Clamps to [bin 0, bin N−1]. Returns Qt::transparent when bin
     *        count is zero.
     */
    [[nodiscard]] QColor colorForValue(double v) const;

    /*!
     * \brief Returns the colour for one already-computed bin index.
     *        Sampled from the ramp at midpoint (i + 0.5) / nBins.
     */
    [[nodiscard]] QColor colorForBin(int bin) const;

    /*!
     * \brief Slice BI Phase 8.13.43-α — Graduated tab's "Output axes"
     *        toggle for size/width. When `outputSizeEnabled = true`,
     *        `symbolFor` writes a bin-index-mapped size into the symbol's
     *        `props["size"]` / `props["width"]` in addition to (or instead
     *        of) the colour. Disabled by default — preserves prior
     *        colour-only behaviour.
     */
    [[nodiscard]] bool outputSizeEnabled() const { return m_outputSizeEnabled; }
    void setOutputSizeEnabled(bool on) { m_outputSizeEnabled = on; }

    [[nodiscard]] bool outputColorEnabled() const { return m_outputColorEnabled; }
    void setOutputColorEnabled(bool on) { m_outputColorEnabled = on; }

    [[nodiscard]] double outputSizeMin() const { return m_outputSizeMin; }
    [[nodiscard]] double outputSizeMax() const { return m_outputSizeMax; }
    void setOutputSizeRange(double minPx, double maxPx);

    /*!
     * \brief Returns the bin-midpoint-mapped size for a bin index, when
     *        outputSizeEnabled. Linear interpolation between
     *        outputSizeMin and outputSizeMax over [0, nBins-1].
     *        Returns outputSizeMin for single-bin or out-of-range.
     */
    [[nodiscard]] double sizeForBin(int bin) const;

    // IFeatureRenderer.
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("graduated"); }
    [[nodiscard]] SymbolStyle symbolFor(const FeatureRef &f,
                                        const QVariantMap &attrs) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IFeatureRenderer> clone() const override;

    // ── Per-class editing (Slice BB Phase 8.6.16) ──────────────────────
    // classKey is the bin index as a string ("0", "1", …). Only colour
    // is editable for graduated — size/width are global on this renderer.
    // Overrides persist through ramp changes but become "dormant" if the
    // bin count drops below the override index (they re-emerge if the
    // bin count grows again, modelling undo-friendly user intent).
    [[nodiscard]] bool supportsClassEdit(ClassEditKind kind) const override
    {
        return kind == ClassEditKind::Color;
    }
    [[nodiscard]] QColor colorForClass(const QString &classKey) const override;
    void setColorForClass(const QString &classKey, const QColor &color) override;
    void clearClassEditOverrides() override;

    /*! \brief Inspect currently-stored per-bin colour overrides — primarily
     *         for unit tests + the property dialog's "Reset" affordance. */
    [[nodiscard]] const QHash<int, QColor> &binColorOverrides() const noexcept
    { return m_binColorOverrides; }

private:
    QString          m_classifyAttribute;
    RasterColorRamp  m_ramp = RasterColorRamp::viridis(0.0, 1.0);
    IntervalBinner   m_binner;          // EqualInterval, 5 bins by default
    QVector<double>  m_lastBreaks;
    SymbolStyle      m_baseSymbol;

    // Slice BI Phase 8.13.43-α — output-axis toggles.
    bool             m_outputColorEnabled = true;
    bool             m_outputSizeEnabled  = false;
    double           m_outputSizeMin      = 2.0;
    double           m_outputSizeMax      = 14.0;

    // Slice BB Phase 8.6.16 — sparse per-bin colour overrides.
    QHash<int, QColor> m_binColorOverrides;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_GRADUATEDRENDERER_H
