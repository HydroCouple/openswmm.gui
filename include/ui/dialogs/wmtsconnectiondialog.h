/*!
 * \file   wmtsconnectiondialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
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

    explicit WMTSConnectionDialog(QWidget *parent = nullptr);
    ~WMTSConnectionDialog() override;

    /*!
     * \brief Returns a configured WMTSLayer for the user's selection.
     * \details Caller takes ownership.  Returns nullptr if rejected.
     */
    [[nodiscard]] WMTSLayer *createLayer(QObject *parent = nullptr) const;

private slots:
    void onConnectClicked();
    void onCapabilitiesFetched(const WMTSServiceInfo &info);
    void onCapabilitiesError(const QString &error);
    void onLayerSelectionChanged();
    void onUrlComboChanged(const QString &text);

private:
    void setupUi();
    void populateLayerList(const WMTSServiceInfo &info);
    void saveRecentUrls();
    void loadRecentUrls();

    QComboBox        *m_urlCombo        = nullptr;
    QTreeWidget      *m_layerList       = nullptr;
    QComboBox        *m_tileMatrixCombo = nullptr;
    QComboBox        *m_styleCombo      = nullptr;
    QComboBox        *m_formatCombo     = nullptr;
    QLabel           *m_serviceTitle    = nullptr;
    QDialogButtonBox *m_buttonBox       = nullptr;

    WMTSLayer        *m_pendingLayer    = nullptr;
    WMTSServiceInfo  *m_serviceInfo     = nullptr;
    QString           m_selectedLayerId;
};

#endif // WMTSCONNECTIONDIALOG_H
