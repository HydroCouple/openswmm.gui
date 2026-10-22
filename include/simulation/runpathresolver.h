/**
 * @file      runpathresolver.h
 * @brief     Strategy C (GUI-only, no engine [OPTIONS] persistence).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice QB.2 — Strategy C (GUI-only, no engine [OPTIONS] persistence).
 *
 * Resolves the `.rpt` / `.out` path SimulationRunner should write to.
 * Today SWMMVis::runActiveProject hard-codes those as `<inp-basename>.rpt`
 * / `<inp-basename>.out`, ignoring the Simulation Options → Output tab
 * overrides that Slice AA-4 already round-trips through QSettings under
 * `SWMMVis/Project/<inpPath>/ReportFilePath` and `OutputFilePath`. This
 * helper consults the QSettings override first, then falls back to the
 * sibling default, so the user's existing override actually takes effect
 * when they hit Run.
 *
 * Strategy C scope (per §Q.5 of GUI_IMPLEMENTATION_PLAN.md): no engine
 * surface, no .inp persistence — the override stays a per-machine
 * preference. A future Strategy A (new OUT_FILE_PATH / RPT_FILE_PATH
 * OPTIONS keys) would extend this resolver with a third tier without
 * breaking callers.
 *
 * The function is intentionally pure over (inpPath, override, kind) so
 * tests can hit every precedence branch without spinning up the full
 * SimulationOptionsDialog + QSettings store.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef RUNPATHRESOLVER_H
#define RUNPATHRESOLVER_H

#include <QString>

namespace openswmmvis
{

/*! Which sibling extension is being resolved. */
enum class RunOutputKind {
    Rpt, ///< Sibling `.rpt` — SWMM summary report file.
    Out  ///< Sibling `.out` — SWMM binary results file.
};

/*! Pure resolver. Reads no global state — the caller is responsible for
 *  having looked up the QSettings override (empty string when none).
 *
 *  Resolution precedence:
 *    1. `override` — when non-empty, takes effect. Absolute paths pass
 *       through unchanged; relative paths are resolved against the
 *       directory containing `inpPath`.
 *    2. Sibling default — `<inpStem>.<rpt|out>` next to the .inp.
 *
 *  Empty `inpPath` returns an empty string regardless of override.
 * @brief Resolve one report/binary destination without reading global preferences.
 * @param[in] inpPath Input path whose parent/stem provide defaults; empty returns empty.
 * @param[in] override_ Override trimmed before use; relative overrides are resolved against the input directory.
 * @param[in] kind Rpt for report output, otherwise binary output.
 * @return Cleaned absolute override or sibling default path; does not check existence, writability, or aliases.
 */
[[nodiscard]] QString resolveRunOutputPath(const QString &inpPath, const QString &override_,
                                           RunOutputKind kind);

/*! Live-QSettings convenience wrapper used by SWMMVis::runActiveProject.
 *  Reads the per-project override from QSettings under
 *  `SWMMVis/Project/<inpPath>/{Report,Output}FilePath` (matching Slice
 *  AA-4's key layout) and dispatches to the pure resolver above.
 *  Tests use the pure variant; production code uses this.
 * @brief Resolve the per-project output preference through QSettings.
 * @param[in] inpPath Input path used verbatim in the preference key.
 * @param[in] kind Report or binary output selector.
 * @return resolveRunOutputPath() result using the stored override; empty input returns empty.
 */
[[nodiscard]] QString resolveRunOutputPathFromSettings(const QString &inpPath, RunOutputKind kind);

/*! Hand the per-project QSettings group `SWMMVis/Project/<path>/…` (2D module
 *  intent, report/output path overrides, Save As filter) from \p fromPath to
 *  \p toPath when a project is saved under a new name.
 *
 *  Whatever an OLDER project left under \p toPath is removed first — those
 *  keys are keyed only by file path, so without this a new project saved over
 *  an old one inherited its settings. The current project's keys under
 *  \p fromPath are then copied across (and kept at \p fromPath, which may
 *  still be another project on disk). An empty \p fromPath (untitled
 *  project) only clears the destination. No-op when the paths are equal.
 * @param[in] fromPath Source preference-key path, empty to copy no values.
 * @param[in] toPath Destination preference-key path; empty or identical to source is a no-op.
 * @details Replaces the destination group before copying source keys, retaining the original group; QSettings persistence errors are not reported.
 */
void moveProjectSettings(const QString &fromPath, const QString &toPath);

} // namespace openswmmvis

#endif // RUNPATHRESOLVER_H
