/*!
 * \file   qsg2drenderstats.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 1 — see qsg2drenderstats.h.
 */
#include "render/qsg2drenderstats.h"

#include <QDebug>
#include <QStringList>

namespace OpenSWMM::Render
{

namespace {
// -1 = follow the environment, 0/1 = forced by a test.
int s_loggingOverride = -1;
} // namespace

void Qsg2DRenderStats::reset()
{
    *this = Qsg2DRenderStats();
}

void Qsg2DRenderStats::addPass(const QString &pass,
                               qint64 builtVertices, qint64 uploadedBytes)
{
    passes.append(PassStats{pass, builtVertices, uploadedBytes});
}

qint64 Qsg2DRenderStats::totalBuiltVertices() const
{
    qint64 total = 0;
    for (const PassStats &p : passes) total += p.builtVertices;
    return total;
}

qint64 Qsg2DRenderStats::totalUploadedBytes() const
{
    qint64 total = 0;
    for (const PassStats &p : passes) total += p.uploadedBytes;
    return total;
}

QString Qsg2DRenderStats::dirtyReasonsToString(quint32 bits)
{
    if (bits == DirtyNone) return QStringLiteral("none");

    // Fixed bit order — the log format is a stable contract (grep-able,
    // locked by test_qsg2d_renderstats).
    static const struct { quint32 bit; const char *name; } kNames[] = {
        {DirtyPan,        "pan"},
        {DirtyZoom,       "zoom"},
        {DirtyTime,       "time"},
        {DirtyStyle,      "style"},
        {DirtySelection,  "selection"},
        {DirtyGeometry,   "geometry"},
        {DirtyLayer,      "layer"},
        {DirtyVisibility, "visibility"},
        {DirtyLod,        "lod"},
    };

    QStringList parts;
    for (const auto &n : kNames)
        if (bits & n.bit) parts.append(QLatin1String(n.name));
    return parts.join(QLatin1Char('|'));
}

QString Qsg2DRenderStats::toLogLine() const
{
    QString line;
    line += QStringLiteral("[render-perf] %1 dirty=%2")
                .arg(rendererName, dirtyReasonsToString(dirtyReasons));
    if (visibleCells    >= 0) line += QStringLiteral(" cells=%1").arg(visibleCells);
    if (visibleEdges    >= 0) line += QStringLiteral(" edges=%1").arg(visibleEdges);
    if (visibleVertices >= 0) line += QStringLiteral(" verts=%1").arg(visibleVertices);

    for (const PassStats &p : passes) {
        line += QStringLiteral(" %1[v=%2 B=%3]")
                    .arg(p.pass)
                    .arg(p.builtVertices)
                    .arg(p.uploadedBytes);
    }
    line += QStringLiteral(" totalV=%1 totalB=%2")
                .arg(totalBuiltVertices())
                .arg(totalUploadedBytes());
    if (repaintMs >= 0.0)
        line += QStringLiteral(" repaintMs=%1").arg(repaintMs, 0, 'f', 2);
    if (grabMs >= 0.0)
        line += QStringLiteral(" grabMs=%1").arg(grabMs, 0, 'f', 2);
    return line;
}

bool Qsg2DRenderStats::loggingEnabled()
{
    if (s_loggingOverride >= 0) return s_loggingOverride != 0;
    static const bool kEnabled = qEnvironmentVariableIntValue("OPENSWMM_RENDER_PERF") == 1;
    return kEnabled;
}

void Qsg2DRenderStats::overrideLoggingForTest(int forced)
{
    s_loggingOverride = forced;
}

void Qsg2DRenderStats::logIfEnabled(
    const std::function<void(const QString &)> &sink) const
{
    if (!loggingEnabled()) return;   // no formatting on the disabled path
    const QString line = toLogLine();
    if (sink) sink(line);
    else      qDebug().noquote() << line;
}

} // namespace OpenSWMM::Render
