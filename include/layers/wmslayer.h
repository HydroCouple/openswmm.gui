/*!
 * \file   wmslayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */

#ifndef WMSLAYER_H
#define WMSLAYER_H

#include "layers/openswmmvislayer.h"

#include <QImage>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QUrl>

class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;
class RasterTileItem;

/*!
 * \struct WMSLayerInfo
 * \brief Describes one named layer returned by a WMS GetCapabilities response.
 */
struct WMSLayerInfo
{
    QString              name;          /*!< Layer "Name" element (used in requests). */
    QString              title;         /*!< Human-readable "Title" element. */
    QString              abstractText;  /*!< Optional "Abstract" element. */
    QStringList          crsIdentifiers; /*!< Supported CRS identifiers ("EPSG:4326", …). */
    QStringList          styles;        /*!< Available style names. */
    MapExtent            geographicBoundingBox; /*!< Always in EPSG:4326. */
    bool                 queryable = false;
};

/*!
 * \struct WMSServiceInfo
 * \brief Metadata returned by a WMS GetCapabilities request.
 */
struct WMSServiceInfo
{
    QString              title;
    QString              abstractText;
    QString              version;       /*!< "1.1.1" or "1.3.0". */
    QStringList          imageFormats;  /*!< e.g. {"image/png", "image/jpeg"}. */
    QList<WMSLayerInfo>  layers;
};

/*!
 * \class WMSLayer
 * \brief A map layer that fetches and displays images from an OGC WMS service.
 * \details Implements the OGC Web Map Service protocol (versions 1.1.1 and 1.3.0)
 *          using Qt's QNetworkAccessManager.  No QGIS dependency is required.
 *
 *          On each render pass the layer issues a GetMap request matching the
 *          current canvas extent, CRS, and pixel dimensions.  The response image
 *          is cached and painted until a new extent is set.
 *
 *          Before adding a WMSLayer to a canvas, call fetchCapabilities() and wait
 *          for capabilitiesFetched() to retrieve the list of available layers.
 */
class WMSLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

    Q_PROPERTY(QUrl     serviceUrl      READ serviceUrl     CONSTANT)
    Q_PROPERTY(QString  wmsVersion      READ wmsVersion     NOTIFY wmsVersionChanged)
    Q_PROPERTY(QString  activeLayerName READ activeLayerName WRITE setActiveLayerName
               NOTIFY activeLayerNameChanged)
    Q_PROPERTY(QString  activeStyle     READ activeStyle    WRITE setActiveStyle
               NOTIFY activeStyleChanged)
    Q_PROPERTY(QString  imageFormat     READ imageFormat    WRITE setImageFormat
               NOTIFY imageFormatChanged)
    Q_PROPERTY(bool     transparent     READ isTransparent  WRITE setTransparent
               NOTIFY transparentChanged)

public:

    /*!
     * \brief Constructs a WMS layer with the given service endpoint URL.
     * \param serviceUrl   Base URL of the WMS service, e.g.
     *                     "https://example.org/wms?SERVICE=WMS".
     * \param parent       Qt parent object.
     */
    explicit WMSLayer(const QUrl &serviceUrl,
                      OpenSWMMVisWorkspace *parent = nullptr);

    ~WMSLayer() override;

    // ----- Service metadata -----------------------------------------------

    [[nodiscard]] QUrl             serviceUrl()      const;
    [[nodiscard]] QString          wmsVersion()      const;
    [[nodiscard]] WMSServiceInfo   serviceInfo()     const;
    [[nodiscard]] bool             capabilitiesReady() const;

    /*!
     * \brief Asynchronously fetches the GetCapabilities document.
     * \details Emits capabilitiesFetched() on success or capabilitiesError() on failure.
     */
    void fetchCapabilities();

    // ----- Active layer / style -------------------------------------------

    [[nodiscard]] QString activeLayerName() const;
    void setActiveLayerName(const QString &layerName);

    [[nodiscard]] QString activeStyle()     const;
    void setActiveStyle(const QString &style);

    // ----- Request parameters ---------------------------------------------

    [[nodiscard]] QString imageFormat()     const;
    void setImageFormat(const QString &fmt);  /*!< e.g. "image/png". */

    [[nodiscard]] bool    isTransparent()   const;
    void setTransparent(bool transparent);

    /*!
     * \brief Returns optional extra key=value pairs appended to every GetMap URL.
     */
    [[nodiscard]] QMap<QString, QString> extraParams() const;
    void setExtraParams(const QMap<QString, QString> &params);

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    [[nodiscard]] bool isRasterLayer() const override { return true; }

    void fetchCache(const MapExtent &extent,
                    const QSize &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    void render(QPainter *painter,
                const MapExtent &extent,
                const QSize &imageSize,
                const SpatialReferenceSystem *srs) override;

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

signals:
    /*!
     * \brief Emitted when the GetCapabilities document has been parsed successfully.
     */
    void capabilitiesFetched(const WMSServiceInfo &info);

    /*!
     * \brief Emitted when fetching capabilities fails.
     */
    void capabilitiesError(const QString &errorMessage);

    /*!
     * \brief Emitted when a GetMap tile has been downloaded and is ready to paint.
     */
    void tileReady();

    void wmsVersionChanged(const QString &version);
    void activeLayerNameChanged(const QString &name);
    void activeStyleChanged(const QString &style);
    void imageFormatChanged(const QString &fmt);
    void transparentChanged(bool transparent);

private slots:
    void onCapabilitiesReply(QNetworkReply *reply);
    void onGetMapReply(QNetworkReply *reply);

private:
    [[nodiscard]] QUrl buildGetMapUrl(const MapExtent &ext,
                                      const SpatialReferenceSystem *srs,
                                      int w, int h) const;

    void parseCapabilities(const QByteArray &xml);
    void requestTile(const MapExtent &ext,
                     const SpatialReferenceSystem *canvasSRS,
                     int w, int h);
    void invalidateCache();

    QUrl                    m_serviceUrl;
    QString                 m_wmsVersion  = "1.3.0";
    QString                 m_activeLayer;
    QString                 m_activeStyle;
    QString                 m_imageFormat = "image/png";
    bool                    m_transparent = true;
    QMap<QString, QString>  m_extraParams;
    WMSServiceInfo          m_serviceInfo;
    bool                    m_capsReady   = false;

    QNetworkAccessManager  *m_nam         = nullptr;  /*!< Owned network manager. */
    QNetworkReply          *m_pendingReply = nullptr; /*!< In-flight GetMap request. */

    // Tile cache — extent/size describe the image already in m_cachedTile
    QImage                  m_cachedTile;
    MapExtent               m_cacheExtent;
    int                     m_cacheWidth  = 0;
    int                     m_cacheHeight = 0;

    // In-flight request metadata — NOT committed to m_cache* until the reply arrives
    MapExtent               m_requestedExtent;
    int                     m_requestedWidth  = 0;
    int                     m_requestedHeight = 0;

    // Persistent scene item (kept visible as placeholder until new tile arrives)
    RasterTileItem         *m_sceneItem   = nullptr;
};

Q_DECLARE_METATYPE(WMSLayer *)
Q_DECLARE_METATYPE(WMSServiceInfo)
Q_DECLARE_METATYPE(WMSLayerInfo)

#endif // WMSLAYER_H
