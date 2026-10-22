/**
 * @file      loadprogress.h
 * @brief     Determinate open-progress model (LOAD_PERF plan Phase 1a).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Determinate open-progress model (LOAD_PERF plan Phase 1a).
 *
 * Opening a project is not one operation — it is a chain of stages spread
 * across THREE async phases (model worker → GUI adopt/sidecar/results →
 * mesh worker → GUI adopt → mesh Phase-B worker) plus post-open work on the
 * GUI thread. Each phase can only report its own local 0-100.
 *
 * OpenProgressModel turns that into a single monotonic 0-100 for the status
 * bar: every stage owns a fixed slice of the total (\ref OpenProgressModel::stageWeight), and
 * the overall percent is the sum of completed slices plus the partial slice
 * of whatever is running.
 *
 * Deliberate design points:
 *
 *  - **Fixed weights, never renormalized.** A model that turns out to have no
 *    2D mesh simply completes the three mesh stages instantly, which makes the
 *    bar JUMP FORWARD. That is fine. Renormalizing the table on stage
 *    discovery would instead make it run BACKWARDS, which reads as a bug.
 *  - **Monotonic by construction.** \ref OpenProgressModel::percent never returns less than it
 *    previously returned. Stages may complete out of order (the mesh worker
 *    and the GUI-thread sidecar apply genuinely overlap) without the bar
 *    stuttering.
 *  - **No Qt Concurrent here.** Producers deep inside the layers take a plain
 *    \ref LoadProgressFn, so swmmmodellayer / inpmeshreader stay free of
 *    QPromise and remain unit-testable.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef LOADPROGRESS_H
#define LOADPROGRESS_H

#include <QObject>
#include <QString>

#include <array>
#include <functional>

/*!
 * \brief Progress sink handed to a worker-side producer.
 *
 * \a localPct is 0-100 WITHIN that producer's own stage — the producer knows
 * nothing about the overall weighting. \a stage is a short human-readable
 * label ("Parsing 2D mesh…"); an empty string keeps the previous label.
 *
 * A default-constructed (empty) function is the "nobody is watching" case and
 * every producer must tolerate it, so the sync code paths and the unit tests
 * can call the same functions with no progress plumbing at all.
 */
using LoadProgressFn = std::function<void(int localPct, const QString &stage)>;

/*!
 * \brief The stages of a single project open, in nominal order.
 *
 * \note Nominal, not guaranteed: Sidecar/Results run on the GUI thread while
 *       MeshParse is already running on a worker. The model tolerates any
 *       completion order.
 */
enum class OpenStage {
    EngineParse = 0, //!< swmm_engine_open — the .inp parse itself
    SoaCopy,         //!< SWMMModelLayer::buildFromEngine SoA pull
    GeomCache,       //!< buildGeometryCache (5 sub-stages)
    CrsFinish,       //!< adoptOpenEngine CRS resolution + finishModelLoad
    Sidecar,         //!< .oswp sidecar apply
    Results,         //!< sibling .out discovery + open
    MeshParse,       //!< InpMeshReader::read
    MeshSceneA,      //!< rebuildSceneGeometryLight + LOD pyramid
    MeshSceneB,      //!< finishSceneGeometryAsync (edges/grids/adjacency)
    Count_           //!< sentinel — not a stage
};

/*!
 * \brief Encode a (stage, localPct) pair into a single QPromise progress int.
 *
 * QPromise gives a worker exactly one integer channel, but the model-load
 * worker spans three stages. Rather than invent a cross-thread pointer with
 * its own lifetime rules, the worker packs the stage into the value and the
 * GUI-side QFutureWatcher handler unpacks it — Qt already guarantees the
 * delivery is thread-safe and lands on the GUI thread.
 *
 * Layout: `stageIndex * 101 + clamp(localPct, 0, 100)`. The stride is 101,
 * not 100, so localPct == 100 is distinguishable from the next stage's 0.
 */
constexpr int kLoadProgressStride = 101;

/**
 * @brief Encode a stage and clamped local percentage in one progress integer.
 * @param[in] stage Stage enum encoded by index; callers must use a real OpenStage value.
 * @param[in] localPct Stage percentage, clamped to [0,100].
 * @return stageIndex * 101 + clamped percentage.
 */
inline int packLoadProgress(OpenStage stage, int localPct)
{
    if (localPct < 0)
        localPct = 0;
    if (localPct > 100)
        localPct = 100;
    return static_cast<int>(stage) * kLoadProgressStride + localPct;
}

/**
 * @brief Recover the stage index from a packed worker progress update.
 * @param[in] packed Nonnegative value produced by packLoadProgress().
 * @return Decoded enum without validity checks; Count_ is not a real stage.
 */
inline OpenStage unpackLoadProgressStage(int packed)
{
    return static_cast<OpenStage>(packed / kLoadProgressStride);
}

/**
 * @brief Recover the local percentage from a packed worker progress update.
 * @param[in] packed Nonnegative value produced by packLoadProgress().
 * @return Remainder modulo 101, in [0,100] for valid input.
 */
inline int unpackLoadProgressPct(int packed)
{
    return packed % kLoadProgressStride;
}

/*!
 * \brief Weighted, monotonic progress aggregator for one project open.
 *
 * One instance per open. SWMVis owns them in a map keyed by open id so a
 * multi-model .oswp can report the minimum across concurrent opens, matching
 * what the simulation path already does for concurrent runs.
 */
class OpenProgressModel : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create a zero-progress aggregator with no current label.
     * @param[in] parent Optional Qt owner. Report stage updates on this object's thread; worker updates must be queued.
     */
    explicit OpenProgressModel(QObject *parent = nullptr);

    /*! Percent of the total attributed to \p stage. Sums to 100 across all
     *  stages. Public so the unit test can assert the invariant rather than
     *  duplicating the table.     * @param[in] stage Stage whose fixed percentage budget is requested.
     * @return Budget in percent of the whole open; zero for Count_ or an invalid enum.
     */
    static int stageWeight(OpenStage stage);

    /*! Canonical user-facing label for \p stage, e.g. "Parsing 2D mesh…".
     *  Lets a worker report a stage without also shipping its own text.     * @param[in] stage Stage requiring a display label.
     * @return Translated stage description, or empty for Count_ or an invalid enum.
     */
    static QString stageLabel(OpenStage stage);

    /*!
     * \brief Report partial progress within \p stage.
     *
     * \p localPct is clamped to [0,100]. A non-empty \p label becomes the
     * current status text. Emits \ref progressChanged when the aggregate
     * percent or the label actually changed — callers may tick freely.
     * @param[in] stage Valid stage; Count_ and invalid values are ignored.
     * @param[in] localPct Stage percentage, clamped and prevented from decreasing.
     * @param[in] label Optional status text; empty retains the current label.
     */
    void setStage(OpenStage stage, int localPct, const QString &label = QString());

    /*!
     * \brief Mark \p stage fully done (idempotent).
     *
     * Call this for stages that turn out not to apply — a model with no 2D
     * mesh finishes MeshParse/MeshSceneA/MeshSceneB immediately — otherwise
     * the open never reaches 100 and the bar never hides.
     * @param[in] stage Stage to mark 100%; invalid values are ignored.
     * @details Recomputes weighted progress and may emit progressChanged and the one-time finished signal.
     */
    void finishStage(OpenStage stage);

    /*! Finish every outstanding stage. Used on error/abandon so a failed open
     *  still tears its indicator down.     * @details Sets all local stages to 100 and triggers completion once, even when a failed open never reported progress.
     */
    void finishAll();

    /*! Aggregate 0-100. Never decreases across calls.     * @return Last monotonic weighted aggregate in [0,100]; querying does not recompute or emit.
     */
    [[nodiscard]] int percent() const { return m_lastPct; }

    /*! Current stage label, or empty before the first labelled tick.     * @return Copy of the last nonempty status label supplied to setStage().
     */
    [[nodiscard]] QString label() const { return m_label; }

    /*! True once every stage has completed.     * @return True only when every real stage has reached 100; weighted rounding to 100 alone is insufficient.
     */
    [[nodiscard]] bool isComplete() const;

signals:
    /*! Aggregate percent and/or label changed.     * @param[in] pct Monotonic overall percentage in [0,100].
     * @param[in] label Current status label, possibly empty.
     */
    void progressChanged(int pct, const QString &label);

    /*! Every stage has completed — the view may hide its indicator. Emitted
     *  exactly once.     * @details Emitted once when all stages reach 100, including completion via finishAll().
     */
    void finished();

private:
    /**
     * @brief Round the weighted integer stage sum and emit only observable changes.
     * @details Preserves the monotonic floor, publishes label-only changes and emits finished exactly once.
     */
    void recompute();

    static constexpr int kStageCount = static_cast<int>(OpenStage::Count_);

    std::array<int, kStageCount> m_localPct{}; //!< 0-100 per stage
    QString m_label;
    QString m_emittedLabel; //!< last label actually emitted
    int m_lastPct = 0;      //!< monotonic clamp floor
    bool m_finishedEmitted = false;
};

#endif // LOADPROGRESS_H
