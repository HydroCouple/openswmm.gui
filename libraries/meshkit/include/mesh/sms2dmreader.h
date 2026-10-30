/**
 * @file      sms2dmreader.h
 * @brief     Phase G5 of workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md — a minimal reader for the
 *            SMS 2DM mesh format (Aquaveo).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * File reads use the bounded native streaming SMS codec; the legacy whole-text
 * parse helper remains for callers that already hold text. This is separate
 * from the application's `[2D_*]` mesh-sidecar grammar.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_SMS2DMREADER_H
#define OPENSWMMVIS_MESH_SMS2DMREADER_H

#include "meshresult.h"

#include <QString>
#include <QStringList>

namespace mesh
{

class Sms2dmReader
{
public:
    /**
     * Stream a bounded two-pass SMS file through meshkit::io. Sparse identifiers
     * and forward references are resolved before returning geometry. Cells are
     * finite, convex and counterclockwise; clockwise input is reoriented. Failure
     * returns no partial mesh. NS node strings become tagged constraint edges.
     * The GUI compatibility mode retains optional/text material tags; unsupported
     * cards and winding changes are reported through warnings when supplied.
     * Coordinates stay in file units. The legacy parse(text) helper remains below.
     */
    [[nodiscard]] static MeshResult read(const QString &path, bool splitQuads = false,
                                        QStringList *warnings = nullptr);

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
