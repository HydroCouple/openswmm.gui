/**
 * @file      layertreetargets.h
 * @brief     What the layer tree's right-click "Open Attribute Table" and "Export…" entries act on
 *            (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §7 Part B, §8 Part C).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * What the layer tree's right-click "Open Attribute Table" and "Export…"
 * entries act on (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md
 * §7 Part B, §8 Part C).
 *
 * One pure predicate per entry decides both whether the entry is enabled and
 * what it does, so the two cannot disagree. Neither dereferences the layer
 * pointer: the caller describes the row by its layer-type ordinal (the
 * LayerTypeOrdinal mirror in layertreecategories.h), so both are
 * unit-testable without the layer object graph.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_LAYERTREETARGETS_H
#define OPENSWMMVIS_UI_PANELS_LAYERTREETARGETS_H

#include <QMetaType>
#include <QString>

class OpenSWMMVisLayer;

namespace openswmmvis::ui
{

/*! The mesh element tables, in MeshAttributeTableModel::Kind order. */
enum class MeshTable { Vertices = 0, Edges = 1, Cells = 2 };

/*!
 * \struct LayerTreeRow
 * \brief A layer-tree row, reduced to what decides its table and export.
 */
struct LayerTreeRow {
    enum class Kind {
        Layer,      ///< A layer row.
        ObjectType, ///< A kind row (Junctions … Rain Gages) under a SWMM model layer.
        Sublayer    ///< A sublayer row (mesh vertices, edges, …; a 1D run's
                    ///< "results.<kind>" object types).
    };
    Kind kind = Kind::Layer;
    /*! The row's layer, or the parent layer of a kind / sublayer row. */
    OpenSWMMVisLayer *layer = nullptr;
    /*! OpenSWMMVisLayer::OpenSWMMVisLayerType of \c layer (LayerTypeOrdinal). */
    int layerType = 0;
    /*! ObjectType rows, and the object-type sublayer rows of a 1D results
     *  layer: the SWMMModelLayer::Category the row stands for; else -1. */
    int category = -1;
    /*! Sublayer rows: ISublayer::id(), e.g. "mesh.edges". */
    QString sublayerId;
};

// ---------------------------------------------------------------------------
// Part B — Open Attribute Table
// ---------------------------------------------------------------------------

/*!
 * \struct AttributeTableTarget
 * \brief What to show. Invalid (Kind::None) means "no table": the entry is
 *        shown disabled, per the menu's disabled-but-visible policy.
 */
struct AttributeTableTarget {
    enum class Kind {
        None,          ///< Raster, imagery, 2D results, contour bands, …
        Layer,         ///< AttributeTablePanel::showLayerSource(layer).
        ModelCategory, ///< The SWMM table on \c category; \c layer is the
                       ///< model, or a 1D results layer whose run supplies
                       ///< the statistics columns.
        MeshTable      ///< Mesh \c layer's \c meshTable.
    };
    Kind kind = Kind::None;
    OpenSWMMVisLayer *layer = nullptr;
    int category = -1; ///< SWMMModelLayer::Category, for ModelCategory.
    MeshTable meshTable = MeshTable::Cells;

    /**
     * @return kind!=None ONLY; does not check layer pointer, category bounds, table enum, object
     *   lifetime or runtime capabilities. Pure structural predicate.
     */
    [[nodiscard]] bool isValid() const { return kind != Kind::None; }
};

/*!
 * \brief The table a row opens (§7.2):
 *   - feature / GIS vector / tabular layer, SWMM model, 1D results layer →
 *     that layer (the model keeps its current object type);
 *   - an object type under the model (kind row) or a 1D results layer
 *     ("results.<kind>" sublayer row with \c category set) → that type
 *     (with the run's statistics for a results layer);
 *   - a 2D mesh layer → its cells (plan Q7);
 *   - mesh sublayers vertices / edges / boundary conditions / fill →
 *     vertices / edges / edges / cells;
 *   - everything else → none.
 * @param[in] row Borrowed type/row-kind/category/ID descriptor, raw layer pointer copied only for
 *   supported target; never dereferenced.
 * @return Value routing described above, None on unsupported. Valid kind can contain NULL/dangling
 *   layer if descriptor inconsistent; caller validates runtime object/lifetime. Categories only
 *   checked >= 0 (not upper bounds), no geometry/data availability check.
 * @note Mesh unknown/non-table sublayers yield None; results sublayer with nonnegative category
 *   routes category regardless of sublayer ID. No table/dialog construction, source mutation,
 *   engine access or ownership.
 */
[[nodiscard]] AttributeTableTarget attributeTableTarget(const LayerTreeRow &row);

// ---------------------------------------------------------------------------
// Part C — Export
// ---------------------------------------------------------------------------

/*!
 * \struct LayerExportTarget
 * \brief What "Export…" writes for a row (§8.2). Invalid means the entry is
 *        shown disabled.
 */
struct LayerExportTarget {
    enum class Kind {
        None,
        SwmmObjects, ///< SWMM object types; \c layer is the model or a 1D results layer.
        VectorLayer, ///< A GIS vector or feature layer, as it is.
        Mesh,        ///< A 2D mesh's cells / edges / vertices.
        Results2D,   ///< The existing Export 2D Results dialog, on this run.
        Raster       ///< A raster layer to GeoTIFF.
    };
    Kind kind = Kind::None;
    OpenSWMMVisLayer *layer = nullptr;
    /*! SwmmObjects: the one object type to tick (SWMMModelLayer::Category);
     *  -1 ticks every spatial type. */
    int category = -1;
    /*! SwmmObjects: add the run's statistics (a 1D results row). */
    bool withResults = false;
    /*! Mesh: the one table to tick; -1 ticks all three. */
    int meshTable = -1;

    /**
     * @return kind!=None ONLY; does not check layer pointer, category bounds, table enum, object
     *   lifetime or runtime capabilities. Pure structural predicate.
     */
    [[nodiscard]] bool isValid() const { return kind != Kind::None; }
};

/*! \brief The export a row offers (§8.2); see LayerExportTarget.
 * @param[in] row Borrowed row/type/category/ID descriptor, raw layer never dereferenced.
 * @return Supported export selector with raw layer copied, else None. Model/result kind rows use
 *   nonnegative category, whole model category -1 all, 1D results withResults true. Mesh layer
 *   exports all; mesh sublayer known table selects it, UNKNOWN mesh sublayer ALSO exports
 *   Mesh/all-three (table -1), unlike attribute-table routing.
 * @note No export/file/dialog action, validation or lifetime pin. Null/dangling pointer can
 *   accompany valid kind; category upper bounds/type coherence caller responsibility. Raster
 *   supported only raster ordinal, imagery/WMS/WMTS/tabular not exported by this helper.
 */
[[nodiscard]] LayerExportTarget layerExportTarget(const LayerTreeRow &row);

} // namespace openswmmvis::ui

Q_DECLARE_METATYPE(openswmmvis::ui::AttributeTableTarget)
Q_DECLARE_METATYPE(openswmmvis::ui::LayerExportTarget)

#endif // OPENSWMMVIS_UI_PANELS_LAYERTREETARGETS_H
