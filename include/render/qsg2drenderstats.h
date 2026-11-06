/**
 * @file      qsg2drenderstats.h
 * @brief     Performance counters for 2D scene graph rendering.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 1 — per-sync render statistics for the 2D mesh / results
 * QSG renderers.
 *
 * A Qsg2DRenderStats value is filled in during one updatePaintNode() sync
 * (dirty reasons, visible element counts, per-pass built-vertex and
 * uploaded-byte counters, timings) and emitted as a single log line when
 * runtime perf logging is enabled:
 *
 *      OPENSWMM_RENDER_PERF=1
 *
 * Logging is silent (and the formatting path is never entered) unless the
 * environment variable is set, so shipping builds pay only a cached bool
 * check per sync.
 *
 * The struct is deliberately Qt-Core-only (no Qt Quick / Gui types) so it
 * can be unit-tested headlessly — see tests/unit/test_qsg2d_renderstats.cpp.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_QSG2DRENDERSTATS_H
#define OPENSWMM_RENDER_QSG2DRENDERSTATS_H

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <functional>

namespace OpenSWMM::Render
{

class Qsg2DRenderStats
{
public:
    /*! Why the renderer woke up this sync. Multiple bits may be set. */
    enum DirtyReason : quint32 {
        DirtyNone = 0,
        DirtyPan = 1u << 0,
        DirtyZoom = 1u << 1,
        DirtyTime = 1u << 2,
        DirtyStyle = 1u << 3,
        DirtySelection = 1u << 4,
        DirtyGeometry = 1u << 5,
        DirtyLayer = 1u << 6,
        DirtyVisibility = 1u << 7,
        /*! LOD content re-key (bucket/zoom-octave change, coverage miss,
         *  viewport/DPR resize) — a view-driven rebuild that is neither a
         *  plain pan nor a content change. */
        DirtyLod = 1u << 8,
    };

    /*! Per-pass build/upload accounting. */
    struct PassStats {
        QString pass;             ///< e.g. "fill", "edges", "selection"
        qint64 builtVertices = 0; ///< vertices assembled CPU-side this sync
        qint64 uploadedBytes = 0; ///< bytes copied into QSG geometry
    };

    QString rendererName; ///< "mesh2d" or "results2d"
    quint32 dirtyReasons = DirtyNone;
    qint64 visibleCells = -1; ///< -1 = not measured this sync
    qint64 visibleEdges = -1;
    qint64 visibleVertices = -1;
    double repaintMs = -1.0; ///< QSG repaint time, -1 = not measured
    double grabMs = -1.0;    ///< framebuffer grab time, -1 = not measured
    QVector<PassStats> passes;

    /** @brief Replace this snapshot with defaults, clearing names/passes and measured values. */
    void reset();
    /** @brief Append one accounting row without merging repeated pass names.
     * @param pass Display key copied verbatim into the log line.
     * @param builtVertices Vertex count for this pass, expected nonnegative; not validated.
     * @param uploadedBytes Uploaded byte count, expected nonnegative; not validated. */
    void addPass(const QString &pass, qint64 builtVertices, qint64 uploadedBytes);

    /** @brief Sum built-vertex counters across all appended rows.
     * @return Signed total; callers must keep the sum within qint64 range. */
    [[nodiscard]] qint64 totalBuiltVertices() const;
    /** @brief Sum upload counters across all appended rows.
     * @return Signed byte total; callers must keep the sum within qint64 range. */
    [[nodiscard]] qint64 totalUploadedBytes() const;

    /*! Stable "pan|zoom|time|..." rendering of a dirty-reason bitset;
     *  returns "none" for 0. Order is fixed (bit order) so log lines are
     *  greppable and the unit test can lock the format.
     * @param bits DirtyReason mask to encode; unrecognized bits are ignored.
     * @return Pipe-separated known labels, none for zero, or empty text for a
     * nonzero mask containing only unrecognized bits. */
    [[nodiscard]] static QString dirtyReasonsToString(quint32 bits);

    /** @brief Format all current counters without emitting a message.
     * @return Summary with dirty reasons, each pass and totals. Negative visibility
     * counts/timings are omitted; measured timings use two decimal places. Names
     * are copied verbatim, so callers should avoid embedded newlines in log keys. */
    [[nodiscard]] QString toLogLine() const;

    /*! True when OPENSWMM_RENDER_PERF=1 (cached after first read), or when
     *  overridden via \ref overrideLoggingForTest.
     * @return Effective flag. The environment is sampled once on the first call
     * that is not overridden. The override is process-global and unsynchronized. */
    [[nodiscard]] static bool loggingEnabled();

    /** @brief Override the process-global logging flag for a test.
     * @param forced Zero forces off, positive forces on, any negative follows the
     * cached environment setting. Calls must be serialized with logging access. */
    static void overrideLoggingForTest(int forced);

    /*! Emit \ref toLogLine when logging is enabled. When disabled this
     *  returns before any formatting happens. \p sink defaults to qDebug;
     *  tests inject a capturing sink.
     * @param sink Optional callable invoked synchronously once with the formatted
     * line when enabled; empty selects qDebug(). Exceptions from a supplied sink
     * propagate to the caller. No formatting/callback occurs when disabled. */
    void logIfEnabled(const std::function<void(const QString &)> &sink = {}) const;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QSG2DRENDERSTATS_H
