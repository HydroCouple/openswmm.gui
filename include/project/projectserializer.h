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

#include "mesh/channelburnprofile.h"

#include <QDir>
#include <QJsonArray>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <QVector>

class SWMMVisProjectWindow;
class SWMMVisProject;
class OpenSWMMVisLayer;
class MapCanvas;

class ProjectSerializer
{
public:
    /*! Current writer version. */
    /*! Schema 5 (2026-09-07) added the `"feature"` gisLayers type — an
     *  editable, GeoPackage-backed FeatureLayer carrying role / zPolicy /
     *  symbol alongside the path-shaped record the other two types use.
     *  Readers of schema 4 dispatch only on "raster" / "vector" and fall
     *  through silently on an unknown type, so older builds degrade by
     *  skipping feature layers rather than failing to open the project. */
    static constexpr int kCurrentSchemaVersion = 5;

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
     *  opportunistically.  Single-instance overload reads sessions[0].
     *  Referenced files that no longer exist (results .out, GIS
     *  rasters/vectors) are skipped and reported via \p warningsOut
     *  so the caller can surface them — the load itself proceeds. */
    static bool applyFromFile(const QString &oswpPath,
                              SWMMVisProjectWindow *pw,
                              QString *errorOut = nullptr,
                              QStringList *warningsOut = nullptr);

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

    /*! Channel burn-in settings ↔ JSON (CHANNEL_BURN_IN_PLAN_2026-09-21.md D-H).
     *
     *  Written only when the tab has been switched on, so a project that never
     *  used the burn keeps the same .oswp it had before the feature existed.
     *  Every read falls back to the struct's own default, so an older file — or a
     *  newer one missing a key — loads as the defaults rather than as zeros. */
    [[nodiscard]] static inline QJsonObject channelBurnToJson(const mesh::ChannelBurnSettings &s)
    {
        QJsonObject o;
        o[QStringLiteral("enabled")] = s.enabled;
    
        QJsonObject sel;
        sel[QStringLiteral("mode")] = int(s.selector.mode);
        if (!s.selector.query.isEmpty())
            sel[QStringLiteral("query")] = s.selector.query;
        if (!s.selector.conduitIds.isEmpty())
            sel[QStringLiteral("conduits")] =
                QJsonArray::fromStringList(s.selector.conduitIds);
        o[QStringLiteral("selector")] = sel;
    
        const mesh::BurnOptions &b = s.options;
        QJsonObject op;
        op[QStringLiteral("forceHalfWidth")]  = b.forceHalfWidth;
        op[QStringLiteral("maxHalfWidth")]    = b.maxHalfWidth;
        op[QStringLiteral("clipToBanks")]     = b.clipToBanks;
        op[QStringLiteral("bankPad")]         = b.bankPad;
        op[QStringLiteral("chainageStep")]    = b.chainageStep;
        op[QStringLiteral("lateralStep")]     = b.lateralStep;
        op[QStringLiteral("stringCount")]     = b.stringCount;
        op[QStringLiteral("anchor")]          = int(b.anchor);
        op[QStringLiteral("sectionBlend")]    = b.sectionBlend;
        op[QStringLiteral("enforceMonotone")] = b.enforceMonotone;
        op[QStringLiteral("maxIncision")]     = b.maxIncision;
        op[QStringLiteral("burnStreets")]     = b.burnStreets;
        op[QStringLiteral("quadCorridor")]    = b.quadCorridor;
        op[QStringLiteral("channelCellSize")] = b.channelCellSize;
        op[QStringLiteral("roughnessFromTransect")] = b.roughnessFromTransect;
        op[QStringLiteral("removeBurnedFrom1D")]    = b.removeBurnedFrom1D;
        op[QStringLiteral("convertInterfaceNodes")] = b.convertInterfaceNodes;
        op[QStringLiteral("truncateAtBoundary")]    = b.truncateAtBoundary;
        o[QStringLiteral("options")] = op;
        return o;
    }

    [[nodiscard]] static inline mesh::ChannelBurnSettings channelBurnFromJson(const QJsonObject &o)
    {
        mesh::ChannelBurnSettings s;
        s.enabled = o.value(QStringLiteral("enabled")).toBool(s.enabled);
    
        const QJsonObject sel = o.value(QStringLiteral("selector")).toObject();
        const int mode = sel.value(QStringLiteral("mode")).toInt(int(s.selector.mode));
        if (mode >= int(mesh::BurnSelector::Mode::AllOpen)
            && mode <= int(mesh::BurnSelector::Mode::ExplicitList))
            s.selector.mode = mesh::BurnSelector::Mode(mode);
        s.selector.query = sel.value(QStringLiteral("query")).toString();
        for (const QJsonValue &v : sel.value(QStringLiteral("conduits")).toArray())
            s.selector.conduitIds << v.toString();
    
        const QJsonObject op = o.value(QStringLiteral("options")).toObject();
        mesh::BurnOptions &b = s.options;
        b.forceHalfWidth  = op.value(QStringLiteral("forceHalfWidth")).toDouble(b.forceHalfWidth);
        b.maxHalfWidth    = op.value(QStringLiteral("maxHalfWidth")).toDouble(b.maxHalfWidth);
        b.clipToBanks     = op.value(QStringLiteral("clipToBanks")).toBool(b.clipToBanks);
        b.bankPad         = op.value(QStringLiteral("bankPad")).toDouble(b.bankPad);
        b.chainageStep    = op.value(QStringLiteral("chainageStep")).toDouble(b.chainageStep);
        b.lateralStep     = op.value(QStringLiteral("lateralStep")).toDouble(b.lateralStep);
        b.stringCount     = op.value(QStringLiteral("stringCount")).toInt(b.stringCount);
        const int anchor  = op.value(QStringLiteral("anchor")).toInt(int(b.anchor));
        if (anchor >= int(mesh::SectionAnchor::Thalweg)
            && anchor <= int(mesh::SectionAnchor::StationZero))
            b.anchor = mesh::SectionAnchor(anchor);
        b.sectionBlend    = op.value(QStringLiteral("sectionBlend")).toDouble(b.sectionBlend);
        b.enforceMonotone = op.value(QStringLiteral("enforceMonotone")).toBool(b.enforceMonotone);
        b.maxIncision     = op.value(QStringLiteral("maxIncision")).toDouble(b.maxIncision);
        b.burnStreets     = op.value(QStringLiteral("burnStreets")).toBool(b.burnStreets);
        b.quadCorridor    = op.value(QStringLiteral("quadCorridor")).toBool(b.quadCorridor);
        b.channelCellSize = op.value(QStringLiteral("channelCellSize")).toDouble(b.channelCellSize);
        b.roughnessFromTransect =
            op.value(QStringLiteral("roughnessFromTransect")).toBool(b.roughnessFromTransect);
        b.removeBurnedFrom1D =
            op.value(QStringLiteral("removeBurnedFrom1D")).toBool(b.removeBurnedFrom1D);
        b.convertInterfaceNodes =
            op.value(QStringLiteral("convertInterfaceNodes")).toBool(b.convertInterfaceNodes);
        b.truncateAtBoundary =
            op.value(QStringLiteral("truncateAtBoundary")).toBool(b.truncateAtBoundary);
        return s;
    }

private:
    static QJsonObject serializeSession(SWMMVisProjectWindow *pw,
                                        const QString &oswpFile);
    static bool        applySession(const QJsonObject &sessionObj,
                                    SWMMVisProjectWindow *pw,
                                    const QString &oswpFile,
                                    QStringList *warningsOut = nullptr);

    static bool        writeRootJson(const QString &oswpPath,
                                     const QVector<SWMMVisProjectWindow *> &windows,
                                     QString *errorOut);

    static QJsonObject serializeBasemapLayer(OpenSWMMVisLayer *layer,
                                             const QString &oswpPath);
    static OpenSWMMVisLayer *deserializeBasemapLayer(const QJsonObject &obj,
                                                     QObject *parent,
                                                     const QString &oswpPath,
                                                     QStringList *warningsOut = nullptr);

    // GIS data layers (schema v4+) — loaded rasters (GDAL) and vector
    // datasets (OGR shapefiles, GeoPackage, …). Persisted by source path so
    // they reopen on project load. deserializeGisLayer opens asynchronously
    // and adds the layer to \p canvas on completion.
    static QJsonObject serializeGisLayer(OpenSWMMVisLayer *layer,
                                         const QString &oswpPath);
    static void        deserializeGisLayer(const QJsonObject &obj,
                                           MapCanvas *canvas,
                                           const QString &oswpPath,
                                           QStringList *warningsOut = nullptr);
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
