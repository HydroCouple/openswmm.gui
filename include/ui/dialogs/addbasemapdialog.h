/**
 * @file      addbasemapdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Unified "Add Basemap" dialog with tabs for XYZ, WMS/WMTS, and ArcGIS REST.
 *
 * @details
 * The dialog has six tabs:
 *  - Tab 0: XYZ Tiles  — URL template, zoom range, tile pixel ratio, axis order,
 *                         authentication, HTTP headers, Test Connection button.
 *  - Tab 1: WMS/WMTS   — URL with protocol auto-detection, Connect, layer tree,
 *                         style/format/CRS/tile-matrix-set, Advanced options,
 *                         authentication, HTTP headers.
 *  - Tab 2: WCS        — URL, Connect, coverage tree, format/CRS/range/interpolation,
 *                         authentication, HTTP headers.
 *  - Tab 3: ArcGIS REST — URL, Portal endpoint fields, Connect, result info panel,
 *                         authentication, HTTP headers.
 *  - Tab 5: WFS — capability discovery and one-time feature fetch.
 *  - Tab 4: Local File  — local raster file (GeoTIFF/PNG/JPEG/BMP/…), optional
 *                         world file, CRS selection.
 *
 * `createLayer(QObject*)` returns an `OpenSWMMVisLayer*` configured from the
 * accepted state (caller takes ownership).
 *
 * The dialog also persists connections via `BasemapConnectionStore`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
    enum Tab { XyzTiles = 0, WmsWmts = 1, Wcs = 2, ArcGisRest = 3, LocalFile = 4, Wfs = 5 };

    /**
     * @brief Build GUI-thread source forms and load saved connection names without contacting
     * services.
     * @param[in] parent Optional Qt owner.
     * Owns widgets/network clients, copied capability metadata and any unclaimed WFS layer. Non-WFS
     * OK accepts form drafts without validating service reachability; WFS OK fetches readable
     * features before accepting.
     */
    explicit AddBasemapDialog(QWidget *parent = nullptr);
    /**
     * @brief Delete any unclaimed fetched WFS layer and owned WCS metadata; unique pointers and Qt
     * parenting release remaining state/clients.
     * Transferred layers remain caller-owned; temporary unparented capability layers follow their
     * own completion/error deletion routes.
     */
    ~AddBasemapDialog() override;

    /*!
     * \brief Limits a feature request to the ground the map is looking at.
     *
     * Longitude and latitude. Only the WFS page uses it: a feature service
     * holds a country and a model needs one catchment, so without this the
     * request is bounded only by a feature count and returns an arbitrary few
     * thousand from wherever the service starts counting.
     *
     * Argument `lonLatBounds`: The map's current extent, or a null rectangle.
     */
    /**
     * @brief Copy the map extent used by later WFS feature requests.
     * @param[in] lonLatBounds Longitude/latitude rectangle; null disables the geographic bound. No
     * normalization/validation occurs. Applied only if the collection advertises EPSG:4326;
     * otherwise requests rely on feature limits.
     */
    void setPreferredExtent(const QRectF &lonLatBounds);

    /*!
     * \brief Asks the WFS at the address now entered what it holds.
     *
     * Public because it is what the page's Connect button does, and a test
     * should press the button rather than reach past it.
     */
    /**
     * @brief Clear collection/capability state and delete an unclaimed WFS result, then
     * asynchronously request capabilities.
     * Uses current URL and credentials/custom headers via the dialog-owned HttpClient; invalid
     * capabilities URL sets a status error. Parses response bodies for service diagnostics rather
     * than treating HTTP success alone as capabilities validity; no request-generation cancellation
     * is added here.
     */
    void connectToWFS();

    //! What the WFS page is currently saying. Empty until it has said
    //! something.
    /**
     * @brief Copy the most recent WFS status message.
     * @return Owned string, initially empty; may describe discovery/fetch progress or failure.
     */
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
     * Argument `parent`: requested Qt parent for the new layer object.
     */
    /**
     * @brief Build or transfer the source described by the currently selected page.
     * @param[in] parent Optional requested Qt parent; XYZ/WFS use it directly, other builders
     * accept only its OpenSWMMVisWorkspace cast and otherwise create an unparented layer.
     * @return Newly allocated configured layer, or the one-time fetched WFS layer (consumed from
     * this dialog), or nullptr for missing/invalid state. Caller owns any unparented object and
     * must respect Qt ownership otherwise.
     * Does not require Accepted state. Can persist connection credentials/settings; local-raster
     * creation can write PAM georeferencing and starts asynchronous open. Network/raster loading
     * may still fail after construction.
     */
    [[nodiscard]] OpenSWMMVisLayer *createLayer(QObject *parent) const;

    /**
     * @brief Select an initial source page using QTabWidget bounds behavior.
     * @param[in] index Tab enum/index; invalid indices are not independently validated here. No-op
     * without tab host.
     */
    void setInitialTab(int index);

private slots:
    // XYZ tab
    /**
     * @brief Load the selected XYZ connection into draft controls.
     * @param[in] name Exact stored connection name; values/credentials are copied, never owned by
     * the form. Clears only name/URL/headers for a blank/new row; saved overrides take precedence
     * over builtins. Does not connect to a service.
     */
    void onXYZConnectionSelected(const QString &name);
    /**
     * @brief Reset XYZ form to blank URL/name/auth/headers, zoom 0..19 and first ratio/axis choices
     * without deleting saved entries.
     */
    void onXYZNew();
    /**
     * @brief Retain the already populated XYZ form; this handler intentionally performs no
     * additional action.
     */
    void onXYZEdit();
    /**
     * @brief Confirm removal of the selected saved XYZ connection and refresh names.
     * Ignores blank/new rows and builtin names, including saved overrides with the same builtin
     * name; no layer or remote data is deleted.
     */
    void onXYZDelete();
    /**
     * @brief Fetch one test tile asynchronously after substituting s=a and z/x/y=0.
     * Applies form custom headers and optional Basic auth, reports received byte count or network
     * error and defers reply deletion. Empty URL reports a prompt. Does not decode imagery or
     * enforce zoom/axis limits.
     */
    void onXYZTest();

    // WMS/WMTS tab
    /**
     * @brief Load the selected WMS connection into draft controls.
     * @param[in] name Exact stored connection name; values/credentials are copied, never owned by
     * the form. Empty/new rows leave existing fields unchanged. Does not connect to a service.
     */
    void onWMSConnectionSelected(const QString &name);
    /**
     * @brief Clear WMS/WMTS URL/auth/headers/tree/options and select the new row without deleting
     * saved connections or existing copied capability objects.
     */
    void onWMSNew();
    /**
     * @brief Retain the populated WMS/WMTS fields; intentionally no additional edit dialog/action.
     */
    void onWMSEdit();
    /**
     * @brief Confirm removal of the selected stored WMS connection and refresh the combo; blank/new
     * rows are ignored.
     */
    void onWMSDelete();
    /**
     * @brief Detect WMS/WMTS from the nonempty trimmed URL and fetch capabilities on a temporary
     * unparented layer.
     * Copies successful metadata into owned state and populates the tree, scheduling temporary
     * deletion on success/error. Form Basic auth/custom headers are not installed on these
     * capability-probe layers (final built layers do receive them). Concurrent requests are not
     * generation-filtered.
     */
    void onWMSConnect();
    /**
     * @brief Set WMS/WMTS status to Connected; a separately connected callback copies
     * metadata/populates the tree.
     */
    void onWMSCapabilitiesFetched();
    /**
     * @brief Display a WMS/WMTS capability error without clearing prior choices.
     * @param[in] error Borrowed network/parser diagnostic copied into status text.
     */
    void onWMSCapabilitiesError(const QString &error);
    /**
     * @brief Populate options for the first selected named layer from current copied capabilities.
     * WMS updates styles/CRSs; WMTS updates styles/formats. Empty selection or missing metadata is
     * ignored; no server request or layer creation occurs.
     */
    void onWMSLayerSelectionChanged();

    // WCS tab
    /**
     * @brief Load the selected WCS connection into draft controls.
     * @param[in] name Exact stored connection name; values/credentials are copied, never owned by
     * the form. Empty/new rows leave existing fields unchanged. Does not connect to a service.
     */
    void onWCSConnectionSelected(const QString &name);
    /**
     * @brief Clear WCS forms/tree/options/auth/headers/status, select first interpolation and
     * delete copied capabilities; saved entries remain.
     */
    void onWCSNew();
    /**
     * @brief Confirm removal of the selected stored WCS connection and refresh names; blank/new
     * rows are ignored.
     */
    void onWCSDelete();
    /**
     * @brief Fetch WCS capabilities asynchronously on a temporary unparented layer.
     * Installs form Basic auth/custom headers, copies metadata on success and schedules probe
     * deletion on either result. Empty URL is ignored; prior capabilities are replaced only on
     * success.
     */
    void onWCSConnect();
    /**
     * @brief Report the successful coverage count.
     * @param[in] info Borrowed successful service metadata; separate connected callback copies it
     * before temporary layer teardown.
     */
    void onWCSCapabilitiesFetched(const struct WCSServiceInfo &info);
    /**
     * @brief Display a WCS capabilities failure without clearing existing metadata/options.
     * @param[in] error Borrowed diagnostic copied into status text.
     */
    void onWCSCapabilitiesError(const QString &error);
    /**
     * @brief Populate first selected coverage formats/CRSs/interpolations from copied metadata.
     * Ensures image/tiff and EPSG:4326 fallbacks, prefers TIFF, and replaces interpolation choices
     * only when advertised; missing selection/metadata is ignored.
     */
    void onWCSCoverageSelectionChanged();

    // WFS tab
    /**
     * @brief Load the selected WFS connection into draft controls.
     * @param[in] name Exact stored connection name; values/credentials are copied, never owned by
     * the form. Empty names are ignored. Does not connect to a service.
     */
    void onWFSConnectionSelected(const QString &name);
    /**
     * @brief Prompt for a nonempty connection name and persist current WFS URL, capability version,
     * selected type, custom headers and enabled Basic credentials.
     * Cancellation/blank name does nothing; refreshes/selects the saved name. Unlike other New
     * handlers this saves the current form.
     */
    void onWFSNew();
    /**
     * @brief Remove the selected named WFS connection immediately and refresh the combo.
     * Empty selection is ignored; this handler does not display a deletion confirmation.
     */
    void onWFSDelete();
    /**
     * @brief Invoke the public asynchronous WFS capabilities discovery path with current form
     * values.
     */
    void onWFSConnect();

    // ArcGIS tab
    /**
     * @brief Load the selected ArcGIS connection into draft controls.
     * @param[in] name Exact stored connection name; values/credentials are copied, never owned by
     * the form. Empty/new rows leave existing fields unchanged. Does not connect to a service.
     */
    void onArcGISConnectionSelected(const QString &name);
    /**
     * @brief Clear ArcGIS URL/portal fields/auth/headers/status and derived XYZ metadata without
     * deleting saved entries.
     */
    void onArcGISNew();
    /**
     * @brief Retain the populated ArcGIS fields; intentionally performs no additional edit action.
     */
    void onArcGISEdit();
    /**
     * @brief Confirm removal of the selected stored ArcGIS connection and refresh names; blank/new
     * rows are ignored.
     */
    void onArcGISDelete();
    /**
     * @brief Request ArcGIS JSON metadata asynchronously and derive a ZYX tile template/zoom range
     * on success.
     * Trims one trailing slash, appends MapServer unless MapServer/FeatureServer is already
     * present, applies enabled Basic auth but not custom headers to this metadata request. Empty
     * URL is ignored; malformed/network replies display errors. Derived tile headers come from the
     * form at completion; no request generation guard is used.
     */
    void onArcGISConnect();
    /**
     * @brief Complete the ArcGIS metadata success notification; status/template were set by the
     * response callback and this hook is empty.
     */
    void onArcGISMetadataFetched();
    /**
     * @brief Display an ArcGIS metadata error without deleting previous derived XYZ state.
     * @param[in] error Borrowed diagnostic copied into status text.
     */
    void onArcGISMetadataError(const QString &error);

    // Local file tab
    /**
     * @brief Load the selected Local connection into draft controls.
     * @param[in] name Exact stored connection name; values/credentials are copied, never owned by
     * the form. Empty/new rows leave existing fields unchanged. Does not connect to a service.
     */
    void onLocalConnectionSelected(const QString &name);
    /**
     * @brief Clear local raster name/path/world-file/CRS/status drafts without removing saved
     * connections.
     */
    void onLocalNew();
    /**
     * @brief Confirm removal of the selected saved local-raster connection and refresh names; does
     * not delete raster/world files.
     */
    void onLocalDelete();
    /**
     * @brief Prompt for a local GDAL raster, update its path on selection and probe
     * georeferencing/name defaults; cancellation leaves the form.
     */
    void onLocalBrowseFile();
    /**
     * @brief Prompt for a world-file sidecar and update its draft path without parsing it;
     * cancellation preserves prior text.
     */
    void onLocalBrowseWorldFile();
    /**
     * @brief Run a CRS selector and copy its nonempty selected authority token into the draft; no
     * raster georeferencing is written yet.
     */
    void onLocalSelectCrs();

private:
    /**
     * @brief Build the XYZ page: name/URL, zoom [0,30], tile ratio/axis, auth/custom headers and
     * test action.
     * @param[in,out] page Required previously unlaid-out widget, receiving a new vertical layout
     * and owning controls; later wrapped/reparented into this dialog. Called once; does not fetch
     * data during construction.
     */
    void setupUiXYZ(QWidget *page);
    /**
     * @brief Build the WMS page: auto-detected WMS/WMTS capabilities,
     * tree/style/format/CRS/matrix-set, advanced flags and auth/headers.
     * @param[in,out] page Required previously unlaid-out widget, receiving a new vertical layout
     * and owning controls; later wrapped/reparented into this dialog. Called once; does not fetch
     * data during construction.
     */
    void setupUiWMS(QWidget *page);
    /**
     * @brief Build the WCS page: coverage discovery/options, range subset, interpolation and
     * auth/headers.
     * @param[in,out] page Required previously unlaid-out widget, receiving a new vertical layout
     * and owning controls; later wrapped/reparented into this dialog. Called once; does not fetch
     * data during construction.
     */
    void setupUiWCS(QWidget *page);
    /**
     * @brief Build the ArcGIS page: metadata URL/portal endpoint fields, auth/headers and derived
     * service status.
     * @param[in,out] page Required previously unlaid-out widget, receiving a new vertical layout
     * and owning controls; later wrapped/reparented into this dialog. Called once; does not fetch
     * data during construction.
     */
    void setupUiArcGIS(QWidget *page);
    /**
     * @brief Build the Local page: raster/world-file/CRS drafts with browse/probe actions.
     * @param[in,out] page Required previously unlaid-out widget, receiving a new vertical layout
     * and owning controls; later wrapped/reparented into this dialog. Called once; does not fetch
     * data during construction.
     */
    void setupUiLocal(QWidget *page);
    /**
     * @brief Append saved-connection controls to an existing vertical page layout.
     * @param[in] parent Required widget with a QVBoxLayout (cast is dereferenced unchecked); owns
     * the new controls.
     * @param[out] combo Replaced with newly allocated saved-name combo.
     * @param[out] newBtn Replaced with new New button.
     * @param[out] editBtn Replaced with new Edit button.
     * @param[out] delBtn Replaced with new Delete button.
     * Prior output objects are not deleted; caller connects handlers after construction.
     */
    void buildConnectionBar(QWidget *parent, QComboBox *&combo, QPushButton *&newBtn,
                            QPushButton *&editBtn, QPushButton *&delBtn);
    /**
     * @brief Append a disabled/checkable Basic-authentication group.
     * @param[in] parent Required page with QVBoxLayout, owning the new group.
     * @param[out] box Replaced with new group, initially unchecked.
     * @param[out] user Replaced with group-owned username input.
     * @param[out] pass Replaced with masked password input.
     * @param[out] eyeBtn Replaced with checkable Show button; caller wires toggling.
     * Does not store/transmit credentials or delete prior output pointers.
     */
    void buildAuthGroup(QWidget *parent, QGroupBox *&box, QLineEdit *&user, QLineEdit *&pass,
                        QPushButton *&eyeBtn);
    /**
     * @brief Synchronize password echo mode and Show/Hide caption.
     * @param[in,out] passEdit Required borrowed password input.
     * @param[in,out] eyeBtn Required borrowed checkable button; checked reveals plain text.
     * Changes display only, not saved credentials.
     */
    void togglePasswordVisibility(QLineEdit *passEdit, QPushButton *eyeBtn);

    /**
     * @brief Rebuild XYZ saved connection names while suppressing selection signals.
     * Adds builtins with saved-name deduplication and a New row; does not preserve selection.
     */
    void refreshXYZCombo();
    /**
     * @brief Rebuild WMS saved connection names while suppressing selection signals.
     * Adds a New first row; does not preserve the previous selected name.
     */
    void refreshWMSCombo();
    /**
     * @brief Rebuild WCS saved connection names while suppressing selection signals.
     * Adds a New first row; does not preserve the previous selected name.
     */
    void refreshWCSCombo();
    /**
     * @brief Rebuild ArcGIS saved connection names while suppressing selection signals.
     * Adds a New first row; does not preserve the previous selected name.
     */
    void refreshArcGISCombo();
    /**
     * @brief Rebuild Local saved connection names while suppressing selection signals.
     * Adds a New first row; does not preserve the previous selected name.
     */
    void refreshLocalCombo();

    /**
     * @brief Hydrate XYZ draft controls from a saved value.
     * @param[in] conn Borrowed connection copied into widgets.
     * @param[in] auth Borrowed Basic credentials copied into inputs; no ownership transfer.
     * Copies name/URL/zoom/ratio/axis/headers; no network request.
     */
    void populateXYZ(const XYZConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Hydrate WMS draft controls from a saved value.
     * @param[in] conn Borrowed connection copied into widgets.
     * @param[in] auth Borrowed Basic credentials copied into inputs; no ownership transfer.
     * Copies URL/auth/advanced flags/headers, but not capabilities/tree selection.
     */
    void populateWMS(const WMSConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Hydrate WCS draft controls from a saved value.
     * @param[in] conn Borrowed connection copied into widgets.
     * @param[in] auth Borrowed Basic credentials copied into inputs; no ownership transfer.
     * Copies URL/range/auth, inserts missing saved CRS/format choices, selects matching
     * interpolation; current implementation does not hydrate the header widget.
     */
    void populateWCS(const struct WCSConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Hydrate ArcGIS draft controls from a saved value.
     * @param[in] conn Borrowed connection copied into widgets.
     * @param[in] auth Borrowed Basic credentials copied into inputs; no ownership transfer.
     * Copies URL/portal endpoints/auth/headers, without rebuilding derived XYZ metadata.
     */
    void populateArcGIS(const ArcGISRestConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Hydrate local-raster drafts and probe their georeferencing.
     * @param[in] conn Borrowed saved name/path/world-file/CRS value copied into widgets; no
     * ownership transfer. Probe may fill missing draft fields without writing files.
     */
    void populateLocal(const LocalRasterConnection &conn);
    /**
     * @brief Synchronously probe an existing raster and display transform/CRS availability.
     * Empty/missing paths clear/show status. Prefills an empty CRS from an embedded authority code,
     * finds a sidecar only if transform/path missing, and seeds an empty name from basename. Does
     * not write files.
     */
    void updateLocalGeorefStatus();

    /**
     * @brief Replace tree-owned rows from WMS named layers and advertised image formats.
     * @param[in] info Borrowed copied service metadata, read synchronously; titles fall back to
     * stable names/IDs stored in UserRole. No service request or layer creation.
     */
    void populateWMSTree(const struct WMSServiceInfo &info);
    /**
     * @brief Replace tree-owned rows from WMTS layer IDs and global tile-matrix-set IDs
     * (formats/styles update after selection).
     * @param[in] info Borrowed copied service metadata, read synchronously; titles fall back to
     * stable names/IDs stored in UserRole. No service request or layer creation.
     */
    void populateWMTSTree(const struct WMTSServiceInfo &info);
    /**
     * @brief Replace tree-owned rows from WCS coverage IDs, clearing prior formats but retaining
     * other options.
     * @param[in] info Borrowed copied service metadata, read synchronously; titles fall back to
     * stable names/IDs stored in UserRole. No service request or layer creation.
     */
    void populateWCSTree(const struct WCSServiceInfo &info);

    // Helpers for createLayer()
    /**
     * @brief Construct/transfer the XYZ source: requires a nonempty trimmed URL, creates 256-pixel
     * tiles with ratio/axis/headers/auth and persists a named connection. Zoom controls are stored
     * in the saved connection but are not applied to the new layer here.
     * @param[in] parent Optional Qt owner installed directly.
     * @return Caller-owned new/transferred layer subject to Qt parenting, or nullptr for the stated
     * missing/invalid state. Later asynchronous load failures are not reported by this return.
     */
    [[nodiscard]] OpenSWMMVisLayer *buildXYZLayer(QObject *parent) const;
    /**
     * @brief Construct/transfer the WMS source: requires copied WMS capabilities and a selected
     * layer; copies service info, selected style/format/CRS, headers/auth and title; does not
     * persist advanced form flags or the connection here.
     * @param[in] parent Optional workspace owner, obtained by OpenSWMMVisWorkspace cast; other
     * QObject types yield an unparented result.
     * @return Caller-owned new/transferred layer subject to Qt parenting, or nullptr for the stated
     * missing/invalid state. Later asynchronous load failures are not reported by this return.
     */
    [[nodiscard]] OpenSWMMVisLayer *buildWMSLayer(QObject *parent) const;
    /**
     * @brief Construct/transfer the WMTS source: requires copied WMTS capabilities and a selected
     * layer; copies service info, selected matrix/style/format, headers/auth and title.
     * @param[in] parent Optional workspace owner, obtained by OpenSWMMVisWorkspace cast; other
     * QObject types yield an unparented result.
     * @return Caller-owned new/transferred layer subject to Qt parenting, or nullptr for the stated
     * missing/invalid state. Later asynchronous load failures are not reported by this return.
     */
    [[nodiscard]] OpenSWMMVisLayer *buildWMTSLayer(QObject *parent) const;
    /**
     * @brief Construct/transfer the WCS source: requires copied WCS capabilities and selected
     * coverage; copies metadata/options/auth/headers and persists a connection named from the
     * coverage. Empty CRS/format/interpolation keeps layer defaults.
     * @param[in] parent Optional workspace owner, obtained by OpenSWMMVisWorkspace cast; other
     * QObject types yield an unparented result.
     * @return Caller-owned new/transferred layer subject to Qt parenting, or nullptr for the stated
     * missing/invalid state. Later asynchronous load failures are not reported by this return.
     */
    [[nodiscard]] OpenSWMMVisLayer *buildWCSLayer(QObject *parent) const;
    /**
     * @brief Construct/transfer the ArcGIS source: requires previously derived XYZ metadata;
     * creates a 256-pixel ZYX tile layer with derived headers/current Basic auth and persists
     * portal/connection fields.
     * @param[in] parent Optional workspace owner, obtained by OpenSWMMVisWorkspace cast; other
     * QObject types yield an unparented result.
     * @return Caller-owned new/transferred layer subject to Qt parenting, or nullptr for the stated
     * missing/invalid state. Later asynchronous load failures are not reported by this return.
     */
    [[nodiscard]] OpenSWMMVisLayer *buildArcGISLayer(QObject *parent) const;
    /**
     * @brief Construct/transfer the LocalRaster source: validates raster/world-file existence,
     * transform/CRS availability, parses world-file/authority overrides and writes PAM
     * georeferencing when needed. Reports validation/write errors, then creates a basemap
     * GISRasterLayer, starts openAsync and persists named connection drafts.
     * @param[in] parent Optional workspace owner, obtained by OpenSWMMVisWorkspace cast; other
     * QObject types yield an unparented result.
     * @return Caller-owned new/transferred layer subject to Qt parenting, or nullptr for the stated
     * missing/invalid state. Later asynchronous load failures are not reported by this return.
     */
    [[nodiscard]] OpenSWMMVisLayer *buildLocalRasterLayer(QObject *parent) const;

    // ---------- Widgets ----------------------------------------------------

    QTabWidget *m_tabs = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    // XYZ
    QComboBox *m_xyzCombo = nullptr;
    QPushButton *m_xyzNew = nullptr;
    QPushButton *m_xyzEdit = nullptr;
    QPushButton *m_xyzDel = nullptr;
    QLineEdit *m_xyzName = nullptr;
    QLineEdit *m_xyzUrl = nullptr;
    QSpinBox *m_xyzZMin = nullptr;
    QSpinBox *m_xyzZMax = nullptr;
    QComboBox *m_xyzPixRatio = nullptr; // 0/1/2
    QComboBox *m_xyzAxis = nullptr;     // ZXY / ZYX
    QGroupBox *m_xyzAuthBox = nullptr;
    QLineEdit *m_xyzUser = nullptr;
    QLineEdit *m_xyzPass = nullptr;
    QPushButton *m_xyzEye = nullptr;
    BasemapHttpHeadersWidget *m_xyzHeaders = nullptr;
    QPushButton *m_xyzTestBtn = nullptr;
    QLabel *m_xyzTestLabel = nullptr;

    // WMS / WMTS
    QComboBox *m_wmsCombo = nullptr;
    QPushButton *m_wmsNew = nullptr;
    QPushButton *m_wmsEdit = nullptr;
    QPushButton *m_wmsDel = nullptr;
    QLineEdit *m_wmsUrl = nullptr;
    QPushButton *m_wmsConnect = nullptr;
    QLabel *m_wmsProtocol = nullptr; // shows "WMS" or "WMTS"
    QGroupBox *m_wmsAuthBox = nullptr;
    QLineEdit *m_wmsUser = nullptr;
    QLineEdit *m_wmsPass = nullptr;
    QPushButton *m_wmsEye = nullptr;
    QTreeWidget *m_wmsTree = nullptr;
    QComboBox *m_wmsStyle = nullptr;
    QComboBox *m_wmsFmt = nullptr;
    QComboBox *m_wmsCrs = nullptr;
    QComboBox *m_wmsTms = nullptr; // tile matrix set (WMTS only)
    QLabel *m_wmsTmsLabel = nullptr;
    // Advanced
    QComboBox *m_wmsDpiMode = nullptr;
    QComboBox *m_wmsRatio = nullptr;
    QCheckBox *m_wmsIgnoreUri = nullptr;
    QCheckBox *m_wmsIgnoreAxis = nullptr;
    QCheckBox *m_wmsInvertAxis = nullptr;
    QCheckBox *m_wmsSmooth = nullptr;
    BasemapHttpHeadersWidget *m_wmsHeaders = nullptr;
    QLabel *m_wmsStatus = nullptr;

    // WCS
    QComboBox *m_wcsCombo = nullptr;
    QPushButton *m_wcsNew = nullptr;
    QPushButton *m_wcsDel = nullptr;
    QLineEdit *m_wcsUrl = nullptr;
    QPushButton *m_wcsConnect = nullptr;
    QLabel *m_wcsStatus = nullptr;
    QTreeWidget *m_wcsCovTree = nullptr;
    QComboBox *m_wcsFmt = nullptr;
    QComboBox *m_wcsCrs = nullptr;
    QLineEdit *m_wcsRange = nullptr;
    QComboBox *m_wcsInterp = nullptr;
    QGroupBox *m_wcsAuthBox = nullptr;
    QLineEdit *m_wcsUser = nullptr;
    QLineEdit *m_wcsPass = nullptr;
    QPushButton *m_wcsEye = nullptr;
    BasemapHttpHeadersWidget *m_wcsHeaders = nullptr;

    // ------ WFS tab ---------------------------------------------------
    QComboBox *m_wfsCombo = nullptr;
    QPushButton *m_wfsNew = nullptr;
    QPushButton *m_wfsDel = nullptr;
    QLineEdit *m_wfsUrl = nullptr;
    QPushButton *m_wfsConnect = nullptr;
    QLabel *m_wfsStatus = nullptr;
    QListWidget *m_wfsTypes = nullptr;
    QGroupBox *m_wfsAuthBox = nullptr;
    QLineEdit *m_wfsUser = nullptr;
    QLineEdit *m_wfsPass = nullptr;
    QPushButton *m_wfsEye = nullptr;
    BasemapHttpHeadersWidget *m_wfsHeaders = nullptr;

    // ArcGIS
    QComboBox *m_arcCombo = nullptr;
    QPushButton *m_arcNew = nullptr;
    QPushButton *m_arcEdit = nullptr;
    QPushButton *m_arcDel = nullptr;
    QLineEdit *m_arcUrl = nullptr;
    QLineEdit *m_arcPrefix = nullptr;
    QLineEdit *m_arcContent = nullptr;
    QLineEdit *m_arcCommunity = nullptr;
    QGroupBox *m_arcAuthBox = nullptr;
    QLineEdit *m_arcUser = nullptr;
    QLineEdit *m_arcPass = nullptr;
    QPushButton *m_arcEye = nullptr;
    BasemapHttpHeadersWidget *m_arcHeaders = nullptr;
    QPushButton *m_arcConnect = nullptr;
    QLabel *m_arcInfoLabel = nullptr;

    // Local file
    QComboBox *m_localCombo = nullptr;
    QPushButton *m_localNew = nullptr;
    QPushButton *m_localDel = nullptr;
    QLineEdit *m_localName = nullptr;
    QLineEdit *m_localFile = nullptr;
    QPushButton *m_localFileBrowse = nullptr;
    QLineEdit *m_localWorld = nullptr;
    QPushButton *m_localWorldBrowse = nullptr;
    QLineEdit *m_localCrs = nullptr; // read-only; set via CRS dialog
    QPushButton *m_localCrsBtn = nullptr;
    QLabel *m_localStatus = nullptr;

    // ---------- Runtime state ----------------------------------------------

    QNetworkAccessManager *m_nam = nullptr;

    // WMS runtime
    bool m_isWMTS = false;
    std::unique_ptr<WMSServiceInfo> m_wmsInfo;
    std::unique_ptr<WMTSServiceInfo> m_wmtsInfo;

    // WCS runtime
    struct WCSServiceInfo *m_wcsInfo = nullptr;

    // ------ WFS state -------------------------------------------------
    /**
     * @brief Build the WFS page: collection discovery, saved connection actions and auth/headers;
     * loads saved names here.
     * @param[in,out] page Required previously unlaid-out widget, receiving a new vertical layout
     * and owning controls; later wrapped/reparented into this dialog. Called once; does not fetch
     * data during construction.
     */
    void setupUiWFS(QWidget *page);
    /**
     * @brief Rebuild WFS saved connection names while suppressing selection signals.
     * Preserves the current name when found, otherwise selects an empty first row; no-op before
     * combo construction.
     */
    void refreshWFSCombo();
    /**
     * @brief Hydrate WFS draft controls from a saved value.
     * @param[in] conn Borrowed connection copied into widgets.
     * @param[in] auth Borrowed Basic credentials copied into inputs; no ownership transfer.
     * Copies URL/headers and credentials; auth is checked only for a nonempty username. Does not
     * restore selected type/capabilities.
     */
    void populateWFS(const WFSConnection &conn, const BasemapAuth &auth);
    /**
     * @brief Parse capabilities and display readable/unreadable feature collections.
     * @param[in] body Borrowed response bytes parsed synchronously into owned capabilities. Caller
     * cleared old list/choices before discovery; this helper appends entries rather than clearing
     * itself.
     * Reports parser/service failure, disables unsupported-format collections with reasons, selects
     * the first usable type and recomputes OK gating after success.
     */
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
    /**
     * @brief Fetch the selected WFS collection and accept only after a layer adopts usable response
     * features.
     * Uses advertised readable format, adds preferred lon/lat extent only with supported EPSG:4326,
     * disables OK while pending, and retains the fetched unparented layer for transfer. Failure
     * deletes the temporary result, displays parser/network text and re-enables OK. No project
     * insertion occurs here.
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
    /**
     * @brief Enable OK on non-WFS pages, or only for an in-range readable WFS collection.
     * No-op before button-box construction; form reachability and most non-WFS validation are
     * deferred to createLayer/loading.
     */
    void updateOkEnabled();

    /**
     * @brief Construct/transfer the WFS source: consumes the already fetched layer from mutable
     * pending state and reparents it. Does not fetch; a second call returns nullptr until another
     * successful fetch.
     * @param[in] parent Optional Qt owner installed directly.
     * @return Caller-owned new/transferred layer subject to Qt parenting, or nullptr for the stated
     * missing/invalid state. Later asynchronous load failures are not reported by this return.
     */
    [[nodiscard]] OpenSWMMVisLayer *buildWFSLayer(QObject *parent) const;

    /**
     * @brief Capture enabled Basic-auth fields and all custom WFS request headers.
     * @return Owned credential/header value; username/password omitted when auth group is
     * unchecked. No network or credential-store write occurs.
     */
    [[nodiscard]] HydroCouple::Ogc::ServiceCredentials wfsCredentials() const;

    HydroCouple::Ogc::HttpClient *m_wfsClient = nullptr;
    HydroCouple::Ogc::WfsCapabilities m_wfsCaps;

    //! Type name, and why it cannot be used. Empty reason means it can.
    QList<QPair<QString, QString>> m_wfsChoices;

    QRectF m_preferredExtent;
    QString m_wfsStatusText;

    /*!
     * \brief The fetched layer, awaiting collection by createLayer().
     *
     * Mutable because createLayer() is const across every other page, where
     * building a layer really is a pure read of the form. Here the layer
     * already exists and is being handed over.
     */
    mutable WFSLayer *m_wfsLayer = nullptr;

    // ArcGIS runtime — derived XYZ connection built after Connect
    XYZConnection m_arcDerivedXYZ;
};

#endif // ADDBASEMAPDIALOG_H
