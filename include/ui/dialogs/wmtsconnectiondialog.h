/**
 * @file      wmtsconnectiondialog.h
 * @brief     Dialog for connecting to an OGC WMTS service and selecting a layer to add.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef WMTSCONNECTIONDIALOG_H
#define WMTSCONNECTIONDIALOG_H

#include <QDialog>
#include <QString>
#include <QUrl>

class QLineEdit;
class QComboBox;
class QTreeWidget;
class QLabel;
class QDialogButtonBox;
class WMTSLayer;
struct WMTSServiceInfo;

/*!
 * \class WMTSConnectionDialog
 * \brief Dialog for connecting to an OGC WMTS service and selecting a layer to add.
 * \details Own implementation — no QGIS dependency.
 *          The dialog allows the user to:
 *  1. Enter a WMTS base URL.
 *  2. Click "Connect" to asynchronously fetch GetCapabilities.
 *  3. Browse the returned layer list.
 *  4. Pick a layer, tile matrix set, style, and image format.
 *  5. Accept to receive a configured WMTSLayer ready to be added to the canvas.
 *
 * Recently used WMTS URLs are persisted in QSettings.
 *
 * Usage:
 * \code
 *   WMTSConnectionDialog dlg(this);
 *   if (dlg.exec() == QDialog::Accepted)
 *   {
 *       WMTSLayer *layer = dlg.createLayer(project);
 *       canvas->addLayer(layer);
 *   }
 * \endcode
 */
class WMTSConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build the service form and load recent URL choices without starting a connection.
     * @note GUI-thread UI/network event loop required. Initially no selected layer and OK disabled;
     *   named dialog/splitter permit host layout persistence. Does not add a canvas layer.
     */
    explicit WMTSConnectionDialog(QWidget *parent = nullptr);
    /**
     * @brief Delete the temporary fetching layer, copied service metadata and owned widgets.
     * @note Returned createLayer objects are independent and are not deleted by this dialog;
     *   pending network objects belong to the temporary layer.
     */
    ~WMTSConnectionDialog() override;

    /*!
     * \brief Returns a configured WMTSLayer for the user's selection.
     * \details Workspace parent owns a parented result; otherwise caller owns it. Caller must check
     *   dialog acceptance.
     * @param[in] parent Optional QObject accepted only when it is an OpenSWMMVisWorkspace; other
     *   types are ignored, yielding an unparented layer.
     * @return New configured layer each call, owned by matching workspace parent when supplied,
     *   otherwise caller-owned. Null for empty selected identifier or trimmed current URL.
     * @note Does NOT check dialog acceptance/rejection; caller must check exec result. Uses CURRENT
     *   edited URL, stored selection and copied capabilities when available. No URL/options
     *   validation or further capabilities request here. Does not attach layer to canvas or
     *   workspace list.
     * @note Copy cached capabilities before setting selected options so first render uses
     *   negotiated service metadata. Copy active layer identifier, tile matrix set, style and
     *   format strings. Use advertised title or identifier as display name when matching metadata
     *   exists. Source dialog retains its own metadata.
     */
    [[nodiscard]] WMTSLayer *createLayer(QObject *parent = nullptr) const;

private slots:
    /**
     * @brief For a nonblank trimmed URL, replace the owned temporary layer and start asynchronous
     *   GetCapabilities, then persist recent URLs.
     * @note Pass current URL unchanged to temporary WMTS layer; its fetch code normalizes OGC query
     *   keys. Clear available items, disable OK and show connecting status; existing cached
     *   metadata/options are not explicitly discarded.
     * @note Blank URL is a no-op. Editing URL does not cancel this request or establish a request
     *   epoch, so a later response can repopulate choices for a URL different from current text. No
     *   canvas/model insertion; errors are delivered through the connected slots.
     */
    void onConnectClicked();
    /**
     * @param[in] info Borrowed successfully parsed metadata copied by value; no ownership of caller
     *   data.
     * @brief Replace cached metadata, update service title, and rebuild available layers.
     * @note Does not automatically select a layer or accept. Per-layer format/matrix/style options
     *   are filled only after selection. No association check against current edited URL; previous
     *   options may remain until a new matching selection.
     */
    void onCapabilitiesFetched(const WMTSServiceInfo &info);
    /**
     * @param[in] error Copied into status label and passed to modal warning; no ownership retained.
     * @brief Report capabilities failure with a status message and modal warning.
     * @note Does not clear cached capabilities/options, cancel another operation or perform a new
     *   request. Nested warning event loop requires ordinary dialog lifetime discipline.
     */
    void onCapabilitiesError(const QString &error);
    /**
     * @brief Store first selected item identifier and enable OK exactly when it is nonempty.
     * @note Empty selection clears identifier/disables OK while leaving option combos unchanged.
     *   Matching cached metadata repopulates tile matrix sets, formats (image/png fallback) and
     *   styles (default fallback). Nonmatching identifier keeps previous options; no CRS/matrix
     *   compatibility check or network request.
     */
    void onLayerSelectionChanged();
    /**
     * @param[in] text Ignored signal payload; function invalidates displayed connection state
     *   irrespective of text.
     * @brief Disable OK, clear available layers and service status.
     * @note Does not cancel pending fetch, delete cached metadata or explicitly clear option
     *   combos. Selection notifications can clear stored identifier when an actual selected item
     *   was removed. A pending old response can still refill choices.
     */
    void onUrlComboChanged(const QString &text);

private:
    /**
     * @brief Create owned editable URL combo, flat layer item widget, option combos and OK/Cancel
     *   controls, then connect their actions.
     * @note OK starts disabled and accepts directly without fetching/validating a new layer. Layer
     *   list is flat with no root decoration. No initial metadata request or settings read in this
     *   helper.
     */
    void setupUi();
    /**
     * @param[in] info Borrowed service metadata read synchronously; each advertised layer becomes
     *   one top-level item.
     * @brief Replace all items in advertised order using title or identifier and abstract tooltip.
     * @note Store raw layer name/identifier in UserRole, including empty values. Does not
     *   reconstruct hierarchy, deduplicate entries, select first item, update cached metadata or
     *   fetch tiles/images. Clearing can emit selection notifications.
     */
    void populateLayerList(const WMTSServiceInfo &info);
    /**
     * @brief Write WMTSConnectionDialog/recentUrls in default-application QSettings from current
     *   combo items plus trimmed current URL first.
     * @note Remove exact duplicates of current URL only, then retain first 15; other
     *   duplicates/order remain. Does not insert new URL into live combo, so repeated connects
     *   within one dialog are not accumulated beyond its loaded items. No URL validation, sync call
     *   or error reporting; called after fetch starts, regardless of eventual success.
     */
    void saveRecentUrls();
    /**
     * @brief Replace URL combo items with stored recent URLs, using the built-in example service
     *   when stored list is empty.
     * @note Stored order/duplicates/raw text accepted without validation or 15-item limit at read.
     *   Combo signals are not blocked and can clear displayed connection state; no automatic fetch
     *   or preference write.
     */
    void loadRecentUrls();

    QComboBox *m_urlCombo = nullptr;
    QTreeWidget *m_layerList = nullptr;
    QComboBox *m_tileMatrixCombo = nullptr;
    QComboBox *m_styleCombo = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QLabel *m_serviceTitle = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;

    WMTSLayer *m_pendingLayer = nullptr;
    WMTSServiceInfo *m_serviceInfo = nullptr;
    QString m_selectedLayerId;
};

#endif // WMTSCONNECTIONDIALOG_H
