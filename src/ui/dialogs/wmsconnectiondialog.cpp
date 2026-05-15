/*!
 * \file   wmsconnectiondialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/wmsconnectiondialog.h"
#include "layers/wmslayer.h"
#include "project/openswmmvisworkspace.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
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

WMSConnectionDialog::WMSConnectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add WMS Layer"));
    resize(720, 520);
    setupUi();
    loadRecentUrls();
}

WMSConnectionDialog::~WMSConnectionDialog()
{
    delete m_pendingLayer;
    m_pendingLayer = nullptr;
    delete m_serviceInfo;
    m_serviceInfo = nullptr;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

WMSLayer *WMSConnectionDialog::createLayer(QObject *project) const
{
    if (m_selectedLayerName.isEmpty() || !m_serviceInfo)
        return nullptr;

    const QString urlText = m_urlCombo->currentText().trimmed();
    if (urlText.isEmpty())
        return nullptr;

    auto *layer = new WMSLayer(QUrl(urlText),
                               qobject_cast<OpenSWMMVisWorkspace *>(project));

    // Transfer the already-fetched service info (including the negotiated WMS
    // version) so the layer uses the correct BBOX axis order and image formats
    // from the first GetMap request — without this the layer uses the default
    // 1.3.0 version string which swaps lat/lon for geographic CRS services.
    layer->setServiceInfo(*m_serviceInfo);

    layer->setActiveLayerName(m_selectedLayerName);
    layer->setActiveStyle(m_styleCombo->currentText());
    layer->setImageFormat(m_formatCombo->currentText());
    layer->setCrs(m_crsCombo->currentText());

    // Set layer name from service info
    for (const WMSLayerInfo &info : m_serviceInfo->layers)
    {
        if (info.name == m_selectedLayerName)
        {
            layer->setName(info.title.isEmpty() ? info.name : info.title);
            break;
        }
    }

    return layer;
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void WMSConnectionDialog::setupUi()
{
    auto *vlay = new QVBoxLayout(this);

    // URL row
    auto *urlRow = new QHBoxLayout;
    urlRow->addWidget(new QLabel(tr("URL:"), this));
    m_urlCombo = new QComboBox(this);
    m_urlCombo->setEditable(true);
    m_urlCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_urlCombo->lineEdit()->setPlaceholderText(
        tr("https://example.org/ows?SERVICE=WMS"));
    urlRow->addWidget(m_urlCombo, 1);
    auto *connectBtn = new QPushButton(tr("Connect"), this);
    urlRow->addWidget(connectBtn);
    vlay->addLayout(urlRow);

    // Service title
    m_serviceTitle = new QLabel(this);
    m_serviceTitle->setWordWrap(true);
    vlay->addWidget(m_serviceTitle);

    // Splitter: layer tree | options
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    m_layerTree = new QTreeWidget(this);
    m_layerTree->setHeaderLabel(tr("Available Layers"));
    m_layerTree->setRootIsDecorated(true);
    splitter->addWidget(m_layerTree);

    // Options panel
    auto *optWidget = new QWidget(this);
    auto *optVlay   = new QVBoxLayout(optWidget);
    optVlay->setContentsMargins(4, 0, 0, 0);

    optVlay->addWidget(new QLabel(tr("Style:"), optWidget));
    m_styleCombo = new QComboBox(optWidget);
    optVlay->addWidget(m_styleCombo);

    optVlay->addWidget(new QLabel(tr("Image format:"), optWidget));
    m_formatCombo = new QComboBox(optWidget);
    optVlay->addWidget(m_formatCombo);

    optVlay->addWidget(new QLabel(tr("CRS:"), optWidget));
    m_crsCombo = new QComboBox(optWidget);
    optVlay->addWidget(m_crsCombo);

    optVlay->addStretch();
    splitter->addWidget(optWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    vlay->addWidget(splitter, 1);

    // Buttons
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    vlay->addWidget(m_buttonBox);

    connect(connectBtn, &QPushButton::clicked, this, &WMSConnectionDialog::onConnectClicked);
    connect(m_urlCombo, &QComboBox::currentTextChanged,
            this, &WMSConnectionDialog::onUrlComboChanged);
    connect(m_layerTree, &QTreeWidget::itemSelectionChanged,
            this, &WMSConnectionDialog::onLayerSelectionChanged);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void WMSConnectionDialog::onConnectClicked()
{
    const QString urlText = m_urlCombo->currentText().trimmed();
    if (urlText.isEmpty())
        return;

    // Ensure SERVICE/VERSION are not duplicated
    QUrl base(urlText);
    QUrlQuery q(base.query());
    q.removeAllQueryItems(QStringLiteral("SERVICE"));
    q.removeAllQueryItems(QStringLiteral("REQUEST"));
    base.setQuery(q);

    // Create/replace a temporary WMSLayer just for fetching capabilities
    delete m_pendingLayer;
    m_pendingLayer = new WMSLayer(base, nullptr);
    m_pendingLayer->setParent(this);

    connect(m_pendingLayer, &WMSLayer::capabilitiesFetched,
            this, &WMSConnectionDialog::onCapabilitiesFetched);
    connect(m_pendingLayer, &WMSLayer::capabilitiesError,
            this, &WMSConnectionDialog::onCapabilitiesError);

    m_serviceTitle->setText(tr("Connecting to %1 …").arg(urlText));
    m_layerTree->clear();
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    m_pendingLayer->fetchCapabilities();
    saveRecentUrls();
}

void WMSConnectionDialog::onCapabilitiesFetched(const WMSServiceInfo &info)
{
    delete m_serviceInfo;
    m_serviceInfo = new WMSServiceInfo(info);

    m_serviceTitle->setText(info.title.isEmpty()
                            ? tr("WMS service (version %1)").arg(info.version)
                            : QStringLiteral("%1  [%2]").arg(info.title, info.version));

    m_formatCombo->clear();
    for (const QString &fmt : info.imageFormats)
        m_formatCombo->addItem(fmt);

    populateLayerTree(info);
}

void WMSConnectionDialog::onCapabilitiesError(const QString &error)
{
    m_serviceTitle->setText(tr("Error: %1").arg(error));
    QMessageBox::warning(this, tr("WMS Connection Error"), error);
}

void WMSConnectionDialog::onLayerSelectionChanged()
{
    const QList<QTreeWidgetItem *> sel = m_layerTree->selectedItems();
    if (sel.isEmpty())
    {
        m_selectedLayerName.clear();
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }

    const QString layerName = sel.first()->data(0, Qt::UserRole).toString();
    m_selectedLayerName = layerName;
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!layerName.isEmpty());

    // Populate styles and CRS for the selected layer
    if (m_serviceInfo)
    {
        for (const WMSLayerInfo &info : m_serviceInfo->layers)
        {
            if (info.name == layerName)
            {
                m_styleCombo->clear();
                m_styleCombo->addItems(info.styles.isEmpty()
                                       ? QStringList{QString()}
                                       : info.styles);

                m_crsCombo->clear();
                m_crsCombo->addItems(info.crsIdentifiers);
                break;
            }
        }
    }
}

void WMSConnectionDialog::onUrlComboChanged(const QString & /*text*/)
{
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_layerTree->clear();
    m_serviceTitle->clear();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void WMSConnectionDialog::populateLayerTree(const WMSServiceInfo &info)
{
    m_layerTree->clear();

    for (const WMSLayerInfo &lyrInfo : info.layers)
    {
        auto *item = new QTreeWidgetItem(m_layerTree);
        item->setText(0, lyrInfo.title.isEmpty() ? lyrInfo.name : lyrInfo.title);
        item->setToolTip(0, lyrInfo.abstractText);
        item->setData(0, Qt::UserRole, lyrInfo.name);
    }
}

void WMSConnectionDialog::saveRecentUrls()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("WMSConnectionDialog"));

    QStringList urls;
    for (int i = 0; i < m_urlCombo->count(); ++i)
        urls << m_urlCombo->itemText(i);

    const QString currentUrl = m_urlCombo->currentText().trimmed();
    urls.removeAll(currentUrl);
    urls.prepend(currentUrl);
    urls = urls.mid(0, 15);  // keep at most 15 recent URLs

    settings.setValue(QStringLiteral("recentUrls"), urls);
    settings.endGroup();
}

void WMSConnectionDialog::loadRecentUrls()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("WMSConnectionDialog"));
    QStringList urls = settings.value(QStringLiteral("recentUrls")).toStringList();
    settings.endGroup();

    if (urls.isEmpty())
        urls << QStringLiteral("https://ows.terrestris.de/osm/service");

    m_urlCombo->clear();
    m_urlCombo->addItems(urls);
}
