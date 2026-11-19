/**
 * @file      seriesacceleration.h
 * @brief     When a chart series takes Qt Charts' OpenGL drawing path.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_SERIESACCELERATION_H
#define OPENSWMMVIS_PLOT_SERIESACCELERATION_H

#include <QtGlobal>

namespace openswmmvis::plot
{

/**
 * @brief Whether a line/scatter series of @p pointCount points should set
 *        QAbstractSeries::setUseOpenGL.
 *
 * @details An OpenGL series makes Qt Charts place a QOpenGLWidget over the
 *          plot area. On Windows the rest of the application composes with
 *          Direct3D, so the first OpenGL widget in a window makes Qt destroy
 *          and re-create that window on an OpenGL surface (it flashes), and
 *          creating the OpenGL context can blank the display while a
 *          hybrid-GPU laptop switches adapters. Windows therefore keeps the
 *          raster path until a series is large enough to stall it (~50k
 *          points). Other platforms already compose with OpenGL and keep the
 *          accelerated path for every series.
 */
inline bool seriesUsesOpenGL(qsizetype pointCount)
{
#ifdef Q_OS_WIN
    return pointCount > 50000;
#else
    Q_UNUSED(pointCount);
    return true;
#endif
}

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESACCELERATION_H
