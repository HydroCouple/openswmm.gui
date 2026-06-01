/*!
 * \file   rulelistio.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Save / load Rule Lists as .swmm-rule.json files (Slice Z.17).
 *
 *         The Rule Model's native persistence format. See
 *         RENDERING_RULE_MODEL_PLAN.md §3.3 + §12.1 for the schema.
 *
 *         File envelope:
 *           {
 *             "format":  "swmm-rule-json",
 *             "version": 1,
 *             "rules":   [ <Rule JSON> ... ],
 *             "symbolLevels": { "enabled": false }
 *           }
 *
 *         Forward compatibility: unknown top-level keys produce a
 *         warning in the Result rather than failing the load. Rule
 *         entries whose renderer id is unknown to the current build
 *         (e.g. a renderer plugin that hasn't been registered) are
 *         skipped with a warning, leaving the rest of the list intact.
 *
 *         Slice Z.17-files ships this native pair. The `.qml` (QGIS)
 *         interop subset is the named Z.17b follow-up — both sides
 *         share the Result + IO conventions defined here.
 */

#ifndef OPENSWMM_RENDER_RULELISTIO_H
#define OPENSWMM_RENDER_RULELISTIO_H

#include <QString>
#include <QStringList>

namespace OpenSWMM::Render {

class RuleList;

/*!
 * \struct RuleListIoResult
 * \brief Outcome of a save / load operation.
 *
 *        Use \c ok as the primary boolean for the success / failure
 *        decision. \c warnings is populated even on success when the
 *        loader encountered forward-compat issues (unknown keys,
 *        skipped Rules). \c error is non-empty only when \c ok is
 *        false.
 */
struct RuleListIoResult
{
    bool         ok = false;
    QString      error;
    QStringList  warnings;
    int          rulesLoaded = 0;   /*!< Populated by load only. */
    int          rulesSkipped = 0;  /*!< Populated by load only. */
};

namespace RuleListIO {

/*! Format discriminator written into the file envelope's "format" key. */
inline constexpr const char *kFormat = "swmm-rule-json";

/*! Current schema version. Loaders accept this version exactly; future
 *  schema bumps land via a migration path keyed on this number. */
inline constexpr int kVersion = 1;

/*!
 * \brief Serialise \p list as a top-level JSON envelope and write it to
 *        \p path. The file is overwritten if it already exists.
 *
 *        Pretty-prints with 2-space indent so the result is diff-friendly
 *        for users who manage style sets in git.
 *
 *        \p list may be null — produces an empty file with the envelope
 *        and zero rules, useful as a template.
 */
[[nodiscard]] RuleListIoResult save(const RuleList *list, const QString &path);

/*!
 * \brief Read \p path and populate \p list. The list is cleared first
 *        when the file loads successfully; on failure \p list is left
 *        unchanged.
 *
 *        Loader is permissive on the envelope: missing "format" / "version"
 *        keys produce warnings rather than failures, so hand-edited or
 *        legacy files can still load. Unknown renderer ids inside the
 *        rules array are skipped with a warning entry per skipped Rule.
 */
[[nodiscard]] RuleListIoResult load(const QString &path, RuleList *list);

} // namespace RuleListIO

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULELISTIO_H
