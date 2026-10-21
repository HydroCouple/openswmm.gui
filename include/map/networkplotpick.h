#ifndef OPENSWMM_NETWORK_PLOT_PICK_H
#define OPENSWMM_NETWORK_PLOT_PICK_H

#include "map/mapcanvas.h"
#include "layers/swmmresultslayer.h"
#include "selection/selectionmanager.h"

namespace openswmmvis {
struct NetworkPlotPick {
    SWMMModelLayer::PickResult object;
    SWMMModelLayer *model = nullptr;
    SWMMResultsLayer *results = nullptr;
    SWMMObjectRef ref() const {
        using K = SWMMObjectRef::ObjectType;
        const auto cat = object.cat;
        const K kind = cat <= SWMMModelLayer::CatDividers ? K::Node
            : cat <= SWMMModelLayer::CatOutlets ? K::Link
            : cat == SWMMModelLayer::CatSubcatchments ? K::Subcatchment : K::RainGage;
        return {object.valid ? kind : K::Unknown, object.name};
    }
};

// Output selection follows drawing order. Model geometry remains a fallback
// for editing and for profiles created before a simulation is available.
inline NetworkPlotPick pickNetworkForPlot(MapCanvas *canvas, double x, double y, double tolerance)
{
    if (!canvas) return {};
    const auto layers = canvas->layers();
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        if (auto *output = qobject_cast<SWMMResultsLayer *>(*it)) {
            const auto hit = output->pickAt(x, y, tolerance);
            if (hit.valid) return {hit, output->modelLayer(), output};
        }
    }
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        auto *model = qobject_cast<SWMMModelLayer *>(*it);
        if (!model || !model->isVisible() || model->opacity() <= 0) continue;
        const auto hit = model->pickAt(x, y, tolerance);
        if (hit.valid) return {hit, model, nullptr};
    }
    return {};
}
}
#endif
