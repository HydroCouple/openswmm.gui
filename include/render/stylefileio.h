/**
 * @file      stylefileio.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Native style-file (.swmm-style.json) import/export +
 *         minimal QGIS .qml import for the common renderer cases.
 *
 *         Slice X.23.  Goals:
 *           - Save / restore a layer's complete styling (renderer +
 *             label config) outside the .oswp container so the same
 *             style can be reused across projects.
 *           - Read enough of a QGIS .qml file to round-trip
 *             single-symbol and categorized vector styles. Categorized rows
 *             currently receive gray placeholders rather than resolving their
 *             referenced symbol definitions. Unsupported renderer types produce
 *             a warning; other unhandled details can be silently ignored.
 *
 *         Native format is JSON with this top-level shape:
 * @code{.json}
 *           {
 *             "schema":    "swmmvis-style/v1",
 *             "layerType": "SWMMModelLayer" | "GISVectorLayer" | ...,
 *             "renderer":  { <IFeatureRenderer::toJson()> },
 *             "labelConfig": { <LabelConfig::toJson()> },
 *             "kindRenderers": { "<kindKey>": <renderer>, ... }   // SWMM only
 *           }
 * @endcode
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_STYLEFILEIO_H
#define OPENSWMM_RENDER_STYLEFILEIO_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

class GISRasterLayer;
class OpenSWMMVisLayer;

namespace OpenSWMM::Render
{

class StyleFileIO
{
public:
    struct Result {
        /** Completion flag; a supported subset can succeed with warnings, and
         * unsupported QML renderers can leave styling unchanged with ok=true. */
        bool ok = false;
        /** Nonfatal schema/renderer compatibility messages, possibly empty. */
        QStringList warnings;
        /** Fatal error explanation when a file/parse/input operation fails. */
        QString errorMessage;
    };

    /*! Write \p layer's full style state to \p path as JSON.
     *  Replaces an existing file through QSaveFile commit.
     * @param layer Optional borrowed source layer; null returns an error result.
     * @param path Destination filesystem path, used verbatim without automatic
     * extension or parent-directory creation. The source layer is not mutated.
     * @return ok=true after commit, otherwise an open/commit/null-layer error.
     * Serializes styleToJson()'s snapshot, including applicable label defaults. */
    static Result exportStyle(const OpenSWMMVisLayer *layer, const QString &path);

    /*! Read a style file and apply it to \p layer.  Auto-detects
     *  native JSON vs QGIS XML using only the case-insensitive suffix: qml selects
     *  QML; every other suffix selects native JSON. No content sniffing occurs.
     * @param[in,out] layer Optional destination layer; null returns an error.
     * Apply on its owning Qt thread; concrete setters can emit repaint signals.
     * @param path Existing source path, read without modification.
     * @return Fatal read/parse errors leave styling untouched. Successful imports
     * apply their supported subset sequentially, without a transaction or rollback.
     * Unsupported QML renderer types return ok=true with a warning and retain the style. */
    static Result importStyle(OpenSWMMVisLayer *layer, const QString &path);

    /*! In-memory counterparts of exportStyle / importNative — the same
     *  JSON shape, no file involved. Used by LayerStyleDialog's Cancel /
     *  undo machinery to snapshot + restore the full renderer state (kind
     *  renderers, layer renderer, label config) that the per-subject
     *  Q_PROPERTY snapshots don't cover. Applicable labelConfig is included
     *  even at defaults, in both memory snapshots and exported files.
     * @param layer Optional borrowed layer, inspected synchronously without mutation.
     * @return Owned native-schema snapshot, or empty for null. Captures sublayers
     * for ISublayerHost, layer renderer, applicable SWMM kind renderers/labels,
     * GIS vector symbol/labels, or GIS raster configuration. */
    static QJsonObject styleToJson(const OpenSWMMVisLayer *layer);
    /** @brief Apply recognized style fields in place without schema/type enforcement.
     * @param[in,out] layer Optional destination layer on its owning Qt thread.
     * @param root Native-style object; absent keys generally retain current state.
     * Layer/kind renderer IDs supported here are single, graduated, categorized
     * and rule. Unknown IDs are silently ignored, including unclassed/multikind.
     * @return Error for null, otherwise ok=true even when nothing recognized was
     * applied. Sublayers restore first, then layer/kind renderers and labels/vector
     * symbol or raster state. Mutations can notify individually and are not rolled
     * back; schema and layerType fields are not checked. */
    static Result applyStyleJson(OpenSWMMVisLayer *layer, const QJsonObject &root);

    /*! Raster-layer style block — `{ renderBand, hillshade{…}, rasterRenderer
     *  {…} }`. styleToJson/applyStyleJson merge it into the root object, and
     *  ProjectSerializer nests the same block in the .oswp layer record, so
     *  Cancel / undo / style files / projects all round-trip one shape.
     *  applyRasterStyleJson restores in the order band → hillshade →
     *  renderer, so the renderer restored last is authoritative (a band
     *  change re-seeds a graduated renderer from band statistics). Missing
     *  keys leave the corresponding state untouched.
     * @param layer Optional borrowed raster layer to inspect.
     * @return Owned render-band/hillshade/renderer snapshot, or empty for null.
     * Angles retain degrees and other stored units; no raster values are read. */
    static QJsonObject rasterStyleToJson(const GISRasterLayer *layer);
    /** @brief Restore raster metadata in band, hillshade, then renderer order.
     * @param[in,out] layer Optional raster layer; null is a no-op. Its setters
     * define clamping, band-statistic refresh and notifications.
     * @param style Recognized raster-style keys. Missing keys retain corresponding
     * state. Unknown raster renderer IDs are ignored and retain the current
     * renderer after any band change; other values use Qt conversions/defaults.
     * No rollback, status result or explicit schema validation is provided. */
    static void applyRasterStyleJson(GISRasterLayer *layer, const QJsonObject &style);

private:
    /** @brief Parse a native JSON file, warn about its schema prefix, then apply it.
     * @param[in,out] layer Non-null borrowed destination layer on its owning Qt thread.
     * @param path Source path read synchronously without modification.
     * @return Read/parse failure, otherwise applyStyleJson()'s result with a warning
     * when schema does not begin with swmmvis-style. Valid nonobject JSON becomes
     * an empty object; mismatched layerType does not prevent application. */
    static Result importNative(OpenSWMMVisLayer *layer, const QString &path);
    /** @brief Parse the limited vector QML subset and install its supported renderer.
     * @param[in,out] layer Non-null borrowed destination layer; changes occur only
     * after XML parsing succeeds, through its ordinary renderer setter.
     * @param path Source QML path, read synchronously without modification.
     * @return Read/XML failure, otherwise ok=true. singleSymbol uses the first
     * parsed symbol; categorizedSymbol uses gray category placeholders and that
     * symbol as fallback. Other types retain style with a warning. Units, symbol
     * references, scale/filter metadata, labels and multi-pass structures are not migrated. */
    static Result importQml(OpenSWMMVisLayer *layer, const QString &path);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_STYLEFILEIO_H
