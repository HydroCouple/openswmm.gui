/**
 * @file      sms2dmreader.h
 * @brief     Phase G5 of workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md — a minimal reader for the
 *            SMS 2DM mesh format (Aquaveo).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_SMS2DMREADER_H
#define OPENSWMMVIS_MESH_SMS2DMREADER_H

#include "meshresult.h"

#include <QString>

namespace mesh
{

class Sms2dmReader
{
public:
    /**
     * @brief Read an entire SMS geometry file as UTF-8 text and pass it to parse().
     * @param[in] path Borrowed input pathname, unchanged; opened read-only/text and closed by
     * local QFile.
     * @param[in] splitQuads True replaces each E4Q with the shared elevation-selected
     * two-triangle fan; false preserves quad records. No CRS/unit conversion or convexity repair
     * accompanies splitting.
     * @return Owned MeshResult. Open failure leaves ok=false and a file diagnostic; parsing
     * failures can retain partial vertices/cells with line-specific error. No separate post-read
     * error check or size budget. Does not modify source files, retain inputs or create engine
     * state; numeric geometry is verbatim file units.
     *
     * @details Parse the SMS 2DM file at \p path into a MeshResult.
     *
     * Nodes land in `vertices` in file order (ids are remapped to 0-based indices, so gaps or
     * out-of-order ids are fine); every element's node references are resolved through that map.
     * Cells land in `triangles` in the engine's order — E3T rows first, then E4Q rows (as quads
     * with `v3 >= 0`), each in file order. The material id becomes the cell tag `mat<n>` (empty
     * when the column is absent); Manning's n is left unset.
     *
     *
     * On failure `ok` is false and `errorMsg` names the offending line.
     */
    [[nodiscard]] static MeshResult read(const QString &path, bool splitQuads = false);

    /**
     * @brief Decode case-sensitive ND/E3T/E4Q cards into owned vertices and engine-ordered
     * cells.
     * @param[in] text Borrowed whole text, unchanged; whitespace simplified per line, no
     * inline-comment grammar. Other card names are ignored; geometry card names must be
     * uppercase despite case-insensitive sniffing.
     * @param[in] splitQuads True emits two adjacent triangle halves for each quad using
     * cellGeom()'s z-selected storage fan; false emits v3>=0 quads after all triangles.
     * @return Owned result with ok=true only for nonempty vertices/cells and successful
     * syntax/reference resolution. Node IDs may be sparse/out-of-order and any convertible int
     * (including nonpositive); duplicates fail, references resolve after reading all nodes.
     * Element IDs are ignored/unvalidated, optional material text becomes mat-prefixed tag,
     * other attributes stay defaults/unset. Numeric coordinates retain file units and are not
     * explicitly finite-checked; repeated corners, winding, convexity/topology and unit metadata
     * are not validated. Checked failure returns ok=false with one-based source line and may
     * include partial geometry. Allocates whole-line/element/map/output storage, no
     * source/engine mutation or ownership retention.
     *
     * @details Same as \ref read, on already-loaded text.
     */
    [[nodiscard]] static MeshResult parse(const QString &text, bool splitQuads = false);

    /**
     * @brief Perform an early-card format sniff on at most 200 nonblank file lines.
     * @param[in] path Borrowed input pathname, unchanged; opened read-only/text, no result
     * retained.
     * @return False on open failure, first encountered '[' section header before a recognized
     * card, or no recognized card within budget. Returns true immediately for case-insensitive
     * MESH2D/ND/E3T/E4Q/ E2L/E6T/E8Q/E9Q first space-delimited token, including cards the
     * geometry parser does not support. Does not inspect later lines after a match or validate
     * syntax. Leading/trailing whitespace trimmed, but embedded tab separators are not
     * normalized here; ';;' comment lines consume the nonblank budget then skip. File extension
     * ignored, no geometry allocations or file/project mutation.
     *
     * @details Cheap format sniff: true when the first non-blank lines of the file carry SMS
     * cards (`MESH2D`, `ND`, `E3T`, `E4Q`) and no SWMMVis `[2D_*]` section header. Both formats
     * share the `.2dm` extension.
     */
    [[nodiscard]] static bool looksLikeSms2dm(const QString &path);
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_SMS2DMREADER_H
