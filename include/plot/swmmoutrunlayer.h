/**
 * @file      swmmoutrunlayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BL — IRunLayer adapter wrapping a SWMMResultsLayer (1D .out).
 *
 * One `SwmmOutRunLayer` is the bridge between the Comparison Plot Dialog's
 * model layer (which thinks in terms of IRunLayer + ObjectRef) and the
 * existing on-canvas `SWMMResultsLayer` that already holds the open .out
 * handle.
 *
 * The adapter is non-owning — it holds a raw `SWMMResultsLayer*` and
 * resolves it on every call. When the underlying layer is destroyed
 * (e.g. project window closed), the adapter detects the dangling pointer
 * via a QPointer guard and returns ok=false from `getSeriesAt`.
 *
 * No standalone .out support yet; *Tools → Comparison Plot → Load Run…*
 * (a follow-up) will spin up a parallel "headless" adapter that owns its
 * own `SWMM_Output` handle.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_SWMMOUTRUNLAYER_H
#define OPENSWMMVIS_PLOT_SWMMOUTRUNLAYER_H

#include "plot/irunlayer.h"

#include <QPointer>

class SWMMResultsLayer;

namespace openswmmvis::plot
{

class SwmmOutRunLayer final : public IRunLayer
{
public:
    /*! \brief Wrap an existing on-canvas SWMMResultsLayer (non-owning).
     * @param[in] layer Optional borrowed results layer, guarded by QPointer without ownership transfer. Calls after its destruction use empty/unavailable fallbacks.
     * @note No separate output handle is opened and no cross-thread synchronization is supplied; respect the underlying results layer's access/lifetime rules.
     */
    explicit SwmmOutRunLayer(SWMMResultsLayer *layer);
    /**
     * @brief Release only the adapter; the borrowed layer and output handle remain owned elsewhere.
     */
    ~SwmmOutRunLayer() override = default;

    /*! \brief Underlying layer (may be null if it was destroyed).
     * @return Borrowed guarded results-layer pointer, null if absent/destroyed.
     */
    SWMMResultsLayer *layer() const;

    // IRunLayer
    /**
     * @brief Read guarded 1D output metadata.
     * @return Underlying scenarioName(), or (closed) for a missing layer.
     */
    QString scenarioName() const override;
    /**
     * @brief Read guarded 1D output metadata.
     * @return Flow-code-derived family or US when the layer is absent/its code is negative. Does not convert returned samples.
     */
    UnitSystem unitSystem() const override;
    /**
     * @brief Read guarded 1D output metadata.
     * @return Engine output start date as SWMM/OLE-Automation days, or NaN when handle/query unavailable.
     */
    double startDateJulian() const override;
    /**
     * @brief Read guarded 1D output metadata.
     * @return Underlying layer's current totalTimeSteps(), or zero when absent.
     */
    int periodCount() const override;
    /**
     * @brief Read guarded 1D output metadata.
     * @return Underlying report step in seconds, or zero without a layer.
     */
    int reportStepSeconds() const override;
    /**
     * @brief Read guarded 1D output metadata.
     * @return Underlying results-file path, or empty after layer destruction.
     */
    QString persistenceKey() const override;

    /**
     * @brief Resolve a compatible fixed 1D/system attribute from the open output handle.
     * @param[in] ref Node/link/subcatch ID or System identity; object indices are resolved against this output file.
     * @param[in] attr Quantity must map to ref.kind through variableCodeFor().
     * @param[in,out] out Clears samples/error/ok, preserves firstPeriod and resolves its clamped tail. Success includes total periodCount and raw output values; failure sets a diagnostic. A failure before fetch may leave an earlier periodCount unchanged.
     * @note No numeric unit conversion occurs. Recorded report timestamps are read per period; accepted-clock snow variables instead use output start plus their stored snow-process seconds. A valid request with no new tail returns ok true with empty samples.
     */
    void getSeriesAt(const ObjectRef &ref, PlotAttribute attr, SeriesData &out) const override;

    /**
     * @param[in] attr Candidate fixed identifier.
     * @return False for Unknown and mesh-cell quantities. Accepted-clock snow attributes require a present file/variable; other identifiers return true without full kind/code checking (including retired or edge identifiers that resolution later rejects).
     */
    bool supportsAttribute(PlotAttribute attr) const override;

    /*! \brief Y2b-1: fixed set + the open run's species BY NAME (D-G1).
     *  Legacy `.out` (no quality) serves the fixed set only.
     * @param[in] kind Requested object discriminator.
     * @return Fixed-kind choices filtered by supportsAttribute(), with current nonempty species names appended only for node/link/subcatch kinds. A missing layer gives no species and excludes unavailable snow fields. No samples are read.
     */
    QVector<ResultDescriptor> resultDescriptorsForKind(ObjectRef::Kind kind) const override;

    /*! \brief Y2b-2: species series resolve NAME → index → per-kind
     *  `SWMM_OUT_*_POLLUT_BASE + index` against the run's live species
     *  list on every call (D-G1 — a stored index would repoint when a
     *  model edit reorders species). Fixed attributes take the enum
     *  path unchanged.
     * @param[in] ref Source-local 1D/system identity.
     * @param[in] descriptor Species names resolve case-sensitively against the live run list. Non-species descriptors forward descriptor.attr to the fixed overload; mesh catalog descriptors therefore fail fixed mapping.
     * @param[in,out] out Same tail/sample/error contract as the fixed overload. Missing species/kind sets ok false with a diagnostic after clearing samples; no source mutation occurs.
     */
    void getSeriesAt(const ObjectRef &ref, const ResultDescriptor &descriptor,
                     SeriesData &out) const override;

    /*! \brief Map a species NAME to the engine's per-object var code:
     *  `SWMM_OUT_{NODE,LINK,SUBCATCH}_POLLUT_BASE + indexOf(name)`.
     *  Returns -1 when the run does not carry the species or the kind
     *  has no species columns. Pure mapping — exposed for testing, and
     *  reorder-safe by construction (the index comes from \p runSpecies
     *  at call time, never from storage).
     * @param[in] species Exact case-sensitive stored name; empty is rejected.
     * @param[in] runSpecies Current run names in file order; the first exact duplicate is used.
     * @param[in] kind Only Node/Link/Subcatch carry species columns.
     * @return Per-kind pollutant base plus current index, or -1 for absent species/unsupported kind. No handle access or unit conversion occurs.
     */
    static int speciesVariableCodeFor(const QString &species, const QStringList &runSpecies,
                                      ObjectRef::Kind kind);

    /*! \brief Map a PlotAttribute + ObjectRef::Kind to the engine's
     *  per-object var code (SWMM_OUT_NODE_*, SWMM_OUT_LINK_*,
     *  SWMM_OUT_SUBCATCH_*, SWMM_OUT_SYS_*). Returns -1 when the
     *  combination isn't valid (e.g. NodeDepth on a Link, or a Mesh2D
     *  attribute on any kind). Pure mapping — exposed for testing.
     * @param[in] attr Fixed result identifier, including supported accepted-clock snow fields.
     * @param[in] kind Node/Link/Subcatch/System discriminator.
     * @return Engine per-kind variable code, or -1 for an unsupported pair, Unknown, retired SystemEvapTotal or a mesh attribute. This mapping alone does not establish variable presence in a particular file.
     */
    static int variableCodeFor(PlotAttribute attr, ObjectRef::Kind kind);

private:
    /*! Shared tail of both getSeriesAt overloads: object resolution +
     *  bulk fetch + time axis for an already-resolved var code.
     * @param[in] ref Supported 1D/system object identity; m_layer and its output handle must already be available.
     * @param[in] varCode Compatible pre-resolved engine variable code.
     * @param[in,out] out Caller must initialize ok false and empty samples/error. Reads clamped [firstPeriod,total) using a bulk value query, sets periodCount once a nonempty file is known, and sets ok true only after full timestamps/values are ready. firstPeriod itself is unchanged.
     * @note Zero periods, unknown ID, engine read failure or missing clocks produce diagnostics. A tail at/after total succeeds empty. Values are copied from output floats to doubles, without scaling.
     */
    void fetchSeriesByCode_(const ObjectRef &ref, int varCode, SeriesData &out) const;

    QPointer<SWMMResultsLayer> m_layer;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SWMMOUTRUNLAYER_H
