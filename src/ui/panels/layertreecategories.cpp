/*!
 * \file   layertreecategories.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Implementation of the headless category-bucketing helpers.
 *         See layertreecategories.h for the design rationale.
 */
#include "ui/panels/layertreecategories.h"

// NOTE: this translation unit deliberately does NOT include
// layers/openswmmvislayer.h. Keeping it Qt-free lets test_layertreecategories
// link against this single .cpp without dragging the whole layer object
// graph (QObject + MapExtent + render/* specs) in. The LayerTypeOrdinal
// mirror enum in the header is statically asserted against
// OpenSWMMVisLayer::OpenSWMMVisLayerType from inside layertreepanel.cpp,
// so drift between the two is caught at build time without a hard include
// dependency here.

namespace openswmmvis::ui {

CategoryId categoryForLayerType(int t)
{
    // Numeric constants match OpenSWMMVisLayer::OpenSWMMVisLayerType enum
    // values exactly (see static_assert block in layertreepanel.cpp). The
    // LayerTypeOrdinal mirror in the header is provided so test code can
    // reference these by name without the openswmmvislayer.h include cost.
    switch (static_cast<LayerTypeOrdinal>(t))
    {
    case LayerTypeOrdinal::SWMMModelLayer:               return CatSwmm;
    case LayerTypeOrdinal::SWMMResultsLayer:             return CatSwmm1DOutputs;
    case LayerTypeOrdinal::SWMM2DMeshLayer:              return CatMeshes;
    case LayerTypeOrdinal::SWMM2DResultsLayer:           return CatSwmm2DOutputs;

    case LayerTypeOrdinal::SWMMVectorLayer:
    case LayerTypeOrdinal::SWMMGISLayer:
    case LayerTypeOrdinal::SWMMSubProjectLayer:          return CatFeatureLayers;

    case LayerTypeOrdinal::SWMMRasterLayer:              return CatRasterLayers;

    case LayerTypeOrdinal::SWMMImageryLayer:
    case LayerTypeOrdinal::SWMMWMSLayer:
    case LayerTypeOrdinal::SWMMWMTSLayer:                return CatBasemaps;

    case LayerTypeOrdinal::SWMMTabularDataLayer:
    case LayerTypeOrdinal::SWMMTabularyTimeSeriesLayer:  return CatTables;

    case LayerTypeOrdinal::SWMMAnnotationLayer:          return CatFeatureLayers;

    case LayerTypeOrdinal::SWMMDefaultLayer:             return CatFeatureLayers;
    }
    // Unknown ordinal — same fall-back the runtime uses (the GUI wrapper
    // in layertreepanel.cpp logs a qWarning before returning this).
    return CatFeatureLayers;
}

CategoryInfo categoryInfo(CategoryId id)
{
    switch (id)
    {
    case CatSwmm:           return {"SWMM",             ":/swmmvis/Layers"};
    case CatMeshes:         return {"Meshes",           ":/swmmvis/CreateMesh"};
    case CatSwmm1DOutputs:  return {"SWMM 1D Outputs",  ":/swmmvis/Chart"};
    case CatSwmm2DOutputs:  return {"SWMM 2D Outputs",  ":/swmmvis/CreateMesh"};
    case CatFeatureLayers:  return {"Feature Layers",   ":/swmmvis/AddVector"};
    case CatRasterLayers:   return {"Raster Layers",    ":/swmmvis/AddRaster"};
    case CatBasemaps:       return {"Basemaps",         ":/swmmvis/Globe"};
    case CatTables:         return {"Tables",           ":/swmmvis/TableView"};
    case CatCount:
    default:                return {"Feature Layers",   ":/swmmvis/AddVector"};
    }
}

} // namespace openswmmvis::ui
