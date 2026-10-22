/**
 * @file      heatrasterassigndialog.h
 * @brief     Raster-based assignment of thermal parameters and forcing.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QDialog>
class SWMMModelLayer;
class MapCanvas;
namespace OpenSWMMVis
{
class HeatRasterAssignDialog final : public QDialog
{
public:
    /**
     * @param[in] model Optional borrowed model guarded in callbacks; must have live engine and
     *   declared model CRS for sampling, no ownership transfer.
     * @param[in] canvas Optional borrowed canvas guarded in callbacks; raster choices initially
     *   snapshot its loaded GIS rasters by layer ID, and supplies stack for apply.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog and keeps it alive
     *   during synchronous progress/event processing.
     * @brief Build thermal-raster sampling preview and live undoable Apply workflow without
     *   sampling or writing on construction.
     * @note Property values are descriptor SI units/temperature degrees C, no display-unit
     *   conversion. Band is one-based and maximum follows selected loaded raster band count;
     *   conduit samples [1,1000], default20. Mesh porosity forces cell scope; bed properties
     *   exclude nodes; other invalid combinations are ultimately rejected by sampleHeatRaster. Tag
     *   trimmed before exact network/region match.
     * @note Preview synchronously resolves chosen layer ID, captures heat configuration
     *   signature/model geometry/CRS, then samples nodes at native position, conduits at midpoint
     *   samples along length and cells by area-weighted raster footprint. Progress pumps GUI every
     *   20 completed objects; cooperative predicates check cancellation, model/raster guards,
     *   engine identity, geometry and model CRS. No assignment until Apply.
     * @note On successful sampling, remove exact unchanged authored overrides (inherited values
     *   still become explicit edits), retain full edit vector but show first 500 changes, record
     *   raster PATH plus file mtime/size AFTER sampling, and enable Apply when edits remain.
     *   Missing/invalid samples preserve existing values; skip counts shown. Model
     *   engine-close/geometry/options and widget changes clear preview/disable Apply; no live
     *   raster-list subscription or explicit preview generation counter.
     * @note Apply checks model geometry/CRS/configuration signature and source mtime/size, refuses
     *   stale result, then delegates all captured edits to applyHeatPropertyEdits with canvas undo
     *   stack. File contents/CRS identity are not separately hashed here and source choices are not
     *   re-enumerated at apply. Success disables Apply and reports completion; failure retains
     *   error. No automatic close or Cancel rollback of prior applied batch, and host preserves
     *   dialog/control lifetime through reentrant progress events.
     */
    HeatRasterAssignDialog(SWMMModelLayer *model, MapCanvas *canvas, QWidget *parent = nullptr);
};
}
