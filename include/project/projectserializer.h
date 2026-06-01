/*!
 * \file   projectserializer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * `.oswp` sidecar serializer.  Captures the GUI-only state that
 * can't round-trip through SWMM's INP format: layer CRS, category
 * order, hidden objects, canvas CRS + extent, basemaps, and (in
 * v4+) the project's instance list.
 *
 * Schema history:
 *   v0 — pre-Slice-X stub  ({ layers: [{ path }] }).  No-op apply.
 *   v1 — Slice X first cut.  Flat root: inpPath + layer + canvas.
 *   v2 — reserved (relative-path migration; never shipped).
 *   v3 — basemaps[] added.
 *   v4 — sessions[] array (Slice AA-3.2):  inpPath / engineVersion /
 *        layer move into sessions[N]; root keeps canvas + basemaps +
 *        (future) preferences.  All path fields stored relative to
 *        the .oswp directory.
 *
 * Forward-compat: unknown root keys are skipped silently.  Apply
 * accepts every prior schema and migrates in-memory.
 */

#ifndef PROJECTSERIALIZER_H
#define PROJECTSERIALIZER_H

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <QVector>

class SWMMVisProjectWindow;
class SWMMVisProject;
class OpenSWMMVisLayer;

class ProjectSerializer
{
public:
    /*! Current writer version. */
    static constexpr int kCurrentSchemaVersion = 4;

    /*! Write a `.oswp` sidecar describing \p pw's current GUI state.
     *  Single-instance convenience overload: synthesises a one-entry
     *  sessions[] array.  Returns true on success; writes an error
     *  string on failure. */
    static bool saveToFile(const QString &oswpPath,
                           SWMMVisProjectWindow *pw,
                           QString *errorOut = nullptr);

    /*! Multi-instance overload (AA-3.4).  Iterates \p proj's instance
     *  list and writes one sessions[N] block per window, plus the
     *  project-level canvas / basemaps / preferences blocks. */
    static bool saveToFile(const QString &oswpPath,
                           SWMMVisProject *proj,
                           QString *errorOut = nullptr);

    /*! Read a `.oswp` and apply any state it carries to \p pw's
     *  already-loaded SWMM layer and canvas. Silently skips keys the
     *  current build doesn't understand (forward-compat). Returns
     *  true if the file was parsed successfully. An absent file is
     *  not an error — callers Just-Load the `.inp` and call this
     *  opportunistically.  Single-instance overload reads sessions[0]. */
    static bool applyFromFile(const QString &oswpPath,
                              SWMMVisProjectWindow *pw,
                              QString *errorOut = nullptr);

    /*! Canonical sidecar path: same directory + basename as the
     *  given `.inp`, with the extension swapped to `.oswp`. Empty
     *  input returns empty. Inlined alongside toRelativePath /
     *  resolveStoredPath (Slice RB.5) so unit tests can exercise
     *  it without linking the full ProjectSerializer .cpp. */
    [[nodiscard]] static inline QString sidecarPathFor(const QString &inpPath);

    // ------------------------------------------------------------
    // Path helpers (Slice AA-3.2) — used by every file-reference
    // field written to / read from `.oswp`.  Two callers today
    // (inpPath and basemap urls don't apply); future slices reuse.
    // ------------------------------------------------------------

    /*! Convert \p path to a path relative to the directory containing
     *  \p oswpFile.  Returns the cleaned absolute path unchanged when
     *  the two are on different volumes (cross-drive on Windows) or
     *  when \p path is already empty.
     *  Uses QDir::relativeFilePath — does **not** resolve symlinks,
     *  to preserve the user's symlink intent across save/load.
     *  Inlined so unit tests can exercise it without linking the
     *  full ProjectSerializer .cpp (which pulls in MapCanvas etc.). */
    [[nodiscard]] static inline QString toRelativePath(const QString &path,
                                                        const QString &oswpFile);

    /*! Resolve a path read from a `.oswp` file: relative paths are
     *  resolved against \p oswpFile's parent directory; absolute paths
     *  pass through unchanged (v1–v3 backward compat); empty input
     *  returns empty.  Inlined alongside toRelativePath. */
    [[nodiscard]] static inline QString resolveStoredPath(const QString &stored,
                                                            const QString &oswpFile);

private:
    static QJsonObject serializeSession(SWMMVisProjectWindow *pw,
                                        const QString &oswpFile);
    static bool        applySession(const QJsonObject &sessionObj,
                                    SWMMVisProjectWindow *pw,
                                    const QString &oswpFile);

    static bool        writeRootJson(const QString &oswpPath,
                                     const QVector<SWMMVisProjectWindow *> &windows,
                                     QString *errorOut);

    static QJsonObject serializeBasemapLayer(OpenSWMMVisLayer *layer);
    static OpenSWMMVisLayer *deserializeBasemapLayer(const QJsonObject &obj,
                                                     QObject *parent);
};

// ---------------------------------------------------------------------------
// Inline path helpers (Slice AA-3.2)
// ---------------------------------------------------------------------------

inline QString ProjectSerializer::toRelativePath(const QString &path,
                                                  const QString &oswpFile)
{
    if (path.isEmpty()) return {};
    const QFileInfo oswpFi(oswpFile);
    const QDir oswpDir = oswpFi.absoluteDir();
    const QString cleanedAbs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    // QDir::relativeFilePath returns the absolute path unchanged when the
    // two are on different volumes (cross-drive on Windows) or can't be
    // made relative — exactly the fallback we want.
    return oswpDir.relativeFilePath(cleanedAbs);
}

inline QString ProjectSerializer::resolveStoredPath(const QString &stored,
                                                     const QString &oswpFile)
{
    if (stored.isEmpty()) return {};
    if (QDir::isAbsolutePath(stored))
        return QDir::cleanPath(stored);
    const QFileInfo oswpFi(oswpFile);
    const QDir oswpDir = oswpFi.absoluteDir();
    return QDir::cleanPath(oswpDir.absoluteFilePath(stored));
}

inline QString ProjectSerializer::sidecarPathFor(const QString &inpPath)
{
    if (inpPath.isEmpty()) return {};
    const QFileInfo fi(inpPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() +
                                     QStringLiteral(".oswp"));
}

#endif // PROJECTSERIALIZER_H
