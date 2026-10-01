/*!
 * \file   meshexport.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Collector for "Export mesh…"
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.2, R10).
 *
 * Snapshots a 2D mesh into ExportTable values for io/vectorexport: cells as
 * polygons (tag, the per-cell parameters of mesh::cellParamSpecs() — Manning's
 * n, initial depth, infiltration — and bed z), edges as lines (boundary flag,
 * conveyance, BC type and the values that type reads) and vertices as points
 * (z, tag, coupled node). Pure data in, data out: it reads a MeshResult and
 * the per-edge BCs, never the layer, so it can be unit-tested and called
 * before the write moves to a worker.
 */

#ifndef OPENSWMMVIS_IO_MESHEXPORT_H
#define OPENSWMMVIS_IO_MESHEXPORT_H

#include "io/vectorexport.h"
#include "mesh/meshedgebc.h"
#include "mesh/meshresult.h"

#include <QString>
#include <QVector>

namespace openswmmvis::io {

/*! Which mesh tables to write (a bit set). */
enum MeshExportPart : unsigned
{
    MeshExportCells    = 1u << 0,
    MeshExportEdges    = 1u << 1,
    MeshExportVertices = 1u << 2,
    MeshExportAll      = MeshExportCells | MeshExportEdges | MeshExportVertices
};

/*!
 * \param mesh        The mesh (map coordinates).
 * \param bcs         Per-edge BCs, flat-indexed by mesh::edgeSlot(cell, edge);
 *                    a short vector reads as walls with conveyance 1.
 * \param parts       MeshExportPart bits.
 * \param lengthUnit  Unit label for z and initial depth, e.g. "m"; may be empty.
 * \returns The tables "mesh_cells", "mesh_edges", "mesh_vertices" that
 *          \p parts asks for, in that order. Every interior edge is listed
 *          once, under its lower slot, as the mesh attribute table lists it.
 */
[[nodiscard]] QVector<ExportTable> meshTables(const mesh::MeshResult &mesh,
                                              const QVector<mesh::MeshEdgeBC> &bcs,
                                              unsigned parts,
                                              const QString &lengthUnit = QString());

}   // namespace openswmmvis::io

#endif // OPENSWMMVIS_IO_MESHEXPORT_H
