/*!
 * \file   gisobjectref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * SVBC round B — helpers to build / parse SWMMObjectRef names for GIS
 * vector features, the `mesh::MeshObjectRef` idiom applied to imported
 * feature layers.
 *
 * The encoded form is:
 *     gis::<layerId>#f<fid>
 *
 * Where <layerId> is `OpenSWMMVisLayer::layerId()` — the panel's existing
 * per-layer key ("gis:<layerId>" combo payloads), stable for the layer's
 * lifetime and unique when several sublayers come from one datasource.
 * The parse splits on the LAST "#f", so a layerId containing '#' cannot
 * confuse it. Kept free of GISVectorLayer includes so it unit-tests
 * without widget/GDAL deps.
 */
#ifndef OPENSWMMVIS_LAYERS_GISOBJECTREF_H
#define OPENSWMMVIS_LAYERS_GISOBJECTREF_H

#include "selection/selectionmanager.h"

#include <QString>

class GisObjectRef
{
public:
    /*! \brief "gis::<layerId>" — the bus-side key for one feature layer. */
    static QString layerKey(const QString &layerId);

    /*! \brief Build a SelectionManager ref naming feature \p fid on the
     *  layer with \p layerId. */
    static SWMMObjectRef feature(const QString &layerId, long long fid);

    /*! \brief Parse a feature ref name. Returns true and fills the outs on
     *  success; false (outs untouched) if the name is malformed or the ref
     *  is not a Feature. */
    static bool parseFeature(const SWMMObjectRef &ref,
                             QString *outLayerId,
                             long long *outFid);
};

#endif // OPENSWMMVIS_LAYERS_GISOBJECTREF_H
