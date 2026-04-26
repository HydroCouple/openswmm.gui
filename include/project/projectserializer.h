/*!
 * \file   projectserializer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice X — Phase 12 first cut. JSON `.oswp` sidecar next to each
 * `.inp` capturing the GUI-only state that can't round-trip through
 * SWMM's INP format:
 *
 *   - Layer CRS authority / code (.inp can store a CRS but older
 *     engines don't — the sidecar makes "I picked EPSG:26910 yesterday"
 *     sticky).
 *   - Canvas CRS + extent (so reopen puts the viewport where you left it).
 *   - SWMM layer state: category order (Slice T.2), per-category object
 *     order overrides (Slice T.3), hidden-object set (Slice O /
 *     Object Browser checkboxes).
 *
 * Not in this first cut: project-scope preference overrides (Slice V
 * deferred piece), multi-layer orderings (one SWMM layer per project
 * window today), GIS basemap registrations (Phase 12.0 IO plugin
 * integration), bookmarks, simulation history.
 *
 * Schema versioning: `schemaVersion` integer at the root. Readers
 * treat an absent version as 0 (the pre-Slice-X format — just
 * `{ layers: [{ path: "…" }] }`) and only honour a layer list. A
 * mismatched higher schema version is loaded best-effort with a log
 * warning.
 */

#ifndef PROJECTSERIALIZER_H
#define PROJECTSERIALIZER_H

#include <QString>

class SWMMVisProjectWindow;

class ProjectSerializer
{
public:
    /*! Current writer version. */
    static constexpr int kCurrentSchemaVersion = 1;

    /*! Write a `.oswp` sidecar describing \p pw's current GUI state.
     *  Returns true on success; writes an error string on failure. */
    static bool saveToFile(const QString &oswpPath,
                           SWMMVisProjectWindow *pw,
                           QString *errorOut = nullptr);

    /*! Read a `.oswp` and apply any state it carries to \p pw's
     *  already-loaded SWMM layer and canvas. Silently skips keys the
     *  current build doesn't understand (forward-compat). Returns
     *  true if the file was parsed successfully. An absent file is
     *  not an error — callers Just-Load the `.inp` and call this
     *  opportunistically. */
    static bool applyFromFile(const QString &oswpPath,
                              SWMMVisProjectWindow *pw,
                              QString *errorOut = nullptr);

    /*! Canonical sidecar path: same directory + basename as the
     *  given `.inp`, with the extension swapped to `.oswp`. Empty
     *  input returns empty. */
    [[nodiscard]] static QString sidecarPathFor(const QString &inpPath);
};

#endif // PROJECTSERIALIZER_H
