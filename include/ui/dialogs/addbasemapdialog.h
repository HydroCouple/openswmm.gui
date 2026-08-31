/*!
 * \file   addbasemapdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Unified "Add Basemap" dialog with tabs for XYZ, WMS/WMTS, and ArcGIS REST.
 *
 * \details
 * The dialog has five tabs:
 *  - Tab 0: XYZ Tiles  — URL template, zoom range, tile pixel ratio, axis order,
 *                         authentication, HTTP headers, Test Connection button.
 *  - Tab 1: WMS/WMTS   — URL with protocol auto-detection, Connect, layer tree,
 *                         style/format/CRS/tile-matrix-set, Advanced options,
 *                         authentication, HTTP headers.
 *  - Tab 2: WCS        — URL, Connect, coverage tree, format/CRS/range/interpolation,
 *                         authentication, HTTP headers.
 *  - Tab 3: ArcGIS REST — URL, Portal endpoint fields, Connect, result info panel,
 *                         authentication, HTTP headers.
 *  - Tab 4: Local File  — local raster file (GeoTIFF/PNG/JPEG/BMP/…), optional
 *                         world file, CRS selection.
 *
 * `createLayer(QObject*)` returns an `OpenSWMMVisLayer*` configured from the
 * accepted state (caller takes ownership).
 *
 * The dialog also persists connections via `BasemapConnectionStore`.
 */
#ifndef ADDBASEMAPDIALOG_H
#define ADDBASEMAPDIALOG_H

#include "connections/basemapconnection.h"

#include <hydrocoupleogc/httpclient.h>
#include <hydrocoupleogc/servicecredentials.h>
#include <hydrocoupleogc/wfscapabilities.h>

#include <QList>
#include <QPair>
#include <QRectF>
#include <QString>

#include <QDialog>

#include <memory>

class QTabWidget;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QGroupBox;
class QDialogButtonBox;
class QNetworkAccessManager;
class QNetworkReply;
class BasemapHttpHeadersWidget;
class OpenSWMMVisLayer;
class WFSLayer;
struct WMSServiceInfo;
struct WMTSServiceInfo;

// ---------------------------------------------------------------------------

/*!
 * \class AddBasemapDialog
 * \brief Unified dialog for adding a layer from a web service or a file.
 */
class AddBasemapDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \enum Tab
     * \brief The dialog's pages, in the order they appear.
     *
     * Named because createLayer() dispatches on the current page and callers
     * open the dialog on a chosen one; bare indices in two files is how that
     * goes wrong quietly.
     *
     * Wfs is last rather than beside the other OGC services, which would read
     * better, because DialogLayoutPersistence remembers the selected page by
     * index -- renumbering would silently open somebody's saved dialog on a
     * different service.
     */
    enum Tab {
        XyzTiles   = 0,
        WmsWmts    = 1,
        Wcs        = 2,
        ArcGisRest = 3,
        LocalFile  = 4,
        Wfs        = 5
    };

    explicit AddBasemapDialog(QWidget *parent = nullptr);
    ~AddBasemapDialog() override;

    /*!
     * \brief Limits a feature request to the ground the map is looking at.
     *
     * Longitude and latitude. Only the WFS page uses it: a feature service
     * holds a country and a model needs one catchment, so without this the
     * request is bounded only by a feature count and returns an arbitrary few
     * thousand from wherever the service starts counting.
     *
     * \param lonLatBounds The map's current extent, or a null rectangle.
     */
    void setPreferredExtent(const QRectF &lonLatBounds);

    /*!
     * \brief Asks the WFS at the address now entered what it holds.
     *
     * Public because it is what the page's Connect button does, and a test
     * should press the button rather than reach past it.
     */
    void connectToWFS();

    //! What the WFS page is currently saying. Empty until it has said
    //! something.
    [[nodiscard]] QString wfsStatus() const;

    /*!
     * \brief Creates and returns the layer the current page describes.
     *
     * Not always a basemap. The WFS page yields a vector layer of features
     * for the layer tree rather than a picture to sit behind the model, and
     * that layer has already been fetched by the time this is called -- see
     * fetchWFSThenAccept().
     *
     * \details Caller owns the returned object.  Returns nullptr on failure.
     * \param parent  Qt parent for the new layer object.
     */
    [[nodiscard]] OpenSWMMVisLayer *createLayer(QObject *parent) const;

    void setInitialTab(int index);

private slots:
    // XYZ tab
    void onXYZConnectionSelected(const QString &name);
    void onXYZNew();
    void onXYZEdit();
    void onXYZDelete();
    void onXYZTest();

    // WMS/WMTS tab
    void onWMSConnectionSelected(const QString &name);
    void onWMSNew();
    void onWMSEdit();
    void onWMSDelete();
    void onWMSConnect();
    void onWMSCapabilitiesFetched();
    void onWMSCapabilitiesError(const QString &error);
    void onWMSLayerSelectionChanged();

    // WCS tab
    void onWCSConnectionSelected(const QString &name);
    void onWCSNew();
    void onWCSDelete();
    void onWCSConnect();
    void onWCSCapabilitiesFetched(const struct WCSServiceInfo &info);
    void onWCSCapabilitiesError(const QString &error);
    void onWCSCoverageSelectionChanged();

    // WFS tab
    void onWFSConnectionSelected(const QString &name);
    void onWFSNew();
    void onWFSDelete();
    void onWFSConnect();

    // ArcGIS tab
    void onArcGISConnectionSelected(const QString &name);
    void onArcGISNew();
    void onArcGISEdit();
    void onArcGISDelete();
    void onArcGISConnect();
    void onArcGISMetadataFetched();
    void onArcGISMetadataError(const QString &error);

    // Local file tab
    void onLocalConnectionSelected(const QString &name);
    void onLocalNew();
    void onLocalDelete();
    void onLocalBrowseFile();
    void onLocalBrowseWorldFile();
    void onLocalSelectCrs();

private:
    void setupUiXYZ(QWidget *page);
    void setupUiWMS(QWidget *page);
    void setupUiWCS(QWidget *page);
    void setupUiArcGIS(QWidget *page);
    void setupUiLocal(QWidget *page);
    void buildConnectionBar(QWidget *parent, QComboBox *&combo,
                            QPushButton *&newBtn, QPushButton *&editBtn,
                            QPushButton *&delBtn);
    void buildAuthGroup(QWidget *parent, QGroupBox *&box,
                        QLineEdit *&user, QLineEdit *&pass,
                        QPushButton *&eyeBtn);
    void togglePasswordVisibility(QLineEdit *passEdit, QPushButton *eyeBtn);

    void refreshXYZCombo();
    void refreshWMSCombo();
    void refreshWCSCombo();
    void refreshArcGISCombo();
    void refreshLocalCombo();

    void populateXYZ(const XYZConnection &conn, const BasemapAuth &auth);
    void populateWMS(const WMSConnection  &conn, const BasemapAuth &auth);
    void populateWCS(const struct WCSConnection &conn, const BasemapAuth &auth);
    void populateArcGIS(const ArcGISRestConnection &conn, const BasemapAuth &auth);
    void populateLocal(const LocalRasterConnection &conn);
    void updateLocalGeorefStatus();

    void populateWMSTree(const struct WMSServiceInfo  &info);
    void populateWMTSTree(const struct WMTSServiceInfo &info);
    void populateWCSTree(const struct WCSServiceInfo   &info);

    // Helpers for createLayer()
    [[nodiscard]] OpenSWMMVisLayer *buildXYZLayer(QObject *parent)    const;
    [[nodiscard]] OpenSWMMVisLayer *buildWMSLayer(QObject *parent)    const;
    [[nodiscard]] OpenSWMMVisLayer *buildWMTSLayer(QObject *parent)   const;
    [[nodiscard]] OpenSWMMVisLayer *buildWCSLayer(QObject *parent)    const;
    [[nodiscard]] OpenSWMMVisLayer *buildArcGISLayer(QObject *parent) const;
    [[nodiscard]] OpenSWMMVisLayer *buildLocalRasterLayer(QObject *parent) const;

    // ---------- Widgets ----------------------------------------------------

    QTabWidget *m_tabs = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    // XYZ
    QComboBox   *m_xyzCombo    = nullptr;
    QPushButton *m_xyzNew      = nullptr;
    QPushButton *m_xyzEdit     = nullptr;
    QPushButton *m_xyzDel      = nullptr;
    QLineEdit   *m_xyzName     = nullptr;
    QLineEdit   *m_xyzUrl      = nullptr;
    QSpinBox    *m_xyzZMin     = nullptr;
    QSpinBox    *m_xyzZMax     = nullptr;
    QComboBox   *m_xyzPixRatio = nullptr;    // 0/1/2
    QComboBox   *m_xyzAxis     = nullptr;    // ZXY / ZYX
    QGroupBox   *m_xyzAuthBox  = nullptr;
    QLineEdit   *m_xyzUser     = nullptr;
    QLineEdit   *m_xyzPass     = nullptr;
    QPushButton *m_xyzEye      = nullptr;
    BasemapHttpHeadersWidget *m_xyzHeaders = nullptr;
    QPushButton *m_xyzTestBtn  = nullptr;
    QLabel      *m_xyzTestLabel = nullptr;

    // WMS / WMTS
    QComboBox   *m_wmsCombo    = nullptr;
    QPushButton *m_wmsNew      = nullptr;
    QPushButton *m_wmsEdit     = nullptr;
    QPushButton *m_wmsDel      = nullptr;
    QLineEdit   *m_wmsUrl      = nullptr;
    QPushButton *m_wmsConnect  = nullptr;
    QLabel      *m_wmsProtocol = nullptr;  // shows "WMS" or "WMTS"
    QGroupBox   *m_wmsAuthBox  = nullptr;
    QLineEdit   *m_wmsUser     = nullptr;
    QLineEdit   *m_wmsPass     = nullptr;
    QPushButton *m_wmsEye      = nullptr;
    QTreeWidget *m_wmsTree     = nullptr;
    QComboBox   *m_wmsStyle    = nullptr;
    QComboBox   *m_wmsFmt      = nullptr;
    QComboBox   *m_wmsCrs      = nullptr;
    QComboBox   *m_wmsTms      = nullptr;   // tile matrix set (WMTS only)
    QLabel      *m_wmsTmsLabel = nullptr;
    // Advanced
    QComboBox   *m_wmsDpiMode  = nullptr;
    QComboBox   *m_wmsRatio    = nullptr;
    QCheckBox   *m_wmsIgnoreUri       = nullptr;
    QCheckBox   *m_wmsIgnoreAxis      = nullptr;
    QCheckBox   *m_wmsInvertAxis      = nullptr;
    QCheckBox   *m_wmsSmooth          = nullptr;
    BasemapHttpHeadersWidget *m_wmsHeaders = nullptr;
    QLabel      *m_wmsStatus   = nullptr;

    // WCS
    QComboBox   *m_wcsCombo      = nullptr;
    QPushButton *m_wcsNew        = nullptr;
    QPushButton *m_wcsDel        = nullptr;
    QLineEdit   *m_wcsUrl        = nullptr;
    QPushButton *m_wcsConnect    = nullptr;
    QLabel      *m_wcsStatus     = nullptr;
    QTreeWidget *m_wcsCovTree    = nullptr;
    QComboBox   *m_wcsFmt        = nullptr;
    QComboBox   *m_wcsCrs        = nullptr;
    QLineEdit   *m_wcsRange      = nullptr;
    QComboBox   *m_wcsInterp     = nullptr;
    QGroupBox   *m_wcsAuthBox    = nullptr;
    QLineEdit   *m_wcsUser       = nullptr;
    QLineEdit   *m_wcsPass       = nullptr;
    QPushButton *m_wcsEye        = nullptr;
    BasemapHttpHeadersWidget *m_wcsHeaders = nullptr;

    // ------ WFS tab ---------------------------------------------------
    QComboBox   *m_wfsCombo      = nullptr;
    QPushButton *m_wfsNew        = nullptr;
    QPushButton *m_wfsDel        = nullptr;
    QLineEdit   *m_wfsUrl        = nullptr;
    QPushButton *m_wfsConnect    = nullptr;
    QLabel      *m_wfsStatus     = nullptr;
    QListWidget *m_wfsTypes      = nullptr;
    QGroupBox   *m_wfsAuthBox    = nullptr;
    QLineEdit   *m_wfsUser       = nullptr;
    QLineEdit   *m_wfsPass       = nullptr;
    QPushButton *m_wfsEye        = nullptr;
    BasemapHttpHeadersWidget *m_wfsHeaders = nullptr;

    // ArcGIS
    QComboBox   *m_arcCombo       = nullptr;
    QPushButton *m_arcNew         = nullptr;
    QPushButton *m_arcEdit        = nullptr;
    QPushButton *m_arcDel         = nullptr;
    QLineEdit   *m_arcUrl         = nullptr;
    QLineEdit   *m_arcPrefix      = nullptr;
    QLineEdit   *m_arcContent     = nullptr;
    QLineEdit   *m_arcCommunity   = nullptr;
    QGroupBox   *m_arcAuthBox     = nullptr;
    QLineEdit   *m_arcUser        = nullptr;
    QLineEdit   *m_arcPass        = nullptr;
    QPushButton *m_arcEye         = nullptr;
    BasemapHttpHeadersWidget *m_arcHeaders = nullptr;
    QPushButton *m_arcConnect     = nullptr;
    QLabel      *m_arcInfoLabel   = nullptr;

    // Local file
    QComboBox   *m_localCombo       = nullptr;
    QPushButton *m_localNew         = nullptr;
    QPushButton *m_localDel         = nullptr;
    QLineEdit   *m_localName        = nullptr;
    QLineEdit   *m_localFile        = nullptr;
    QPushButton *m_localFileBrowse  = nullptr;
    QLineEdit   *m_localWorld       = nullptr;
    QPushButton *m_localWorldBrowse = nullptr;
    QLineEdit   *m_localCrs         = nullptr;   // read-only; set via CRS dialog
    QPushButton *m_localCrsBtn      = nullptr;
    QLabel      *m_localStatus      = nullptr;

    // ---------- Runtime state ----------------------------------------------

    QNetworkAccessManager *m_nam = nullptr;

    // WMS runtime
    bool                      m_isWMTS       = false;
    std::unique_ptr<WMSServiceInfo>  m_wmsInfo;
    std::unique_ptr<WMTSServiceInfo> m_wmtsInfo;

    // WCS runtime
    struct WCSServiceInfo    *m_wcsInfo      = nullptr;

    // ------ WFS state -------------------------------------------------
    void setupUiWFS(QWidget *page);
    void refreshWFSCombo();
    void populateWFS(const WFSConnection &conn, const BasemapAuth &auth);
    void showWFSCollections(const QByteArray &body);

    /*!
     * \brief Fetches the chosen collection, then accepts the dialog.
     *
     * The WFS page accepts differently from the others. Every other page
     * describes a request that will be made later, each time the canvas
     * moves, so nothing can fail while the dialog is open. A feature request
     * is made once and can fail after the user has chosen -- the collection
     * exists, the request is well formed, and the service still answers that
     * it holds nothing over that ground. Fetching before accepting is what
     * lets that be said where the user is looking.
     */
    void fetchWFSThenAccept();

    /*!
     * \brief Enables OK only when the active page can actually produce a
     *        layer.
     *
     * Every other page can: its request is built from form fields and is
     * never wrong enough to refuse. The WFS page can only once the service
     * has answered and a readable collection is chosen, so OK stays off
     * until then rather than accepting into an empty layer.
     */
    void updateOkEnabled();

    [[nodiscard]] OpenSWMMVisLayer *buildWFSLayer(QObject *parent) const;

    [[nodiscard]] HydroCouple::Ogc::ServiceCredentials wfsCredentials() const;

    HydroCouple::Ogc::HttpClient   *m_wfsClient = nullptr;
    HydroCouple::Ogc::WfsCapabilities m_wfsCaps;

    //! Type name, and why it cannot be used. Empty reason means it can.
    QList<QPair<QString, QString>> m_wfsChoices;

    QRectF   m_preferredExtent;
    QString  m_wfsStatusText;

    /*!
     * \brief The fetched layer, awaiting collection by createLayer().
     *
     * Mutable because createLayer() is const across every other page, where
     * building a layer really is a pure read of the form. Here the layer
     * already exists and is being handed over.
     */
    mutable WFSLayer *m_wfsLayer = nullptr;

    // ArcGIS runtime — derived XYZ connection built after Connect
    XYZConnection             m_arcDerivedXYZ;
};

#endif // ADDBASEMAPDIALOG_H
