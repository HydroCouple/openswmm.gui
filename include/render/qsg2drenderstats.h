/*!
 * \file   qsg2drenderstats.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
        DirtyNone       = 0,
        DirtyPan        = 1u << 0,
        DirtyZoom       = 1u << 1,
        DirtyTime       = 1u << 2,
        DirtyStyle      = 1u << 3,
        DirtySelection  = 1u << 4,
        DirtyGeometry   = 1u << 5,
        DirtyLayer      = 1u << 6,
        DirtyVisibility = 1u << 7,
        /*! LOD content re-key (bucket/zoom-octave change, coverage miss,
         *  viewport/DPR resize) — a view-driven rebuild that is neither a
         *  plain pan nor a content change. */
        DirtyLod        = 1u << 8,
    };

    /*! Per-pass build/upload accounting. */
    struct PassStats
    {
        QString pass;               ///< e.g. "fill", "edges", "selection"
        qint64  builtVertices = 0;  ///< vertices assembled CPU-side this sync
        qint64  uploadedBytes = 0;  ///< bytes copied into QSG geometry
    };

    QString rendererName;           ///< "mesh2d" or "results2d"
    quint32 dirtyReasons    = DirtyNone;
    qint64  visibleCells    = -1;   ///< -1 = not measured this sync
    qint64  visibleEdges    = -1;
    qint64  visibleVertices = -1;
    double  repaintMs       = -1.0; ///< QSG repaint time, -1 = not measured
    double  grabMs          = -1.0; ///< framebuffer grab time, -1 = not measured
    QVector<PassStats> passes;

    void reset();
    void addPass(const QString &pass, qint64 builtVertices, qint64 uploadedBytes);

    [[nodiscard]] qint64 totalBuiltVertices() const;
    [[nodiscard]] qint64 totalUploadedBytes() const;

    /*! Stable "pan|zoom|time|..." rendering of a dirty-reason bitset;
     *  returns "none" for 0. Order is fixed (bit order) so log lines are
     *  greppable and the unit test can lock the format. */
    [[nodiscard]] static QString dirtyReasonsToString(quint32 bits);

    /*! One-line summary of the whole sync. */
    [[nodiscard]] QString toLogLine() const;

    /*! True when OPENSWMM_RENDER_PERF=1 (cached after first read), or when
     *  overridden via \ref overrideLoggingForTest. */
    [[nodiscard]] static bool loggingEnabled();

    /*! Test hook: 0 = force off, 1 = force on, -1 = follow the env var. */
    static void overrideLoggingForTest(int forced);

    /*! Emit \ref toLogLine when logging is enabled. When disabled this
     *  returns before any formatting happens. \p sink defaults to qDebug;
     *  tests inject a capturing sink. */
    void logIfEnabled(const std::function<void(const QString &)> &sink = {}) const;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_QSG2DRENDERSTATS_H
