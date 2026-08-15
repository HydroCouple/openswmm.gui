/*!
 * \file   ioportabilitynormalizer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Pre-save check for IO portability across `.inp` and `.gpkg`.
 *
 *         Slice IO-10. Sits between `SWMMVis::onSaveAs` (path dialog
 *         resolution) and `SWMMVisProjectWindow::saveAs` (the engine
 *         writer call). Walks every scalar external-file slot on the
 *         engine via the IO-9 typed C-API and produces:
 *
 *           - a per-slot preview of how the relative form would render
 *             against the destination directory (`.inp` target only);
 *           - a list of non-fatal warnings (cross-volume slots, missing
 *             USE-direction files, etc.) for the GUI status panel.
 *
 *         The normalizer never mutates the engine — the rebase math is
 *         the engine writer's job. Its purpose is to give the GUI an
 *         actionable preview so the user can fix problems before saving.
 *
 *         Vector slots (per-gage, per-timeseries, per-hot-start save)
 *         are covered by their respective editors in Slice IO-11; this
 *         first cut focuses on the 7 scalar `SWMM_FilePathRole`s that
 *         dominate the portability burden.
 *
 *         See `openswmm.gui/docs/IO_PORTABILITY_PLAN.md` §3.8.
 */
#ifndef OPENSWMMVIS_PROJECT_IO_PORTABILITY_NORMALIZER_H
#define OPENSWMMVIS_PROJECT_IO_PORTABILITY_NORMALIZER_H

#include <QString>
#include <QStringList>
#include <QVector>

// The engine handle is an opaque `void*` typedef. Include the public
// header directly — it's a slim C header with no transitive Qt pulls.
#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::project {

/*!
 * \brief Per-slot preview row returned by the normalizer.
 *
 * \details Carries enough information for the GUI to (a) display the
 *          slot in a "Paths" review pane, (b) flag any portability
 *          issue, and (c) provide a one-click "rebase" hint that the
 *          user can confirm or override.
 */
struct SlotPreview {
    int     role          = 0;   ///< SWMM_FilePathRole numeric value.
    QString role_label;          ///< Human-readable role name.
    QString owner;               ///< Empty for scalar slots.
    QString original;            ///< Token currently stored on the engine.
    QString absolute;            ///< Resolved absolute path (or empty).
    QString preview_relative;    ///< What the writer would emit (INP only).
    bool    crosses_volume = false;
    bool    file_missing   = false;
    QString warning;             ///< Empty on a clean slot.
};

/*!
 * \brief Outcome of a single pre-flight pass.
 *
 * \details `slotPreviews` always lists every scalar role examined; `warnings`
 *          deduplicates the per-slot warning strings for one-line
 *          surfacing in a status bar.
 */
struct PreflightResult {
    QVector<SlotPreview> slotPreviews;
    QStringList          warnings;
};

/*!
 * \class IoPortabilityNormalizer
 * \brief Stateless utility — every method is static.
 */
class IoPortabilityNormalizer
{
public:
    /*! Walk the scalar slots and produce previews against `dst_inp_path`. */
    static PreflightResult preflightInpSave(SWMM_Engine engine,
                                              const QString& dst_inp_path);

    /*! Walk the scalar slots and verify every USE-direction file exists. */
    static PreflightResult preflightGpkgSave(SWMM_Engine engine,
                                               const QString& dst_gpkg_path);

private:
    IoPortabilityNormalizer() = delete;
};

} // namespace openswmmvis::project

#endif // OPENSWMMVIS_PROJECT_IO_PORTABILITY_NORMALIZER_H
