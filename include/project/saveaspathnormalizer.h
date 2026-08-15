/*!
 * \file   saveaspathnormalizer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Save As dialog result canonicalization (Slice RA, Phase RA.1).
 *
 * The Save As dialog can return paths with stacked extensions like
 * `model.inp.oswp` (when the dialog's selectFile() pre-fill carries a
 * `.inp` and the user then picks a `.oswp` filter without clearing
 * the basename — see §R.1 of GUI_IMPLEMENTATION_PLAN.md). Downstream
 * code in SWMMVis::onSaveAs uses QFileInfo::completeBaseName() which
 * strips only the last extension, producing `model.inp.inp` for the
 * .inp save and `model.inp.oswp` for the sidecar — both undesired.
 *
 * normalizeSaveAsPath() collapses any `<ext>.<ext>` duplication where
 * both halves are known writable kinds, then computes the canonical
 * inpPath and isProject flag the way onSaveAs needs them. The helper
 * is intentionally a pure function over the dialog string + a writable-
 * extensions set so the unit test (test_saveaspathnormalizer.cpp) can
 * exercise it without spinning up the dialog or FileFilterRegistry.
 */

#ifndef SAVEASPATHNORMALIZER_H
#define SAVEASPATHNORMALIZER_H

#include <QSet>
#include <QString>

namespace openswmmvis {

/*! Result of normalizeSaveAsPath. */
struct SaveAsPathResult
{
    /*! Canonical path to pass to SWMMVisProjectWindow::saveAs.
     *  - When isProject is true, this is `<stem>.inp` (the .inp the
     *    engine actually writes; the .oswp sidecar is written next
     *    to it by the caller using ProjectSerializer::sidecarPathFor).
     *  - When isProject is false, this is the dialog path with any
     *    `<ext>.<ext>` duplications collapsed (still ending in whatever
     *    writable extension the user picked — `.inp`, `.gpkg`, etc.). */
    QString inpPath;

    /*! True iff the dialog's *original* (post-collapse) extension is
     *  `.oswp` — i.e. the user intended a project-file save. The caller
     *  uses this to drive sidecar-only logic (e.g. picking the .oswp
     *  filename to round-trip). */
    bool isProject = false;

    /*! True iff at least one duplicate-extension layer was collapsed.
     *  The caller can surface a debug log when set; not user-visible. */
    bool wasNormalized = false;
};

/*! Collapse duplicated writable extensions and compute (inpPath, isProject).
 *
 *  \param dialogPath  Raw path returned by QFileDialog::selectedFiles().
 *                     Empty input returns an empty result with
 *                     isProject=false, wasNormalized=false.
 *  \param writableExtensions  Lowercase extensions (no leading dot) that
 *                     the GUI's FileFilterRegistry recognises as writable
 *                     formats — today: `{"inp", "oswp", "gpkg"}`. The
 *                     normalizer only collapses `<ext>.<ext>` duplicates
 *                     when *both* halves appear in this set, so a user-
 *                     coined filename like `model.bak.bak` (custom kind)
 *                     passes through unchanged.
 *  \return SaveAsPathResult with the canonical inpPath + isProject flag.
 *
 *  Examples (writableExtensions = {inp, oswp, gpkg}):
 *    "/p/m.inp"          → {"/p/m.inp",  isProject=false, wasNormalized=false}
 *    "/p/m.inp.inp"      → {"/p/m.inp",  isProject=false, wasNormalized=true}
 *    "/p/m.oswp"         → {"/p/m.inp",  isProject=true,  wasNormalized=false}
 *    "/p/m.inp.oswp"     → {"/p/m.inp",  isProject=true,  wasNormalized=true}
 *    "/p/m.oswp.inp"     → {"/p/m.inp",  isProject=false, wasNormalized=true}
 *    "/p/m.gpkg.gpkg"    → {"/p/m.gpkg", isProject=false, wasNormalized=true}
 *    "/p/m.bak.bak"      → {"/p/m.bak.bak", isProject=false, wasNormalized=false}
 *    "/p/m.inp.inp.inp"  → {"/p/m.inp",  isProject=false, wasNormalized=true}
 *    ""                  → {"",          isProject=false, wasNormalized=false}
 */
[[nodiscard]] SaveAsPathResult
normalizeSaveAsPath(const QString &dialogPath,
                    const QSet<QString> &writableExtensions);

} // namespace openswmmvis

#endif // SAVEASPATHNORMALIZER_H
