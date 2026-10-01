/*!
 * \file   layertreetargets.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/panels/layertreetargets.h"

#include "ui/panels/layertreecategories.h"

namespace openswmmvis::ui {

namespace {

using T = LayerTypeOrdinal;

/*! The mesh table a mesh sublayer row stands for, or -1 (no table). */
int meshTableForSublayer(const QString &id)
{
    if (id == QLatin1String("mesh.vertices"))
        return static_cast<int>(MeshTable::Vertices);
    if (id == QLatin1String("mesh.edges") || id == QLatin1String("mesh.bc"))
        return static_cast<int>(MeshTable::Edges);
    if (id == QLatin1String("mesh.fill"))
        return static_cast<int>(MeshTable::Cells);
    return -1;   // mesh.coupledNodes, mesh.contourBands, mesh.isolines
}

bool isSwmmParent(T type)
{
    return type == T::SWMMModelLayer || type == T::SWMMResultsLayer;
}

}   // namespace

AttributeTableTarget attributeTableTarget(const LayerTreeRow &row)
{
    const auto type = static_cast<T>(row.layerType);
    AttributeTableTarget out;

    switch (row.kind) {
    case LayerTreeRow::Kind::Layer:
        switch (type) {
        case T::SWMMVectorLayer:
        case T::SWMMGISLayer:
        case T::SWMMFeatureLayer:
        case T::SWMMTabularDataLayer:
        case T::SWMMModelLayer:
        case T::SWMMResultsLayer:
            out.kind = AttributeTableTarget::Kind::Layer;
            break;
        case T::SWMM2DMeshLayer:
            // Cells carry Manning's n, initial depth, infiltration and tags —
            // where most mesh edits happen (Q7). Sublayer rows open the rest.
            out.kind = AttributeTableTarget::Kind::MeshTable;
            out.meshTable = MeshTable::Cells;
            break;
        default:
            break;
        }
        break;

    case LayerTreeRow::Kind::ObjectType:
        if (isSwmmParent(type) && row.category >= 0) {
            out.kind = AttributeTableTarget::Kind::ModelCategory;
            out.category = row.category;
        }
        break;

    case LayerTreeRow::Kind::Sublayer:
        if (type == T::SWMM2DMeshLayer) {
            const int t = meshTableForSublayer(row.sublayerId);
            if (t >= 0) {
                out.kind = AttributeTableTarget::Kind::MeshTable;
                out.meshTable = static_cast<MeshTable>(t);
            }
        } else if (type == T::SWMMResultsLayer && row.category >= 0) {
            // A 1D run shows its object types as "results.<kind>" sublayer
            // rows, not kind rows: same table as the model's kind row, with
            // this run's statistics.
            out.kind = AttributeTableTarget::Kind::ModelCategory;
            out.category = row.category;
        }
        break;
    }

    if (out.isValid()) out.layer = row.layer;
    return out;
}

LayerExportTarget layerExportTarget(const LayerTreeRow &row)
{
    const auto type = static_cast<T>(row.layerType);
    LayerExportTarget out;

    switch (row.kind) {
    case LayerTreeRow::Kind::Layer:
        switch (type) {
        case T::SWMMModelLayer:
            out.kind = LayerExportTarget::Kind::SwmmObjects;
            break;
        case T::SWMMResultsLayer:
            out.kind = LayerExportTarget::Kind::SwmmObjects;
            out.withResults = true;
            break;
        case T::SWMMVectorLayer:
        case T::SWMMGISLayer:
        case T::SWMMFeatureLayer:
            out.kind = LayerExportTarget::Kind::VectorLayer;
            break;
        case T::SWMM2DMeshLayer:
            out.kind = LayerExportTarget::Kind::Mesh;
            break;
        case T::SWMM2DResultsLayer:
            out.kind = LayerExportTarget::Kind::Results2D;
            break;
        case T::SWMMRasterLayer:
            out.kind = LayerExportTarget::Kind::Raster;
            break;
        default:
            break;
        }
        break;

    case LayerTreeRow::Kind::ObjectType:
        if (isSwmmParent(type) && row.category >= 0) {
            out.kind = LayerExportTarget::Kind::SwmmObjects;
            out.category = row.category;
            out.withResults = (type == T::SWMMResultsLayer);
        }
        break;

    case LayerTreeRow::Kind::Sublayer:
        if (type == T::SWMM2DMeshLayer) {
            out.kind = LayerExportTarget::Kind::Mesh;
            out.meshTable = meshTableForSublayer(row.sublayerId);   // -1 → all three
        } else if (type == T::SWMMResultsLayer && row.category >= 0) {
            out.kind = LayerExportTarget::Kind::SwmmObjects;
            out.category = row.category;
            out.withResults = true;
        }
        break;
    }

    if (out.isValid()) out.layer = row.layer;
    return out;
}

}   // namespace openswmmvis::ui
