/**
 * @file      examplesseeder.h
 * @brief     Bundled-example seeding and discovery for the Welcome screen.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Bundled-example seeding and discovery for the Welcome screen.
 *
 * The read-only install payload (macOS &lt;bundle&gt;/Resources/examples,
 * Linux/Windows &lt;prefix&gt;/share/openswmmgui/examples) is synced
 * incrementally into the per-user data dir
 * (QStandardPaths::AppLocalDataLocation + "/examples") at every startup, and the
 * Welcome panel lists whatever directory the seeder hands back. Opening an
 * example ALWAYS copies it to a user-chosen folder first — the baseline
 * (both install payload and appdata mirror) is never opened in place, so
 * simulation results can never pollute it.
 *
 * Widget-free (Qt Core only) so everything here is unit-testable headless.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PROJECT_EXAMPLESSEEDER_H
#define OPENSWMMVIS_PROJECT_EXAMPLESSEEDER_H

#include <QString>
#include <QVector>

namespace openswmmvis::project::examples
{

/*! One entry for the Welcome screen's Example Projects panel. */
struct ExampleInfo {
    QString displayName; //!< From example.json "name", else prettified file/dir name.
    QString description; //!< From example.json "description"; may be empty.
    QString category;    //!< From example.json "category"; empty = the Welcome
                         //!< panel's default group (it owns that group's label,
                         //!< which needs tr() — this header stays widget-free).
    QString openPath;    //!< Absolute path of the .oswp (preferred) or .inp inside sourceRoot.
    QString sourceRoot; //!< Directory to copy (dir example) or the single .inp file (flat example).
    bool isDirectory = false; //!< Directory-per-example vs legacy flat .inp.
};

/*! Name of the version-marker file written into the appdata examples dir
 *  ("\.seeded_version"). Excluded from discovery and from copy-on-open.
 * @return Marker basename .seeded_version.
 */
QString seedMarkerFileName();

/*! First existing candidate of the install payload dir, matching the
 *  historical Welcome-panel scan: &lt;appdir&gt;/../share/openswmmgui/examples,
 *  &lt;appdir&gt;/../Resources/examples (macOS bundle), &lt;appdir&gt;/examples.
 *  Empty when none exists (dev build without staged examples).
 * @return Absolute first existing install directory, or empty if none exists; no directory is created.
 */
QString installExamplesDir();

/*! The per-user writable mirror:
 *  QStandardPaths::writableLocation(AppLocalDataLocation) + "/examples".
 *  Requires the application identity (org/app name) to be set first.
 * @return Writable-location path plus /examples, or empty if the platform supplies no location; no creation is attempted.
 */
QString appDataExamplesDir();

/*!
 * \brief Recursively copies \a srcDir into \a dstDir (created if missing).
 *
 * Existing destination files are overwritten only when size or mtime differ
 * (so re-seeding is cheap and user file timestamps survive no-op syncs).
 * The seed marker file is never copied.
 *
 * \return false on the first failed mkdir/copy, with \a err describing it.
 * @param[in] srcDir Existing source directory; hidden entries are included.
 * @param[in] dstDir Destination tree, created as needed.
 * @param[out] err Optional first mkdir/remove/copy diagnostic.
 * @note A failure can leave earlier files copied or an old destination removed; no rollback occurs. Restoring modification times is best-effort.
 */
bool copyDirectoryRecursively(const QString &srcDir, const QString &dstDir, QString *err = nullptr);

/*!
 * \brief Syncs the install payload into the per-user dir.
 *
 * The tree is always walked (see copyDirectoryRecursively — a per-file
 * size + mtime no-op, so an up-to-date mirror costs one stat per file) and
 * the marker is rewritten with \a version as a record of the last seeding
 * app. There is deliberately no marker-equals-version fast path: the
 * payload changes within a version on every dev build and on any release
 * that adds examples without a version bump, and a fast path left the
 * mirror — and therefore the Welcome page — without the new examples.
 *
 * Known limitation: the sync only adds and refreshes. An example removed
 * from the install payload is NOT pruned from the mirror and keeps
 * appearing on the Welcome page until the per-user examples dir is deleted.
 *
 * \return false when the destination could not be created/written.
 * @param[in] srcDir Existing install example tree.
 * @param[in] dstDir Writable mirror to create/refresh.
 * @param[in] version Marker content, trimmed before UTF-8 encoding.
 * @param[out] err Optional first copy/marker-open diagnostic.
 * @note The marker write count is not checked after opening. Sync may leave partial updates on failure.
 */
bool syncFromInstall(const QString &srcDir, const QString &dstDir, const QString &version,
                     QString *err = nullptr);

/*!
 * \brief Seeds and returns the directory the Welcome panel should scan.
 *
 * Sync install → appdata and return the appdata dir; on any failure
 * (unwritable appdata, …) warn and fall back to the read-only install dir.
 * Empty when no install payload exists either.
 * @param[in] version App version recorded during this synchronization.
 * @return Per-user mirror on success, install directory on sync failure, or empty if no install payload exists. Failures are logged.
 */
QString preferredExamplesDir(const QString &version);

/*!
 * \brief Enumerates examples in \a dir.
 *
 * Directory examples: every subdirectory containing at least one .oswp
 * (preferred open target) or .inp. Flat examples: every top-level .inp
 * (legacy single-file bundles). Display name/description/category come from
 * an optional example.json manifest
 * ({"name": …, "description": …, "category": …}) in the subdirectory,
 * falling back to the prettified dir/file base name ('_'/'-' → spaces) and
 * an empty category.
 * @param[in] dir Directory containing immediate example subdirectories and flat .inp files.
 * @return Directory examples first, then flat inputs, alphabetically within each group; empty if the directory does not exist. Invalid/unreadable manifests silently use defaults.
 */
QVector<ExampleInfo> discoverExamples(const QString &dir);

} // namespace openswmmvis::project::examples

#endif // OPENSWMMVIS_PROJECT_EXAMPLESSEEDER_H
