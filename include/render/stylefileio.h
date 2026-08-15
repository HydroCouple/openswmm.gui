/*!
 * \file   stylefileio.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Native style-file (.swmm-style.json) import/export +
 *         minimal QGIS .qml import for the common renderer cases.
 *
 *         Slice X.23.  Goals:
 *           - Save / restore a layer's complete styling (renderer +
 *             label config) outside the .oswp container so the same
 *             style can be reused across projects.
 *           - Read enough of a QGIS .qml file to round-trip
 *             SingleSymbol / Graduated / Categorized renderers
 *             produced by QGIS for vector layers (the typical
 *             interop case for SWMM modellers who first style their
 *             data in QGIS).  We don't aim for full QGIS schema
 *             coverage — anything we don't recognise is ignored and
 *             the user gets a warning in the return value.
 *
 *         Native format is JSON with this top-level shape:
 *           {
 *             "schema":    "swmmvis-style/v1",
 *             "layerType": "SWMMModelLayer" | "GISVectorLayer" | ...,
 *             "renderer":  { <IFeatureRenderer::toJson()> },
 *             "labelConfig": { <LabelConfig::toJson()> },
 *             "kindRenderers": { "<kindKey>": <renderer>, ... }   // SWMM only
 *           }
 */
#ifndef OPENSWMM_RENDER_STYLEFILEIO_H
#define OPENSWMM_RENDER_STYLEFILEIO_H

#include <QString>
#include <QStringList>

class OpenSWMMVisLayer;

namespace OpenSWMM::Render {

class StyleFileIO
{
public:
    struct Result {
        bool        ok = false;
        QStringList warnings;
        QString     errorMessage;
    };

    /*! Write \p layer's full style state to \p path as JSON.
     *  Overwrites any existing file. */
    static Result exportStyle(const OpenSWMMVisLayer *layer, const QString &path);

    /*! Read a style file and apply it to \p layer.  Auto-detects
     *  native (.swmm-style.json) vs QGIS (.qml) by the file extension
     *  (and falls back to content sniffing on extension mismatch). */
    static Result importStyle(OpenSWMMVisLayer *layer, const QString &path);

private:
    static Result importNative(OpenSWMMVisLayer *layer, const QString &path);
    static Result importQml   (OpenSWMMVisLayer *layer, const QString &path);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_STYLEFILEIO_H
