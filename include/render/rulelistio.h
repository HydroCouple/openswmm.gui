/**
 * @file      rulelistio.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Save / load Rule Lists as .swmm-rule.json files (Slice Z.17).
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
 *         (i.e. unsupported by Rule::fromJson()) are
 *         skipped with a warning, leaving the rest of the list intact.
 *
 *         Slice Z.17-files ships this native pair. The `.qml` (QGIS)
 *         interop subset is the named Z.17b follow-up — both sides
 *         share the Result + IO conventions defined here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_RULELISTIO_H
#define OPENSWMM_RENDER_RULELISTIO_H

#include <QString>
#include <QStringList>

namespace OpenSWMM::Render
{

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
struct RuleListIoResult {
    bool ok = false;
    QString error;
    QStringList warnings;
    int rulesLoaded = 0;  /*!< Populated by load only. */
    int rulesSkipped = 0; /*!< Populated by load only. */
};

namespace RuleListIO
{

/*! Format discriminator written into the file envelope's "format" key. */
inline constexpr const char *kFormat = "swmm-rule-json";

/*! Current schema version. A mismatch produces a warning while the loader
 *  proceeds optimistically; no version-specific migration is performed. */
inline constexpr int kVersion = 1;

/*!
 * \brief Serialise \p list as a top-level JSON envelope and write it to
 *        \p path. The file is overwritten if it already exists.
 *
 *        Uses QJsonDocument::Indented formatting so the result is diff-friendly
 *        for users who manage style sets in git.
 *
 *        \p list may be null — produces an empty file with the envelope
 *        and zero rules, useful as a template.
 */
/** @param list Borrowed list read synchronously; null produces a valid envelope
 * with an empty rules array and empty symbolLevels object. No style validation occurs.
 * @param path Destination path; empty paths fail. QSaveFile writes a temporary
 * file and commits after a complete write; short writes are cancelled. No extension
 * is enforced.
 * @return ok on successful commit, otherwise an error describing open/write/commit
 * failure. The source list is never modified. */
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
/** @param path Source JSON file; requires an object containing a rules array.
 * Format/version mismatches and unknown top-level keys warn without rejecting the file.
 * The symbolLevels envelope field is recognized but ignored.
 * @param[in,out] list Required target on its owning Qt thread. File/JSON/envelope
 * errors leave it unchanged. Nonobject/unrecognized-renderer entries are probed and
 * skipped before mutation; then clear()/fromJson() destroy and replace old rules
 * with normal notifications. Allocation/construction exceptions are not caught, so
 * this final replacement has no exception rollback guarantee.
 * @return ok and accepted/skipped counts after replacement, plus warnings. Unknown
 * renderer acceptance follows Rule::fromJson()'s concrete factory, not a plugin registry.
 * An entirely skipped/empty array is a successful load which empties the list. */
[[nodiscard]] RuleListIoResult load(const QString &path, RuleList *list);

} // namespace RuleListIO

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULELISTIO_H
