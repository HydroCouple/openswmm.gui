/**
 * @file      projectserializer.h
 * @brief     `.oswp` sidecar serializer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /** Schema 6 adds native time-series datasets, recoverable missing references,
     * optional feature links and model-assignment provenance. */
    static constexpr int kCurrentSchemaVersion = 6;

    /*! Write a `.oswp` sidecar describing \p pw's current GUI state.
     *  Single-instance convenience overload: synthesises a one-entry
     *  sessions[] array.  Returns true on success; writes an error
     *  string on failure.
     * @param[in] oswpPath Nonempty destination sidecar filename.
     * @param[in] pw Borrowed non-null window with a model layer and usable canvas. Call from the GUI thread.
     * @param[out] errorOut Optional failure diagnostic; unchanged on success.
     * @return True after QSaveFile commits the sidecar; false for invalid state, unsaved/busy analysis, invalid recipes or IO failure.
     * @note Managed analysis assets may be copied to the destination directory before writing the sidecar; those copies are not rolled back on a later failure. Model input data is not saved by this method.
     * Direct calls publish a correlated Sidecar save outcome and adopt the logical
     * project path only on success. Nested model-save/recovery stages join their
     * outer operation and do not publish intermediate persistence or path identities.
     */
    static bool saveToFile(const QString &oswpPath, SWMMVisProjectWindow *pw,
                           QString *errorOut = nullptr);

    /*! Multi-instance overload (AA-3.4).  Iterates \p proj's instance
     *  list and writes one sessions[N] block per window, plus the
     *  project-level canvas / basemaps / preferences blocks.
     * @param[in] oswpPath Nonempty destination sidecar filename.
     * @param[in] proj Borrowed non-null project with at least one registered window; live windows must have usable canvases.
     * @param[out] errorOut Optional failure diagnostic.
     * @return True after committing the sidecar; false for empty project, invalid recipes/analysis state or IO failure.
     * @note Null entries are skipped; project canvas/basemap/GIS state comes from the first entry. Managed analysis copies can remain after a later failure. Invoke on the GUI thread.
     * All affected sessions share a save correlation identity; synchronous collection
     * revalidates every window lifetime before writing or adopting the project path.
     */
    static bool saveToFile(const QString &oswpPath, SWMMVisProject *proj,
                           QString *errorOut = nullptr);

    /*! Read a `.oswp` and apply any state it carries to \p pw's
     *  already-loaded SWMM layer and canvas. Silently skips keys the
     *  current build doesn't understand (forward-compat). Returns
     *  true if the file was parsed successfully. An absent or unreadable file returns false with a diagnostic. Callers may choose to load only the `.inp`. The selected modern session is applied to this window.
     *  Referenced files that no longer exist (results .out, GIS
     *  rasters/vectors) are skipped and reported via \p warningsOut
     *  so the caller can surface them — the load itself proceeds.
     * @param[in] oswpPath Existing readable JSON sidecar filename.
     * @param[in,out] pw Required window with its model already loaded; receives GUI state and new layers on the GUI thread.
     * @param[out] errorOut Optional file/JSON/corridor-recipe diagnostic.
     * @param[in,out] warningsOut Optional list receiving skipped-file/invalid-history warnings; asynchronous open failures are not uniformly reported here.
     * @param[in] sessionIndex Zero-based entry in a modern sessions array; legacy root formats require zero.
     * @return False for invalid arguments/session selection, unreadable/invalid JSON or rejected corridor recipe; true for applied or legacy no-op content, even when referenced layers were skipped.
     * @note Existing layers are not first cleared. Results/raster/vector opens may finish later; editable feature tables open synchronously. A previously clean window is restored to clean after synchronous hydration.
     */
    static bool applyFromFile(const QString &oswpPath, SWMMVisProjectWindow *pw,
                              QString *errorOut = nullptr, QStringList *warningsOut = nullptr,
                              int sessionIndex = 0);

    /*! Canonical sidecar path: same directory + basename as the
     *  given `.inp`, with the extension swapped to `.oswp`. Empty
     *  input returns empty. Inlined alongside toRelativePath /
     *  resolveStoredPath (Slice RB.5) so unit tests can exercise
     *  it without linking the full ProjectSerializer .cpp.
     * @param[in] inpPath Model path; no filesystem existence check is made.
     * @return Absolute sibling .oswp path using completeBaseName(), or empty for empty input.
     */
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
     *  full ProjectSerializer .cpp (which pulls in MapCanvas etc.).
     * @param[in] path Referenced filename; relative input first resolves against the process working directory.
     * @param[in] oswpFile Sidecar filename whose absolute parent is the output anchor.
     * @return Cleaned relative token, absolute cross-volume fallback, or empty for empty path.
     */
    [[nodiscard]] static inline QString toRelativePath(const QString &path,
                                                       const QString &oswpFile);

    /*! Resolve a path read from a `.oswp` file: relative paths are
     *  resolved against \p oswpFile's parent directory; absolute paths
     *  pass through unchanged (v1–v3 backward compat); empty input
     *  returns empty.  Inlined alongside toRelativePath.
     * @param[in] stored Stored filename token, including empty or absolute paths.
     * @param[in] oswpFile Sidecar filename whose parent anchors relative tokens.
     * @return Cleaned absolute path for a relative token, cleaned absolute token as supplied, or empty. No existence/symlink check occurs.
     */
    [[nodiscard]] static inline QString resolveStoredPath(const QString &stored,
                                                          const QString &oswpFile);

    /*! Channel burn-in settings ↔ JSON (CHANNEL_BURN_IN_PLAN_2026-09-21.md D-H).
     *
     *  Written even when disabled so configured options survive reopening.
     *  Every read falls back to the struct's own default, so an older file — or a
     *  newer one missing a key — loads as the defaults rather than as zeros.
     * @param[in] s Burn/export/selector settings; dimensioned values preserve BurnOptions units (metres for geometryTolerance) and configured export scale.
     * @return Version-3 JSON with explicit options and selector fields. No semantic validation or file copying occurs.
     */
    [[nodiscard]] static inline QJsonObject channelBurnToJson(const mesh::ChannelBurnSettings &s)
    {
        QJsonObject o;
        o[QStringLiteral("enabled")] = s.enabled;
        o[QStringLiteral("exportRaster")] = s.exportRaster;
        o[QStringLiteral("exportDemPath")] = s.exportDemPath;
        o[QStringLiteral("exportDirectory")] = s.exportDirectory;
        o[QStringLiteral("exportRasterZToSI")] = s.exportRasterZToSI;
        o[QStringLiteral("maxMeshCells")] = s.maxMeshCells;

        QJsonObject sel;
        sel[QStringLiteral("mode")] = int(s.selector.mode);
        if (!s.selector.query.isEmpty())
            sel[QStringLiteral("query")] = s.selector.query;
        if (!s.selector.conduitIds.isEmpty())
            sel[QStringLiteral("conduits")] = QJsonArray::fromStringList(s.selector.conduitIds);
        o[QStringLiteral("selector")] = sel;

        const mesh::BurnOptions &b = s.options;
        QJsonObject op;
        op[QStringLiteral("forceHalfWidth")] = b.forceHalfWidth;
        op[QStringLiteral("maxHalfWidth")] = b.maxHalfWidth;
        op[QStringLiteral("clipToBanks")] = b.clipToBanks;
        op[QStringLiteral("bankPad")] = b.bankPad;
        op[QStringLiteral("chainageStep")] = b.chainageStep;
        op[QStringLiteral("lateralStep")] = b.lateralStep;
        op[QStringLiteral("stringCount")] = b.stringCount;
        op[QStringLiteral("anchor")] = int(b.anchor);
        op[QStringLiteral("sectionBlend")] = b.sectionBlend;
        op[QStringLiteral("enforceMonotone")] = b.enforceMonotone;
        op[QStringLiteral("maxIncision")] = b.maxIncision;
        op[QStringLiteral("burnStreets")] = b.burnStreets;
        op[QStringLiteral("quadCorridor")] = b.quadCorridor;
        op[QStringLiteral("channelCellSize")] = b.channelCellSize;
        op[QStringLiteral("geometryToleranceM")] = b.geometryTolerance;
        op[QStringLiteral("channelAspectMax")] = b.channelAspectMax;
        o[QStringLiteral("version")] = 3;
        op[QStringLiteral("roughnessFromTransect")] = b.roughnessFromTransect;
        op[QStringLiteral("removeBurnedFrom1D")] = b.removeBurnedFrom1D;
        op[QStringLiteral("convertInterfaceNodes")] = b.convertInterfaceNodes;
        op[QStringLiteral("truncateAtBoundary")] = b.truncateAtBoundary;
        op[QStringLiteral("connectorLength")] = b.connectorLength;
        o[QStringLiteral("options")] = op;
        return o;
    }

    /**
     * @brief Decode persisted burn settings using defaults for missing or mistyped values.
     * @param[in] o Stored burn settings object, including an empty legacy object.
     * @return Value settings; enum ranges are checked, maxMeshCells is at least one, invalid exportRasterZToSI resets to one, aspect to four and geometryTolerance to 0.05 metres. Other option ranges are left for later validation.
     */
    [[nodiscard]] static inline mesh::ChannelBurnSettings channelBurnFromJson(const QJsonObject &o)
    {
        mesh::ChannelBurnSettings s;
        s.enabled = o.value(QStringLiteral("enabled")).toBool(s.enabled);
        s.exportRaster = o.value(QStringLiteral("exportRaster")).toBool(false);
        s.exportDemPath = o.value(QStringLiteral("exportDemPath")).toString();
        s.exportDirectory = o.value(QStringLiteral("exportDirectory")).toString();
        s.exportRasterZToSI = o.value(QStringLiteral("exportRasterZToSI")).toDouble(1.0);
        if (!std::isfinite(s.exportRasterZToSI) || s.exportRasterZToSI <= 0)
            s.exportRasterZToSI = 1.0;
        s.maxMeshCells = std::max(1, o.value(QStringLiteral("maxMeshCells")).toInt(2000000));

        const QJsonObject sel = o.value(QStringLiteral("selector")).toObject();
        const int mode = sel.value(QStringLiteral("mode")).toInt(int(s.selector.mode));
        if (mode >= int(mesh::BurnSelector::Mode::AllOpen) &&
            mode <= int(mesh::BurnSelector::Mode::ExplicitList))
            s.selector.mode = mesh::BurnSelector::Mode(mode);
        s.selector.query = sel.value(QStringLiteral("query")).toString();
        for (const QJsonValue &v : sel.value(QStringLiteral("conduits")).toArray())
            s.selector.conduitIds << v.toString();

        const QJsonObject op = o.value(QStringLiteral("options")).toObject();
        mesh::BurnOptions &b = s.options;
        b.forceHalfWidth = op.value(QStringLiteral("forceHalfWidth")).toDouble(b.forceHalfWidth);
        b.maxHalfWidth = op.value(QStringLiteral("maxHalfWidth")).toDouble(b.maxHalfWidth);
        b.clipToBanks = op.value(QStringLiteral("clipToBanks")).toBool(b.clipToBanks);
        b.bankPad = op.value(QStringLiteral("bankPad")).toDouble(b.bankPad);
        b.chainageStep = op.value(QStringLiteral("chainageStep")).toDouble(b.chainageStep);
        b.lateralStep = op.value(QStringLiteral("lateralStep")).toDouble(b.lateralStep);
        b.stringCount = op.value(QStringLiteral("stringCount")).toInt(b.stringCount);
        const int anchor = op.value(QStringLiteral("anchor")).toInt(int(b.anchor));
        if (anchor >= int(mesh::SectionAnchor::Thalweg) &&
            anchor <= int(mesh::SectionAnchor::StationZero))
            b.anchor = mesh::SectionAnchor(anchor);
        b.sectionBlend = op.value(QStringLiteral("sectionBlend")).toDouble(b.sectionBlend);
        b.enforceMonotone = op.value(QStringLiteral("enforceMonotone")).toBool(b.enforceMonotone);
        b.maxIncision = op.value(QStringLiteral("maxIncision")).toDouble(b.maxIncision);
        b.burnStreets = op.value(QStringLiteral("burnStreets")).toBool(b.burnStreets);
        b.quadCorridor = op.value(QStringLiteral("quadCorridor")).toBool(b.quadCorridor);
        b.channelCellSize = op.value(QStringLiteral("channelCellSize")).toDouble(b.channelCellSize);
        b.channelAspectMax = op.value(QStringLiteral("channelAspectMax")).toDouble(4.0);
        if (!std::isfinite(b.channelAspectMax) || b.channelAspectMax < 1)
            b.channelAspectMax = 4.0;
        b.geometryTolerance =
            op.value(QStringLiteral("geometryToleranceM")).toDouble(b.geometryTolerance);
        if (!std::isfinite(b.geometryTolerance) || b.geometryTolerance <= 0)
            b.geometryTolerance = 0.05;
        b.roughnessFromTransect =
            op.value(QStringLiteral("roughnessFromTransect")).toBool(b.roughnessFromTransect);
        b.removeBurnedFrom1D =
            op.value(QStringLiteral("removeBurnedFrom1D")).toBool(b.removeBurnedFrom1D);
        b.convertInterfaceNodes =
            op.value(QStringLiteral("convertInterfaceNodes")).toBool(b.convertInterfaceNodes);
        b.truncateAtBoundary =
            op.value(QStringLiteral("truncateAtBoundary")).toBool(b.truncateAtBoundary);
        b.connectorLength = op.value(QStringLiteral("connectorLength")).toDouble(b.connectorLength);
        return s;
    }

private:
    /**
     * @brief Capture GUI-only model, style, result-reference, annotation and recipe state for one window.
     * @param[in] pw Borrowed window; null or missing model returns an empty object. Live windows require a usable stats registry/canvas.
     * @param[in] oswpFile Destination sidecar filename anchoring referenced paths.
     * @return Session JSON with relative paths and selected default fields omitted.
     * @note Caller writeRootJson() first validates recipe stores and saved analyses; this helper assumes those preconditions rather than reporting their errors.
     */
    static QJsonObject serializeSession(SWMMVisProjectWindow *pw, const QString &oswpFile);
    /**
     * @brief Restore one session's metadata, styles, references and layer display state.
     * @param[in] sessionObj Stored session object; absent optional fields leave defaults where supported.
     * @param[in,out] pw Borrowed live target window; null is rejected. Its canvas must be available when restoring layers.
     * @param[in] oswpFile Source sidecar filename for resolving references.
     * @param[in,out] warningsOut Optional appended history/section and missing-result diagnostics.
     * @return False only for null pw; true can include skipped invalid optional state or pending asynchronous results opens.
     * @note Existing input CRS takes precedence over the sidecar CRS. Mesh styles match already loaded source identities; 2D result styles are deferred on the window. No transactional rollback is provided.
     */
    static bool applySession(const QJsonObject &sessionObj, SWMMVisProjectWindow *pw,
                             const QString &oswpFile, QStringList *warningsOut = nullptr);

    /**
     * @brief Validate session stores, migrate managed analysis assets and atomically write schema-5 sidecar JSON.
     * @param[in] oswpPath Nonempty sidecar destination filename.
     * @param[in] windows Nonempty borrowed window list; null entries are skipped, live entries require usable canvases.
     * @param[out] errorOut Optional failure diagnostic.
     * @return True after QSaveFile commit; false preserves the prior sidecar but may leave analysis asset copies/directories already created.
     * @note Project canvas/order/basemaps/GIS state is taken from windows.front(); call on the GUI thread after analyses finish saving.
     */
    static bool writeRootJson(const QString &oswpPath,
                              const QVector<SWMMVisProjectWindow *> &windows, QString *errorOut);

    /**
     * @brief Encode the supported service or local-raster basemap configuration.
     * @param[in] layer Borrowed XYZ/WMTS/WMS or flagged GIS raster layer; temporary, other or
     *   null pointers yield empty JSON.
     * @param[in] oswpPath Destination sidecar anchor for a local-raster path.
     * @return Type-discriminated configuration, including HTTP headers and raster style where applicable; no dataset bytes are embedded.
     */
    static QJsonObject serializeBasemapLayer(OpenSWMMVisLayer *layer, const QString &oswpPath);
    /**
     * @brief Construct a supported basemap and start local-raster loading when needed.
     * @param[in] obj Type-discriminated basemap JSON.
     * @param[in] parent QObject parent passed to XYZ construction; caller must adopt other returned layer types.
     * @param[in] oswpPath Source sidecar anchor for local paths.
     * @param[in,out] warningsOut Optional appended missing-local-file diagnostic.
     * @return Newly allocated layer for caller adoption, or null for unknown type, missing path or absent local file.
     * @note Local raster opens asynchronously and applies stored style after success; returning a pointer does not establish successful dataset loading.
     */
    static OpenSWMMVisLayer *deserializeBasemapLayer(const QJsonObject &obj, QObject *parent,
                                                     const QString &oswpPath,
                                                     QStringList *warningsOut = nullptr);

    // GIS data layers (schema v4+) — loaded rasters (GDAL) and vector
    // datasets (OGR shapefiles, GeoPackage, …). Persisted by source path so
    // they reopen on project load. deserializeGisLayer opens asynchronously
    // and adds the layer to \p canvas on completion.
    /**
     * @brief Encode persistent raster/vector/feature layer references and display state.
     * @param[in] layer Borrowed GIS layer; null, temporary, unsupported or pathless layers yield empty JSON.
     * @param[in] oswpPath Destination sidecar anchor for relative dataset references.
     * @return JSON carrying stable layer ID, type/path/name/visibility/opacity and supported sublayer/style/feature metadata.
     */
    static QJsonObject serializeGisLayer(OpenSWMMVisLayer *layer, const QString &oswpPath);
    /**
     * @brief Reopen a referenced GIS layer and add it to the canvas without an undo command.
     * @param[in] obj Stored raster/vector/feature record.
     * @param[in,out] canvas Borrowed target canvas and async callback lifetime context; null is ignored.
     * @param[in] oswpPath Sidecar anchor for the source reference.
     * @param[in,out] warningsOut Optional list for missing files or synchronous feature-table failures.
     * @note Raster/vector layers open asynchronously, are adopted on success and scheduled for deletion on failure. Feature tables open synchronously. Unknown types and empty references are ignored.
     */
    static void deserializeGisLayer(const QJsonObject &obj, MapCanvas *canvas,
                                    const QString &oswpPath, QStringList *warningsOut = nullptr);
};

// ---------------------------------------------------------------------------
// Inline path helpers (Slice AA-3.2)
// ---------------------------------------------------------------------------

inline QString ProjectSerializer::toRelativePath(const QString &path, const QString &oswpFile)
{
    if (path.isEmpty())
        return {};
    const QFileInfo oswpFi(oswpFile);
    const QDir oswpDir = oswpFi.absoluteDir();
    const QString cleanedAbs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    // QDir::relativeFilePath returns the absolute path unchanged when the
    // two are on different volumes (cross-drive on Windows) or can't be
    // made relative — exactly the fallback we want.
    return oswpDir.relativeFilePath(cleanedAbs);
}

inline QString ProjectSerializer::resolveStoredPath(const QString &stored, const QString &oswpFile)
{
    if (stored.isEmpty())
        return {};
    if (QDir::isAbsolutePath(stored))
        return QDir::cleanPath(stored);
    const QFileInfo oswpFi(oswpFile);
    const QDir oswpDir = oswpFi.absoluteDir();
    return QDir::cleanPath(oswpDir.absoluteFilePath(stored));
}

inline QString ProjectSerializer::sidecarPathFor(const QString &inpPath)
{
    if (inpPath.isEmpty())
        return {};
    const QFileInfo fi(inpPath);
    return fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".oswp"));
}

#endif // PROJECTSERIALIZER_H
