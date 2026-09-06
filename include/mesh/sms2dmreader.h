/*!
 * \file   sms2dmreader.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase G5 of workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md — a minimal
 * reader for the SMS 2DM mesh format (Aquaveo). Only the cards that carry
 * geometry are consumed:
 *
 *     ND  id  x  y  z              node (1-based id)
 *     E3T id  n1 n2 n3 [mat]       triangle
 *     E4Q id  n1 n2 n3 n4 [mat]    quadrilateral (cyclic node order)
 *
 * Every other card (MESH2D, NUM_MATERIALS_PER_ELEM, NS, BEGPARAMDEF, …) is
 * ignored. NOT the engine's own `.2dm` sidecar grammar (that is
 * mesh::InpMeshReader's `[2D_*]` sections in a file that merely happens to
 * use the same extension).
 */
#ifndef OPENSWMMVIS_MESH_SMS2DMREADER_H
#define OPENSWMMVIS_MESH_SMS2DMREADER_H

#include "meshresult.h"

#include <QString>

namespace mesh {

class Sms2dmReader
{
public:
    /*! \brief Parse the SMS 2DM file at \p path into a MeshResult.
     *
     *  Nodes land in `vertices` in file order (ids are remapped to 0-based
     *  indices, so gaps or out-of-order ids are fine); every element's node
     *  references are resolved through that map. Cells land in `triangles`
     *  in the engine's order — E3T rows first, then E4Q rows (as quads with
     *  `v3 >= 0`), each in file order. The material id becomes the cell tag
     *  `mat<n>` (empty when the column is absent); Manning's n is left unset.
     *
     *  \param splitQuads  Split every E4Q into two triangles along the
     *         mesh::cellGeom diagonal (the same elevation-ordered split the
     *         engine's quad storage model uses) — for engines without quad
     *         support. Cells are then all triangles: the E3T rows, then each
     *         E4Q's two halves (adjacent, carrying the quad's tag).
     *
     *  On failure `ok` is false and `errorMsg` names the offending line. */
    [[nodiscard]] static MeshResult read(const QString &path, bool splitQuads = false);

    /*! \brief Same as \ref read, on already-loaded text. */
    [[nodiscard]] static MeshResult parse(const QString &text, bool splitQuads = false);

    /*! \brief Cheap format sniff: true when the first non-blank lines of the
     *  file carry SMS cards (`MESH2D`, `ND`, `E3T`, `E4Q`) and no SWMMVis
     *  `[2D_*]` section header. Both formats share the `.2dm` extension. */
    [[nodiscard]] static bool looksLikeSms2dm(const QString &path);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_SMS2DMREADER_H
