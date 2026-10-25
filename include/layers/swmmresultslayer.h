/**
 * @file      swmmresultslayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Layer that colour-maps SWMM simulation output over the network
 *         geometry for interactive time-stepped playback.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMRESULTSLAYER_H
#define SWMMRESULTSLAYER_H

#include "layers/openswmmvislayer.h"
#include "layers/speciesattributes.h"
#include "io/lidstates.h"
#include "layers/gisrasterlayer.h" // RasterColorRamp
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QPen>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <memory>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class QGraphicsItem;           // Slice §Y.2 — m_itemByFeature stores QGraphicsItem*
class QGraphicsSimpleTextItem; // L-1 — m_labelByFeature stores label items

// Slice U-0 — granular per-Category sublayer mix. The legacy four
// sublayers (NodeMarker / ConduitLine / ConduitArrow / SubcatchmentFill)
// are replaced by one FeatureSublayer instance per SWMMModelLayer::Category
// (11 in total). Each instance owns its own archetype-appropriate
// style bag so every visual object type can be styled independently.
#include "render/iattributeprovider.h" // Slice DM.1
#include "render/isublayerhost.h"
#include "render/sublayers/feature/featuresublayer.h"

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class GraduatedRenderer;
class Rule;
class RuleList;           // Slice B.5 — see ruleList() override below.
enum class ClassEditKind; // Gap B1 — legend facade (full def in ifeaturerenderer.h)
}

/*!
 * \enum SWMMResultVariable
 * \brief Selectable result output variables for colour-mapped display.
 */
enum class SWMMResultVariable {
    // Node results
    NodeDepth = 0,
    NodeHead = 1,
    NodeVolume = 2,
    NodeInflow = 3,
    NodeOverflow = 4,
    NodeLateralInflow = 5,
    // Link results
    LinkFlow = 10,
    LinkDepth = 11,
    LinkVelocity = 12,
    LinkCapacity = 13,
    // Subcatchment results
    SubcatchRunoff = 20,
    SubcatchInfiltration = 21,
    SubcatchEvaporation = 22,
    SubcatchSnowDepth = 23,
    SubcatchRainfall = 24,
    SubcatchGwFlow = 25,
    SubcatchGwElevation = 26,
    SubcatchSoilMoisture = 27,
};

/*!
 * \class SWMMResultsLayer
 * \brief Overlays time-stepped simulation results on the SWMM network geometry.
 * \details Reads output from an OpenSWMMEngine binary results file (.out), maps
 *          a selected variable at the current simulation time step to a colour
 *          ramp, and paints the network elements with those colours.
 *
 *          Supports:
 *          - Animation: step forward/backward through the time series.
 *          - Any SWMMResultVariable (node depth/head, link flow/velocity, etc.).
 *          - A configurable RasterColorRamp for consistent colour mapping.
 *          - Legend rendering.
 *
 *          The layer delegates geometry loading to an associated SWMMModelLayer
 *          and only manages the colour-mapping / time-stepped rendering.
 */
class SWMMResultsLayer : public OpenSWMMVisLayer,
                         public OpenSWMM::Render::ISublayerHost,
                         public OpenSWMM::Render::IAttributeProvider // Slice DM.1
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Render::IAttributeProvider) // Slice DM.1

    Q_PROPERTY(QString resultsFilePath READ resultsFilePath NOTIFY resultsFilePathChanged)
    Q_PROPERTY(
        QString scenarioName READ scenarioName WRITE setScenarioName NOTIFY scenarioNameChanged)
    Q_PROPERTY(QColor profileLineColor READ profileLineColor WRITE setProfileLineColor NOTIFY
                   profileLineColorChanged)
    Q_PROPERTY(QPen profileHglLinePen READ profileHglLinePen WRITE setProfileHglLinePen NOTIFY
                   profileStyleChanged)
    Q_PROPERTY(QBrush profileHglFillBrush READ profileHglFillBrush WRITE setProfileHglFillBrush
                   NOTIFY profileStyleChanged)
    Q_PROPERTY(QPen profileEglLinePen READ profileEglLinePen WRITE setProfileEglLinePen NOTIFY
                   profileStyleChanged)
    // EGL renders as a line only (no physically-meaningful fill above HGL).
    Q_PROPERTY(QPen profileMaxHglLinePen READ profileMaxHglLinePen WRITE setProfileMaxHglLinePen
                   NOTIFY profileStyleChanged)
    Q_PROPERTY(QBrush profileMaxHglFillBrush READ profileMaxHglFillBrush WRITE
                   setProfileMaxHglFillBrush NOTIFY profileStyleChanged)
    Q_PROPERTY(QPen profileMaxEglLinePen READ profileMaxEglLinePen WRITE setProfileMaxEglLinePen
                   NOTIFY profileStyleChanged)
    Q_PROPERTY(int currentTimeStep READ currentTimeStep WRITE setCurrentTimeStep NOTIFY
                   currentTimeStepChanged)
    Q_PROPERTY(QDateTime currentDateTime READ currentDateTime NOTIFY currentDateTimeChanged)
    Q_PROPERTY(int totalTimeSteps READ totalTimeSteps NOTIFY totalTimeStepsChanged)
    Q_PROPERTY(SWMMResultVariable variable READ variable WRITE setVariable NOTIFY variableChanged)
    Q_PROPERTY(bool showLegend READ showLegend WRITE setShowLegend NOTIFY showLegendChanged)

public:
    /**
     * @brief Create a results view bound to borrowed model geometry, without opening the output.
     * @param[in] resultsFilePath Stored binary output path for later open/live operations.
     * @param[in] modelLayer Optional nonowned model geometry/topology/attributes source; must outlive every operation that uses it.
     * @param[in] parent Optional workspace QObject owner.
     * @note Copies model CRS when present, owns eleven category sublayers/default renderer and seeds profile colours/pens from a process-wide palette counter. Substyle changes can refetch the current frame and request structural repaint; source selection changes request value restyle. Use GUI-thread state/scene operations and serialized output-reader access.
     */
    explicit SWMMResultsLayer(const QString &resultsFilePath, class SWMMModelLayer *modelLayer,
                              OpenSWMMVisWorkspace *parent = nullptr);

    /**
     * @brief Close results/cache state and release the owned canvas transformation.
     * @note Scene items must be detached before destruction because they borrow this layer/model; QObject children own sublayers/rules/styles.
     */
    ~SWMMResultsLayer() override;

    /*! The SWMM model layer that supplies network topology + attributes
     *  for this run.  Categorized rendering on results layers reads
     *  categorical strings ("tag", "Link type", …) through here.
     * @return Borrowed original geometry source or null; no ownership/lifetime guard is acquired.
     */
    [[nodiscard]] class SWMMModelLayer *modelLayer() const { return m_modelLayer; }

    /*! Pick this output's visible network categories independently of model visibility.
     * @param[in] mapX,mapY Finite canvas-map coordinates in the model layer's currently established transform target.
     * @param[in] tolerance Nonnegative canvas-map search distance.
     * @return Model-identity hit restricted to visible positive-opacity output categories and objects present in the output name maps; empty when this layer is hidden/transparent, unbound or closed. Model-layer visibility itself does not suppress output picking.
     * @note Delegates model picking with category predicate and, on geographic inverse misses, tries nearest forward-projected model nodes. Reads stable model engine/cache/output maps without changing selection.
     */
    [[nodiscard]] SWMMModelLayer::PickResult pickAt(double mapX, double mapY,
                                                    double tolerance) const;

    // ----- Results file ---------------------------------------------------

    /**
     * @return Absolute filesystem interpretation of stored output path, or empty; no canonicalization/existence check.
     */
    [[nodiscard]] QString resultsFilePath() const;

    /*!
     * \brief Path of the `.rpt` summary report written by the run that
     *        produced this layer's `.out` file (empty when unknown).
     * \details Set by SWMMVis when a simulation finishes; persisted in the
     *          `.oswp` sidecar (keyed by the result path) so the Report
     *          Viewer can list every run's report after a project reload.
     * @return Absolute filesystem interpretation of optional stored report path, or empty; no report file is read.
     */
    [[nodiscard]] QString reportFilePath() const;
    /**
     * @param[in] path Raw optional provenance path stored verbatim.
     * @note No existence check, report parsing, settings write or notification occurs.
     */
    void setReportFilePath(const QString &path);

    // ----- Self-description for the Layer Properties dialog ----------------
    /**
     * @return Absolute output path or no-results placeholder, by value without IO.
     */
    [[nodiscard]] QString sourceDescription() const override;
    /**
     * @return Owned display metadata containing known report path, valid stored start/end range and positive report-step seconds; no statistical scan.
     */
    [[nodiscard]] QVector<QPair<QString, QString>> extendedMetadata() const override;

    /*!
     * \brief User-visible name for this run/scenario (editable, persisted in .oswp).
     * \details The constructor leaves it empty; callers can assign it explicitly.
     * @return Raw editable scenario text, initially empty in this constructor until explicitly assigned/opened.
     */
    [[nodiscard]] QString scenarioName() const;
    /**
     * @param[in] name Text copied verbatim, including empty/whitespace.
     * @note Equality is a no-op; change emits scenarioNameChanged() only, without renaming the base layer or repainting.
     */
    void setScenarioName(const QString &name);

    /*!
     * \brief Per-layer line/fill color used by Slice BC's profile plot.
     * \details Auto-assigned at construction from the categorical palette
     *          (cycled by an internal layer counter), but user-editable via
     *          the Sources panel.  Persisted in .oswp once the result-layer
     *          schema upgrades to objects (currently still a path-string array).
     * @return Copied categorical/profile identity colour; individual overlay pen/brush colours are independent after construction.
     */
    [[nodiscard]] QColor profileLineColor() const;
    /**
     * @param[in] color Raw identity colour, copied without validity check.
     * @note Equality is a no-op; change emits profileLineColorChanged() only. Existing profile overlay pens/brushes retain their independent values.
     */
    void setProfileLineColor(const QColor &color);

    // -------------------------------------------------------------------
    // Per-source styling for Slice BC's profile plot.
    //
    // Each result layer (think "model run" or "scenario") carries its own
    // pen+brush for the four overlay layers — Current HGL, Current EGL,
    // Max HGL, Max EGL — so two layers plotted side by side can be styled
    // independently (e.g. baseline solid blue vs. proposed dashed red).
    //
    // Defaults are derived from profileLineColor() at construction.
    // setProfileLineColor() does NOT re-derive them: once the user
    // customises a pen, it stays put across colour changes.  Each setter
    // emits the matching changed() signal; the dialog uses that to
    // refresh its preview swatch.
    // -------------------------------------------------------------------
    /**
     * @return Owned Qt style value for this run's current hydraulic-grade line. Pen widths/dashes use plot logical-pixel conventions; brush settings retain alpha/pattern. No source reading/painting.
     */
    [[nodiscard]] QPen profileHglLinePen() const;
    /**
     * @return Owned Qt style value for this run's current hydraulic-grade under-fill. Pen widths/dashes use plot logical-pixel conventions; brush settings retain alpha/pattern. No source reading/painting.
     */
    [[nodiscard]] QBrush profileHglFillBrush() const;
    /**
     * @return Owned Qt style value for this run's current energy-grade line. Pen widths/dashes use plot logical-pixel conventions; brush settings retain alpha/pattern. No source reading/painting.
     */
    [[nodiscard]] QPen profileEglLinePen() const;
    /**
     * @return Owned Qt style value for this run's maximum hydraulic-grade envelope line. Pen widths/dashes use plot logical-pixel conventions; brush settings retain alpha/pattern. No source reading/painting.
     */
    [[nodiscard]] QPen profileMaxHglLinePen() const;
    /**
     * @return Owned Qt style value for this run's maximum hydraulic-grade under-fill. Pen widths/dashes use plot logical-pixel conventions; brush settings retain alpha/pattern. No source reading/painting.
     */
    [[nodiscard]] QBrush profileMaxHglFillBrush() const;
    /**
     * @return Owned Qt style value for this run's maximum energy-grade envelope line. Pen widths/dashes use plot logical-pixel conventions; brush settings retain alpha/pattern. No source reading/painting.
     */
    [[nodiscard]] QPen profileMaxEglLinePen() const;

    /**
     * @param[in] pen Complete Qt style copied for this run's current hydraulic-grade line, without additional validation or normalization.
     * @note Equality is a no-op; change emits profileStyleChanged() only. Does not mutate another run, the plot-global defaults or profileLineColor().
     */
    void setProfileHglLinePen(const QPen &pen);
    /**
     * @param[in] brush Complete Qt style copied for this run's current hydraulic-grade under-fill, without additional validation or normalization.
     * @note Equality is a no-op; change emits profileStyleChanged() only. Does not mutate another run, the plot-global defaults or profileLineColor().
     */
    void setProfileHglFillBrush(const QBrush &brush);
    /**
     * @param[in] pen Complete Qt style copied for this run's current energy-grade line, without additional validation or normalization.
     * @note Equality is a no-op; change emits profileStyleChanged() only. Does not mutate another run, the plot-global defaults or profileLineColor().
     */
    void setProfileEglLinePen(const QPen &pen);
    /**
     * @param[in] pen Complete Qt style copied for this run's maximum hydraulic-grade envelope line, without additional validation or normalization.
     * @note Equality is a no-op; change emits profileStyleChanged() only. Does not mutate another run, the plot-global defaults or profileLineColor().
     */
    void setProfileMaxHglLinePen(const QPen &pen);
    /**
     * @param[in] brush Complete Qt style copied for this run's maximum hydraulic-grade under-fill, without additional validation or normalization.
     * @note Equality is a no-op; change emits profileStyleChanged() only. Does not mutate another run, the plot-global defaults or profileLineColor().
     */
    void setProfileMaxHglFillBrush(const QBrush &brush);
    /**
     * @param[in] pen Complete Qt style copied for this run's maximum energy-grade envelope line, without additional validation or normalization.
     * @note Equality is a no-op; change emits profileStyleChanged() only. Does not mutate another run, the plot-global defaults or profileLineColor().
     */
    void setProfileMaxEglLinePen(const QPen &pen);

    /*!
     * \brief Writes this layer's per-source profile pens & brushes to the
     *        current QSettings position.  Called from the dialogs' source
     *        persistence loops while QSettings is already positioned at
     *        the matching array index.
     *
     *        Keys written (prefix-free, relative to the current group/array index):
     *          profileHglLinePen, profileHglFillBrush,
     *          profileEglLinePen,
     *          profileMaxHglLinePen, profileMaxHglFillBrush,
     *          profileMaxEglLinePen
     *        Values are QPen / QBrush variants — Qt serialises them via
     *        QDataStream so colour, width, style, and dash pattern all round-trip.
     * @param[in,out] settings Borrowed QSettings positioned at the caller-selected group/array index. Writes six QPen/QBrush variants relative to that position, without entering/leaving groups.
     * @note Qt preserves complete pen/brush variant details; this method performs settings writes but does not call sync(), inspect status or report persistence failure.
     */
    void writeProfileStyle(class QSettings &settings) const;

    /*!
     * \brief Companion of writeProfileStyle.  Reads any of the six keys
     *        present at the current QSettings position and pushes them
     *        through the per-setter, so missing keys leave the layer's
     *        defaults untouched.
     * @param[in,out] settings Borrowed QSettings whose current group/array index supplies the six optional style keys; position is unchanged.
     * @note Missing/nonconvertible values are ignored; each convertible value goes through its typed setter and can emit profileStyleChanged(). Legacy EGL-fill keys are ignored. No atomic batch/rollback or disk write.
     */
    void readProfileStyle(class QSettings &settings);

    /*!
     * \brief Opens the binary results file.
     * \returns true if the file was opened and indexed successfully.
     * @param[in,out] warnings Caller-owned list retained unchanged by current finishOpen(); not cleared.
     * @param[in,out] errors Existing entries retained and an explanatory error appended for missing/unopenable/zero-period completed file.
     * @return True after reader adoption/maps/default-kind installation/period-zero fetch and resultsOpened(); false emits resultsError() and leaves previous reader closed.
     * @note Synchronous GUI-thread operation closes current results before existence checking and can block on file/index reads. Output needs at least one period unless opened through the live path. No automatic scene population or resultsFilePathChanged() is emitted here.
     */
    bool openResults(QList<QString> &warnings, QList<QString> &errors);

    /*!
     * \brief Asynchronous counterpart to \ref openResults. Runs the blocking
     *        swmm_output_open (file + header/index parse) on a worker thread;
     *        the renderer install + period-0 fetch + emits run on the GUI
     *        thread, and \ref resultsOpenFinished(bool) fires on completion.
     * @note Owner-thread entry checks existence first: missing path emits resultsError()/resultsOpenFinished(false) while preserving an existing reader. Otherwise closes current results, worker-opens one new handle, then GUI finishOpen() performs adoption/notifications. No overlap/source-generation guard; serialize calls. Destroyed layer closes orphan handle. Error detail reaches signals, not caller-owned lists.
     */
    void openResultsAsync();

    /*!
     * \brief Open a results file the engine is still writing (live tail).
     *
     * Uses swmm_output_open_live: only the header needs to be on disk; the
     * period count comes from the file size and grows via \ref refreshLive.
     * Returns false WITHOUT emitting resultsError when the header is not
     * there yet — the caller retries on the next progress tick. Zero
     * periods is a valid live state (totalTimeSteps() == 0 until the first
     * report step is flushed).
     * @param[in,out] errors Retained list receiving finishOpen() failures, but missing file/unreadable-yet header returns false without appending.
     * @return True after adopting live-capable reader, including zero periods. False closes previous results; early readiness failure emits no resultsError().
     * @note GUI-thread call; finishOpen() notifications/default renderers still occur on success. Header readiness retries belong to caller; internal reader growth requires refreshLive().
     */
    bool openResultsLive(QList<QString> &errors);

    /*!
     * \brief Re-count periods on a live file. Grows totalTimeSteps(), extends
     *        endDateTime(), drops whole-file aggregate caches, and emits
     *        totalTimeStepsChanged + \ref periodsAppended when new periods
     *        appeared. When the writer's closing records have landed the
     *        layer leaves live mode and emits \ref resultsFinalized.
     * \returns the current period count (unchanged when not live).
     * @return Current stored period count, unchanged for closed/nonlive/refresh failure.
     * @note GUI-thread/serialized reader access. Only growth updates count/end/report origin and clears whole-run ranges/statistics; first data fetches stepzero, marks structural and emits current step/time. Emits totalTimeStepsChanged()/periodsAppended() on growth, then resultsFinalized() if footer detected. It neither follows the newest step automatically nor emits repaintRequested() directly; count decreases are ignored. Inflight full-run range scans are not cancelled here.
     */
    int refreshLive();

    /*! \brief True between openResultsLive() and the footer's arrival.
     * @return Stored live-mode flag, cleared by footer detection/close; not an immediate filesystem/footer probe.
     */
    [[nodiscard]] bool isLive() const noexcept { return m_live; }

    /**
     * @brief Close reader/LID frame source and reset period/current-step/name/value/range/statistics caches.
     * @note GUI-thread serialized operation; emits no closed/count/time/repaint signal and does not delete scene items or reset stored datetime/path/profile/style/renderer/rule/override state. Inflight asynchronous opens/range scans require caller lifecycle coordination. Borrowed reader handle becomes invalid.
     */
    void closeResults();

    // Same immutable frame source for live tail and saved profile playback.
    /**
     * @param[in] time Requested frame datetime in the LID reader's run clock.
     * @param[in] nodes Node identity list to resolve through the shared sidecar frame reader.
     * @return Owned immutable LID state frame from resultsFilePath()/isLive(), or a status-only No open results source frame when closed. Reader caches may mutate; serialize with other calls/close.
     */
    openswmmvis::io::LidStateFrame lidStateAt(const QDateTime &time, const QStringList &nodes);

    // ----- Per-output node summary statistics (Slice QA.3 + QA-01) -------
    //
    // These four accessors return the same statistics the editing-engine
    // exposes via `swmm_node_get_stat_*`, but sourced from THIS output
    // file rather than the editing engine's ambient state. The node
    // attribute panel (Slice QA.2) dispatches to one of these when the
    // user picks a non-null "Stats source" from the combo, so multiple
    // loaded SWMMResultsLayers can be compared in the same panel.
    //
    // Backed by engine API QA-01 (`swmm_output_get_node_stat_*` in
    // openswmm_output.h), which aggregates the four flooding stats
    // on-demand from the .out file's per-period node results. The
    // aggregation runs at REPORT-step resolution — see the QA-01
    // header doc for the precision caveat when the routing step is
    // much finer than the report step.
    //
    // \p nodeName is the SWMM model-side identifier (e.g. "J1"); the
    // implementation resolves it to the output-side index via the
    // already-built nodeOutputIndex() map. Returns 0.0 for unknown
    // names or when the underlying file is closed.
    //
    // Each aggregator is one O(n_periods) walk of the .out file, so all
    // four are memoised per node index (see m_nodeStats / nodeStatsFor,
    // cleared by closeResults()) — same reason and same shape as the
    // link / subcatchment caches below.
    /**
     * @param[in] nodeName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Maximum reported node water depth in file length units (feet or metres). Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double nodeStatMaxDepth(const QString &nodeName) const;
    /**
     * @param[in] nodeName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Maximum reported overflow rate in file flow units. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double nodeStatMaxOverflow(const QString &nodeName) const;
    /**
     * @param[in] nodeName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Flooded volume integrated by the engine output aggregator in file volume units (ft³/m³). Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double nodeStatVolFlooded(const QString &nodeName) const;
    /**
     * @param[in] nodeName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Flooded duration in hours, converted from the engine aggregator's seconds. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double nodeStatTimeFlooded(const QString &nodeName) const;

    // ----- Per-output link / subcatchment summary statistics -------------
    //
    // Peers of the four node accessors above, for the dynamics columns the
    // Attribute Table shows for links and subcatchments. Unlike the node
    // stats there is no `swmm_output_get_*_stat_*` engine API for these, so
    // they are aggregated HERE by walking the .out file's per-period series
    // once per object. The result is cached per output index (see
    // m_linkStats / m_subcatchStats, cleared by closeResults()), as are the
    // node stats above.
    //
    // PRECISION CAVEAT — identical in kind to the QA-01 node aggregators:
    // the walk sees REPORT steps, not routing steps. Maxima are therefore
    // the largest REPORTED value (a peak between two report times is
    // missed), and the volume / duration integrals use the report step as
    // dt. Expect small disagreement with the .rpt summary tables when
    // REPORT_STEP is much coarser than ROUTING_STEP; they converge as the
    // two approach each other.
    //
    // Units match the editing-engine getters they stand in for: flow and
    // velocity in project flow / velocity units, filling dimensionless,
    // volumes in project volume units, durations in hours. Volume is the
    // one that needs work — the .out file stores a RATE in the file's own
    // flow units, so the integral goes through Qcf/Ucf (see the .cpp).
    //
    // The three pump statistics reconstruct too, from a combination that is
    // not obvious: the engine's on/off test is `setting > 0 && flow > 0`, and
    // the .out file carries BOTH — flow directly, and `setting` as the
    // CAPACITY variable, which for every non-conduit link is the pump speed /
    // regulator opening rather than a fill ratio (legacy link.c). See the
    // extra accuracy note on linkStatPumpCycles.
    /**
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Maximum absolute reported link flow in file flow units. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatMaxFlow(const QString &linkName) const;
    /**
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Maximum absolute reported velocity in file velocity units (ft/s or m/s). Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatMaxVelocity(const QString &linkName) const;
    /**
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Maximum reported depth divided by the current model resolved full depth, dimensionless. Zero if no usable full depth or link is a pump. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatMaxFilling(const QString &linkName) const;
    /**
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Rectangle-rule sum of absolute reported flow times report-step, converted to file volume units (ft³/m³). Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatVolFlow(const QString &linkName) const;
    /**
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Reported duration at depth/full-depth>=one, in hours; requires current model full depth and is zero for pumps. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatSurchargeTime(const QString &linkName) const;

    /*! Pump start-up count.
     *
     *  ACCURACY: this is the statistic the report-step caveat hurts most, and
     *  it is a LOWER BOUND rather than an approximation with a small error
     *  bar. A pump that switches off and back on entirely between two report
     *  times leaves no trace in the .out file, so that cycle is invisible.
     *  Validated at REPORT_STEP 60 s / ROUTING_STEP 0.5 s: four of five pumps
     *  counted exactly, the fifth read 2 against the report's 3. On-time and
     *  volume pumped degrade gracefully over the same run (within 0.3 % and
     *  0.04 %) because a brief outage barely moves an integral.
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Count of reported off-to-on transitions for current-model pump links, using setting>zero and absolute flow>zero. Zero for other types/unavailable series; sub-report cycles are invisible. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatPumpCycles(const QString &linkName) const;
    /*! Pump on-time in HOURS — the Attribute Table column's unit. Note the
     *  editing-engine getter it stands in for reports SECONDS; the model's
     *  engine path wraps that in a conversion, and this path bypasses it.
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Pump on-report-count times report-step in hours; current model must identify the link as a pump. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatPumpOnTime(const QString &linkName) const;
    /**
     * @param[in] linkName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Absolute flow integral during reconstructed pump-on reports, in file volume units; zero without pump metadata/series. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double linkStatPumpVolume(const QString &linkName) const;

    /**
     * @param[in] subName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Rainfall-rate integral times current model subcatchment area, converted to file volume units; zero when area cannot be resolved. Area conversion assumes model/file unit system agreement. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double subcatchStatPrecip(const QString &subName) const;
    /**
     * @param[in] subName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Signed reported runoff-rate sum times report-step in file volume units; negative samples are not forced positive. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double subcatchStatRunoffVol(const QString &subName) const;
    /**
     * @param[in] subName Exact case-sensitive object name resolved through this output's cached ID map.
     * @return Maximum positive reported runoff in file flow units; accumulator starts zero. Unknown name/closed source/failed contributing getter yields zero for the unavailable statistic, which is indistinguishable from a true zero.
     * @note Lazy per-output-index memoization can read complete series on first access; serialize with all other reader/cache users. Report-step extrema/integrals need not match routing-step report summaries. Caches clear on close/live growth; current-model topology/units/area changes alone do not invalidate them.
     */
    [[nodiscard]] double subcatchStatMaxRunoff(const QString &subName) const;

    // ----- Animation ------------------------------------------------------

    /**
     * @return Stored zero-based current report ordinal, initially/resetzero; countzero means it is not a readable frame.
     */
    [[nodiscard]] int currentTimeStep() const;
    /**
     * @return Current period time converted through SWMM datetime convention; closed/zero-period source returns stored simulation start. Reader time-getter status is not checked.
     */
    [[nodiscard]] QDateTime currentDateTime() const;
    /**
     * @return Stored known period count, grown only by explicit live refresh; zero after close.
     */
    [[nodiscard]] int totalTimeSteps() const;
    /**
     * @return Stored simulation/header start datetime, distinct from first report; retained after close.
     */
    [[nodiscard]] QDateTime startDateTime() const;
    /**
     * @return Stored last-known report datetime, retained after close and updated on live growth.
     */
    [[nodiscard]] QDateTime endDateTime() const;
    // 2026-07-19 — reported data origin (slider range fix). startDateTime()
    // is the SIMULATION start (`swmm_output_get_start_date`), but the first
    // data frame lives at period 0's report time = reportStart + reportStep.
    // Anchoring the animation range / period grid on the simulation start
    // produced a dead zone at the left of the global slider whenever
    // REPORT_START != START_DATE (and an off-by-one in
    // periodIndexForDateTime). Falls back to startDateTime() when period 0's
    // time is unavailable.
    /**
     * @return Stored valid period-zero datetime, falling back to simulation start when invalid; zero-period live adoption temporarily uses header start.
     */
    [[nodiscard]] QDateTime reportedStartDateTime() const;
    /**
     * @return Stored report interval seconds, zero when closed; not routing timestep.
     */
    [[nodiscard]] int reportStepSeconds() const;

    /*!
     * \brief Seeks to the given 0-based time step.
     * @param[in] step Requested ordinal clamped to available[0,count-1]; no periods is a no-op.
     * @note Same ordinal is a no-op only when legacy node-results vector is nonempty. Otherwise fetches needed frame attributes/overrides, escalates Values dirtiness and emits currentTimeStepChanged()/currentDateTimeChanged()/repaintRequested(). Per-frame classification is connected to the step signal; source reads are synchronous.
     */
    void setCurrentTimeStep(int step);

    /*!
     * \brief Returns the period index closest to \p dt on this layer's time grid.
     * \details Used by AnimationController to snap secondary layers to the
     *          primary layer's current time.  Result is clamped to [0, totalTimeSteps-1].
     * @param[in] dt Valid requested datetime; offsets must fit int after period conversion.
     * @return Nearest report-grid period, with ties choosing the later frame, clamped to the
     * available range. Fractional intervals whose legacy header step is zero use a binary
     * search of recorded millisecond timestamps. Other intervals use reportedStartDateTime().
     * Returns zero when timing is unavailable; does not mutate the current frame.
     */
    [[nodiscard]] int periodIndexForDateTime(const QDateTime &dt) const;

    /*!
     * \brief Causal peer of \ref periodIndexForDateTime() : the latest period at
     *        or before \p dt (floor instead of nearest), clamped to
     *        [0, totalTimeSteps-1]. Used by AnimationController so a secondary
     *        output never displays a frame ahead of the animation cursor.
     * @param[in] dt Valid requested datetime; offsets must fit int after period conversion.
     * @return Latest report period at/before the request, clamped to the available range.
     * Fractional intervals whose legacy header step is zero search recorded millisecond
     * timestamps; other intervals use reportedStartDateTime() and floor with 1e-6 period
     * tolerance. Returns zero when timing is unavailable; does not mutate the current frame.
     */
    [[nodiscard]] int periodIndexForDateTimeAsOf(const QDateTime &dt) const;

    /*!
     * \brief Advances to the next time step (wraps at end if \p loop is true).
     * @param[in] loop Whether crossing the available endpoint wraps to the opposite endpoint; false clamps at endpoint.
     * @note No periods is a no-op; otherwise passes the next ordinal to setCurrentTimeStep(), inheriting its fetch/no-op/signals.
     */
    void stepForward(bool loop = false);

    /*!
     * \brief Goes back one time step (wraps at start if \p loop is true).
     * @param[in] loop Whether crossing the available endpoint wraps to the opposite endpoint; false clamps at endpoint.
     * @note No periods is a no-op; otherwise passes the previous ordinal to setCurrentTimeStep(), inheriting its fetch/no-op/signals.
     */
    void stepBackward(bool loop = false);

    // ----- Variable & colour mapping --------------------------------------

    /**
     * @return Stored legacy layer-level variable enum, initially NodeDepth; per-kind renderers/substyles can select independent attributes.
     */
    [[nodiscard]] SWMMResultVariable variable() const;
    /**
     * @param[in] var New enum/raw-cast variable, stored without validity enforcement; equality is a no-op.
     * @note Retargets graduated per-kind renderers in the matching scope to the enum-name attribute and clears their breaks, retaining other scopes/custom renderer types. Clears legacy vectors, fetches current needed values, marks Structural and emits variableChanged()/repaintRequested(). Does not automatically restretch the legacy ramp or notify rendererChanged().
     */
    void setVariable(SWMMResultVariable var);

    /**
     * @return Owned legacy fallback ramp value; active per-kind graduated renderers retain their own classified ranges.
     */
    [[nodiscard]] RasterColorRamp colorRamp() const;
    /**
     * @param[in] ramp Complete fallback ramp copied without validation/equality suppression.
     * @note Applies stops/interpolation to every graduated kind renderer while retaining each renderer's own min/max, rebuilds overrides, marks Values and requests repaint. No dedicated rampChanged()/rendererChanged() signal.
     */
    void setColorRamp(const RasterColorRamp &ramp);

    /*!
     * \brief Automatically sets the colour ramp range from the data min/max
     *        across all time steps for the current variable.
     * @note No-op when closed/no periods/unsupported variable. Otherwise captures path/variable/count and schedules an independent-reader scan; guarded GUI completion updates only legacy ramp min/max then requests repaint. It does not retarget/reclassify category renderers, rebuild current overrides or mark scene dirty. No generation guard prevents older variable/source scan completion overwriting the current ramp.
     */
    void autoStretchColorRamp();

    // ----- Renderer (Slice BI Phase 8.13.6.3) -----------------------------
    // The renderer is the §J.2 seam every future paint path will go through.
    // In sub-phase 8.13.6.3 this is API plumbing only — the existing paint
    // loop still reads m_colorRamp / m_variable directly. Sub-phase 8.13.6.4
    // will swap the paint loop to consult m_renderer instead, after which
    // m_colorRamp / m_variable become private implementation details of the
    // default GraduatedRenderer.

    /*!
     * \brief The IFeatureRenderer that will drive this layer's paint pass.
     * \details Constructed eagerly as a default GraduatedRenderer so callers
     *          never have to null-check.  Owned by the layer; do not delete.
     *          Returned non-const so the caller may classify / configure the
     *          renderer in place (mutations the layer should know about should
     *          go via setRenderer() to trigger \ref rendererChanged()).
     * @return Borrowed owned layer-level fallback renderer, eagerly nonnull; invalidated by replacement/destruction. Per-kind renderers and legacy ramp paths drive current feature overrides independently.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *renderer() const;

    /*!
     * \brief Replaces the current renderer.
     * \details The layer takes ownership.  A null pointer is rejected (the
     *          method silently no-ops) so renderer() never returns nullptr.
     *          Emits \ref rendererChanged() when the renderer pointer actually
     *          changes.
     * @param[in] r Fresh exclusively owned renderer; null is ignored. Never wrap the already owned raw pointer.
     * @note Replaces only layer-level renderer, marks Structural and emits rendererChanged() without repaintRequested(). Does not install/replace category renderer slots or force a frame fetch; caller arranges later repaint.
     */
    void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    // ----- Per-kind renderers (Slice OUT.1) -------------------------------
    // Mirrors SWMMModelLayer's per-kind renderer slots so the user can
    // style Junctions vs Outfalls (etc.) independently for the same
    // NodeDepth/LinkFlow variable. The layer-level renderer() above stays
    // as the fallback when no kind-renderer is installed.

    /*!
     * \brief Returns the per-kind renderer for category \p c, or nullptr
     *        when none has been installed yet (callers should fall back
     *        to the layer-level renderer()).
     *
     *        The kind ordinals match SWMMModelLayer::Category (11
     *        entries: 4 node kinds + 5 link kinds + Subcatchments +
     *        RainGages — though RainGages doesn't carry results today).
     */
    /**
     * @param[in] c Category ordinal selecting the independent renderer slot.
     * @return Borrowed owned slot renderer, null for out-of-range/uninstalled; invalidated by slot replacement/destruction. No lazy installation occurs here.
     */
    [[nodiscard]] OpenSWMM::Render::IFeatureRenderer *
    kindRenderer(SWMMModelLayer::Category c) const;

    /*!
     * \brief Installs (or replaces) the per-kind renderer for category
     *        \p c. The layer takes ownership. A null pointer clears the
     *        slot (paint reverts to the layer-level renderer). Emits
     *        \ref rendererChanged() when the slot pointer changes.
     * @param[in] c Valid category ordinal; invalid discards/destroys the supplied owned renderer.
     * @param[in] r Fresh exclusively owned renderer or null to clear. Never wrap an already owned slot pointer.
     * @note GUI-thread change migrates graduated size axes into sublayer channels, replaces slot, rebuilds overrides, refreshes an existing rule mirror unless propagating from it, marks Structural and emits rendererChanged()/repaintRequested(). Does not fetch any newly required result field immediately; next frame/style fetch supplies it.
     */
    void setKindRenderer(SWMMModelLayer::Category c,
                         std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r);

    /*!
     * \brief Restores the per-kind slot for \p c to its compile-time
     *        default (GraduatedRenderer keyed to the scope's primary
     *        result variable, Viridis ramp, EqualInterval 5-bin).
     * @param[in] c Category selecting an archetype-seeded graduated dynamic FixedOverRun default, primary variable per scope.
     * @note Delegates setKindRenderer() with its migration/cache/mirror/signals; invalid ordinal is rejected there. Palette default is viridis/five equal intervals from the shared factory.
     */
    void resetKindRendererToDefaults(SWMMModelLayer::Category c);

    /*!
     * \brief True when the installed kind renderer for \p c is semantically
     *        identical to the compile-time default — ignoring derived state
     *        (classification breaks, sampled ramp range). Gap A2.1 installs
     *        defaults eagerly on openResults(); the project serializer uses
     *        this to elide untouched slots so .oswp files stay minimal.
     * @param[in] c Category slot; empty/invalid slot counts as default.
     * @return Renderer ID and normalized JSON equality with a fresh compile-time factory default, ignoring lastBreaks and sampled ramp min/max. Does not compare numerical output or mutate the live renderer.
     */
    [[nodiscard]] bool kindRendererIsDefault(SWMMModelLayer::Category c) const;

    /*!
     * \brief Re-clones the live kind renderer for \p c into the Rule
     *        mirror (no-op when the rule list hasn't been built). Dialog
     *        editors read the mirror; call this before mounting one so it
     *        reflects in-place mutations (variable retargeting,
     *        re-classification, per-frame rebin) the setKindRenderer sync
     *        hook cannot observe.
     * @param[in] c Category whose live nonnull renderer is cloned.
     * @note No-op without built list/valid slot. Finds first rule by kind name and replaces its renderer under QSignalBlocker, preventing back-propagation. Does not create a list, handle cleared slots or emit repaint; old borrowed rule-renderer pointers are invalidated.
     */
    void refreshRuleMirror(SWMMModelLayer::Category c);

    // ----- Gap B1 — legend-as-editor facade (mirrors SWMMModelLayer) -----
    //
    // The legend views read rows through legendSymbolItems() and route
    // per-class edits through the facade below. Class keys are
    // kind-qualified ("<kindKey>\x1F<innerKey>") so edits land on the
    // right kind renderer even when two kinds share inner keys.

    /**
     * @return Feature sublayerLegendItems() plus visible saved-trace analysis legends in paint order, by value. Can schedule lazy range scans; no renderer/style ownership escapes.
     */
    [[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem> legendSymbolItems();
    /**
     * @param[in] kind Requested renderer class-edit capability.
     * @return True if any installed category renderer supports it, regardless of current sublayer visibility; layer-level fallback renderer is not consulted.
     */
    [[nodiscard]] bool supportsClassEdit(OpenSWMM::Render::ClassEditKind kind) const;
    /**
     * @param[in] classKey Exact kindKey + U+001F separator + renderer-specific inner class key.
     * @return Renderer class colour, invalid QColor for malformed kind key/missing slot or renderer-specific miss.
     */
    [[nodiscard]] QColor colorForClass(const QString &classKey) const;
    /**
     * @param[in] classKey Kind-qualified renderer class identity.
     * @param[in] color Colour forwarded without layer-level validity/equality/capability checks.
     * @note Malformed key/missing slot is a no-op; otherwise renderer handles inner key, overrides rebuild, Values dirtiness/repaint requested even if edit had no effect. Does not refresh rule mirror or emit rendererChanged().
     */
    void setColorForClass(const QString &classKey, const QColor &color);
    /**
     * @param[in] classKey Exact kindKey + U+001F separator + renderer-specific inner class key.
     * @return Renderer class size in logical pixels, minus one for malformed kind key/missing slot or renderer-specific absence.
     */
    [[nodiscard]] qreal sizeForClass(const QString &classKey) const;

    // ----- Rule Model (Slice B.5, Phase B) --------------------------------
    //
    // Mirrors SWMMModelLayer::ruleList() — 11 kindRenderer slots → 11
    // Rules, lazy-built on first access. Each Rule owns a clone of the
    // matching kindRenderer; Rule-side swaps propagate back via
    // setKindRenderer. Slot replacement refreshes an existing mirror; in-place renderer
    // mutations require refreshRuleMirror() before mounting an editor.
    /**
     * @return Borrowed layer-owned rule mirror, built lazily even through const access; GUI-thread use. Initially one cloned renderer per category, SingleSymbol fallback for empty slot. Rule replacements propagate into kind slots; in-place kind mutations require refreshRuleMirror().
     */
    [[nodiscard]] OpenSWMM::Render::RuleList *ruleList() override;
    /**
     * @return Borrowed layer-owned rule mirror, built lazily even through const access; GUI-thread use. Initially one cloned renderer per category, SingleSymbol fallback for empty slot. Rule replacements propagate into kind slots; in-place kind mutations require refreshRuleMirror().
     */
    [[nodiscard]] const OpenSWMM::Render::RuleList *ruleList() const override;

    // ----- IAttributeProvider (Slice DM.1) --------------------------------
    //
    // Returns the engine output codes available for the given category.
    // All entries are isDynamic=true (per-frame values). Drives the
    // attribute combo in the Graduated / Categorized renderer panels —
    // see RENDERING_DIALOG_DEMO_PLAN.md §2.
    /**
     * @param[in] cat Category selecting node/link/subcatch hydraulic fields plus run species; gages/unsupported categories return empty.
     * @return Owned dynamic Double descriptors with canonical short field keys. Labels currently use fixed SI-looking hydraulic unit strings and mg/L mass-species fallback even for US-unit files; this metadata accessor does not convert actual native output values. Age/temperature use reserved species labels.
     */
    [[nodiscard]] QVector<OpenSWMM::Render::AttributeField>
    availableAttributes(OpenSWMMVis::SwmmCategory cat) const override;

    /*! \brief Y2a — the loaded run's species names, in `.out` order
     *         (pollutants plus the reserved age/temperature columns).
     *  \details Empty when no results are open or the run carries no
     *           quality. Read live from the reader rather than cached: the
     *           handle is reopened on reload and a stale list would
     *           repoint every species theme by a slot.
     * @return Owned nonempty UTF-8 pollutant/reserved-species IDs in current reader order, empty when closed/no quality. Null/empty IDs are skipped rather than preserving a slot placeholder. Serialized reader access required; no cache retained.
     */
    [[nodiscard]] QStringList speciesNames() const;

    // ----- Legend ---------------------------------------------------------

    /**
     * @return Stored legend-display request flag, initially true; actual legend rendering is external.
     */
    [[nodiscard]] bool showLegend() const;
    /**
     * @param[in] show New external legend-display request.
     * @note Equality is a no-op; change emits showLegendChanged()/repaintRequested() without modifying legend row contents or scene geometry.
     */
    void setShowLegend(bool show);

    // ----- Bulk output access (Slice BC) ----------------------------------

    /*!
     * \brief Raw `SWMM_Output` handle to the open `.out` file, or nullptr
     *        when results aren't open yet.  Exposed so Slice BC's profile
     *        plot can call `swmm_output_get_node_series` /
     *        `swmm_output_get_link_series` for bulk time-series fetches
     *        without a per-period round-trip.
     * @return Borrowed mutable reader handle, invalidated by close/reopen/destruction; callers must not close it and must serialize access with frame fetch/live refresh/statistics. No automatic locking/lifetime lease is acquired.
     */
    [[nodiscard]] SWMM_Output outputHandle() const;

    /*!
     * \brief Output-file index for the named node, or -1 if not found.
     *        The output file uses its own indexing scheme (a name-keyed
     *        order) that may differ from the engine's node index, so
     *        callers must resolve via name.
     * @param[in] name Exact case-sensitive output object identity.
     * @return Output-file global ordinal from the cached name map or minus one on miss/closed source. Do not substitute a model/category row; no source IO occurs.
     */
    [[nodiscard]] int nodeOutputIndex(const QString &name) const;

    /*!
     * \brief Output-file index for the named link, or -1.
     * @param[in] name Exact case-sensitive output object identity.
     * @return Output-file global ordinal from the cached name map or minus one on miss/closed source. Do not substitute a model/category row; no source IO occurs.
     */
    [[nodiscard]] int linkOutputIndex(const QString &name) const;

    /*!
     * \brief Output-file index for the named subcatchment, or -1.
     *        Mirrors \ref nodeOutputIndex / \ref linkOutputIndex; added in Slice BL
     *        so the Comparison Plot Dialog can read subcatchment series.
     * @param[in] name Exact case-sensitive output object identity.
     * @return Output-file global ordinal from the cached name map or minus one on miss/closed source. Do not substitute a model/category row; no source IO occurs.
     */
    [[nodiscard]] int subcatchOutputIndex(const QString &name) const;

    /*!
     * \brief Engine-reported flow units (0=CFS, 1=GPM, 2=MGD, 3=CMS, 4=LPS,
     *        5=MLD). -1 when results aren't open. Used by the Comparison
     *        Plot Dialog to label axes in US vs SI.
     * @return Current file flow-unit code, or minus one when closed; native sample units are not converted by this accessor.
     */
    [[nodiscard]] int flowUnits() const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    /**
     * @brief Append graphics items from current model geometry and saved result caches.
     * @param[in] scene Required stable destination scene whenever an item can be added; scene owns created/tagged items until depopulation.
     * @param[in] canvasExtent Ignored by network population; forwarded to trace analysis population.
     * @param[in] canvasSRS Ignored by network population (no transform applied here); forwarded to trace analysis. Model cache coordinates must match expected scene map frame.
     * @note GUI-thread operation skips hidden/transparent/unbound-model state. Analysis parts populate before output-handle/period prerequisite; network then resets item caches and appends visible categories without deleting prior items. Caller must depopulate first to avoid duplicates/dangling caches.
     * @note Colour priority is kind override then sampled legacy ramp then static style; invalid/missing themed rows can be omitted. Points ignore view transforms, lines use cosmetic pens, polygons honour patterned fill on population, arrows use signed flow midpoint and full-run magnitude scale. Applies shared model selection and labels afterward. No source frame fetch occurs here.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] scene Optional destination passed to analysis/base deletion; base handles tagged ownership.
     * @note Removes tagged network/label and trace items, clears all cached raw item/label pointers. Does not reset scene-dirty state, output values or style. GUI thread only.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /*! L-1 — a label-config change (enable / font / placement) only affects
     *  the label items, but refreshScene early-returns when the scene is
     *  Clean. Escalate to Values so the next refresh runs restyleScene →
     *  refreshLabels and the change is reflected without an animation tick.
     * @param[in] cfg Complete layer fallback labels policy copied through base setter.
     * @note Newly enabled/changed field/expression can synchronously refetch needed frame values. Always escalates Values, even if equal config; base emits label/repaint only for effective change. Sublayer enabled config takes precedence when labels refresh.
     */
    void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg) override;

    /**
     * @param[in] newCanvasSRS Optional borrowed target CRS read synchronously to replace owned transform.
     * @note Destroys old transform first. Missing/invalid/equivalent CRS returns before marking scene Structural; only the distinct usable-CRS path escalates. This transform is currently not applied by feature population, which consumes model cache coordinates; trace analysis handles its own conversion. No repaint signal.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- ISublayerHost interface (Slice U-0) ------------------------------
    //
    // Returns the granular per-Category sublayer mix (paint order = list
    // order, bottom-up). Order matches SWMMModelLayer::Category but groups
    // by archetype so polygon fills paint first, then lines, then point
    // markers, then rain gages on top:
    //
    //   [0]  Subcatchments        (FillKind,    polygon archetype, dynamic)
    //   [1]  Conduits             (LineKind,    line archetype,    dynamic)
    //   [2]  Pumps                (LineKind,    line archetype,    dynamic)
    //   [3]  Orifices             (LineKind,    line archetype,    dynamic)
    //   [4]  Weirs                (LineKind,    line archetype,    dynamic)
    //   [5]  Outlets              (LineKind,    line archetype,    dynamic)
    //   [6]  Junctions            (MarkerKind,  point archetype,   dynamic)
    //   [7]  Outfalls             (MarkerKind,  point archetype,   dynamic)
    //   [8]  Storage              (MarkerKind,  point archetype,   dynamic)
    //   [9]  Dividers             (MarkerKind,  point archetype,   dynamic)
    //   [10] RainGages            (MarkerKind,  point archetype,   static)
    //
    // The sublayer instances are owned by this layer via QObject parent-
    // child.
    /**
     * @return Owned pointer-list copy of borrowed QObject-owned parts in bottom-up paint order, lazily initialized to polygon/links/nodes/gages unless traceStoredRunId dynamic property is nonempty. Additional analysis parts append to that list.
     * @note Const method mutates lazy order, so serialize with edits/painting; callers must not delete borrowed parts.
     */
    [[nodiscard]] QList<OpenSWMM::Render::ISublayer *> sublayers() const override;
    /**
     * @param[in] sub Optional new analysis part; null/already listed is a no-op. Must not remain listed by another host when transferring ownership.
     * @note Seeds category order, reparents this QObject, appends, connects visibility/invalidation to structural repaint, emits childrenChanged()/repaintRequested(). Layer owns it afterward; invalidation/visibility connections can produce repeated requests.
     */
    void addAnalysisSublayer(OpenSWMM::Render::ISublayer *sub);
    /**
     * @param[in] sub Borrowed part removed only when present in paint order.
     * @note Disconnects both directions, marks structural, emits childrenChanged()/repaintRequested(). Despite the name, does not clear QObject parent or return ownership; caller must explicitly reparent before retaining beyond this layer.
     */
    void takeAnalysisSublayer(OpenSWMM::Render::ISublayer *sub);

    // 2026-07-19 — animation-tick fast path (slider scrub perf). The base
    // ISublayerHost::dispatchAnimationTick() invalidates every dynamic
    // sublayer; in this layer each ISublayer::invalidated fires the
    // wireRefresh lambda, which re-runs fetchResultsForStep() PER SUBLAYER
    // and escalates SceneDirty::Structural — turning the intended cheap
    // Values-only restyle into ~8 redundant .out fetches plus a full
    // populateScene rebuild on every tick. Animation ticks change only
    // per-feature values, never the item set, so this override routes
    // through setCurrentTimeStep()'s Values path and never touches the
    // sublayer-invalidation (Structural) machinery. wireRefresh keeps its
    // Structural escalation for its real purpose: visibility / style /
    // attribute edits.
    /**
     * @param[in] period Requested animation ordinal, clamped by setCurrentTimeStep() when different.
     * @note No periods is a no-op. Different step takes the normal frame-fetch path; equal step only escalates Values and requests repaint, avoiding per-sublayer refetch/structural invalidation.
     */
    void dispatchAnimationTick(int period) override;

    /*! Reorder sublayers in paint order (bottom-up).  Emits
     *  repaintRequested() on success.  Returns false on out-of-range indices.
     * @param[in] from,to Existing zero-based bottom-up order positions.
     * @return False for invalid/equal positions; true moves one entry and emits repaintRequested(), without directly rebuilding/tagging current scene geometry.
     */
    bool moveSublayer(int from, int to) override;

    /*! Typed convenience accessor by Category. Returns nullptr if the
     *  category has no sublayer (out-of-range Category). */
    /**
     * @param[in] c Category ordinal; out-of-range returns null.
     * @return Borrowed owned category FeatureSublayer, independent of current paint reordering/visibility.
     */
    [[nodiscard]] OpenSWMM::Render::FeatureSublayer *
    featureSublayer(SWMMModelLayer::Category c) const;

    /*! Per-feature marker size / stroke width override for this frame
     *  (negative = no override), by category row.
     * @param[in] c Category selecting the cached output override vector.
     * @param[in] row Zero-based model-category row, not an output-file global index.
     * @return Cached logical-pixel marker diameter, or minus one for invalid category/row/no override. No renderer evaluation or source read occurs.
     */
    [[nodiscard]] double featureSizeOverride(SWMMModelLayer::Category c, int row) const;
    /**
     * @param[in] c Category selecting the cached output override vector.
     * @param[in] row Zero-based model-category row, not an output-file global index.
     * @return Cached logical-pixel stroke width, or minus one for invalid category/row/no override. No renderer evaluation or source read occurs.
     */
    [[nodiscard]] double featureWidthOverride(SWMMModelLayer::Category c, int row) const;

    /*!
     * \brief Phase 8 (2026-05-25) — ramp-aware legend rows.
     *
     *        Replaces the generic ISublayer::legendSymbolItems()
     *        single-swatch output with graduated legend rows that reflect
     *        what populateScene actually paints (5 bins from m_colorRamp,
     *        ranged across each sublayer's sampled attribute range, labelled
     *        with value samples). Used by LegendLayerTreeModel when the
     *        layer is a SWMMResultsLayer; the generic ISublayer aggregator
     *        remains the fallback for other host layers.
     *
     *        Each item carries the sublayerId so the legend dock's
     *        right-click → "Edit Sublayer Style…" path (Phase 5) keeps
     *        working unchanged.
     *
     *        Non-const because it may trigger lazy ensure*AttributeRange()
     *        sampling when first asked for an attribute's legend bins.
     * @return Owned rows for visible feature sublayers in paint order. Installed kind-renderer legends take precedence and keys are kind-qualified; fallback uses five ramp bins or one static swatch, plus enabled size min/max and flow-arrow rows. Analysis parts are skipped by this feature-specific accessor.
     * @note Can lazily schedule full-run range scans and return placeholder ranges immediately; row requests do not mean source pixels/geometry have been repainted yet. No ownership of renderer/style escapes.
     */
    [[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem> sublayerLegendItems();

    /*! Slice U-2 — surface every visible-results sublayer (and the
     *  scenario / variable controls) as styleable subjects for the
     *  unified LayerStyleDialog. */
    /**
     * @return Owned wrappers borrowing category style QObjects grouped by Nodes/Links/Areas/Other plus saved trace analysis styles. Wrappers must not outlive this layer/parts. No scenario/global variable subject is added by the current implementation.
     */
    [[nodiscard]] std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
    styleSubjects() override;

signals:
    /**
     * @param[in] path Declared output path payload; current open/adoption paths do not emit it.
     */
    void resultsFilePathChanged(const QString &path);
    /**
     * @param[in] name New raw scenario text after explicit setter.
     */
    void scenarioNameChanged(const QString &name);
    /**
     * @param[in] color New raw profile identity colour; individual pens remain unchanged.
     */
    void profileLineColorChanged(const QColor &color);
    /**
     * @note Emitted after an effective individual pen/brush setter; settings restore may emit once per changed field. No aggregate coalescing or repaint is performed by those setters.
     */
    void profileStyleChanged(); /*!< Any profile overlay pen/brush changed. */
    /**
     * @param[in] step Stored zero-based ordinal after adoption/first live frame/seek; zero may accompany a zero-period live source.
 */
    void currentTimeStepChanged(int step);
    /**
     * @param[in] dt Datetime following corresponding step/adoption notification.
     */
    void currentDateTimeChanged(const QDateTime &dt);
    /**
     * @param[in] count New count after adoption/live growth; closeResults() resets count silently.
     */
    void totalTimeStepsChanged(int count);
    /*! Live tail: periods [firstNew, firstNew+count) are now readable. Views
     *  that keep incremental state (plot tails) append from here; the
     *  animation range follows totalTimeStepsChanged as before.
     * @param[in] firstNew Previous available count, the first newly readable ordinal.
     * @param[in] count Positive number of newly available periods.
     * @note Emitted after totalTimeStepsChanged() during explicit live refresh; does not automatically seek newest frame.
     */
    void periodsAppended(int firstNew, int count);
    /*! Live tail: the writer's closing records arrived; the file is final.
     * @note Emitted once when refreshLive() detects writer footer and clears stored live flag, including finalization without newly appended periods.
     */
    void resultsFinalized();
    /**
     * @param[in] var Raw newly selected layer-level variable after synchronous fetch/rebuild.
     */
    void variableChanged(SWMMResultVariable var);
    /**
     * @param[in] show New external legend-display flag.
     */
    void showLegendChanged(bool show);
    /**
     * @note Emitted after successful finishOpen() count/step/time notifications, including header-only live state; does not mean scene items were created.
     */
    void resultsOpened();
    /**
     * @param[in] message Missing/open/zero-period finalized-source explanation; live header-not-ready is silent.
     */
    void resultsError(const QString &message);
    /*! \brief Emitted on the GUI thread when \ref openResultsAsync() completes.
     * @param[in] ok Asynchronous open/adoption result, after relevant error/opened notifications.
     */
    void resultsOpenFinished(bool ok);
    /*! \brief Emitted when setRenderer() swaps the renderer pointer.
     * @note Emitted for layer renderer replacement and category slot changes. Category change also requests repaint; layer-level setRenderer() does not. Direct per-class colour edits and per-frame classifications can update without this signal.
     */
    void rendererChanged();

public:
    /*!
     * \brief Slice §Y.2 — incremental scene refresh.
     * \details Branches on m_sceneDirty: ValuesDirty walks the per-feature
     *          item cache and applies setBrush/setPen in place; Structural
     *          falls back to the base class depopulate + populate path.
     *          Clean returns immediately.  The cache is populated during
     *          populateScene and cleared in depopulateScene.
     * @param[in] scene Stable destination scene containing this layer's tracked items.
     * @param[in] canvasExtent Current map extent, forwarded on structural rebuild.
     * @param[in] canvasSRS Optional stable target CRS forwarded on structural rebuild/trace population.
     * @note Clean returns. Values with no visible arrow line calls restyleScene(), otherwise base depopulate/populate rebuild. Sets Clean afterward even if restyle/population prerequisites fail; caller must escalate again for later changes. GUI-thread raw item pointers require no external deletion.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

private:
    /*! Y2b-3 (D-G1 warn-on-miss): one warning per unresolvable species
     *  token per run — reset in finishOpen when a new .out arrives. */
    OpenSWMMVis::Species::SpeciesMissWarner m_speciesMissWarner;

    /*! \brief Shared post-open adoption used by both openResults() (sync) and
     *         openResultsAsync(): reads the header, builds id maps, installs
     *         eager renderers, pre-fetches period 0, and emits. \p msOpen is
     *         the swmm_output_open time (for the load telemetry). Assumes
     *         m_handle is the freshly-opened handle.
     * @param[in] msOpen Elapsed worker/synchronous open milliseconds for telemetry only.
     * @param[in,out] warnings Currently unused, retained unchanged.
     * @param[in,out] errors Receives zero-period finalized-file error without clearing prior messages.
     * @return True after header/maps/default result-bearing renderers/stepzero are prepared; false emits error and closes reader for invalid completed period count.
     * @note Requires freshly assigned m_handle and GUI thread. Allows zero live periods, resets species-miss warning ledger, updates stored times and emits total count/current step/current datetime/resultsOpened() in order. Individual header/time getters are not status-checked. Existing per-kind renderers persist; new default installation can emit renderer/repaint notifications.
     */
    bool finishOpen(qint64 msOpen, QList<QString> &warnings, QList<QString> &errors);

    /**
     * @param[in] step Valid zero-based available report period; closed/out-of-range is a no-op retaining old caches.
     * @note GUI-thread serialized reader access. Collects selected variable, visible sublayer colour/label/expression/arrow fields, installed renderer attributes and enabled size channels. Resolves qual:name against run species, logs first unresolved token per run, drops unneeded per-variable buffers, reads needed full-object vectors and updates legacy mirrors/overrides.
     * @note Reader result statuses are not checked after buffer resize; no failure signal/transactional restore is supplied. Does not set m_currentStep, emit time/repaint or mark scene dirty itself.
     */
    void fetchResultsForStep(int step);
    /**
     * @brief Populate exact UTF-8 output-name maps from the currently adopted reader.
     * @note Requires open stable handle and prior cleared maps; inserts without clearing, so later duplicate IDs overwrite earlier ordinals. Does not validate model topology or emit signals.
     */
    void buildOutputIdMaps();

    /*! Slice §Y.2 — apply current per-feature colour / pen state to the
     *  cached scene items without destroying / recreating them. Called from
     *  refreshScene when m_sceneDirty == ValuesDirty.
     * @param[in] scene Borrowed destination for label refresh; should contain all cached items.
     * @note GUI-thread value-only path recolours existing nonnull items/hides missing values and adjusts line pens, then reapplies selection/labels. Does not recreate missing rows or marker geometry/size, arrow geometry or polygon fill pattern (uses solid brush); incompatible category cache counts are skipped, not automatically escalated. RequiresStructuralRebuildForRestyle() must gate arrows.
     */
    void restyleScene(QGraphicsScene *scene);

    /*! Apply the shared network selection pens and fills to rendered output
     *  items, retaining each output sublayer's marker size and line width.
     * @brief Overlay current-model typed selection pens/brushes onto visible cached output items.
     * @note No-op without selected source elements. Link pen adds selection width to current output width and may override dash; point/polygon selection replaces pen/brush, scaled by sublayer opacity. Call after base style restoration so repeated selection application does not accumulate link halo width. No scene allocation/source mutation/signals.
     */
    void applySelectionStyles();

    /*! L-1 — (re)build / update per-feature labels from labelConfig(). Each
     *  visible feature gets a "name: value unit" text item showing the
     *  current timestep's value for the colouring attribute. Called at the
     *  end of populateScene (structural) and restyleScene (per-frame) so the
     *  text follows the animation. No-op (and clears) when labels are off.
     * @param[in] scene Optional destination; null/unbound model clears all labels.
     * @note GUI thread resolves enabled sublayer config or layer fallback, scale-window gates via owning canvas views and updates labels only for existing visible feature items with matching category counts. Expressions substitute case-insensitive name and exact result tokens, unknown/nonfinite values empty; no arithmetic/evaluation engine.
     * @note Default text adds descriptor unit strings to raw native values without conversion. Explicit field shows value only; expressions retain literal text. Uses label decimal/font/colour/halo settings, anchors at scene-bounds centre plus scene-unit placement offsets, applies layer opacity, and deletes disabled-category labels. No collision layout, source IO or signal occurs here.
     */
    void refreshLabels(QGraphicsScene *scene);
    /**
     * @brief Remove/delete every tracked label from its actual scene and clear label vectors.
     * @note GUI-thread operation assumes cached pointers are still live; base depopulation already clears them to avoid double deletion. Does not change label configurations, source items or emit signals.
     */
    void clearLabels();

    /*! Slice §Y.2 — incremental restyle path can't safely handle line
     *  sublayers with flow arrows on (arrow geometry depends on the flow
     *  value, not just its colour). Returns true when at least one visible
     *  sublayer would need a structural rebuild.
     * @return True if a visible positive-opacity line-category sublayer requests flow arrows, false otherwise. Does not inspect actual flow availability or layer visibility.
     */
    bool requiresStructuralRebuildForRestyle() const;

    // Phase 7 (2026-05-25) — lazy attribute-range samplers. Walk every
    // period of (kind, outCode) once; cache the (min, max) result. Cleared
    // on closeResults. Used by populateScene to build a local
    // RasterColorRamp whose range is data-derived rather than the
    // hardcoded [0, 1] of the layer's m_colorRamp. Returns {0,1} on
    // sampling failure so paint still has a defined ramp.
    /**
     * @param[in] outCode Scope-appropriate output code; negative/closed/zero-period source returns[0,1].
     * @return Cached native-unit full-run min/max or immediate[0,1] placeholder. First uncached code schedules a private-reader background scan, with one pending marker per scope/code.
     * @note GUI-thread entry; completion on application thread inserts range, removes marker, reclassifies matching fixed-over-run kind renderers and requests repaint. Guarded layer lifetime prevents dead-object writes, but no source-generation/live-growth guard prevents an older scan from filling a newer cache. Pending markers are not reset by closeResults().
     */
    QPair<double, double> ensureNodeAttributeRange(int outCode);
    /**
     * @param[in] outCode Scope-appropriate output code; negative/closed/zero-period source returns[0,1].
     * @return Cached native-unit full-run min/max or immediate[0,1] placeholder. First uncached code schedules a private-reader background scan, with one pending marker per scope/code.
     * @note GUI-thread entry; completion on application thread inserts range, removes marker, reclassifies matching fixed-over-run kind renderers and requests repaint. Guarded layer lifetime prevents dead-object writes, but no source-generation/live-growth guard prevents an older scan from filling a newer cache. Pending markers are not reset by closeResults().
     */
    QPair<double, double> ensureLinkAttributeRange(int outCode);
    /**
     * @param[in] outCode Scope-appropriate output code; negative/closed/zero-period source returns[0,1].
     * @return Cached native-unit full-run min/max or immediate[0,1] placeholder. First uncached code schedules a private-reader background scan, with one pending marker per scope/code.
     * @note GUI-thread entry; completion on application thread inserts range, removes marker, reclassifies matching fixed-over-run kind renderers and requests repaint. Guarded layer lifetime prevents dead-object writes, but no source-generation/live-growth guard prevents an older scan from filling a newer cache. Pending markers are not reset by closeResults().
     */
    QPair<double, double> ensureSubcatchAttributeRange(int outCode);

    // Engine output handle ------------------------------------------------
    SWMM_Output m_handle = nullptr;

    // File & scenario metadata --------------------------------------------
    QString m_resultsFilePath;
    QString m_reportFilePath; ///< sibling .rpt of the run (may be empty)
    QString m_scenarioName;
    QColor m_profileLineColor;
    int m_reportStepSec = 0;

    // Per-source profile styling -----------------------------------------
    // Initialised from m_profileLineColor at construction; independently
    // editable thereafter (color changes do NOT clobber them).
    QPen m_profileHglLinePen;
    QBrush m_profileHglFillBrush;
    QPen m_profileEglLinePen;
    QPen m_profileMaxHglLinePen;
    QBrush m_profileMaxHglFillBrush;
    QPen m_profileMaxEglLinePen;

    // Model layer (non-owning) --------------------------------------------
    class SWMMModelLayer *m_modelLayer = nullptr;

    // Animation state -----------------------------------------------------
    int m_currentStep = 0;
    int m_totalSteps = 0;
    openswmmvis::io::LidStateReader m_lidStateReader;
    bool m_live = false; ///< openResultsLive() until the footer lands
    QDateTime m_startDateTime;
    QDateTime m_endDateTime;
    // 2026-07-19 — period 0's report time; see reportedStartDateTime().
    QDateTime m_reportedStartDateTime;

    // Variable & colour mapping -------------------------------------------
    SWMMResultVariable m_variable = SWMMResultVariable::NodeDepth;
    RasterColorRamp m_colorRamp;
    bool m_showLegend = true;

    // Slice BI Phase 8.13.6.3 — renderer plumbing. Initialised eagerly in
    // the ctor (default GraduatedRenderer) so renderer() never returns null.
    // The existing paint loop still reads m_colorRamp / m_variable; 8.13.6.4
    // refactors that to consult m_renderer instead.
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> m_renderer;

    // Slice OUT.1 — per-kind renderer slots. Indexed by
    // SWMMModelLayer::Category ordinal; slots default to nullptr (no
    // override) so paint can fall back to m_renderer. Slice OUT.2 hooks
    // the per-feature override cache into these and refactors the paint
    // loop in populateScene() to consult them.
    std::vector<std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>> m_kindRenderers;

    // Slice B.5 — RuleList mirroring m_kindRenderers (lazy-built).
    mutable std::unique_ptr<OpenSWMM::Render::RuleList> m_ruleList;
    /*! Re-entrancy guard for the kind ↔ Rule mirror sync: set while a
     *  Rule-side renderer swap is being propagated INTO setKindRenderer so
     *  the mirror-refresh there doesn't bounce back into the Rule. */
    bool m_ruleKindSyncGuard = false;
    /**
     * @brief Create owned per-category rule clones and attach replacement/repopulation hooks.
     * @note Caller invokes only when no list exists; initializes all categories by name, including empty slots as SingleSymbol. Const method mutates QObject state via owner, so GUI-thread only. No per-frame signal connection is installed here; constructor already owns that wiring.
     */
    void buildRuleListLazy() const;
    // Feedback C1 — Rule → kind plumbing shared by the lazy build and by
    // lists repopulated in place (Style Manager "Apply to layer").
    /**
     * @param[in] rule Optional owned-list rule; null/already marked kindAttached is a no-op.
     * @param[in] c Category captured for replacement back-propagation.
     * @note Marks a dynamic property and connects rendererReplaced() to applyRuleToKind(); hook does not itself install current contents or validate rule name/category agreement.
     */
    void attachRuleToKind(OpenSWMM::Render::Rule *rule, SWMMModelLayer::Category c);
    /**
     * @param[in] rule Optional stable rule; null returns.
     * @param[in] c Target category.
     * @note Clones any renderer through guarded setKindRenderer(). For a SingleSymbol first layer also projects supported marker/line/polygon fields into its FeatureSublayerStyle; shapes outside legacy0..4 fall back Circle. Additional layers/richer unsupported props are not projected. Style setters can emit additional fetch/structural repaint signals.
     */
    void applyRuleToKind(OpenSWMM::Render::Rule *rule, SWMMModelLayer::Category c);
    /**
     * @brief Attach/apply previously unattached rules whose exact names match category kind keys.
     * @note Null list is a no-op; unknown names ignored and already attached rules skipped. Supports in-place library reload without deleting canonical category slots for omitted rules.
     */
    void onRuleListRepopulated();

    // Slice Z.7a — per-frame rebinning for Rules with rebinPerFrame=true.
    // Connected to currentTimeStepChanged in buildRuleListLazy.
    /**
     * @brief Reclassify installed PerFrameAutoStretch graduated renderers from currently cached full-scope frame samples.
     * @note Reads species-aware attributes and skips missing/empty vectors; rebuilds that kind's overrides. Drives canonical slots, not lazy rule mirror. Does not emit repaint, mark dirty or fetch source values itself; invoked by currentTimeStepChanged().
     */
    void rebinDynamicRulesIfNeeded();

    // Slice OUT.2 — per-feature override cache. Sized to category row
    // count at rebuild time. Read by populateScene's paint loop in
    // preference to m_colorRamp.colorForValue(). Rebuilt whenever
    // result values change (setVariable / setCurrentTimeStep /
    // animation tick) OR when setKindRenderer() swaps a slot.
    QVector<QColor> m_kindFeatureColors[SWMMModelLayer::NumCategories];
    QVector<double>
        m_kindFeatureSizes[SWMMModelLayer::NumCategories]; /*!< negative = no override */
    QVector<double>
        m_kindFeatureWidths[SWMMModelLayer::NumCategories];          /*!< negative = no override */
    QVector<int> m_kindFeatureShapes[SWMMModelLayer::NumCategories]; /*!< -1 = no override */
    QVector<int> m_kindFeatureDashes[SWMMModelLayer::NumCategories]; /*!< -1 = no override */
    bool m_kindUsesOverrides[SWMMModelLayer::NumCategories] = {};
    /*! Value range the size channel last sized against (low already
     *  zero-based), for its legend rows; NaN when the channel did not run. */
    struct SizeRange {
        double low = qQNaN(), high = qQNaN();
    };
    SizeRange m_kindSizeRange[SWMMModelLayer::NumCategories];

    // Slice X.21 — categorized-renderer string-attribute cache.  Keyed
    // by "<categoryOrdinal>/<classifyAttribute>" → per-row string
    // value pulled from m_modelLayer->identifyByName(...).  Identity-
    // by-name is a hot lookup that touches every cached node / link /
    // subcatch entry, so re-fetching it every animation tick is
    // wasteful — the source data is topology-invariant.  Flat string
    // key avoids the QHash<QPair<int,QString>, …> requirement of an
    // explicit qHash overload.
    mutable QHash<QString, QVector<QString>> m_categoricalAttrCache;

    /*! Slice OUT.2 — recomputes the per-feature override cache for the
     *  one kind \p c by walking every feature in the category, building
     *  an attribute map from the current result value, and asking the
     *  per-kind renderer for the symbol. Sets m_kindUsesOverrides[c].
     *  Quietly clears the cache when no per-kind renderer is installed
     *  for c, when no result vector is available for the kind's scope,
     *  or when the category cannot resolve an output scope.
     * @param[in] c Category ordinal; invalid returns without mutation.
     * @note Clears that category colour/size/width/shape/dash/range caches first; unbound model/output/missing renderer/empty category returns without active overrides. Otherwise maps model names to output indices, asks renderer per row, retains fallback ramp colour where possible and applies sublayer size channel last.
     * @note Numeric classify/independent-size lookup currently uses hydraulic-only mappers here; qual:name values fetched elsewhere are not resolved by those local lookups. Categorized nonnumeric fallback caches current-model identify strings by category/attribute and count, without automatically invalidating equal-count attribute edits.
     * @note Graduated classification can schedule full-run range scans; FixedOverRun adds extrema to current frame samples rather than collecting full-run distribution. No signals/dirty escalation emitted here; caller coordinates scene refresh and serialized state access.
     */
    void rebuildKindFeatureOverrides(SWMMModelLayer::Category c);

    /*! Writes the kind's size channel (sublayer style) into the size /
     *  width override cache. Tail of rebuildKindFeatureOverrides.
     * @param[in] c Valid category ordinal, with open model/output and already initialized override vectors.
     * @note Enabled point/line channel overrides renderer size/width using its own species-aware cached result field. FixedUser uses raw bounds, FixedOverRun cache or schedules scan with current-frame fallback, PerFrame uses current finite scope extrema. Stores legend bounds and at least0.001pixel finite overrides; absent name/value yields minus one. Does not alter polygon size or emit signals.
     */
    void applySizeChannel(SWMMModelLayer::Category c);

    /*! Moves a Graduated renderer's size/width output axes into the kind's
     *  size channel (once) and switches the axes off. setKindRenderer.
     * @param[in] c Target kind whose point/line sublayer channel can receive legacy axis state.
     * @param[in,out] g Newly installed graduated renderer; any enabled output axes are disabled at end, even when no compatible channel was installed.
     * @note Seeds only a currently disabled point/line channel, linear transform/nonzero-based FixedOverRun with renderer pixel limits/effective attribute. Existing channel wins; style setter notifications may fire. No renderer clone occurs.
     */
    void migrateGraduatedOutputAxes(SWMMModelLayer::Category c,
                                    OpenSWMM::Render::GraduatedRenderer &g);

    /*! Slice OUT.2 — rebuilds every kind whose geometry scope matches
     *  all installed categories independently of the layer variable.
     * @brief Rebuild every category override cache unconditionally.
     * @note Despite historical active-scope name, all categories are visited; each helper self-exits for missing sources/renderers. Can classify/schedule range scans, but emits no own dirty/repaint signal.
     */
    void rebuildAllActiveKindFeatureOverrides();

    /*! Gap A2.2 — called on the layer thread when an async full-run
     *  attribute-range scan resolves. Re-classifies every Graduated kind
     *  renderer in \p scope (0 = node, 1 = link, 2 = subcatch) whose
     *  classify attribute maps to \p outCode and whose RangeMode is
     *  FixedOverRun, so breaks span the whole run instead of the frame
     *  that happened to be showing at install time.
     * @param[in] scope Zero node, one link, two subcatch; other values select no colour kinds.
     * @param[in] outCode Resolved scope output code; negative is a no-op.
     * @note Clears breaks/rebuilds matching FixedOverRun graduated kinds and matching fixed-run point/line size channels from newly cached extremes. Marks Values only if any rebuilt; caller emits repaint. Does not refresh rule mirror or reader buffers.
     */
    void reclassifyKindsForResolvedRange(int scope, int outCode);

    // Per-step result caches (fetched from .out each time step changes) ----
    QVector<float> m_nodeResults;     /*!< [nodeIdx] for current step */
    QVector<float> m_linkResults;     /*!< [linkIdx] for current step */
    QVector<float> m_subcatchResults; /*!< [subIdx]  for current step */

    // Phase 2 (2026-05-25) — concurrent multi-attribute paint. Each visible
    // sublayer carries its own `style.attribute` Q_PROPERTY; per period,
    // fetchResultsForStep populates a vector per (kind, varCode) pair that
    // any visible sublayer needs. populateScene reads from these maps using
    // the sublayer's attribute. The legacy m_nodeResults/m_linkResults/
    // m_subcatchResults remain populated (for the active m_variable) so
    // existing callers continue to work unchanged.
    QHash<int, QVector<float>> m_nodeResultsByVar;
    QHash<int, QVector<float>> m_linkResultsByVar;
    QHash<int, QVector<float>> m_subcatchResultsByVar;

    // Phase 7 (2026-05-25) — per-attribute observed range, computed by
    // sampling EVERY period of a variable when first needed. Replaces the
    // hardcoded [0, 1] m_colorRamp range so values aren't clamped at one
    // end of the ramp. Cleared on closeResults / openResults; lazily
    // populated by ensure*AttributeRange() helpers in the cpp.
    QHash<int, QPair<double, double>> m_nodeAttributeRange;
    QHash<int, QPair<double, double>> m_linkAttributeRange;
    QHash<int, QPair<double, double>> m_subcatchAttributeRange;

    // Per-attribute scan in-flight markers. A request from the paint path
    // (ensure*AttributeRange) returns a placeholder range immediately,
    // queues a background QtConcurrent scan of every period, and on
    // completion the result is inserted into the matching cache and a
    // repaintRequested() is emitted from the layer thread.
    QSet<int> m_nodeRangePending;
    QSet<int> m_linkRangePending;
    QSet<int> m_subcatchRangePending;

    // Name → output-file index maps (built once on openResults) -----------
    QHash<QString, int> m_nodeOutputIdx;
    QHash<QString, int> m_linkOutputIdx;
    QHash<QString, int> m_subcatchOutputIdx;

    // Aggregated per-object summary statistics, keyed by OUTPUT index and
    // filled lazily by linkStatsFor() / subcatchStatsFor(). Each miss costs
    // one series read per contributing variable over the whole run, so the
    // cache is what keeps an Attribute Table scroll from re-walking the
    // .out file for every painted cell. Mutable so the const accessors can
    // populate it; cleared by closeResults().
    struct LinkStats {
        double maxFlow = 0.0, maxVelocity = 0.0, maxFilling = 0.0;
        double volFlow = 0.0, surchargeTimeHr = 0.0;
        // Pumps only; left at zero for every other link type.
        double pumpCycles = 0.0, pumpOnTimeHr = 0.0, pumpVolume = 0.0;
    };
    struct SubcatchStats {
        double precipVol = 0.0, runoffVol = 0.0, maxRunoff = 0.0;
    };
    // Nodes get the same treatment. Their four values come from engine
    // aggregators rather than being computed here, but each of those is
    // still one O(n_periods) walk of the .out file — a seek plus a 4-byte
    // read per period — so uncached they cost four whole-file walks per
    // painted row, per repaint.
    struct NodeStats {
        double maxDepth = 0.0, maxOverflow = 0.0;
        double volFlooded = 0.0;
        //! HOURS. The engine aggregator returns seconds; converted on the
        //! way in so every consumer of NodeStats sees the column's unit.
        double timeFloodedHr = 0.0;
    };
    mutable QHash<int, LinkStats> m_linkStats;
    mutable QHash<int, SubcatchStats> m_subcatchStats;
    mutable QHash<int, NodeStats> m_nodeStats;

    /*! Aggregate (and cache) the summary statistics for one output-side
     *  link / subcatchment / node index. Returns a zeroed struct when the
     *  file is closed or the index is out of range.
     * @param[in] outIdx Output-file global index, not a model/category row. Negative/closed source returns zero struct; underlying getters reject other invalid indices.
     * @return Owned memoized statistic bundle; failed contributing series/getters leave corresponding fields zero, with no separate error status.
     * @note Reads full flow/velocity/depth/setting series and current model type/resolved full depth. Absolute flows drive volume/pump tests; file flow-unit conversion tables give ft³/m³ and hours. No mutex; requires serialized reader/model/cache access. Successful bundles, including failed-zero fields, remain cached until explicit invalidation.
     */
    [[nodiscard]] LinkStats linkStatsFor(int outIdx) const;
    /**
     * @param[in] outIdx Output-file global index, not a model/category row. Negative/closed source returns zero struct; underlying getters reject other invalid indices.
     * @return Owned memoized statistic bundle; failed contributing series/getters leave corresponding fields zero, with no separate error status.
     * @note Reads full runoff/rainfall series; rainfall volume additionally needs current model area, assuming model/file unit-system agreement. Signed runoff/rainfall samples are summed without finite filtering. No mutex; requires serialized reader/model/cache access. Successful bundles, including failed-zero fields, remain cached until explicit invalidation.
     */
    [[nodiscard]] SubcatchStats subcatchStatsFor(int outIdx) const;
    /**
     * @param[in] outIdx Output-file global index, not a model/category row. Negative/closed source returns zero struct; underlying getters reject other invalid indices.
     * @return Owned memoized statistic bundle; failed contributing series/getters leave corresponding fields zero, with no separate error status.
     * @note Runs the four engine node aggregators, retaining zero on individual failure and converting time seconds to hours. No mutex; requires serialized reader/model/cache access. Successful bundles, including failed-zero fields, remain cached until explicit invalidation.
     */
    [[nodiscard]] NodeStats nodeStatsFor(int outIdx) const;

    // GDAL coordinate transform -------------------------------------------
    class OGRCoordinateTransformation *m_transform = nullptr;

    // Granular per-Category sublayer storage (Slice U-0) -------------------
    // One FeatureSublayer per SWMMModelLayer::Category. Indexed by the
    // category ordinal; size = SWMMModelLayer::NumCategories. Constructed
    // in the ctor with `this` as QObject parent (no manual delete). The
    // sublayers() return-list orders them by archetype (polygon → line →
    // point) so paint order looks correct out of the box.
    OpenSWMM::Render::FeatureSublayer *m_featureSublayers[SWMMModelLayer::NumCategories] = {};

    // User-customisable sublayer paint order (Slice GUI-2026-05-30 §2).
    // Populated lazily on first sublayers() / moveSublayer() call from the
    // archetype-default sequence (polygon → line → marker).  Reordered via
    // moveSublayer(); persisted by ISublayerHost::saveSublayersToJson and
    // restored by loadSublayersFromJson on .oswp load.
    mutable QList<OpenSWMM::Render::ISublayer *> m_sublayerOrder;

    // Slice §Y.2 — incremental scene refresh state.
    //
    // m_sceneDirty drives refreshScene's branch.  Structural forces the
    // legacy depopulate + populate path (every QGraphicsItem destroyed and
    // re-allocated).  Values reuses the cached items and only mutates
    // brush / pen.  Clean returns immediately.  The default is Structural
    // so the first paint after construction (or after a results file
    // open) still produces a full scene.
    enum class SceneDirty : int { Clean = 0, Values = 1, Structural = 2 };
    SceneDirty m_sceneDirty{SceneDirty::Structural};

    // Per-Category item cache.  populateScene records the primary
    // QGraphicsItem for every feature row into the matching slot; null
    // entries are valid (NaN values / missing geometry / filtered rows).
    // restyleScene walks the slots and applies setBrush / setPen in place.
    // depopulateScene clears the slots.  Sizing tracks the model's
    // categoryCount at population time; restyleScene falls back to
    // structural when the size mismatches the current count (mid-session
    // topology edit).
    QVector<QGraphicsItem *> m_itemByFeature[SWMMModelLayer::NumCategories];

    // L-1 — parallel per-feature label items ("name: value"). Same sizing /
    // lifecycle contract as m_itemByFeature: built/refreshed by refreshLabels,
    // cleared by depopulateScene (base destroys the tagged scene items).
    QVector<QGraphicsSimpleTextItem *> m_labelByFeature[SWMMModelLayer::NumCategories];

    /*! Slice §Y.2 — flip to the strongest pending dirtiness without ever
     *  downgrading (Structural beats Values beats Clean).  Used by the
     *  setter cluster (setVariable, setColorRamp, sublayer toggles, …) so
     *  a pending structural rebuild isn't lost when a subsequent values-
     *  only mutation arrives.
     * @param[in] next Requested Clean/Values/Structural level.
     * @note Only raises the stored level, never downgrades or emits repaint; GUI-thread callers coordinate actual refresh.
     */
    void escalateSceneDirty(SceneDirty next);
};

Q_DECLARE_METATYPE(SWMMResultsLayer *)
Q_DECLARE_METATYPE(SWMMResultVariable)

#endif // SWMMRESULTSLAYER_H
