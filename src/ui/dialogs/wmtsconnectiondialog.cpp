/*!
 * \file   wmtsconnectiondialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/wmtsconnectiondialog.h"
#include "layers/wmtslayer.h"
#include "project/openswmmvisworkspace.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrlQuery>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

WMTSConnectionDialog::WMTSConnectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add WMTS Layer"));
    resize(720, 500);
    setupUi();
    loadRecentUrls();
}

WMTSConnectionDialog::~WMTSConnectionDialog()
{
    delete m_pendingLayer;
    m_pendingLayer = nullptr;
    delete m_serviceInfo;
    m_serviceInfo = nullptr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

WMTSLayer *WMTSConnectionDialog::createLayer(QObject *parent) const
{
    if (m_selectedLayerId.isEmpty())
        return nullptr;

    const QString urlText = m_urlCombo->currentText().trimmed();
    if (urlText.isEmpty())
        return nullptr;

    auto *layer = new WMTSLayer(QUrl(urlText),
                                qobject_cast<OpenSWMMVisWorkspace *>(parent));
    layer->setActiveLayerId(m_selectedLayerId);
    layer->setActiveTileMatrixSet(m_tileMatrixCombo->currentText());
    layer->setActiveStyle(m_styleCombo->currentText());
    layer->setImageFormat(m_formatCombo->currentText());

    // Set human-readable name
    if (m_serviceInfo)
    {
        for (const WMTSLayerInfo &info : m_serviceInfo->layers)
        {
            if (info.identifier == m_selectedLayerId)
            {
                layer->setName(info.title.isEmpty() ? info.identifier : info.title);
                break;
            }
        }
    }

    return layer;
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void WMTSConnectionDialog::setupUi()
{
    auto *vlay = new QVBoxLayout(this);

    // URL row
    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(new QLabel(tr("URL:"), this));
    m_urlCombo = new QComboBox(this);
    m_urlCombo->setEditable(true);
    m_urlCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_urlCombo->lineEdit()->setPlaceholderText(
        tr("https://example.org/wmts?SERVICE=WMTS"));
    urlRow->addWidget(m_urlCombo, 1);
    auto *connectBtn = new QPushButton(tr("Connect"), this);
    urlRow->addWidget(connectBtn);
    vlay->addLayout(urlRow);

    // Service title
    m_serviceTitle = new QLabel(this);
    m_serviceTitle->setWordWrap(true);
    vlay->addWidget(m_serviceTitle);

    // Splitter: layer list | options
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_layerList = new QTreeWidget(this);
    m_layerList->setHeaderLabel(tr("Available Layers"));
    m_layerList->setRootIsDecorated(false);
    splitter->addWidget(m_layerList);

    // Options panel
    auto *optWidget = new QWidget(this);
    auto *optVlay   = new QVBoxLayout(optWidget);
    optVlay->setContentsMargins(4, 0, 0, 0);

    optVlay->addWidget(new QLabel(tr("Tile Matrix Set:"), optWidget));
    m_tileMatrixCombo = new QComboBox(optWidget);
    optVlay->addWidget(m_tileMatrixCombo);

    optVlay->addWidget(new QLabel(tr("Style:"), optWidget));
    m_styleCombo = new QComboBox(optWidget);
    optVlay->addWidget(m_styleCombo);

    optVlay->addWidget(new QLabel(tr("Image format:"), optWidget));
    m_formatCombo = new QComboBox(optWidget);
    optVlay->addWidget(m_formatCombo);

    optVlay->addStretch();
    splitter->addWidget(optWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    vlay->addWidget(splitter, 1);

    // Buttons
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    vlay->addWidget(m_buttonBox);

    connect(connectBtn, &QPushButton::clicked, this, &WMTSConnectionDialog::onConnectClicked);
    connect(m_urlCombo, &QComboBox::currentTextChanged,
            this, &WMTSConnectionDialog::onUrlComboChanged);
    connect(m_layerList, &QTreeWidget::itemSelectionChanged,
            this, &WMTSConnectionDialog::onLayerSelectionChanged);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void WMTSConnectionDialog::onConnectClicked()
{
    const QString urlText = m_urlCombo->currentText().trimmed();
    if (urlText.isEmpty())
        return;

    delete m_pendingLayer;
    m_pendingLayer = new WMTSLayer(QUrl(urlText), nullptr);
    m_pendingLayer->setParent(this);

    connect(m_pendingLayer, &WMTSLayer::capabilitiesFetched,
            this, &WMTSConnectionDialog::onCapabilitiesFetched);
    connect(m_pendingLayer, &WMTSLayer::capabilitiesError,
            this, &WMTSConnectionDialog::onCapabilitiesError);

    m_serviceTitle->setText(tr("Connecting to %1 …").arg(urlText));
    m_layerList->clear();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    m_pendingLayer->fetchCapabilities();
    saveRecentUrls();
}

void WMTSConnectionDialog::onCapabilitiesFetched(const WMTSServiceInfo &info)
{
    delete m_serviceInfo;
    m_serviceInfo = new WMTSServiceInfo(info);

    m_serviceTitle->setText(info.title.isEmpty()
                            ? tr("WMTS service")
                            : info.title);

    populateLayerList(info);
}

void WMTSConnectionDialog::onCapabilitiesError(const QString &error)
{
    m_serviceTitle->setText(tr("Error: %1").arg(error));
    QMessageBox::warning(this, tr("WMTS Connection Error"), error);
}

void WMTSConnectionDialog::onLayerSelectionChanged()
{
    const QList<QTreeWidgetItem *> sel = m_layerList->selectedItems();
    if (sel.isEmpty())
    {
        m_selectedLayerId.clear();
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }

    m_selectedLayerId = sel.first()->data(0, Qt::UserRole).toString();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!m_selectedLayerId.isEmpty());

    // Populate tile matrix set, styles, formats for this layer
    if (m_serviceInfo)
    {
        for (const WMTSLayerInfo &info : m_serviceInfo->layers)
        {
            if (info.identifier == m_selectedLayerId)
            {
                m_tileMatrixCombo->clear();
                m_tileMatrixCombo->addItems(info.tileMatrixSetIds);

                m_formatCombo->clear();
                m_formatCombo->addItems(info.formats.isEmpty()
                                        ? QStringList{QStringLiteral("image/png")}
                                        : info.formats);

                // Style: WMTS layers typically have a "default" style
                m_styleCombo->clear();
                m_styleCombo->addItem(QStringLiteral("default"));
                break;
            }
        }
    }
}

void WMTSConnectionDialog::onUrlComboChanged(const QString & /*text*/)
{
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_layerList->clear();
    m_serviceTitle->clear();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void WMTSConnectionDialog::populateLayerList(const WMTSServiceInfo &info)
{
    m_layerList->clear();

    for (const WMTSLayerInfo &lyrInfo : info.layers)
    {
        auto *item = new QTreeWidgetItem(m_layerList);
        item->setText(0, lyrInfo.title.isEmpty() ? lyrInfo.identifier : lyrInfo.title);
        item->setToolTip(0, lyrInfo.abstractText);
        item->setData(0, Qt::UserRole, lyrInfo.identifier);
    }
}

void WMTSConnectionDialog::saveRecentUrls()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("WMTSConnectionDialog"));

    QStringList urls;
    for (int i = 0; i < m_urlCombo->count(); ++i)
        urls << m_urlCombo->itemText(i);

    const QString currentUrl = m_urlCombo->currentText().trimmed();
    urls.removeAll(currentUrl);
    urls.prepend(currentUrl);
    urls = urls.mid(0, 15);

    settings.setValue(QStringLiteral("recentUrls"), urls);
    settings.endGroup();
}

void WMTSConnectionDialog::loadRecentUrls()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("WMTSConnectionDialog"));
    const QStringList urls = settings.value(QStringLiteral("recentUrls")).toStringList();
    settings.endGroup();

    m_urlCombo->clear();
    m_urlCombo->addItems(urls);
}
