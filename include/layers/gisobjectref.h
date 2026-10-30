/**
 * @file      gisobjectref.h
 * @brief     SVBC round B — helpers to build / parse SWMMObjectRef names for GIS vector features,
 *            the `mesh::MeshObjectRef` idiom applied to imported feature layers.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * SVBC round B — helpers to build / parse SWMMObjectRef names for GIS
 * vector features, the `mesh::MeshObjectRef` idiom applied to imported
 * feature layers.
 *
 * The encoded form is:
 *     gis::&lt;layerId&gt;&#35;f&lt;fid&gt;
 *
 * Where &lt;layerId&gt; is `OpenSWMMVisLayer::layerId()` — the panel's existing
 * per-layer key ("gis:&lt;layerId&gt;" combo payloads), stable for the layer's
 * lifetime and unique when several sublayers come from one datasource.
 * The parse splits on the LAST "&#35;f", so a layerId containing '#' cannot
 * confuse it. Kept free of GISVectorLayer includes so it unit-tests
 * without widget/GDAL deps.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LAYERS_GISOBJECTREF_H
#define OPENSWMMVIS_LAYERS_GISOBJECTREF_H

#include "selection/selectionmanager.h"

#include <QString>

class GisObjectRef
{
public:
    /*! \brief "gis::&lt;layerId&gt;" — the bus-side key for one feature layer.
     * @param[in] layerId Layer-lifetime identity, copied without validation/escaping.
     * @return gis:: plus the exact identity; empty layerId still produces a prefix-only key.
     */
    static QString layerKey(const QString &layerId);

    /*! \brief Build a SelectionManager ref naming feature \p fid on the
     *  layer with \p layerId.
     * @param[in] layerId Exact layer identity.
     * @param[in] fid Feature ID encoded as a decimal signed integer; negative IDs are not rejected during construction but parseFeature() refuses them.
     * @return Value Feature-category reference with encoded layer/feature identity, without any datasource lookup.
     */
    static SWMMObjectRef feature(const QString &layerId, long long fid);

    /*! \brief Parse a feature ref name. Returns true and fills the outs on
     *  success; false (outs untouched) if the name is malformed or the ref
     *  is not a Feature.
     * @param[in] ref Must have Feature category and an exact gis:: prefix followed by a last &#35;f delimiter and nonnegative signed-64-bit decimal ID.
     * @param[out] outLayerId Optional decoded identity, assigned only after all checks succeed.
     * @param[out] outFid Optional feature ID, likewise assigned only on success.
     * @return Whether structural decoding succeeds. Does not check source existence; the current delimiter-position check permits an empty layer ID even though construction normally uses a nonempty one.
     */
    static bool parseFeature(const SWMMObjectRef &ref, QString *outLayerId, long long *outFid);
};

#endif // OPENSWMMVIS_LAYERS_GISOBJECTREF_H
