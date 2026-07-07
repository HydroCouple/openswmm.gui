/*!
 * \file   addbasemapdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "ui/dialogs/addbasemapdialog.h"
#include "ui/widgets/basemaphttpheaderswidget.h"
#include "connections/basemapconnection.h"
#include "connections/basemapconnectionstore.h"
#include "project/openswmmvisworkspace.h"
#include "layers/wcslayer.h"
#include "layers/wmslayer.h"
#include "layers/wmtslayer.h"
#include "layers/xyztilelayer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool urlIsWMTS(const QString &url)
{
    const QString lower = url.toLower();
    return lower.contains("service=wmts") || lower.contains("/wmtscapabilities.xml");
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AddBasemapDialog::AddBasemapDialog(QWidget *parent)
    : QDialog(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_wmsInfo(nullptr)
    , m_wmtsInfo(nullptr)
    , m_wcsInfo(nullptr)
{
    setWindowTitle(tr("Add Basemap"));
    resize(740, 580);

    auto *root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);

    auto *xyzPage    = new QWidget;
    auto *wmsPage    = new QWidget;
    auto *wcsPage    = new QWidget;
    auto *arcPage    = new QWidget;

    setupUiXYZ(xyzPage);
    setupUiWMS(wmsPage);
    setupUiWCS(wcsPage);
    setupUiArcGIS(arcPage);

    m_tabs->addTab(xyzPage,    tr("XYZ Tiles"));
    m_tabs->addTab(wmsPage,    tr("WMS / WMTS"));
    m_tabs->addTab(wcsPage,    tr("WCS"));
    m_tabs->addTab(arcPage,    tr("ArcGIS REST"));

    root->addWidget(m_tabs);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshXYZCombo();
    refreshWMSCombo();
    refreshWCSCombo();
    refreshArcGISCombo();
}

void AddBasemapDialog::setInitialTab(int index)
{
    if (m_tabs)
        m_tabs->setCurrentIndex(index);
}

AddBasemapDialog::~AddBasemapDialog()
{
    // m_wmsInfo / m_wmtsInfo are unique_ptr — released automatically.
    delete m_wcsInfo;
}

// ---------------------------------------------------------------------------
// UI setup helpers
// ---------------------------------------------------------------------------

void AddBasemapDialog::buildConnectionBar(QWidget *parent, QComboBox *&combo,
                                           QPushButton *&newBtn, QPushButton *&editBtn,
                                           QPushButton *&delBtn)
{
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Saved connections:"), parent));
    combo  = new QComboBox(parent);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    row->addWidget(combo);
    newBtn  = new QPushButton(tr("New"),    parent);
    editBtn = new QPushButton(tr("Edit"),   parent);
    delBtn  = new QPushButton(tr("Delete"), parent);
    row->addWidget(newBtn);
    row->addWidget(editBtn);
    row->addWidget(delBtn);
    qobject_cast<QVBoxLayout *>(parent->layout())->addLayout(row);
}

void AddBasemapDialog::buildAuthGroup(QWidget *parent, QGroupBox *&box,
                                       QLineEdit *&user, QLineEdit *&pass,
                                       QPushButton *&eyeBtn)
{
    box = new QGroupBox(tr("Authentication (Basic)"), parent);
    box->setCheckable(true);
    box->setChecked(false);
    auto *gl = new QFormLayout(box);
    user = new QLineEdit(box);
    pass = new QLineEdit(box);
    pass->setEchoMode(QLineEdit::Password);
    eyeBtn = new QPushButton(tr("Show"), box);
    eyeBtn->setCheckable(true);
    eyeBtn->setMaximumWidth(60);
    auto *passRow = new QHBoxLayout;
    passRow->addWidget(pass);
    passRow->addWidget(eyeBtn);
    gl->addRow(tr("Username:"), user);
    gl->addRow(tr("Password:"), passRow);
    qobject_cast<QVBoxLayout *>(parent->layout())->addWidget(box);
}

void AddBasemapDialog::togglePasswordVisibility(QLineEdit *passEdit, QPushButton *eyeBtn)
{
    passEdit->setEchoMode(eyeBtn->isChecked() ? QLineEdit::Normal : QLineEdit::Password);
    eyeBtn->setText(eyeBtn->isChecked() ? tr("Hide") : tr("Show"));
}

// ---------------------------------------------------------------------------
// Tab 1 — XYZ
// ---------------------------------------------------------------------------

void AddBasemapDialog::setupUiXYZ(QWidget *page)
{
    auto *vlay = new QVBoxLayout(page);
    vlay->setSpacing(6);

    buildConnectionBar(page, m_xyzCombo, m_xyzNew, m_xyzEdit, m_xyzDel);
    vlay->addWidget(new QLabel(tr("───────────────────────────────────────"), page));

    auto *form = new QFormLayout;
    m_xyzName = new QLineEdit(page);
    m_xyzUrl  = new QLineEdit(page);
    m_xyzUrl->setPlaceholderText("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png");

    auto *zoomRow = new QHBoxLayout;
    m_xyzZMin = new QSpinBox(page); m_xyzZMin->setRange(0, 30); m_xyzZMin->setValue(0);
    m_xyzZMax = new QSpinBox(page); m_xyzZMax->setRange(0, 30); m_xyzZMax->setValue(19);
    zoomRow->addWidget(new QLabel(tr("Min:"), page)); zoomRow->addWidget(m_xyzZMin);
    zoomRow->addSpacing(12);
    zoomRow->addWidget(new QLabel(tr("Max:"), page)); zoomRow->addWidget(m_xyzZMax);
    zoomRow->addStretch();

    m_xyzPixRatio = new QComboBox(page);
    m_xyzPixRatio->addItem(tr("Undefined"),           0);
    m_xyzPixRatio->addItem(tr("Standard 96 DPI"),     1);
    m_xyzPixRatio->addItem(tr("HiDPI 192 DPI"),       2);

    m_xyzAxis = new QComboBox(page);
    m_xyzAxis->addItem(tr("ZXY  (standard OSM)"),     static_cast<int>(TileAxisOrder::ZXY));
    m_xyzAxis->addItem(tr("ZYX  (ArcGIS REST)"),      static_cast<int>(TileAxisOrder::ZYX));

    form->addRow(tr("Name:"),             m_xyzName);
    form->addRow(tr("URL template:"),     m_xyzUrl);
    form->addRow(tr("Zoom range:"),       zoomRow);
    form->addRow(tr("Tile pixel ratio:"), m_xyzPixRatio);
    form->addRow(tr("Axis order:"),       m_xyzAxis);
    vlay->addLayout(form);

    buildAuthGroup(page, m_xyzAuthBox, m_xyzUser, m_xyzPass, m_xyzEye);

    m_xyzHeaders = new BasemapHttpHeadersWidget(page);
    vlay->addWidget(m_xyzHeaders);

    auto *testRow = new QHBoxLayout;
    m_xyzTestBtn   = new QPushButton(tr("Test Connection"), page);
    m_xyzTestLabel = new QLabel(page);
    testRow->addWidget(m_xyzTestBtn);
    testRow->addWidget(m_xyzTestLabel);
    testRow->addStretch();
    vlay->addLayout(testRow);
    vlay->addStretch();

    // Wiring
    connect(m_xyzCombo,   &QComboBox::currentTextChanged,
            this, &AddBasemapDialog::onXYZConnectionSelected);
    connect(m_xyzNew,     &QPushButton::clicked, this, &AddBasemapDialog::onXYZNew);
    connect(m_xyzEdit,    &QPushButton::clicked, this, &AddBasemapDialog::onXYZEdit);
    connect(m_xyzDel,     &QPushButton::clicked, this, &AddBasemapDialog::onXYZDelete);
    connect(m_xyzTestBtn, &QPushButton::clicked, this, &AddBasemapDialog::onXYZTest);
    connect(m_xyzEye,     &QPushButton::toggled, this, [this](bool) {
        togglePasswordVisibility(m_xyzPass, m_xyzEye);
    });
}

// ---------------------------------------------------------------------------
// Tab 2 — WMS / WMTS
// ---------------------------------------------------------------------------

void AddBasemapDialog::setupUiWMS(QWidget *page)
{
    auto *vlay = new QVBoxLayout(page);
    vlay->setSpacing(6);

    buildConnectionBar(page, m_wmsCombo, m_wmsNew, m_wmsEdit, m_wmsDel);

    // URL + Connect row
    auto *urlRow = new QHBoxLayout;
    m_wmsUrl = new QLineEdit(page);
    m_wmsUrl->setPlaceholderText("https://...");
    m_wmsConnect = new QPushButton(tr("Connect"), page);
    urlRow->addWidget(new QLabel(tr("URL:"), page));
    urlRow->addWidget(m_wmsUrl);
    urlRow->addWidget(m_wmsConnect);
    vlay->addLayout(urlRow);

    m_wmsProtocol = new QLabel(page);
    vlay->addWidget(m_wmsProtocol);

    buildAuthGroup(page, m_wmsAuthBox, m_wmsUser, m_wmsPass, m_wmsEye);

    // Layer tree + options
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    m_wmsTree = new QTreeWidget(splitter);
    m_wmsTree->setHeaderLabel(tr("Available Layers"));

    auto *optWidget = new QWidget(splitter);
    auto *optForm   = new QFormLayout(optWidget);
    m_wmsStyle   = new QComboBox(optWidget);
    m_wmsFmt     = new QComboBox(optWidget);
    m_wmsCrs     = new QComboBox(optWidget);
    m_wmsTmsLabel = new QLabel(tr("Tile matrix set:"), optWidget);
    m_wmsTms      = new QComboBox(optWidget);
    optForm->addRow(tr("Style:"),          m_wmsStyle);
    optForm->addRow(tr("Format:"),         m_wmsFmt);
    optForm->addRow(tr("CRS:"),            m_wmsCrs);
    optForm->addRow(m_wmsTmsLabel,         m_wmsTms);
    splitter->addWidget(m_wmsTree);
    splitter->addWidget(optWidget);
    vlay->addWidget(splitter);

    // Advanced options
    auto *advBox = new QGroupBox(tr("Advanced"), page);
    advBox->setCheckable(true);
    advBox->setChecked(false);
    auto *advForm = new QFormLayout(advBox);
    m_wmsDpiMode   = new QComboBox(advBox);
    m_wmsDpiMode->addItem(tr("All"),        7);
    m_wmsDpiMode->addItem(tr("None"),       0);
    m_wmsDpiMode->addItem(tr("QGIS"),       1);
    m_wmsDpiMode->addItem(tr("UMN MapServer"), 2);
    m_wmsDpiMode->addItem(tr("GeoServer"),  4);
    m_wmsRatio     = new QComboBox(advBox);
    m_wmsRatio->addItem(tr("Undefined"),        0);
    m_wmsRatio->addItem(tr("Standard 96 DPI"),  1);
    m_wmsRatio->addItem(tr("HiDPI 192 DPI"),    2);
    m_wmsIgnoreUri  = new QCheckBox(tr("Ignore GetMap URI"),          advBox);
    m_wmsIgnoreAxis = new QCheckBox(tr("Ignore axis orientation"),    advBox);
    m_wmsInvertAxis = new QCheckBox(tr("Invert axis orientation"),    advBox);
    m_wmsSmooth     = new QCheckBox(tr("Smooth pixmap transform"),    advBox);
    m_wmsSmooth->setChecked(true);
    advForm->addRow(tr("DPI mode:"),        m_wmsDpiMode);
    advForm->addRow(tr("Tile pixel ratio:"),m_wmsRatio);
    advForm->addRow(m_wmsIgnoreUri);
    advForm->addRow(m_wmsIgnoreAxis);
    advForm->addRow(m_wmsInvertAxis);
    advForm->addRow(m_wmsSmooth);
    vlay->addWidget(advBox);

    m_wmsHeaders = new BasemapHttpHeadersWidget(page);
    vlay->addWidget(m_wmsHeaders);

    m_wmsStatus = new QLabel(page);
    vlay->addWidget(m_wmsStatus);

    // Wiring
    connect(m_wmsCombo,    &QComboBox::currentTextChanged,
            this, &AddBasemapDialog::onWMSConnectionSelected);
    connect(m_wmsNew,      &QPushButton::clicked, this, &AddBasemapDialog::onWMSNew);
    connect(m_wmsEdit,     &QPushButton::clicked, this, &AddBasemapDialog::onWMSEdit);
    connect(m_wmsDel,      &QPushButton::clicked, this, &AddBasemapDialog::onWMSDelete);
    connect(m_wmsConnect,  &QPushButton::clicked, this, &AddBasemapDialog::onWMSConnect);
    connect(m_wmsTree,     &QTreeWidget::itemSelectionChanged,
            this, &AddBasemapDialog::onWMSLayerSelectionChanged);
    connect(m_wmsEye,      &QPushButton::toggled, this, [this](bool) {
        togglePasswordVisibility(m_wmsPass, m_wmsEye);
    });
    connect(m_wmsUrl,      &QLineEdit::textChanged, this, [this](const QString &t) {
        const bool wmts = urlIsWMTS(t);
        m_wmsProtocol->setText(wmts ? tr("Protocol: WMTS (auto-detected)")
                                    : tr("Protocol: WMS (auto-detected)"));
        m_wmsTms->setVisible(wmts);
        m_wmsTmsLabel->setVisible(wmts);
    });
    // Start with the labels hidden
    m_wmsTms->setVisible(false);
    m_wmsTmsLabel->setVisible(false);
    m_wmsProtocol->setText(tr("Protocol: WMS (auto-detected)"));
}

// ---------------------------------------------------------------------------
// Tab 3 — WCS
// ---------------------------------------------------------------------------

void AddBasemapDialog::setupUiWCS(QWidget *page)
{
    auto *vlay = new QVBoxLayout(page);
    vlay->setSpacing(6);

    // Saved-connections bar (New + Delete; no Edit — same pattern as ArcGIS)
    auto *connRow = new QHBoxLayout;
    connRow->addWidget(new QLabel(tr("Saved connections:"), page));
    m_wcsCombo = new QComboBox(page);
    m_wcsCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connRow->addWidget(m_wcsCombo);
    m_wcsNew = new QPushButton(tr("New"),    page);
    m_wcsDel = new QPushButton(tr("Delete"), page);
    connRow->addWidget(m_wcsNew);
    connRow->addWidget(m_wcsDel);
    vlay->addLayout(connRow);

    // URL + Connect
    auto *urlRow = new QHBoxLayout;
    m_wcsUrl = new QLineEdit(page);
    m_wcsUrl->setPlaceholderText(QStringLiteral("https://example.org/wcs"));
    m_wcsConnect = new QPushButton(tr("Connect"), page);
    urlRow->addWidget(new QLabel(tr("URL:"), page));
    urlRow->addWidget(m_wcsUrl);
    urlRow->addWidget(m_wcsConnect);
    vlay->addLayout(urlRow);

    m_wcsStatus = new QLabel(page);
    vlay->addWidget(m_wcsStatus);

    // Coverage tree + options panel
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    m_wcsCovTree = new QTreeWidget(splitter);
    m_wcsCovTree->setHeaderLabel(tr("Available Coverages"));

    auto *optWidget = new QWidget(splitter);
    auto *optForm   = new QFormLayout(optWidget);

    m_wcsFmt   = new QComboBox(optWidget);
    m_wcsCrs   = new QComboBox(optWidget);
    m_wcsRange = new QLineEdit(optWidget);
    m_wcsRange->setPlaceholderText(tr("e.g. band[1]  (leave blank for all)"));
    m_wcsInterp = new QComboBox(optWidget);
    m_wcsInterp->addItems({ tr("nearest"), tr("bilinear"), tr("bicubic") });

    optForm->addRow(tr("Format:"),        m_wcsFmt);
    optForm->addRow(tr("Output CRS:"),    m_wcsCrs);
    optForm->addRow(tr("Range subset:"),  m_wcsRange);
    optForm->addRow(tr("Interpolation:"), m_wcsInterp);

    splitter->addWidget(m_wcsCovTree);
    splitter->addWidget(optWidget);
    vlay->addWidget(splitter);

    buildAuthGroup(page, m_wcsAuthBox, m_wcsUser, m_wcsPass, m_wcsEye);

    m_wcsHeaders = new BasemapHttpHeadersWidget(page);
    vlay->addWidget(m_wcsHeaders);
    vlay->addStretch();

    // Wiring
    connect(m_wcsCombo,   &QComboBox::currentTextChanged,
            this, &AddBasemapDialog::onWCSConnectionSelected);
    connect(m_wcsNew,     &QPushButton::clicked, this, &AddBasemapDialog::onWCSNew);
    connect(m_wcsDel,     &QPushButton::clicked, this, &AddBasemapDialog::onWCSDelete);
    connect(m_wcsConnect, &QPushButton::clicked, this, &AddBasemapDialog::onWCSConnect);
    connect(m_wcsCovTree, &QTreeWidget::itemSelectionChanged,
            this, &AddBasemapDialog::onWCSCoverageSelectionChanged);
    connect(m_wcsEye, &QPushButton::toggled, this, [this](bool) {
        togglePasswordVisibility(m_wcsPass, m_wcsEye);
    });
}

// ---------------------------------------------------------------------------
// Tab 4 — ArcGIS REST
// ---------------------------------------------------------------------------

void AddBasemapDialog::setupUiArcGIS(QWidget *page)
{
    auto *vlay = new QVBoxLayout(page);
    vlay->setSpacing(6);

    buildConnectionBar(page, m_arcCombo, m_arcNew, m_arcEdit, m_arcDel);

    auto *form = new QFormLayout;
    m_arcUrl       = new QLineEdit(page);
    m_arcUrl->setPlaceholderText("https://server.arcgisonline.com/arcgis/rest/services/...");
    m_arcPrefix    = new QLineEdit(page);
    m_arcContent   = new QLineEdit(page);
    m_arcContent->setPlaceholderText(tr("…/sharing/rest/content  (Portal only)"));
    m_arcCommunity = new QLineEdit(page);
    m_arcCommunity->setPlaceholderText(tr("…/sharing/rest/community  (Portal only)"));
    form->addRow(tr("URL:"),                m_arcUrl);
    form->addRow(tr("URL Prefix:"),         m_arcPrefix);
    form->addRow(tr("Content endpoint:"),   m_arcContent);
    form->addRow(tr("Community endpoint:"), m_arcCommunity);
    vlay->addLayout(form);

    buildAuthGroup(page, m_arcAuthBox, m_arcUser, m_arcPass, m_arcEye);

    m_arcHeaders = new BasemapHttpHeadersWidget(page);
    vlay->addWidget(m_arcHeaders);

    m_arcConnect = new QPushButton(tr("Connect"), page);
    vlay->addWidget(m_arcConnect);

    m_arcInfoLabel = new QLabel(page);
    m_arcInfoLabel->setWordWrap(true);
    vlay->addWidget(m_arcInfoLabel);
    vlay->addStretch();

    // Wiring
    connect(m_arcCombo,    &QComboBox::currentTextChanged,
            this, &AddBasemapDialog::onArcGISConnectionSelected);
    connect(m_arcNew,      &QPushButton::clicked, this, &AddBasemapDialog::onArcGISNew);
    connect(m_arcEdit,     &QPushButton::clicked, this, &AddBasemapDialog::onArcGISEdit);
    connect(m_arcDel,      &QPushButton::clicked, this, &AddBasemapDialog::onArcGISDelete);
    connect(m_arcConnect,  &QPushButton::clicked, this, &AddBasemapDialog::onArcGISConnect);
    connect(m_arcEye,      &QPushButton::toggled, this, [this](bool) {
        togglePasswordVisibility(m_arcPass, m_arcEye);
    });
}

// ---------------------------------------------------------------------------
// Combo refresh helpers
// ---------------------------------------------------------------------------

void AddBasemapDialog::refreshXYZCombo()
{
    m_xyzCombo->blockSignals(true);
    m_xyzCombo->clear();
    m_xyzCombo->addItem(tr("— New connection —"));
    for (const XYZConnection &b : XYZConnection::builtins())
        m_xyzCombo->addItem(b.name);
    const QStringList saved = BasemapConnectionStore::instance()->xyzConnectionNames();
    for (const QString &n : saved)
        m_xyzCombo->addItem(n);
    m_xyzCombo->blockSignals(false);
}

void AddBasemapDialog::refreshWMSCombo()
{
    m_wmsCombo->blockSignals(true);
    m_wmsCombo->clear();
    m_wmsCombo->addItem(tr("— New connection —"));
    const QStringList saved = BasemapConnectionStore::instance()->wmsConnectionNames();
    for (const QString &n : saved)
        m_wmsCombo->addItem(n);
    m_wmsCombo->blockSignals(false);
}

void AddBasemapDialog::refreshWCSCombo()
{
    m_wcsCombo->blockSignals(true);
    m_wcsCombo->clear();
    m_wcsCombo->addItem(tr("— New connection —"));
    const QStringList saved = BasemapConnectionStore::instance()->wcsConnectionNames();
    for (const QString &n : saved)
        m_wcsCombo->addItem(n);
    m_wcsCombo->blockSignals(false);
}

void AddBasemapDialog::refreshArcGISCombo()
{
    m_arcCombo->blockSignals(true);
    m_arcCombo->clear();
    m_arcCombo->addItem(tr("— New connection —"));
    const QStringList saved = BasemapConnectionStore::instance()->arcGISConnectionNames();
    for (const QString &n : saved)
        m_arcCombo->addItem(n);
    m_arcCombo->blockSignals(false);
}

// ---------------------------------------------------------------------------
// Populate helpers
// ---------------------------------------------------------------------------

void AddBasemapDialog::populateXYZ(const XYZConnection &conn, const BasemapAuth &auth)
{
    m_xyzName->setText(conn.name);
    m_xyzUrl->setText(conn.urlTemplate);
    m_xyzZMin->setValue(conn.zMin);
    m_xyzZMax->setValue(conn.zMax);
    m_xyzPixRatio->setCurrentIndex(m_xyzPixRatio->findData(conn.tilePixelRatio));
    m_xyzAxis->setCurrentIndex(m_xyzAxis->findData(static_cast<int>(conn.axisOrder)));
    m_xyzHeaders->setHeaders(conn.httpHeaders);
    m_xyzUser->setText(auth.username);
    m_xyzPass->setText(auth.password);
    m_xyzAuthBox->setChecked(!auth.isEmpty());
}

void AddBasemapDialog::populateWMS(const WMSConnection &conn, const BasemapAuth &auth)
{
    m_wmsUrl->setText(conn.url);
    m_wmsUser->setText(auth.username);
    m_wmsPass->setText(auth.password);
    m_wmsAuthBox->setChecked(!auth.isEmpty());
    m_wmsDpiMode->setCurrentIndex(m_wmsDpiMode->findData(conn.dpiMode));
    m_wmsRatio->setCurrentIndex(m_wmsRatio->findData(conn.tilePixelRatio));
    m_wmsIgnoreUri->setChecked(conn.ignoreGetMapURI);
    m_wmsIgnoreAxis->setChecked(conn.ignoreAxisOrientation);
    m_wmsInvertAxis->setChecked(conn.invertAxisOrientation);
    m_wmsSmooth->setChecked(conn.smoothPixmapTransform);
    m_wmsHeaders->setHeaders(conn.httpHeaders);
}

void AddBasemapDialog::populateWCS(const WCSConnection &conn, const BasemapAuth &auth)
{
    m_wcsUrl->setText(conn.url);
    m_wcsRange->setText(conn.rangeSubset);
    m_wcsUser->setText(auth.username);
    m_wcsPass->setText(auth.password);
    m_wcsAuthBox->setChecked(!auth.isEmpty());
    // CRS / format / interpolation combos may not yet contain the saved value;
    // insert it if missing so round-trips always work.
    if (m_wcsCrs->findText(conn.outputCrs) < 0 && !conn.outputCrs.isEmpty())
        m_wcsCrs->addItem(conn.outputCrs);
    m_wcsCrs->setCurrentText(conn.outputCrs);
    if (m_wcsFmt->findText(conn.outputFormat) < 0 && !conn.outputFormat.isEmpty())
        m_wcsFmt->addItem(conn.outputFormat);
    m_wcsFmt->setCurrentText(conn.outputFormat);
    const int interpIdx = m_wcsInterp->findText(conn.interpolation);
    if (interpIdx >= 0) m_wcsInterp->setCurrentIndex(interpIdx);
}

void AddBasemapDialog::populateWCSTree(const WCSServiceInfo &info)
{
    m_wcsCovTree->clear();
    m_wcsFmt->clear();
    for (const WCSCoverageInfo &ci : info.coverages) {
        auto *item = new QTreeWidgetItem(m_wcsCovTree);
        item->setText(0, ci.title.isEmpty() ? ci.identifier : ci.title);
        item->setData(0, Qt::UserRole, ci.identifier);
    }
}

void AddBasemapDialog::populateArcGIS(const ArcGISRestConnection &conn,
                                       const BasemapAuth          &auth)
{
    m_arcUrl->setText(conn.url);
    m_arcPrefix->setText(conn.urlPrefix);
    m_arcContent->setText(conn.contentEndpoint);
    m_arcCommunity->setText(conn.communityEndpoint);
    m_arcUser->setText(auth.username);
    m_arcPass->setText(auth.password);
    m_arcAuthBox->setChecked(!auth.isEmpty());
    m_arcHeaders->setHeaders(conn.httpHeaders);
}

// ---------------------------------------------------------------------------
// XYZ slots
// ---------------------------------------------------------------------------

void AddBasemapDialog::onXYZConnectionSelected(const QString &name)
{
    if (name.isEmpty() || name.startsWith("—")) {
        m_xyzName->clear(); m_xyzUrl->clear(); m_xyzHeaders->clear(); return;
    }
    // Check builtins first
    for (const XYZConnection &b : XYZConnection::builtins()) {
        if (b.name == name) { populateXYZ(b, {}); return; }
    }
    const XYZConnection conn = BasemapConnectionStore::instance()->loadXYZ(name);
    const BasemapAuth   auth = BasemapConnectionStore::instance()->loadXYZAuth(name);
    populateXYZ(conn, auth);
}

void AddBasemapDialog::onXYZNew()
{
    m_xyzCombo->setCurrentIndex(0);
    m_xyzName->clear(); m_xyzUrl->clear();
    m_xyzZMin->setValue(0); m_xyzZMax->setValue(19);
    m_xyzPixRatio->setCurrentIndex(0);
    m_xyzAxis->setCurrentIndex(0);
    m_xyzHeaders->clear();
    m_xyzUser->clear(); m_xyzPass->clear();
    m_xyzAuthBox->setChecked(false);
}

void AddBasemapDialog::onXYZEdit()
{
    // Fields already populated by onXYZConnectionSelected — nothing extra to do.
}

void AddBasemapDialog::onXYZDelete()
{
    const QString name = m_xyzCombo->currentText();
    if (name.isEmpty() || name.startsWith("—")) return;
    // Prevent deletion of builtins
    for (const XYZConnection &b : XYZConnection::builtins())
        if (b.name == name) return;

    if (QMessageBox::question(this, tr("Delete Connection"),
            tr("Delete connection \"%1\"?").arg(name)) != QMessageBox::Yes) return;
    BasemapConnectionStore::instance()->removeXYZ(name);
    refreshXYZCombo();
}

void AddBasemapDialog::onXYZTest()
{
    const QString url = m_xyzUrl->text().trimmed();
    if (url.isEmpty()) { m_xyzTestLabel->setText(tr("Enter a URL first.")); return; }

    QString testUrl = url;
    testUrl.replace("{s}", "a").replace("{z}", "0").replace("{x}", "0").replace("{y}", "0");
    QNetworkRequest req{QUrl(testUrl)};

    const BasemapHttpHeaders hdrs = m_xyzHeaders->headers();
    for (auto it = hdrs.cbegin(); it != hdrs.cend(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());

    if (m_xyzAuthBox->isChecked() && !m_xyzUser->text().isEmpty()) {
        const QByteArray cred = QByteArray((m_xyzUser->text() + ':' + m_xyzPass->text()).toUtf8()).toBase64();
        req.setRawHeader("Authorization", "Basic " + cred);
    }

    m_xyzTestLabel->setText(tr("Testing…"));
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError)
            m_xyzTestLabel->setText(tr("OK (%1 bytes)").arg(reply->bytesAvailable()));
        else
            m_xyzTestLabel->setText(tr("Error: %1").arg(reply->errorString()));
        reply->deleteLater();
    });
}

// ---------------------------------------------------------------------------
// WMS / WMTS slots
// ---------------------------------------------------------------------------

void AddBasemapDialog::onWMSConnectionSelected(const QString &name)
{
    if (name.isEmpty() || name.startsWith("—")) return;
    const WMSConnection conn = BasemapConnectionStore::instance()->loadWMS(name);
    const BasemapAuth   auth = BasemapConnectionStore::instance()->loadWMSAuth(name);
    populateWMS(conn, auth);
}

void AddBasemapDialog::onWMSNew()
{
    m_wmsCombo->setCurrentIndex(0);
    m_wmsUrl->clear(); m_wmsUser->clear(); m_wmsPass->clear();
    m_wmsAuthBox->setChecked(false); m_wmsHeaders->clear();
    m_wmsTree->clear(); m_wmsStyle->clear(); m_wmsFmt->clear();
    m_wmsCrs->clear(); m_wmsTms->clear();
}

void AddBasemapDialog::onWMSEdit() {}

void AddBasemapDialog::onWMSDelete()
{
    const QString name = m_wmsCombo->currentText();
    if (name.isEmpty() || name.startsWith("—")) return;
    if (QMessageBox::question(this, tr("Delete Connection"),
            tr("Delete connection \"%1\"?").arg(name)) != QMessageBox::Yes) return;
    BasemapConnectionStore::instance()->removeWMS(name);
    refreshWMSCombo();
}

void AddBasemapDialog::onWMSConnect()
{
    const QString urlText = m_wmsUrl->text().trimmed();
    if (urlText.isEmpty()) return;

    m_isWMTS = urlIsWMTS(urlText);
    m_wmsStatus->setText(tr("Connecting…"));

    if (m_isWMTS) {
        // Create a temporary WMTSLayer to fetch capabilities
        auto *layer = new WMTSLayer(QUrl(urlText), nullptr);
        connect(layer, &WMTSLayer::capabilitiesFetched, this,
                &AddBasemapDialog::onWMSCapabilitiesFetched);
        connect(layer, &WMTSLayer::capabilitiesError, this,
                &AddBasemapDialog::onWMSCapabilitiesError);
        // On error the temporary layer self-destructs; on success it is torn
        // down inside the handler below, only after its serviceInfo() is read.
        connect(layer, &WMTSLayer::capabilitiesError,   layer, &QObject::deleteLater);
        m_wmtsInfo.reset();
        connect(layer, &WMTSLayer::capabilitiesFetched, this, [this, layer]() {
            m_wmtsInfo = std::make_unique<WMTSServiceInfo>(layer->serviceInfo());
            populateWMTSTree(*m_wmtsInfo);
            layer->deleteLater();   // done reading from layer; schedule teardown
        });
        layer->fetchCapabilities();
    } else {
        auto *layer = new WMSLayer(QUrl(urlText), nullptr);
        connect(layer, &WMSLayer::capabilitiesFetched, this,
                &AddBasemapDialog::onWMSCapabilitiesFetched);
        connect(layer, &WMSLayer::capabilitiesError,  this,
                &AddBasemapDialog::onWMSCapabilitiesError);
        // On error the temporary layer self-destructs; on success it is torn
        // down inside the handler below, only after its serviceInfo() is read.
        connect(layer, &WMSLayer::capabilitiesError,   layer, &QObject::deleteLater);
        m_wmsInfo.reset();
        connect(layer, &WMSLayer::capabilitiesFetched, this, [this, layer]() {
            m_wmsInfo = std::make_unique<WMSServiceInfo>(layer->serviceInfo());
            populateWMSTree(*m_wmsInfo);
            layer->deleteLater();   // done reading from layer; schedule teardown
        });
        layer->fetchCapabilities();
    }
}

void AddBasemapDialog::onWMSCapabilitiesFetched()
{
    m_wmsStatus->setText(tr("Connected."));
}

void AddBasemapDialog::onWMSCapabilitiesError(const QString &error)
{
    m_wmsStatus->setText(tr("Error: %1").arg(error));
}

void AddBasemapDialog::populateWMSTree(const WMSServiceInfo &info)
{
    m_wmsTree->clear();
    m_wmsFmt->clear();
    for (const WMSLayerInfo &l : info.layers) {
        auto *item = new QTreeWidgetItem(m_wmsTree);
        item->setText(0, l.title.isEmpty() ? l.name : l.title);
        item->setData(0, Qt::UserRole, l.name);
    }
    for (const QString &fmt : info.imageFormats)
        m_wmsFmt->addItem(fmt);
}

void AddBasemapDialog::populateWMTSTree(const WMTSServiceInfo &info)
{
    m_wmsTree->clear();
    m_wmsTms->clear();
    for (const WMTSLayerInfo &l : info.layers) {
        auto *item = new QTreeWidgetItem(m_wmsTree);
        item->setText(0, l.title.isEmpty() ? l.identifier : l.title);
        item->setData(0, Qt::UserRole, l.identifier);
    }
    for (const WMTSTileMatrixSet &tms : info.tileMatrixSets)
        m_wmsTms->addItem(tms.identifier);
}

void AddBasemapDialog::onWMSLayerSelectionChanged()
{
    const QList<QTreeWidgetItem *> sel = m_wmsTree->selectedItems();
    if (sel.isEmpty()) return;
    if (!m_isWMTS && m_wmsInfo) {
        const QString layerName = sel.first()->data(0, Qt::UserRole).toString();
        m_wmsStyle->clear();
        m_wmsCrs->clear();
        for (const WMSLayerInfo &l : m_wmsInfo->layers) {
            if (l.name == layerName) {
                for (const QString &s : l.styles)     m_wmsStyle->addItem(s);
                for (const QString &c : l.crsIdentifiers) m_wmsCrs->addItem(c);
                break;
            }
        }
    } else if (m_isWMTS && m_wmtsInfo) {
        const QString layerId = sel.first()->data(0, Qt::UserRole).toString();
        m_wmsStyle->clear();
        m_wmsFmt->clear();
        for (const WMTSLayerInfo &l : m_wmtsInfo->layers) {
            if (l.identifier == layerId) {
                for (const QString &s : l.styles)  m_wmsStyle->addItem(s);
                for (const QString &f : l.formats) m_wmsFmt->addItem(f);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// WCS slots
// ---------------------------------------------------------------------------

void AddBasemapDialog::onWCSConnectionSelected(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QStringLiteral("—"))) return;
    const WCSConnection conn = BasemapConnectionStore::instance()->loadWCS(name);
    const BasemapAuth   auth = BasemapConnectionStore::instance()->loadWCSAuth(name);
    populateWCS(conn, auth);
}

void AddBasemapDialog::onWCSNew()
{
    m_wcsCombo->setCurrentIndex(0);
    m_wcsUrl->clear();
    m_wcsCovTree->clear();
    m_wcsFmt->clear();
    m_wcsCrs->clear();
    m_wcsRange->clear();
    m_wcsInterp->setCurrentIndex(0);
    m_wcsUser->clear();
    m_wcsPass->clear();
    m_wcsAuthBox->setChecked(false);
    m_wcsHeaders->clear();
    m_wcsStatus->clear();
    delete m_wcsInfo;
    m_wcsInfo = nullptr;
}

void AddBasemapDialog::onWCSDelete()
{
    const QString name = m_wcsCombo->currentText();
    if (name.isEmpty() || name.startsWith(QStringLiteral("—"))) return;
    if (QMessageBox::question(this, tr("Delete Connection"),
            tr("Delete connection \"%1\"?").arg(name)) != QMessageBox::Yes) return;
    BasemapConnectionStore::instance()->removeWCS(name);
    refreshWCSCombo();
}

void AddBasemapDialog::onWCSConnect()
{
    const QString urlText = m_wcsUrl->text().trimmed();
    if (urlText.isEmpty()) return;

    m_wcsStatus->setText(tr("Connecting…"));

    auto *layer = new WCSLayer(QUrl(urlText), nullptr);

    if (m_wcsAuthBox->isChecked() && !m_wcsUser->text().isEmpty())
        layer->setBasicAuth(m_wcsUser->text(), m_wcsPass->text());
    layer->setHttpHeaders(m_wcsHeaders->headers());

    connect(layer, &WCSLayer::capabilitiesFetched, this,
            &AddBasemapDialog::onWCSCapabilitiesFetched);
    connect(layer, &WCSLayer::capabilitiesError, this,
            &AddBasemapDialog::onWCSCapabilitiesError);
    connect(layer, &WCSLayer::capabilitiesFetched, layer, &QObject::deleteLater);
    connect(layer, &WCSLayer::capabilitiesError,   layer, &QObject::deleteLater);

    connect(layer, &WCSLayer::capabilitiesFetched, this,
            [this, layer](const WCSServiceInfo &) {
                delete m_wcsInfo;
                m_wcsInfo = new WCSServiceInfo(layer->serviceInfo());
                populateWCSTree(*m_wcsInfo);
            });

    layer->fetchCapabilities();
}

void AddBasemapDialog::onWCSCapabilitiesFetched(const WCSServiceInfo &info)
{
    m_wcsStatus->setText(tr("Connected — %1 coverage(s) found.")
                             .arg(info.coverages.size()));
}

void AddBasemapDialog::onWCSCapabilitiesError(const QString &error)
{
    m_wcsStatus->setText(tr("Error: %1").arg(error));
}

void AddBasemapDialog::onWCSCoverageSelectionChanged()
{
    const QList<QTreeWidgetItem *> sel = m_wcsCovTree->selectedItems();
    if (sel.isEmpty() || !m_wcsInfo) return;

    const QString covId = sel.first()->data(0, Qt::UserRole).toString();
    m_wcsFmt->clear();
    m_wcsCrs->clear();

    for (const WCSCoverageInfo &ci : m_wcsInfo->coverages) {
        if (ci.identifier == covId) {
            for (const QString &f : ci.supportedFormats) m_wcsFmt->addItem(f);
            // Always ensure GeoTIFF is available as a fallback
            if (m_wcsFmt->findText(QStringLiteral("image/tiff"), Qt::MatchContains) < 0)
                m_wcsFmt->addItem(QStringLiteral("image/tiff"));
            // Prefer GeoTIFF
            const int tiffIdx = m_wcsFmt->findText(
                QStringLiteral("image/tiff"), Qt::MatchContains);
            if (tiffIdx >= 0) m_wcsFmt->setCurrentIndex(tiffIdx);

            for (const QString &c : ci.supportedCrs) m_wcsCrs->addItem(c);
            // Always ensure EPSG:4326 is available as a fallback
            if (m_wcsCrs->findText(QStringLiteral("EPSG:4326"), Qt::MatchContains) < 0)
                m_wcsCrs->addItem(QStringLiteral("EPSG:4326"));

            // Populate interpolation from coverage metadata when available
            if (!ci.interpolations.isEmpty()) {
                m_wcsInterp->clear();
                m_wcsInterp->addItems(ci.interpolations);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// ArcGIS slots
// ---------------------------------------------------------------------------

void AddBasemapDialog::onArcGISConnectionSelected(const QString &name)
{
    if (name.isEmpty() || name.startsWith("—")) return;
    const ArcGISRestConnection conn = BasemapConnectionStore::instance()->loadArcGIS(name);
    const BasemapAuth          auth = BasemapConnectionStore::instance()->loadArcGISAuth(name);
    populateArcGIS(conn, auth);
}

void AddBasemapDialog::onArcGISNew()
{
    m_arcCombo->setCurrentIndex(0);
    m_arcUrl->clear(); m_arcPrefix->clear();
    m_arcContent->clear(); m_arcCommunity->clear();
    m_arcUser->clear(); m_arcPass->clear();
    m_arcAuthBox->setChecked(false); m_arcHeaders->clear();
    m_arcInfoLabel->clear();
    m_arcDerivedXYZ = XYZConnection{};
}

void AddBasemapDialog::onArcGISEdit() {}

void AddBasemapDialog::onArcGISDelete()
{
    const QString name = m_arcCombo->currentText();
    if (name.isEmpty() || name.startsWith("—")) return;
    if (QMessageBox::question(this, tr("Delete Connection"),
            tr("Delete connection \"%1\"?").arg(name)) != QMessageBox::Yes) return;
    BasemapConnectionStore::instance()->removeArcGIS(name);
    refreshArcGISCombo();
}

void AddBasemapDialog::onArcGISConnect()
{
    QString base = m_arcUrl->text().trimmed();
    if (base.endsWith('/')) base.chop(1);
    if (base.isEmpty()) return;

    // Normalise: if the user pasted the full MapServer URL, don't double-append.
    // ArcGIS REST tile endpoint: {serviceBase}/tile/{z}/{row}/{col}
    // ArcGIS REST metadata endpoint: {serviceBase}?f=json
    const bool endsWithMapServer = base.endsWith(QStringLiteral("/mapserver"), Qt::CaseInsensitive)
                                || base.endsWith(QStringLiteral("/featureserver"), Qt::CaseInsensitive);
    const QString serviceBase = endsWithMapServer ? base : base + QStringLiteral("/MapServer");

    m_arcInfoLabel->setText(tr("Connecting…"));
    qDebug() << "[ArcGIS] connecting to" << serviceBase;
    const QUrl metaUrl(serviceBase + QStringLiteral("?f=json"));
    QNetworkRequest req{metaUrl};

    if (m_arcAuthBox->isChecked() && !m_arcUser->text().isEmpty()) {
        const QByteArray cred = QByteArray((m_arcUser->text() + ':' + m_arcPass->text()).toUtf8()).toBase64();
        req.setRawHeader("Authorization", "Basic " + cred);
    }

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, serviceBase]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[ArcGIS] metadata error:" << reply->errorString();
            onArcGISMetadataError(reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        qDebug() << "[ArcGIS] metadata bytes=" << data.size()
                 << "contentType=" << reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        if (obj.isEmpty()) { onArcGISMetadataError(tr("Empty JSON response")); return; }

        const QString serviceName = obj.value("mapName").toString(
            obj.value("documentInfo").toObject().value("Title").toString("ArcGIS Layer"));
        const QJsonObject tileInfo = obj.value("tileInfo").toObject();
        int zMin = 0, zMax = 19;
        if (!tileInfo.isEmpty()) {
            const QJsonArray lods = tileInfo.value("lods").toArray();
            if (!lods.isEmpty()) {
                zMin = lods.first().toObject().value("level").toInt(0);
                zMax = lods.last().toObject().value("level").toInt(19);
            }
        }

        m_arcDerivedXYZ.name        = serviceName;
        m_arcDerivedXYZ.urlTemplate = serviceBase + QStringLiteral("/tile/{z}/{x}/{y}");
        m_arcDerivedXYZ.zMin        = zMin;
        m_arcDerivedXYZ.zMax        = zMax;
        m_arcDerivedXYZ.axisOrder   = TileAxisOrder::ZYX;
        m_arcDerivedXYZ.httpHeaders = m_arcHeaders->headers();

        qDebug() << "[ArcGIS] template=" << m_arcDerivedXYZ.urlTemplate
                 << "zMin=" << zMin << "zMax=" << zMax;

        m_arcInfoLabel->setText(tr("Service: %1\nTile levels: %2 – %3")
            .arg(serviceName).arg(zMin).arg(zMax));

        onArcGISMetadataFetched();
    });
}

void AddBasemapDialog::onArcGISMetadataFetched() {}
void AddBasemapDialog::onArcGISMetadataError(const QString &error)
{
    m_arcInfoLabel->setText(tr("Error: %1").arg(error));
}

// ---------------------------------------------------------------------------
// createLayer() — factory
// ---------------------------------------------------------------------------

OpenSWMMVisLayer *AddBasemapDialog::createLayer(QObject *parent) const
{
    switch (m_tabs->currentIndex()) {
    case 0: return buildXYZLayer(parent);
    case 1: return m_isWMTS ? buildWMTSLayer(parent) : buildWMSLayer(parent);
    case 2: return buildWCSLayer(parent);
    case 3: return buildArcGISLayer(parent);
    default: return nullptr;
    }
}

OpenSWMMVisLayer *AddBasemapDialog::buildXYZLayer(QObject *parent) const
{
    const QString url = m_xyzUrl->text().trimmed();
    if (url.isEmpty()) return nullptr;

    auto *layer = new XYZTileLayer(url, 256, parent);
    layer->setName(m_xyzName->text().isEmpty() ? url : m_xyzName->text());
    layer->setTilePixelRatio(m_xyzPixRatio->currentData().toInt());
    layer->setAxisOrder(static_cast<TileAxisOrder>(m_xyzAxis->currentData().toInt()));
    layer->setHttpHeaders(m_xyzHeaders->headers());
    if (m_xyzAuthBox->isChecked() && !m_xyzUser->text().isEmpty())
        layer->setBasicAuth(m_xyzUser->text(), m_xyzPass->text());

    // Save connection if name is filled
    if (!m_xyzName->text().isEmpty()) {
        XYZConnection conn;
        conn.name           = m_xyzName->text();
        conn.urlTemplate    = url;
        conn.zMin           = m_xyzZMin->value();
        conn.zMax           = m_xyzZMax->value();
        conn.tilePixelRatio = m_xyzPixRatio->currentData().toInt();
        conn.axisOrder      = static_cast<TileAxisOrder>(m_xyzAxis->currentData().toInt());
        conn.httpHeaders    = m_xyzHeaders->headers();
        BasemapAuth auth;
        if (m_xyzAuthBox->isChecked()) {
            auth.username = m_xyzUser->text();
            auth.password = m_xyzPass->text();
        }
        BasemapConnectionStore::instance()->saveXYZ(conn, auth);
    }
    return layer;
}

OpenSWMMVisLayer *AddBasemapDialog::buildWMSLayer(QObject *parent) const
{
    if (!m_wmsInfo) return nullptr;
    const QList<QTreeWidgetItem *> sel = m_wmsTree->selectedItems();
    if (sel.isEmpty()) return nullptr;

    const QString layerName = sel.first()->data(0, Qt::UserRole).toString();
    auto *layer = new WMSLayer(QUrl(m_wmsUrl->text().trimmed()),
                               qobject_cast<OpenSWMMVisWorkspace *>(parent));
    layer->setServiceInfo(*m_wmsInfo);
    layer->setActiveLayerName(layerName);
    layer->setActiveStyle(m_wmsStyle->currentText());
    layer->setImageFormat(m_wmsFmt->currentText());
    if (!m_wmsCrs->currentText().isEmpty())
        layer->setCrs(m_wmsCrs->currentText());
    layer->setHttpHeaders(m_wmsHeaders->headers());
    if (m_wmsAuthBox->isChecked() && !m_wmsUser->text().isEmpty())
        layer->setBasicAuth(m_wmsUser->text(), m_wmsPass->text());
    // Set display name from service info
    for (const WMSLayerInfo &li : m_wmsInfo->layers) {
        if (li.name == layerName) { layer->setName(li.title.isEmpty() ? li.name : li.title); break; }
    }
    return layer;
}

OpenSWMMVisLayer *AddBasemapDialog::buildWMTSLayer(QObject *parent) const
{
    if (!m_wmtsInfo) return nullptr;
    const QList<QTreeWidgetItem *> sel = m_wmsTree->selectedItems();
    if (sel.isEmpty()) return nullptr;

    const QString layerId = sel.first()->data(0, Qt::UserRole).toString();
    auto *layer = new WMTSLayer(QUrl(m_wmsUrl->text().trimmed()),
                                qobject_cast<OpenSWMMVisWorkspace *>(parent));
    layer->setServiceInfo(*m_wmtsInfo);
    layer->setActiveLayerId(layerId);
    layer->setActiveTileMatrixSet(m_wmsTms->currentText());
    layer->setActiveStyle(m_wmsStyle->currentText());
    layer->setImageFormat(m_wmsFmt->currentText());
    layer->setHttpHeaders(m_wmsHeaders->headers());
    if (m_wmsAuthBox->isChecked() && !m_wmsUser->text().isEmpty())
        layer->setBasicAuth(m_wmsUser->text(), m_wmsPass->text());
    for (const WMTSLayerInfo &li : m_wmtsInfo->layers) {
        if (li.identifier == layerId) { layer->setName(li.title.isEmpty() ? li.identifier : li.title); break; }
    }
    return layer;
}

OpenSWMMVisLayer *AddBasemapDialog::buildWCSLayer(QObject *parent) const
{
    if (!m_wcsInfo) return nullptr;
    const QList<QTreeWidgetItem *> sel = m_wcsCovTree->selectedItems();
    if (sel.isEmpty()) return nullptr;

    const QString covId = sel.first()->data(0, Qt::UserRole).toString();
    auto *layer = new WCSLayer(QUrl(m_wcsUrl->text().trimmed()),
                               qobject_cast<OpenSWMMVisWorkspace *>(parent));
    layer->setServiceInfo(*m_wcsInfo);
    layer->setActiveCoverageId(covId);
    // Only override defaults when the combo/edit has an actual value —
    // an empty string would clobber the sensible "image/tiff" / "EPSG:4326" defaults.
    if (!m_wcsFmt->currentText().isEmpty())
        layer->setOutputFormat(m_wcsFmt->currentText());
    if (!m_wcsCrs->currentText().isEmpty())
        layer->setOutputCrs(m_wcsCrs->currentText());
    layer->setRangeSubset(m_wcsRange->text().trimmed());
    if (!m_wcsInterp->currentText().isEmpty())
        layer->setInterpolation(m_wcsInterp->currentText());
    layer->setHttpHeaders(m_wcsHeaders->headers());
    if (m_wcsAuthBox->isChecked() && !m_wcsUser->text().isEmpty())
        layer->setBasicAuth(m_wcsUser->text(), m_wcsPass->text());

    // Set display name from coverage metadata
    for (const WCSCoverageInfo &ci : m_wcsInfo->coverages) {
        if (ci.identifier == covId) {
            layer->setName(ci.title.isEmpty() ? ci.identifier : ci.title);
            break;
        }
    }

    // Persist connection
    if (!m_wcsUrl->text().isEmpty()) {
        WCSConnection conn;
        conn.name          = layer->name();
        conn.url           = m_wcsUrl->text().trimmed();
        conn.version       = m_wcsInfo->version;
        conn.coverageId    = covId;
        conn.outputFormat  = m_wcsFmt->currentText();
        conn.outputCrs     = m_wcsCrs->currentText();
        conn.rangeSubset   = m_wcsRange->text().trimmed();
        conn.interpolation = m_wcsInterp->currentText();
        conn.httpHeaders   = m_wcsHeaders->headers();
        BasemapAuth auth;
        if (m_wcsAuthBox->isChecked()) {
            auth.username = m_wcsUser->text();
            auth.password = m_wcsPass->text();
        }
        BasemapConnectionStore::instance()->saveWCS(conn, auth);
    }
    return layer;
}

OpenSWMMVisLayer *AddBasemapDialog::buildArcGISLayer(QObject *parent) const
{
    if (m_arcDerivedXYZ.urlTemplate.isEmpty()) return nullptr;

    // ArcGIS REST tiles are just an XYZTileLayer with ZYX axis order
    auto *layer = new XYZTileLayer(m_arcDerivedXYZ.urlTemplate,
                                   256,
                                   qobject_cast<OpenSWMMVisWorkspace *>(parent));
    layer->setName(m_arcDerivedXYZ.name);
    layer->setAxisOrder(TileAxisOrder::ZYX);
    layer->setHttpHeaders(m_arcDerivedXYZ.httpHeaders);
    if (m_arcAuthBox->isChecked() && !m_arcUser->text().isEmpty())
        layer->setBasicAuth(m_arcUser->text(), m_arcPass->text());

    // Save connection
    if (!m_arcUrl->text().isEmpty()) {
        ArcGISRestConnection conn;
        conn.name              = m_arcDerivedXYZ.name;
        conn.url               = m_arcUrl->text().trimmed();
        conn.urlPrefix         = m_arcPrefix->text().trimmed();
        conn.contentEndpoint   = m_arcContent->text().trimmed();
        conn.communityEndpoint = m_arcCommunity->text().trimmed();
        conn.httpHeaders       = m_arcHeaders->headers();
        BasemapAuth auth;
        if (m_arcAuthBox->isChecked()) {
            auth.username = m_arcUser->text();
            auth.password = m_arcPass->text();
        }
        BasemapConnectionStore::instance()->saveArcGIS(conn, auth);
    }
    return layer;
}
