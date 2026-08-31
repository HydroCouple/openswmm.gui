/*!
 * \file   wfslayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  WFSLayer — features fetched from an OGC Web Feature Service.
 *
 * \details
 * The feature half of the OGC family, which this GUI has never had: WMS,
 * WMTS and WCS all answer with pictures, and a WFS answers with the data.
 * That makes it the one of the four whose result can be queried, classified
 * and labelled like any other vector layer — so it is one, deriving from
 * GISVectorLayer rather than repeating it.
 *
 * GDAL does not fetch it. This build compiles GDAL without curl, so
 * RegisterOGRWFS is absent entirely and CPLHTTPFetch is a stub; the bytes
 * come over Qt Network through HydroCoupleOgc, and GDAL is handed them
 * afterwards. Everything about parsing the service — its capabilities, the
 * version-dependent request spelling, the axis order of a bounding box —
 * lives in that shared library and is tested there, against saved responses
 * from real servers.
 */

#ifndef WFSLAYER_H
#define WFSLAYER_H

#include "layers/gisvectorlayer.h"

#include <QByteArray>
#include <QString>

/*!
 * \class WFSLayer
 * \brief A vector layer whose features came from a web feature service.
 */
class WFSLayer : public GISVectorLayer
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs an empty layer; adopt a response to fill it.
     * \param name Layer name shown in the legend.
     * \param parent Owning object.
     */
    explicit WFSLayer(const QString &name,
                      OpenSWMMVisWorkspace *parent = nullptr);

    ~WFSLayer() override;

    /*!
     * \brief Takes a GetFeature response and opens it as this layer's data.
     *
     * The bytes are put where GDAL can open them and left there for as long
     * as the layer lives, because a GISVectorLayer keeps its dataset open
     * and reads features from it per paint — unlike a layer that reads
     * everything into memory once, this one cannot let go of its source.
     * The name is therefore unique per layer, and removed on destruction.
     *
     * \param body The response body — GeoJSON or GML.
     * \param[out] message Why not, on failure. A service that refuses
     *             answers with a document rather than an HTTP error, so
     *             this carries the service's own words where it has them.
     * \returns True when features were opened.
     */
    [[nodiscard]] bool adoptResponse(const QByteArray &body, QString &message);

    //! The service this came from, for the layer's properties.
    [[nodiscard]] QString serviceUrl() const { return m_serviceUrl; }

    void setServiceUrl(const QString &url) { m_serviceUrl = url; }

    //! The collection, as the service names it.
    [[nodiscard]] QString typeName() const { return m_typeName; }

    void setTypeName(const QString &typeName) { m_typeName = typeName; }

    /*!
     * \brief Where this layer's data came from.
     *
     * The service and collection, never the in-memory path GDAL was handed
     * — that names nothing a user could open and does not survive the
     * session.
     */
    [[nodiscard]] QString sourceDescription() const override;

private:
    QString m_serviceUrl;
    QString m_typeName;

    //! The in-memory file backing the open dataset; removed on destruction.
    QString m_vsiPath;
};

#endif // WFSLAYER_H
