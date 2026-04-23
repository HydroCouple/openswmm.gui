/*!
 * \file   wmsconnectiondialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef WMSCONNECTIONDIALOG_H
#define WMSCONNECTIONDIALOG_H

#include <QDialog>
#include <QString>
#include <QUrl>
#include <QList>

class QLineEdit;
class QComboBox;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QDialogButtonBox;
class QProgressIndicator;
class WMSLayer;
struct WMSServiceInfo;
struct WMSLayerInfo;

/*!
 * \class WMSConnectionDialog
 * \brief Dialog for connecting to an OGC WMS service and selecting a layer to add.
 * \details Own implementation — no QGIS dependency.
 *          The dialog allows the user to:
 *  1. Enter a WMS base URL.
 *  2. Click "Connect" to asynchronously fetch GetCapabilities.
 *  3. Browse the returned layer tree.
 *  4. Pick a layer, style, image format, and CRS.
 *  5. Accept to receive a configured WMSLayer ready to be added to the canvas.
 *
 * The dialog also persists a list of recently used WMS URLs in QSettings.
 *
 * Usage:
 * \code
 *   WMSConnectionDialog dlg(this);
 *   if (dlg.exec() == QDialog::Accepted)
 *   {
 *       WMSLayer *layer = dlg.createLayer(project);
 *       canvas->addLayer(layer);
 *   }
 * \endcode
 */
class WMSConnectionDialog : public QDialog
{
    Q_OBJECT

public:

    explicit WMSConnectionDialog(QWidget *parent = nullptr);
    ~WMSConnectionDialog() override;

    /*!
     * \brief Returns a configured WMSLayer instance for the user's selection.
     * \details Caller takes ownership.  Returns nullptr if rejected or no layer selected.
     * \param project  Optional Qt parent for the new layer object.
     */
    [[nodiscard]] WMSLayer *createLayer(QObject *project = nullptr) const;

private slots:
    void onConnectClicked();
    void onCapabilitiesFetched(const WMSServiceInfo &info);
    void onCapabilitiesError(const QString &error);
    void onLayerSelectionChanged();
    void onUrlComboChanged(const QString &text);

private:
    void setupUi();
    void populateLayerTree(const WMSServiceInfo &info);
    void saveRecentUrls();
    void loadRecentUrls();

    QComboBox         *m_urlCombo        = nullptr;
    QTreeWidget       *m_layerTree       = nullptr;
    QComboBox         *m_styleCombo      = nullptr;
    QComboBox         *m_formatCombo     = nullptr;
    QComboBox         *m_crsCombo        = nullptr;
    QLabel            *m_serviceTitle    = nullptr;
    QDialogButtonBox  *m_buttonBox       = nullptr;

    WMSLayer          *m_pendingLayer    = nullptr; /*!< Temporary layer for caps fetch. */
    WMSServiceInfo    *m_serviceInfo     = nullptr;
    QString            m_selectedLayerName;
};

#endif // WMSCONNECTIONDIALOG_H
