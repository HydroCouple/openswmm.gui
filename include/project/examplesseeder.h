/*!
 * \file   examplesseeder.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Bundled-example seeding and discovery for the Welcome screen.
 *
 * The read-only install payload (macOS <bundle>/Resources/examples,
 * Linux/Windows <prefix>/share/openswmmgui/examples) is synced
 * incrementally into the per-user data dir
 * (QStandardPaths::AppLocalDataLocation + "/examples") at every startup, and the
 * Welcome panel lists whatever directory the seeder hands back. Opening an
 * example ALWAYS copies it to a user-chosen folder first — the baseline
 * (both install payload and appdata mirror) is never opened in place, so
 * simulation results can never pollute it.
 *
 * Widget-free (Qt Core only) so everything here is unit-testable headless.
 */
#ifndef OPENSWMMVIS_PROJECT_EXAMPLESSEEDER_H
#define OPENSWMMVIS_PROJECT_EXAMPLESSEEDER_H

#include <QString>
#include <QVector>

namespace openswmmvis::project::examples {

/*! One entry for the Welcome screen's Example Projects panel. */
struct ExampleInfo
{
    QString displayName;   //!< From example.json "name", else prettified file/dir name.
    QString description;   //!< From example.json "description"; may be empty.
    QString category;      //!< From example.json "category"; empty = the Welcome
                           //!< panel's default group (it owns that group's label,
                           //!< which needs tr() — this header stays widget-free).
    QString openPath;      //!< Absolute path of the .oswp (preferred) or .inp inside sourceRoot.
    QString sourceRoot;    //!< Directory to copy (dir example) or the single .inp file (flat example).
    bool    isDirectory = false;  //!< Directory-per-example vs legacy flat .inp.
};

/*! Name of the version-marker file written into the appdata examples dir
 *  ("\.seeded_version"). Excluded from discovery and from copy-on-open. */
QString seedMarkerFileName();

/*! First existing candidate of the install payload dir, matching the
 *  historical Welcome-panel scan: <appdir>/../share/openswmmgui/examples,
 *  <appdir>/../Resources/examples (macOS bundle), <appdir>/examples.
 *  Empty when none exists (dev build without staged examples). */
QString installExamplesDir();

/*! The per-user writable mirror:
 *  QStandardPaths::writableLocation(AppLocalDataLocation) + "/examples".
 *  Requires the application identity (org/app name) to be set first. */
QString appDataExamplesDir();

/*!
 * \brief Recursively copies \a srcDir into \a dstDir (created if missing).
 *
 * Existing destination files are overwritten only when size or mtime differ
 * (so re-seeding is cheap and user file timestamps survive no-op syncs).
 * The seed marker file is never copied.
 *
 * \return false on the first failed mkdir/copy, with \a err describing it.
 */
bool copyDirectoryRecursively(const QString &srcDir, const QString &dstDir,
                              QString *err = nullptr);

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
 */
bool syncFromInstall(const QString &srcDir, const QString &dstDir,
                     const QString &version, QString *err = nullptr);

/*!
 * \brief Seeds and returns the directory the Welcome panel should scan.
 *
 * Sync install → appdata and return the appdata dir; on any failure
 * (unwritable appdata, …) warn and fall back to the read-only install dir.
 * Empty when no install payload exists either.
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
 */
QVector<ExampleInfo> discoverExamples(const QString &dir);

} // namespace openswmmvis::project::examples

#endif // OPENSWMMVIS_PROJECT_EXAMPLESSEEDER_H
