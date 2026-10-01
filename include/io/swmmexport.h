/*!
 * \file   swmmexport.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Collector for "Export ▸ SWMM objects…"
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.2, §8.4, R9).
 *
 * Snapshots SWMM object types into ExportTable values for io/vectorexport.
 * The attributes are read through SWMMAttributeTableModel, so what is
 * exported is what the Attribute Table shows — the same columns, values in the
 * same display units, a loaded run's statistics when one is given. The
 * geometry comes from the model layer's caches. Runs on the main thread (it
 * reads the engine); only the write moves to a worker.
 *
 * Field names are Import Feature Layer's attribute keys where a column has
 * one (invertElev, maxDepth, fromNode, …) so an exported layer re-imports
 * with Auto-match (plan Q9); every other column gets a sanitised form of its
 * label. Choice columns are written as their INP tokens.
 */

#ifndef OPENSWMMVIS_IO_SWMMEXPORT_H
#define OPENSWMMVIS_IO_SWMMEXPORT_H

#include "io/vectorexport.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

class SWMMModelLayer;
class SWMMResultsLayer;

namespace openswmmvis { struct ColumnSpec; }

namespace openswmmvis::io {

/*! The 11 spatial object types (SWMMModelLayer::Category), in export order:
 *  points, lines, subcatchments, rain gages. */
[[nodiscard]] QList<int> swmmSpatialCategories();

/*! Output table name for \p category: "junctions", "storage_units", … */
[[nodiscard]] QString swmmTableName(int category);

/*!
 * \brief The output field name for a table column: Import Feature Layer's
 *        key when the column maps to one, otherwise the sanitised label.
 */
[[nodiscard]] QString swmmExportFieldName(int category, const openswmmvis::ColumnSpec &spec);

/*!
 * \brief Snapshot \p categories of \p model.
 * \param run           A 1D results layer whose statistics columns are added;
 *                      null exports inputs only.
 * \param selectedOnly  Only the objects selected on the map.
 * \details Objects without geometry are left out and listed in each table's
 *          \c skipped: a subcatchment with no polygon (plan Q11), a link whose
 *          end node has no coordinates, an un-located rain gage.
 */
[[nodiscard]] QVector<ExportTable> swmmObjectTables(SWMMModelLayer *model,
                                                    const QList<int> &categories,
                                                    SWMMResultsLayer *run,
                                                    bool selectedOnly);

}   // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_SWMMEXPORT_H
