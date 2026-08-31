/*!
 * \file   wfsservicedialog.cpp
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/wfsservicedialog.h"

#include "layers/wfslayer.h"

#include <hydrocoupleogc/servicediscovery.h>
#include <hydrocoupleogc/wfsrequest.h>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

using HydroCouple::Ogc::HttpResponse;

WFSServiceDialog::WFSServiceDialog(QWidget *parent)
    : QDialog(parent), m_client(new HydroCouple::Ogc::HttpClient(this))
{
    setWindowTitle(tr("Add Web Feature Service"));
    setObjectName(QStringLiteral("wfsServiceDialog"));

    m_url = new QLineEdit(this);
    m_url->setObjectName(QStringLiteral("wfsUrlEdit"));
    m_url->setPlaceholderText(
        tr("https://example.org/geoserver/wfs"));

    m_username = new QLineEdit(this);
    m_username->setObjectName(QStringLiteral("wfsUsernameEdit"));

    m_password = new QLineEdit(this);
    m_password->setObjectName(QStringLiteral("wfsPasswordEdit"));
    m_password->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout;
    form->addRow(tr("&Address"),   m_url);
    form->addRow(tr("&User name"), m_username);
    form->addRow(tr("&Password"),  m_password);

    m_connect = new QPushButton(tr("&Connect"), this);
    m_connect->setObjectName(QStringLiteral("wfsConnectButton"));

    m_types = new QListWidget(this);
    m_types->setObjectName(QStringLiteral("wfsCollectionList"));

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("wfsStatusLabel"));
    m_status->setWordWrap(true);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Add Layer"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_connect);
    layout->addWidget(m_types, 1);
    layout->addWidget(m_status);
    layout->addWidget(m_buttons);

    connect(m_connect, &QPushButton::clicked, this,
            &WFSServiceDialog::connectToService);
    connect(m_buttons, &QDialogButtonBox::accepted, this,
            &WFSServiceDialog::fetchThenAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this,
            &WFSServiceDialog::reject);

    connect(m_types, &QListWidget::currentRowChanged, this, [this](int row) {
        const bool usable = row >= 0 && row < m_choices.size()
                            && m_choices.at(row).second.isEmpty();
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(usable);

        if (row >= 0 && row < m_choices.size() && !usable)
            m_status->setText(m_choices.at(row).second);
    });

    resize(560, 480);
}

WFSServiceDialog::~WFSServiceDialog()
{
    delete m_layer;
}

HydroCouple::Ogc::ServiceCredentials WFSServiceDialog::credentials() const
{
    HydroCouple::Ogc::ServiceCredentials credentials;
    credentials.username = m_username->text();
    credentials.password = m_password->text();

    return credentials;
}

void WFSServiceDialog::setPreferredExtent(const QRectF &lonLatBounds)
{
    m_preferredExtent = lonLatBounds;
}

QString WFSServiceDialog::status() const
{
    return m_statusText;
}

QString WFSServiceDialog::serviceTitle() const
{
    return m_capabilities.ok && !m_capabilities.title.isEmpty()
               ? m_capabilities.title
               : m_url->text();
}

WFSLayer *WFSServiceDialog::takeLayer()
{
    WFSLayer *layer = m_layer;
    m_layer = nullptr;

    return layer;
}

void WFSServiceDialog::connectToService()
{
    m_types->clear();
    m_choices.clear();
    m_capabilities = {};
    delete m_layer;
    m_layer = nullptr;
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    const QString url = HydroCouple::Ogc::buildCapabilitiesUrl(
        m_url->text(), HydroCouple::Ogc::ServiceKind::Wfs);

    if (url.isEmpty()) {
        m_statusText = tr("That is not a web address.");
        m_status->setText(m_statusText);
        return;
    }

    m_statusText = tr("Asking %1…").arg(m_url->text());
    m_status->setText(m_statusText);

    m_client->get(QUrl(url), credentials(),
                  [this](const HttpResponse &response) {
                      showCollections(response.body);
                  });
}

void WFSServiceDialog::showCollections(const QByteArray &body)
{
    m_capabilities = HydroCouple::Ogc::parseWfsCapabilities(body);

    if (!m_capabilities.ok) {
        // The service's own account of what is wrong, which it sends in the
        // body under an HTTP 200 as often as not.
        m_statusText = m_capabilities.message.isEmpty()
                           ? tr("That address is not a WFS.")
                           : m_capabilities.message;
        m_status->setText(m_statusText);
        return;
    }

    int usable = 0;

    for (const HydroCouple::Ogc::WfsFeatureType &type :
         m_capabilities.featureTypes) {
        const QString title = type.title.isEmpty() ? type.name : type.title;
        QString reason;

        if (HydroCouple::Ogc::preferredOutputFormat(
                type, m_capabilities.outputFormats)
                .isEmpty()) {
            reason = tr("“%1” is offered only in formats this program cannot "
                        "read.")
                         .arg(title);
        }

        m_choices.append({type.name, reason});

        auto *item = new QListWidgetItem(title, m_types);

        if (!reason.isEmpty()) {
            // Shown rather than hidden: a user looking for a collection that
            // is there needs to be told why it cannot be used.
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(reason);
        } else {
            ++usable;
        }
    }

    m_statusText =
        usable > 0
            ? tr("%1: %2 of %3 collections can be read.")
                  .arg(serviceTitle())
                  .arg(usable)
                  .arg(m_choices.size())
            : tr("%1 holds nothing this program can read.").arg(serviceTitle());
    m_status->setText(m_statusText);

    for (int row = 0; row < m_choices.size(); ++row) {
        if (m_choices.at(row).second.isEmpty()) {
            m_types->setCurrentRow(row);
            break;
        }
    }
}

void WFSServiceDialog::fetchThenAccept()
{
    const int row = m_types->currentRow();

    if (row < 0 || row >= m_choices.size()) return;

    const HydroCouple::Ogc::WfsFeatureType *type =
        m_capabilities.featureType(m_choices.at(row).first);

    if (!type) return;

    HydroCouple::Ogc::WfsGetFeatureRequest request;
    request.typeName = type->name;
    request.outputFormat = HydroCouple::Ogc::preferredOutputFormat(
        *type, m_capabilities.outputFormats);

    // Asked for in longitude and latitude when the collection publishes
    // them, so the ground the map is looking at can be named in the same
    // terms. A collection published in a projected grid alone is fetched
    // whole, up to the feature limit.
    request.crs = type->spellingOf(QStringLiteral("EPSG:4326"));

    if (!request.crs.isEmpty() && !m_preferredExtent.isNull())
        request.extent = m_preferredExtent;

    const QString url =
        HydroCouple::Ogc::buildGetFeatureUrl(m_capabilities, request);

    if (url.isEmpty()) {
        m_statusText = tr("That collection cannot be asked for.");
        m_status->setText(m_statusText);
        return;
    }

    const QString title = m_types->item(row)->text();

    m_statusText = tr("Fetching %1…").arg(title);
    m_status->setText(m_statusText);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    const QString typeName = type->name;
    const QString service  = m_url->text();

    m_client->get(QUrl(url), credentials(),
                  [this, title, typeName, service](const HttpResponse &response) {
                      auto *layer = new WFSLayer(title);
                      QString message;

                      if (!layer->adoptResponse(response.body, message)) {
                          delete layer;

                          // Said here, where the user is looking, rather than
                          // after the dialog has closed on an empty layer.
                          m_statusText = message.isEmpty() ? response.error
                                                           : message;
                          m_status->setText(m_statusText);
                          m_buttons->button(QDialogButtonBox::Ok)
                              ->setEnabled(true);
                          return;
                      }

                      layer->setServiceUrl(service);
                      layer->setTypeName(typeName);

                      delete m_layer;
                      m_layer = layer;

                      accept();
                  });
}
