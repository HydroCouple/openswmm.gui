/*!
 * \file   runpathresolver.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */

#ifndef RUNPATHRESOLVER_H
#define RUNPATHRESOLVER_H

#include <QString>

namespace openswmmvis {

/*! Which sibling extension is being resolved. */
enum class RunOutputKind
{
    Rpt,   ///< Sibling `.rpt` — SWMM summary report file.
    Out    ///< Sibling `.out` — SWMM binary results file.
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
 */
[[nodiscard]] QString
resolveRunOutputPath(const QString &inpPath,
                     const QString &override_,
                     RunOutputKind  kind);

/*! Live-QSettings convenience wrapper used by SWMMVis::runActiveProject.
 *  Reads the per-project override from QSettings under
 *  `SWMMVis/Project/<inpPath>/{Report,Output}FilePath` (matching Slice
 *  AA-4's key layout) and dispatches to the pure resolver above.
 *  Tests use the pure variant; production code uses this. */
[[nodiscard]] QString
resolveRunOutputPathFromSettings(const QString &inpPath,
                                 RunOutputKind  kind);

} // namespace openswmmvis

#endif // RUNPATHRESOLVER_H
