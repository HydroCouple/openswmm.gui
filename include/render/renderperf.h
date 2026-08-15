/*!
 * \file   renderperf.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Opt-in canvas render-pipeline profiling category. Enable with:
 *           QT_LOGGING_RULES="openswmm.render.perf.debug=true"
 *         Times MapRenderJob passes (total + per layer), the QGraphicsScene
 *         rasterisation, and MapCanvas::paintEvent. Zero-cost when disabled
 *         (qCDebug skips argument evaluation; the explicit isDebugEnabled()
 *         gates skip the QElapsedTimer work too). Definition lives in
 *         src/render/qsg2drenderstats.cpp.
 */

#ifndef OPENSWMM_RENDER_RENDERPERF_H
#define OPENSWMM_RENDER_RENDERPERF_H

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcRenderPerf)

#endif // OPENSWMM_RENDER_RENDERPERF_H
