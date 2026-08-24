/*!
 * \file   comparisonplotmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — model layer for the Comparison Plot Dialog.
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

namespace openswmmvis::plot {

/*! \brief One loaded run in the plot — a 1D `.out`, an observed CSV, or a 2D mesh layer. */
struct RunSource {
    std::shared_ptr<IRunLayer> layer;        ///< The actual data source. May be owned by the dialog.
    QString                    label;        ///< Display label; defaults to layer->scenarioName().
    bool                       isBaseline = false; ///< Anchor for the 1v1 scatter column.
    bool                       isTransient = false; ///< True when owned by the dialog (loaded ad hoc).
    SeriesStyle                defaultStyle;       ///< Cycle position for series in this run.

    int                        cycleSeed = 0;      ///< Internal: used to spread colours within the run.
};

/*! \brief One chart series — points at a RunSource via index and identifies
 *  what to read from it. */
struct SeriesSpec {
    int            runIndex   = -1;
    ObjectRef      objectRef;
    PlotAttribute  attribute  = PlotAttribute::Unknown;
    /*! Y2b-2 (amendment D-Y4): species NAME when this series plots a
     *  water-quality species; empty for a fixed attribute. The name is
     *  the identity (D-G1) — resolution to the run's column index
     *  happens at fetch time, never here. attribute stays Unknown for a
     *  species series. */
    QString        species;
    SeriesStyle    style;
    QString        legendOverride;   ///< Empty = auto-generate from (run, ref, attr).

    /*! What this series plots, as the descriptor the resolution and
     *  row-keying paths consume. */
    ResultDescriptor descriptor() const
    {
        return species.isEmpty() ? ResultDescriptor::forAttribute(attribute)
                                 : ResultDescriptor::forSpecies(species);
    }

    bool isValid() const noexcept
    {
        return runIndex >= 0 && objectRef.isValid() &&
               (attribute != PlotAttribute::Unknown || !species.isEmpty());
    }
};

/*! \brief Derived aggregation for one chart row. The model recomputes
 *  these whenever the spec list changes; views read them to lay out axes. */
struct AttributeRow {
    PlotAttribute     attribute = PlotAttribute::Unknown;
    /*! Y2b-2: species NAME for a species row (attribute == Unknown).
     *  Each species gets its own chart row — TSS and Lead share units
     *  but are different quantities, exactly like the Mesh2D split. */
    QString           species;
    QVector<int>      seriesIndices;   ///< Indices into ComparisonPlotModel::specs() that target this attr.
    double            ymin = 0.0;
    double            ymax = 1.0;
    UnitSystem        unitSystem = UnitSystem::US;
    QString           unitsLabel;      ///< e.g. "ft", "m/s".
};

/*! \brief One user-configured 1v1 comparison: X-series vs Y-series
 *  (COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5). Both series must target
 *  the same PlotAttribute (same chart row). When the pair list is empty
 *  the view falls back to auto pairing (baseline vs every other run,
 *  matched by objectRef). */
struct ComparisonPair {
    int xSeriesIndex = -1;   ///< Index into specs() — scatter X values.
    int ySeriesIndex = -1;   ///< Index into specs() — scatter Y values.

    bool operator==(const ComparisonPair& o) const noexcept
    { return xSeriesIndex == o.xSeriesIndex && ySeriesIndex == o.ySeriesIndex; }
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
    explicit ComparisonPlotModel(QObject *parent = nullptr);
    ~ComparisonPlotModel() override;

    // ----- RunSource list -------------------------------------------------

    /*! \brief Append a run. Returns the new run's index. */
    int addRunSource(RunSource src);

    /*! \brief Remove a run by index. All SeriesSpec entries referencing the
     *  removed run are dropped first; remaining specs have their runIndex
     *  shifted down where appropriate. */
    void removeRunSource(int runIndex);

    int  runSourceCount() const noexcept { return m_runs.size(); }
    const RunSource& runSource(int i) const { return m_runs.at(i); }
    RunSource&       runSource(int i)       { return m_runs[i]; }

    /*! \brief Find run by IRunLayer pointer; -1 if not found. */
    int  findRunByLayer(const IRunLayer* layer) const;

    /*! \brief Mark a run as baseline. -1 clears the baseline. */
    void setBaseline(int runIndex);
    int  baselineRunIndex() const noexcept { return m_baselineIdx; }

    // ----- SeriesSpec list ------------------------------------------------

    /*! \brief Append a series spec. Auto-assigns a default style cycle if
     *  `spec.style` is the default-constructed style. Returns the series index. */
    int addSeries(SeriesSpec spec);

    /*! \brief Remove a series. */
    void removeSeries(int seriesIndex);

    /*! \brief Update a series' style. Emits `styleChanged`. */
    void updateStyle(int seriesIndex, const SeriesStyle& style);

    /*! \brief Update a series' legend override (spec-level — wins over
     *  the auto-generated "run — object (attr)" label). Empty restores
     *  the auto label. Emits `styleChanged` so chart rows relabel. */
    void updateLegendOverride(int seriesIndex, const QString& legendOverride);

    int seriesCount() const noexcept { return m_specs.size(); }
    const SeriesSpec& spec(int i) const { return m_specs.at(i); }
    SeriesSpec&       spec(int i)       { return m_specs[i]; }
    const QVector<SeriesSpec>& specs() const noexcept { return m_specs; }

    // ----- 1v1 comparison pairs (Phase 5) ----------------------------------
    // Empty list = auto mode (baseline vs every other run by objectRef).

    /*! \brief Append a pair. Rejects (returns -1) invalid/duplicate pairs,
     *  self-pairs, and pairs whose series target different attributes.
     *  Emits `pairsChanged` on success. */
    int addPair(ComparisonPair pair);

    /*! \brief Remove a pair by index. Emits `pairsChanged`. */
    void removePair(int pairIndex);

    /*! \brief Drop all pairs (back to auto mode). Emits `pairsChanged`
     *  when the list was non-empty. */
    void clearPairs();

    const QVector<ComparisonPair>& pairs() const noexcept { return m_pairs; }

    // ----- Derived row layout --------------------------------------------

    /*! \brief Distinct attributes present in the spec list, ordered by the
     *  order each attribute was first added. */
    const QVector<AttributeRow>& rows() const noexcept { return m_rows; }

    /*! \brief Force re-derivation of `rows()`. Called automatically after
     *  add/remove; exposed so views can refresh after a runSource yields
     *  a new periodCount during live mode. */
    void rebuildRows();

    // ----- Series resolution ---------------------------------------------

    /*! \brief Resolve a single series. Convenience wrapper over the
     *  RunSource layer's IRunLayer::getSeriesAt. */
    void resolveSeries(int seriesIndex, SeriesData& out) const;

    // ----- Animation cursor ----------------------------------------------

    /*! \brief Current animation time. Invalid when nothing is playing. */
    QDateTime animationTime() const noexcept { return m_animTime; }

    /*! \brief Slot for AnimationController::currentTimeChanged. Emits
     *  `animationTimeChanged` for views to update the dashed cursor line. */
    void setAnimationTime(QDateTime t);

signals:
    void runSourceAdded(int runIndex);
    void runSourceRemoved(int runIndex);
    void baselineChanged(int runIndex);
    void seriesAdded(int seriesIndex);
    void seriesRemoved(int seriesIndex);
    void styleChanged(int seriesIndex);
    void rowsChanged();
    void animationTimeChanged(QDateTime t);

    /*! \brief Phase 5 — the 1v1 pair list changed (add/remove/clear or
     *  reindex after a series removal). */
    void pairsChanged();

private:
    QVector<RunSource>   m_runs;
    QVector<SeriesSpec>  m_specs;
    QVector<AttributeRow> m_rows;
    QVector<ComparisonPair> m_pairs;        ///< Phase 5 — empty = auto mode.
    int                  m_baselineIdx = -1;
    QDateTime            m_animTime;        ///< Invalid when no AC bound / stopped.

    /*! \brief Populate `m_rows` from `m_specs`. Idempotent. */
    void deriveRows_();

    /*! \brief Phase 5 — drop pairs referencing the removed series index and
     *  shift higher indices down. Returns true when the list changed. */
    bool fixupPairsAfterSeriesRemoval_(int seriesIndex);
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_COMPARISONPLOTMODEL_H
