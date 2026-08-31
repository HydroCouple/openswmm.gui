/*!
 * \file   wfsservicedialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  WFSServiceDialog — connecting to a Web Feature Service and taking
 *         a collection off it.
 *
 * \details
 * The user pastes an address; everything else is asked of the server. Which
 * version it speaks, what collections it holds, what each can be asked for
 * and in which coordinate systems are all answers, not questions.
 *
 * Deliberately not part of AddBasemapDialog. A feature collection is data,
 * not a backdrop: it goes into the layer tree where it can be queried,
 * classified and labelled, and the basemap dialog's tabs are all about
 * pictures.
 */

#ifndef WFSSERVICEDIALOG_H
#define WFSSERVICEDIALOG_H

#include <hydrocoupleogc/httpclient.h>
#include <hydrocoupleogc/servicecredentials.h>
#include <hydrocoupleogc/wfscapabilities.h>

#include <QDialog>
#include <QRectF>
#include <QString>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class WFSLayer;

/*!
 * \class WFSServiceDialog
 * \brief Lists what a feature service holds and fetches one collection.
 */
class WFSServiceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WFSServiceDialog(QWidget *parent = nullptr);

    ~WFSServiceDialog() override;

    /*!
     * \brief Asks the service at the address now entered what it holds.
     *
     * Returns as soon as the request is made; the list fills in when the
     * answer arrives. Public because it is what the Connect button does,
     * and a test should press the button rather than reach past it.
     */
    void connectToService();

    /*!
     * \brief The ground a request should be limited to, in degrees.
     *
     * A feature service holds a region or a country and a model needs one
     * catchment. Without this the request is limited only by a feature
     * count, and returns an arbitrary few thousand from wherever the
     * service starts counting.
     */
    void setPreferredExtent(const QRectF &lonLatBounds);

    /*!
     * \brief The layer built from the fetched collection.
     *
     * Fetched before the dialog accepts, because a feature request is the
     * one thing here that can fail after the user has chosen: the
     * collection exists, the request is well formed, and the service may
     * still hold nothing over that ground.
     *
     * \returns The layer, or nullptr. The caller takes ownership.
     */
    [[nodiscard]] WFSLayer *takeLayer();

    //! What the service calls itself, once connected.
    [[nodiscard]] QString serviceTitle() const;

    //! What went wrong, or what was found.
    [[nodiscard]] QString status() const;

private:
    void fetchThenAccept();

    void showCollections(const QByteArray &body);

    [[nodiscard]] HydroCouple::Ogc::ServiceCredentials credentials() const;

    QLineEdit        *m_url      = nullptr;
    QLineEdit        *m_username = nullptr;
    QLineEdit        *m_password = nullptr;
    QPushButton      *m_connect  = nullptr;
    QListWidget      *m_types    = nullptr;
    QLabel           *m_status   = nullptr;
    QDialogButtonBox *m_buttons  = nullptr;

    HydroCouple::Ogc::HttpClient    *m_client = nullptr;
    HydroCouple::Ogc::WfsCapabilities m_capabilities;

    //! One row per collection; empty reason means it can be read.
    QList<QPair<QString, QString>> m_choices;

    QRectF   m_preferredExtent;
    QString  m_statusText;
    WFSLayer *m_layer = nullptr;
};

#endif // WFSSERVICEDIALOG_H
