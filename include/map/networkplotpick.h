/**
 * @file      networkplotpick.h
 * @brief     Network plot selection using display order and model geometry.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_NETWORK_PLOT_PICK_H
#define OPENSWMM_NETWORK_PLOT_PICK_H

#include "map/mapcanvas.h"
#include "layers/swmmresultslayer.h"
#include "selection/selectionmanager.h"

namespace openswmmvis
{
struct NetworkPlotPick {
    SWMMModelLayer::PickResult object;
    SWMMModelLayer *model = nullptr;
    SWMMResultsLayer *results = nullptr;
    /**
     * @return Value selection reference using picked category subtype to derive namespace; invalid
     *   pick gives Unknown while preserving name. Valid unexpected category falls through RainGage
     *   mapping. No model/results pointer ownership or existence validation.
     */
    SWMMObjectRef ref() const
    {
        using K = SWMMObjectRef::ObjectType;
        const auto cat = object.cat;
        const K kind = cat <= SWMMModelLayer::CatDividers        ? K::Node
                       : cat <= SWMMModelLayer::CatOutlets       ? K::Link
                       : cat == SWMMModelLayer::CatSubcatchments ? K::Subcatchment
                                                                 : K::RainGage;
        return {object.valid ? kind : K::Unknown, object.name};
    }
};

// Output selection follows drawing order. Model geometry remains a fallback
// for editing and for profiles created before a simulation is available.
/**
 * @param[in] canvas Optional borrowed canvas; null returns default invalid result.
 * @param[in] x Canvas-MAP X, not scene-Y-flipped.
 * @param[in] y Canvas-MAP Y.
 * @param[in] tolerance Finite nonnegative radius forwarded to tiered layer picking in canvas-map
 *   units.
 * @return First valid output-layer hit scanning top-to-bottom, then visible positive-opacity model
 *   fallback. Output phase delegates visibility entirely to results-layer pickAt. Returned
 *   model/results are BORROWED; invalid after layer removal/destruction or replacement. Model
 *   fallback results pointer null. Does not change selection/plot or engine.
 */
inline NetworkPlotPick pickNetworkForPlot(MapCanvas *canvas, double x, double y, double tolerance)
{
    if (!canvas)
        return {};
    const auto layers = canvas->layers();
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        if (auto *output = qobject_cast<SWMMResultsLayer *>(*it)) {
            const auto hit = output->pickAt(x, y, tolerance);
            if (hit.valid)
                return {hit, output->modelLayer(), output};
        }
    }
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        auto *model = qobject_cast<SWMMModelLayer *>(*it);
        if (!model || !model->isVisible() || model->opacity() <= 0)
            continue;
        const auto hit = model->pickAt(x, y, tolerance);
        if (hit.valid)
            return {hit, model, nullptr};
    }
    return {};
}
}
#endif
