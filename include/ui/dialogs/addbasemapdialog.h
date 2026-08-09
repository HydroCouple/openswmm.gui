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

#include <QDialog>

#include <memory>

class QTabWidget;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QGroupBox;
class QDialogButtonBox;
class QNetworkAccessManager;
class QNetworkReply;
class BasemapHttpHeadersWidget;
class OpenSWMMVisLayer;
struct WMSServiceInfo;
struct WMTSServiceInfo;

// ---------------------------------------------------------------------------

/*!
 * \class AddBasemapDialog
 * \brief Unified add-basemap dialog (3 tabs).
 */
class AddBasemapDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddBasemapDialog(QWidget *parent = nullptr);
    ~AddBasemapDialog() override;

    /*!
     * \brief Creates and returns a configured basemap layer.
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

    // ArcGIS runtime — derived XYZ connection built after Connect
    XYZConnection             m_arcDerivedXYZ;
};

#endif // ADDBASEMAPDIALOG_H
