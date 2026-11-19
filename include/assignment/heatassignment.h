/**
 * @file      heatassignment.h
 * @brief     Spatial assignment of thermal properties and forcing to mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "ui/heatpropertyedit.h"
#include <functional>
class GISRasterLayer;
namespace OpenSWMMVis
{
struct HeatRasterPreview {
    QVector<HeatPropertyEdit> edits;
    int skipped = 0, skippedNoData = 0, skippedGeometry = 0, skippedRange = 0;
    QString error;
};
/**
 * @brief Sample raster values into validated thermal-property edits without applying them.
 * @param[in] model Borrowed model with an open engine; mesh sampling requires an available CRS.
 * @param[in] raster Borrowed raster providing georeferenced band values.
 * @param[in] band One-based raster band index.
 * @param[in] attr Heat-property descriptor index; the descriptor determines units and permitted range.
 * @param[in] scope Heat edit scope selecting nodes, conduit links or 2D cells.
 * @param[in] tag Exact region/object tag restriction; empty includes every tag.
 * @param[in] linkSamples Number of midpoint samples averaged along each conduit, must be at least one.
 * @param[in] progress Optional synchronous callback receiving completed/total objects; false cancels.
 * @param[in] cancelled Optional synchronous callback; true cancels.
 * @return Proposed edits and skip counters for geometry, NoData and ranges, with a nonempty error on refusal or cancellation. Callers must reject errored previews.
 * @details Uses live layer and engine data: call on the GUI thread. Nodes sample their position, links average along length, and 2D cells use area-weighted footprint sampling.
 */
HeatRasterPreview sampleHeatRaster(SWMMModelLayer *model, GISRasterLayer *raster, int band,
                                   int attr, int scope, const QString &tag = {},
                                   int linkSamples = 20,
                                   std::function<bool(int, int)> progress = {},
                                   std::function<bool()> cancelled = {});
}
