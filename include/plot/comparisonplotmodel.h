/**
 * @file      comparisonplotmodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BL — model layer for the Comparison Plot Dialog.
 *
 * Holds three things:
 *   - A list of `RunSource` records (one per loaded run / observed dataset).
 *     One run is flagged `isBaseline` for the 1v1 scatter column.
 *   - A list of `SeriesSpec` records (one per chart series).
 *   - A cache of `AttributeRow` aggregations (one row per distinct
 *     PlotAttribute present in the spec list).
 *
 * The model owns no chart widgets — it emits signals; the view subscribes.
 * Series resolution is delegated to each RunSource's `IRunLayer`.
 *
 * CF.3 amendment (2026-05-21): no model-level changes needed; the
 * Mesh2DCell `ObjectRef` discriminator + Mesh2D* attributes were added
 * to the underlying types directly, and `SWMM2DResultsLayer` implements
 * `IRunLayer` to participate as a RunSource.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_COMPARISONPLOTMODEL_H
#define OPENSWMMVIS_PLOT_COMPARISONPLOTMODEL_H

#include "plot/irunlayer.h"
#include "plot/plotattribute.h"
#include "plot/seriesstyle.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace openswmmvis::plot
{

/*! \brief One loaded run in the plot — a 1D `.out`, an observed CSV, or a 2D mesh layer. */
struct RunSource {
    std::shared_ptr<IRunLayer> layer; ///< The actual data source. May be owned by the dialog.
    QString label;                    ///< Display label; defaults to layer->scenarioName().
    bool isBaseline = false;          ///< Anchor for the 1v1 scatter column.
    bool isTransient = false;         ///< True when owned by the dialog (loaded ad hoc).
    SeriesStyle defaultStyle;         ///< Cycle position for series in this run.

    int cycleSeed = 0; ///< Internal: used to spread colours within the run.
};

/*! \brief One chart series — points at a RunSource via index and identifies
 *  what to read from it. */
struct SeriesSpec {
    int runIndex = -1;
    ObjectRef objectRef;
    PlotAttribute attribute = PlotAttribute::Unknown;
    /*! Y2b-2 (amendment D-Y4): species NAME when this series plots a
     *  water-quality species; empty for a fixed attribute. The name is
     *  the identity (D-G1) — resolution to the run's column index
     *  happens at fetch time, never here. attribute stays Unknown for a
     *  species series. */
    QString species;
    /*! 2D catalog variable (key + display label/units) when this series
     *  plots a `ResultDescriptor::isMeshVariable()` result; empty otherwise. */
    QString variableKey;
    QString variableLabel;
    QString variableUnits;
    QString observationKey; ///< Stable observation quantity/statistic/unit/time basis.
    QString observationLabel; ///< Observation axis caption.
    QString observationUnits; ///< Explicit native observation unit.
    SeriesStyle style;
    QString legendOverride; ///< Empty = auto-generate from (run, ref, attr).

    /*! What this series plots, as the descriptor the resolution and
     *  row-keying paths consume.
     * @return Independent descriptor with priority mesh key, then species name, then fixed attribute. Stored display metadata accompanies mesh keys; no source validation occurs.
     */
    ResultDescriptor descriptor() const
    {
        if (!observationKey.isEmpty())
            return ResultDescriptor::forObservation(observationKey, observationLabel, observationUnits);
        if (!variableKey.isEmpty())
            return ResultDescriptor::forMeshVariable(variableKey, variableLabel, variableUnits);
        return species.isEmpty() ? ResultDescriptor::forAttribute(attribute)
                                 : ResultDescriptor::forSpecies(species);
    }

    /**
     * @brief Copy every descriptor identity/metadata field into this specification.
     * @param[in] d Descriptor copied verbatim; does not normalize inconsistent branches, change object/run/style or notify a model.
     */
    void setDescriptor(const ResultDescriptor &d)
    {
        attribute = d.attr;
        species = d.species;
        variableKey = d.variableKey;
        variableLabel = d.variableLabel;
        variableUnits = d.variableUnits;
        observationKey = d.observationKey;
        observationLabel = d.observationLabel;
        observationUnits = d.observationUnits;
    }

    /**
     * @brief Test structural presence of a run index, object reference and descriptor.
     * @return True when runIndex is nonnegative and the other identities are structurally valid. No run-count/source-availability check occurs.
     */
    bool isValid() const noexcept
    {
        return runIndex >= 0 && objectRef.isValid() && descriptor().isValid();
    }
};

/*! \brief Derived aggregation for one chart row. The model recomputes
 *  these whenever the spec list changes; views read them to lay out axes. */
struct AttributeRow {
    PlotAttribute attribute = PlotAttribute::Unknown;
    /*! Y2b-2: species NAME for a species row (attribute == Unknown).
     *  Each species gets its own chart row — TSS and Lead share units
     *  but are different quantities, exactly like the Mesh2D split. */
    QString species;
    /*! 2D catalog variable rows: one row per variable key. */
    QString variableKey;
    QString variableLabel;
    QString variableUnits;
    QString observationKey; ///< Stable observation quantity/statistic/unit/time basis.
    QString observationLabel; ///< Observation axis caption.
    QString observationUnits; ///< Explicit native observation unit.
    QVector<int>
        seriesIndices; ///< Indices into ComparisonPlotModel::specs() that target this attr.
    double ymin = 0.0;
    double ymax = 1.0;
    UnitSystem unitSystem = UnitSystem::US;
    QString unitsLabel; ///< e.g. "ft", "m/s".

    /*! What this row plots (chart title, tab caption, row matching).
     * @return Independent row descriptor with mesh-key, species-name, fixed-attribute priority; no result-source lookup occurs.
     */
    ResultDescriptor descriptor() const
    {
        if (!observationKey.isEmpty())
            return ResultDescriptor::forObservation(observationKey, observationLabel, observationUnits);
        if (!variableKey.isEmpty())
            return ResultDescriptor::forMeshVariable(variableKey, variableLabel, variableUnits);
        return species.isEmpty() ? ResultDescriptor::forAttribute(attribute)
                                 : ResultDescriptor::forSpecies(species);
    }
};

/*! \brief One user-configured 1v1 comparison: X-series vs Y-series
 *  (COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5). Both series must target
 *  the same PlotAttribute (same chart row). When the pair list is empty
 *  the view falls back to auto pairing (baseline vs every other run,
 *  matched by objectRef). */
struct ComparisonPair {
    int xSeriesIndex = -1; ///< Index into specs() — scatter X values.
    int ySeriesIndex = -1; ///< Index into specs() — scatter Y values.

    /**
     * @brief Compare directed X/Y specification indices.
     * @param[in] o Other directed pair.
     * @return True only for identical ordered indices; reversing X/Y creates a distinct pair.
     */
    bool operator==(const ComparisonPair &o) const noexcept
    {
        return xSeriesIndex == o.xSeriesIndex && ySeriesIndex == o.ySeriesIndex;
    }
};

/*! \brief Model layer for the Comparison Plot Dialog.
 *
 * Lifecycle:
 *  - Construct empty; dialog adds RunSource(s) via `addRunSource`.
 *  - Add series via `addSeries`. Row layout updates and signal fires.
 *  - Mark baseline via `setBaseline(runIndex)`.
 *  - View subscribes to `seriesAdded` / `seriesRemoved` / `styleChanged` /
 *    `baselineChanged` / `runSourceAdded` / `runSourceRemoved` /
 *    `animationTimeChanged` and re-lays-out incrementally.
 */
class ComparisonPlotModel : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty run/specification model with no baseline or animation time.
     * @param[in] parent Optional QObject owner. Chart widgets are never owned by this model.
     */
    explicit ComparisonPlotModel(QObject *parent = nullptr);
    /**
     * @brief Release model vectors and their shared run-source references; other shared owners may retain the sources.
     */
    ~ComparisonPlotModel() override;

    // ----- RunSource list -------------------------------------------------

    /*! \brief Append a run. Returns the new run's index.
     * @param[in] src Run record moved into storage, retaining shared ownership of its source if present. Empty label uses scenarioName(), then persistence-key file name. Null sources are accepted.
     * @return Appended zero-based run index. Emits runSourceAdded(); if no baseline existed it silently chooses this index without baselineChanged(). Existing isBaseline flags are not synchronized.
     */
    int addRunSource(RunSource src);

    /*! \brief Remove a run by index. All SeriesSpec entries referencing the
     *  removed run are dropped first; remaining specs have their runIndex
     *  shifted down where appropriate.
     * @param[in] runIndex Zero-based index; invalid indices are ignored.
     * @note Removes matching specs in descending index order (emitting seriesRemoved), reindexes remaining specs/pairs/runs, and derives rows. Emits runSourceRemoved(), baselineChanged() only if a baseline remains, rowsChanged(), then pairsChanged() if needed. Removing the baseline selects run zero or -1 when empty.
     */
    void removeRunSource(int runIndex);

    /**
     * @return Number of current run records, including any null-source records.
     */
    int runSourceCount() const noexcept { return m_runs.size(); }
    /**
     * @brief Access an immutable run record.
     * @param[in] i Valid index in [0,runSourceCount()); no recoverable range-error result is provided.
     * @return Borrowed record, invalidated by vector mutations.
     */
    const RunSource &runSource(int i) const { return m_runs.at(i); }
    /**
     * @brief Access a mutable run record.
     * @param[in] i Valid index in [0,runSourceCount()); no recoverable range-error result is provided.
     * @return Borrowed record, invalidated by vector mutations. Direct edits bypass notifications/row derivation; caller must coordinate refresh.
     */
    RunSource &runSource(int i) { return m_runs[i]; }

    /*! \brief Find run by IRunLayer pointer; -1 if not found.
     * @param[in] layer Borrowed source identity pointer; null returns -1.
     * @return First run whose shared pointer has this exact address, or -1; does not compare file paths or labels.
     */
    int findRunByLayer(const IRunLayer *layer) const;

    /*! \brief Mark a run as baseline. -1 clears the baseline.
     * @param[in] runIndex -1 clears; [0,runSourceCount()) selects. Out-of-range or unchanged values are no-ops.
     * @note Updates only the baseline index and emits baselineChanged(); RunSource::isBaseline flags and row layout are not modified.
     */
    void setBaseline(int runIndex);
    /**
     * @return Baseline run index or -1 for none; use this index rather than RunSource::isBaseline.
     */
    int baselineRunIndex() const noexcept { return m_baselineIdx; }

    // ----- SeriesSpec list ------------------------------------------------

    /*! \brief Append a series spec. Auto-assigns a default style cycle if
     *  `spec.style.color` equals the default colour. Returns the series index.
     * @param[in] spec Specification moved into storage without structural/source validation. If its colour alone equals default SeriesStyle colour and its run index exists, the entire style is replaced by the next cycle style, even if other style fields were customized.
     * @return Appended index; derives rows, emits seriesAdded() then rowsChanged(). Invalid stored specs remain present but are omitted from rows.
     */
    int addSeries(SeriesSpec spec);
    /*! Append a selection with one row derivation and one view notification.
     * @param[in] specs Specifications in append order, moved from the passed-by-value vector. Uses the same colour-only style replacement test as addSeries().
     * @return Number appended, not the starting index. Empty input returns zero silently; otherwise derives rows once and emits seriesBatchAdded() then rowsChanged(), without per-series added signals.
     */
    int addSeriesBatch(QVector<SeriesSpec> specs);
    /**
     * @brief Resolve current specifications in per-run batches while retaining specification order.
     * @param[in] firstPeriods Optional per-spec zero-based tail cursors; missing entries default zero, extra entries are ignored.
     * @return One independently owned response per spec. Invalid specs receive resolveSeries() diagnostics; valid requests use each source's batch method. Missing source responses retain a default unsuccessful result. No common snapshot is guaranteed between runs.
     */
    QVector<SeriesData> resolveAllSeries(const QVector<int> &firstPeriods = {}) const;

    /*! \brief Remove a series.
     * @param[in] seriesIndex Current index; invalid is a no-op.
     * @note Erases the spec, drops/reindexes affected pairs and derives rows; emits seriesRemoved(), rowsChanged(), then pairsChanged() if needed. Other spec indices shift down.
     */
    void removeSeries(int seriesIndex);

    /*! \brief Update a series' style. Emits `styleChanged`.
     * @param[in] seriesIndex Current spec index; invalid is ignored.
     * @param[in] style Complete raw style copied verbatim.
     * @note Emits styleChanged(seriesIndex) even for an identical style; no row derivation or rendering occurs in this model.
     */
    void updateStyle(int seriesIndex, const SeriesStyle &style);

    /*! \brief Update a series' legend override (spec-level — wins over
     *  the auto-generated "run — object (attr)" label). Empty restores
     *  the auto label. Emits `styleChanged` so chart rows relabel.
     * @param[in] seriesIndex Current spec index; invalid is ignored.
     * @param[in] legendOverride Verbatim label; empty restores host-generated naming.
     * @note Unchanged text is a no-op; otherwise emits styleChanged() with this spec index and does not derive rows.
     */
    void updateLegendOverride(int seriesIndex, const QString &legendOverride);

    /**
     * @return Current spec vector length, including structurally invalid entries.
     */
    int seriesCount() const noexcept { return m_specs.size(); }
    /**
     * @brief Inspect a stored specification without resolution.
     * @param[in] i Index in [0,seriesCount()); no recoverable range-error result is supplied.
     * @return Borrowed immutable reference, invalidated by vector mutations.
     */
    const SeriesSpec &spec(int i) const { return m_specs.at(i); }
    /**
     * @brief Access a mutable specification without resolution.
     * @param[in] i Index in [0,seriesCount()); no recoverable range-error result is supplied.
     * @return Borrowed reference, invalidated by vector mutations. Direct edits do not notify or update rows; caller must coordinate rebuildRows().
     */
    SeriesSpec &spec(int i) { return m_specs[i]; }
    /**
     * @return Borrowed immutable spec vector; references/indices can change after add/remove operations. No resolution occurs.
     */
    const QVector<SeriesSpec> &specs() const noexcept { return m_specs; }

    // ----- 1v1 comparison pairs (Phase 5) ----------------------------------
    // Empty list = auto mode (baseline vs every other run by objectRef).

    /*! \brief Append a pair. Rejects (returns -1) invalid/duplicate pairs,
     *  self-pairs, and pairs whose series target different attributes.
     *  Emits `pairsChanged` on success.
     * @param[in] pair Directed X/Y spec indices. Both must exist, differ and have equal descriptors; this checks descriptor identity including species/key, not compatible unit systems.
     * @return Appended pair index, or -1 for invalid/self/duplicate/different-descriptor requests. Reverse-direction pairs are distinct. Success emits pairsChanged().
     */
    int addPair(ComparisonPair pair);

    /*! \brief Remove a pair by index. Emits `pairsChanged`.
     * @param[in] pairIndex Index into pairs(); invalid is a no-op. Success erases/reindexes the vector and emits pairsChanged().
     */
    void removePair(int pairIndex);

    /*! \brief Drop all pairs (back to auto mode). Emits `pairsChanged`
     *  when the list was non-empty.
     * @note Does not remove specs or change the baseline; a nonempty-to-empty transition emits pairsChanged().
     */
    void clearPairs();

    /**
     * @return Borrowed immutable directed pair vector; empty requests the view's auto-pairing policy. Mutations invalidate references.
     */
    const QVector<ComparisonPair> &pairs() const noexcept { return m_pairs; }

    // ----- Derived row layout --------------------------------------------

    /*! \brief Distinct attributes present in the spec list, ordered by the
     *  order each attribute was first added.
     * @return Borrowed row vector in first-encounter order. Derived range fields are [0,1] defaults rather than sample-computed extrema; source units/metadata come from the first contributing spec. No inter-run value conversion occurs.
     */
    const QVector<AttributeRow> &rows() const noexcept { return m_rows; }

    /*! \brief Force re-derivation of `rows()`. Called automatically after
     *  add/remove; exposed so views can refresh after a runSource yields
     *  a new periodCount during live mode.
     * @note Derives metadata/grouping without fetching samples, then emits rowsChanged() even if rows did not differ.
     */
    void rebuildRows();

    // ----- Series resolution ---------------------------------------------

    /*! \brief Resolve a single series. Convenience wrapper over the
     *  RunSource layer's IRunLayer::getSeriesAt.
     * @param[in] seriesIndex Current spec index.
     * @param[in,out] out Preserves firstPeriod as the caller's requested cursor; clears samples/error/ok/periodCount before validation and virtual descriptor resolution.
     * @note Bad index/spec/run/null-source returns ok false with a diagnostic. On source success/failure, response units and tail handling follow IRunLayer; caller must inspect ok. No model signals are emitted.
     */
    void resolveSeries(int seriesIndex, SeriesData &out) const;

    // ----- Animation cursor ----------------------------------------------

    /*! \brief Current animation time. Invalid when nothing is playing.
     * @return Stored animation instant, possibly invalid; this getter does not consult an AnimationController.
     */
    QDateTime animationTime() const noexcept { return m_animTime; }

    /*! \brief Slot for AnimationController::currentTimeChanged. Emits
     *  `animationTimeChanged` for views to update the dashed cursor line.
     * @param[in] t Instant copied/moved verbatim, including invalid dates; an equal value is a no-op.
     * @note Emits animationTimeChanged() only; does not resolve samples or move a controller itself.
     */
    void setAnimationTime(QDateTime t);

signals:
    /**
     * @brief Notify a model list/selection edit.
     * @param[in] runIndex Appended current run index; automatic first-baseline selection may occur without a separate baseline signal.
     */
    void runSourceAdded(int runIndex);
    /**
     * @brief Notify a model list/selection edit.
     * @param[in] runIndex Removed former run index; remaining run/spec indices have already been adjusted.
     */
    void runSourceRemoved(int runIndex);
    /**
     * @brief Notify a model list/selection edit.
     * @param[in] runIndex Current baseline index, including -1 from explicit clearing. Removing the last baseline emits no -1 signal.
     */
    void baselineChanged(int runIndex);
    /**
     * @brief Notify a model list/selection edit.
     * @param[in] seriesIndex Appended current spec index after rows were derived.
     */
    void seriesAdded(int seriesIndex);
    /**
     * @brief Notify one nonempty batch append after row derivation; no per-spec seriesAdded() signals accompany it.
     */
    void seriesBatchAdded();
    /**
     * @brief Notify a model list/selection edit.
     * @param[in] seriesIndex Removed former spec index; subsequent entries shift down. During run removal, several descending notifications may arrive before rows are rederived.
     */
    void seriesRemoved(int seriesIndex);
    /**
     * @brief Notify a model list/selection edit.
     * @param[in] seriesIndex Current spec index whose style or instance legend override was edited.
     */
    void styleChanged(int seriesIndex);
    /**
     * @brief Notify rebuilt row metadata/grouping; does not imply samples were fetched or actual extrema computed.
     */
    void rowsChanged();
    /**
     * @brief Notify an edited cursor instant without changing sources.
     * @param[in] t New stored QDateTime, possibly invalid.
     */
    void animationTimeChanged(QDateTime t);

    /*! \brief Phase 5 — the 1v1 pair list changed (add/remove/clear or
     *  reindex after a series removal).
     * @note The pair vector may be empty, selecting the view's automatic-pairing policy.
     */
    void pairsChanged();

private:
    QVector<RunSource> m_runs;
    QVector<SeriesSpec> m_specs;
    QVector<AttributeRow> m_rows;
    QVector<ComparisonPair> m_pairs; ///< Phase 5 — empty = auto mode.
    int m_baselineIdx = -1;
    QDateTime m_animTime; ///< Invalid when no AC bound / stopped.

    /*! \brief Populate `m_rows` from `m_specs`. Idempotent.
     * @note Groups structurally valid specs by raw attribute plus exact species or prefixed mesh key, preserving encounter order. Uses the first spec's source unit family (US if unavailable), descriptor metadata and default concentration units; no samples are fetched and all ranges end at [0,1]. Emits no signals itself.
     */
    void deriveRows_();

    /*! \brief Phase 5 — drop pairs referencing the removed series index and
     *  shift higher indices down. Returns true when the list changed.
     * @param[in] seriesIndex Former removed spec index, expected valid for the removal that just occurred.
     * @return True if any pair was dropped or either endpoint shifted; no notifications are emitted by this helper.
     */
    bool fixupPairsAfterSeriesRemoval_(int seriesIndex);
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_COMPARISONPLOTMODEL_H
