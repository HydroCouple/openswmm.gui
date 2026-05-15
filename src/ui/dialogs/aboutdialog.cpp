/*!
 * \file   aboutdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/aboutdialog.h"
#include "version.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QOperatingSystemVersion>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QSysInfo>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

constexpr int RoleComponentIndex = Qt::UserRole + 1;   // int → m_components index
constexpr int RoleIsLeaf         = Qt::UserRole + 2;   // bool

QString applicationVersionString()
{
    return QStringLiteral("%1.%2.%3")
        .arg(SWMM_VERSION_MAJOR)
        .arg(SWMM_VERSION_MINOR)
        .arg(SWMM_VERSION_PATCH);
}

QString environmentSummary()
{
    return QStringLiteral(
               "SWMMVis %1\n"
               "Build:    %2 %3\n"
               "Qt:       %4\n"
               "OS:       %5 (%6)\n"
               "Arch:     %7")
        .arg(applicationVersionString())
        .arg(QStringLiteral(__DATE__))
        .arg(QStringLiteral(__TIME__))
        .arg(QString::fromLatin1(qVersion()))
        .arg(QSysInfo::prettyProductName())
        .arg(QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion())
        .arg(QSysInfo::currentCpuArchitecture());
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About SWMMVis"));
    resize(880, 600);

    buildUi();
    loadManifest();
    populateTree();
    showApplicationOverview();
}

AboutDialog::~AboutDialog() = default;

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void AboutDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // ── Header strip ────────────────────────────────────────────────────
    auto *header = new QHBoxLayout;
    m_headerLabel = new QLabel(this);
    m_headerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_headerLabel->setText(environmentSummary().replace('\n', QStringLiteral("<br>")));
    m_headerLabel->setTextFormat(Qt::RichText);
    header->addWidget(m_headerLabel, 1);

    m_copyEnvButton = new QPushButton(tr("Copy environment"), this);
    m_copyEnvButton->setToolTip(tr("Copy the build / OS / Qt summary to the clipboard"));
    connect(m_copyEnvButton, &QPushButton::clicked, this, []() {
        QApplication::clipboard()->setText(environmentSummary());
    });
    header->addWidget(m_copyEnvButton, 0, Qt::AlignTop);
    root->addLayout(header);

    // ── Master / detail splitter ────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // Left: search + tree
    auto *leftBox  = new QWidget(splitter);
    auto *leftLay  = new QVBoxLayout(leftBox);
    leftLay->setContentsMargins(0, 0, 0, 0);

    m_searchEdit = new QLineEdit(leftBox);
    m_searchEdit->setPlaceholderText(tr("Filter components…"));
    m_searchEdit->setClearButtonEnabled(true);
    leftLay->addWidget(m_searchEdit);

    m_tree       = new QTreeView(leftBox);
    m_treeModel  = new QStandardItemModel(this);
    m_treeProxy  = new QSortFilterProxyModel(this);
    m_treeProxy->setSourceModel(m_treeModel);
    m_treeProxy->setRecursiveFilteringEnabled(true);
    m_treeProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_tree->setModel(m_treeProxy);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    leftLay->addWidget(m_tree, 1);

    splitter->addWidget(leftBox);

    // Right: detail pane
    auto *rightBox = new QWidget(splitter);
    auto *rightLay = new QVBoxLayout(rightBox);
    rightLay->setContentsMargins(8, 0, 0, 0);

    m_nameLabel = new QLabel(rightBox);
    QFont nameFont = m_nameLabel->font();
    nameFont.setPointSize(nameFont.pointSize() + 4);
    nameFont.setBold(true);
    m_nameLabel->setFont(nameFont);
    m_nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLay->addWidget(m_nameLabel);

    m_metaLabel = new QLabel(rightBox);
    m_metaLabel->setTextFormat(Qt::RichText);
    m_metaLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_metaLabel->setOpenExternalLinks(true);
    m_metaLabel->setWordWrap(true);
    rightLay->addWidget(m_metaLabel);

    m_licenseText = new QPlainTextEdit(rightBox);
    m_licenseText->setReadOnly(true);
    m_licenseText->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_licenseText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    rightLay->addWidget(m_licenseText, 1);

    auto *btnRow = new QHBoxLayout;
    m_copyButton     = new QPushButton(tr("Copy License"),  rightBox);
    m_homepageButton = new QPushButton(tr("Open Homepage"), rightBox);
    m_sourceButton   = new QPushButton(tr("Open Source"),   rightBox);
    btnRow->addWidget(m_copyButton);
    btnRow->addWidget(m_homepageButton);
    btnRow->addWidget(m_sourceButton);
    btnRow->addStretch();
    rightLay->addLayout(btnRow);

    splitter->addWidget(rightBox);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    // ── Bottom Close button ─────────────────────────────────────────────
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    // ── Connections ─────────────────────────────────────────────────────
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &AboutDialog::onSearchTextChanged);
    connect(m_copyButton,     &QPushButton::clicked, this, &AboutDialog::onCopyLicense);
    connect(m_homepageButton, &QPushButton::clicked, this, &AboutDialog::onOpenHomepage);
    connect(m_sourceButton,   &QPushButton::clicked, this, &AboutDialog::onOpenSource);
}

// ---------------------------------------------------------------------------
// Manifest loading
// ---------------------------------------------------------------------------

void AboutDialog::loadManifest()
{
    QFile f(QStringLiteral(":/about/components.json"));
    if (!f.open(QIODevice::ReadOnly))
    {
        // Manifest missing — show only the application overview.
        m_components.clear();
        return;
    }

    QString err;
    m_components = parseManifest(f.readAll(), &err);
    if (!err.isEmpty())
        m_components.clear();
}

QVector<AboutDialog::Component>
AboutDialog::parseManifest(const QByteArray &json, QString *errorOut)
{
    QVector<Component> out;
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (doc.isNull() || !doc.isArray())
    {
        if (errorOut)
            *errorOut = pe.errorString().isEmpty()
                            ? QStringLiteral("Manifest root is not a JSON array.")
                            : pe.errorString();
        return out;
    }
    const QJsonArray arr = doc.array();
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
    {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        Component c;
        c.category    = o.value(QStringLiteral("category")).toString();
        c.name        = o.value(QStringLiteral("name")).toString();
        c.version     = o.value(QStringLiteral("version")).toString();
        c.role        = o.value(QStringLiteral("role")).toString();
        c.homepage    = o.value(QStringLiteral("homepage")).toString();
        c.sourceUrl   = o.value(QStringLiteral("source")).toString();
        c.spdx        = o.value(QStringLiteral("spdx")).toString();
        c.provenance  = o.value(QStringLiteral("provenance")).toString();
        c.licenseFile = o.value(QStringLiteral("licenseFile")).toString();
        if (!c.name.isEmpty())
            out.append(std::move(c));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tree population
// ---------------------------------------------------------------------------

void AboutDialog::populateTree()
{
    m_treeModel->clear();
    m_treeModel->setHorizontalHeaderLabels({tr("Component")});

    // Group by category, preserving manifest order within each group.
    QHash<QString, QStandardItem *> catItems;
    for (int i = 0; i < m_components.size(); ++i)
    {
        const Component &c = m_components[i];
        const QString cat = c.category.isEmpty() ? tr("Other") : c.category;

        QStandardItem *catItem = catItems.value(cat);
        if (!catItem)
        {
            catItem = new QStandardItem(cat);
            QFont f = catItem->font();
            f.setBold(true);
            catItem->setFont(f);
            catItem->setData(false, RoleIsLeaf);
            catItem->setFlags(Qt::ItemIsEnabled);
            m_treeModel->appendRow(catItem);
            catItems.insert(cat, catItem);
        }

        auto *leaf = new QStandardItem(c.version.isEmpty()
                                           ? c.name
                                           : QStringLiteral("%1  (%2)").arg(c.name, c.version));
        leaf->setData(i,    RoleComponentIndex);
        leaf->setData(true, RoleIsLeaf);
        leaf->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        catItem->appendRow(leaf);
    }

    m_tree->expandAll();

    // Selection wiring (must be after model is set on the proxy).
    if (auto *sm = m_tree->selectionModel())
        connect(sm, &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex &, const QModelIndex &) {
                    onSelectionChanged();
                });
}

// ---------------------------------------------------------------------------
// Detail pane
// ---------------------------------------------------------------------------

void AboutDialog::showApplicationOverview()
{
    m_currentComponent = -1;
    m_nameLabel->setText(QStringLiteral("SWMMVis %1").arg(applicationVersionString()));
    m_metaLabel->setText(tr(
        "Open-source Qt6 GUI for the OpenSWMM engine.<br>"
        "Select a component on the left to view its license and metadata."));
    m_licenseText->clear();
    m_licenseText->setPlaceholderText(tr("Select a component to view its license."));
    m_copyButton->setEnabled(false);
    m_homepageButton->setEnabled(false);
    m_sourceButton->setEnabled(false);
}

void AboutDialog::showComponent(const Component &c)
{
    m_nameLabel->setText(c.name);

    QStringList meta;
    if (!c.version.isEmpty())     meta << tr("<b>Version:</b> %1").arg(c.version.toHtmlEscaped());
    if (!c.role.isEmpty())        meta << tr("<b>Role:</b> %1").arg(c.role.toHtmlEscaped());
    if (!c.spdx.isEmpty())        meta << tr("<b>License:</b> %1").arg(c.spdx.toHtmlEscaped());
    if (!c.provenance.isEmpty())  meta << tr("<b>Source:</b> %1").arg(c.provenance.toHtmlEscaped());
    if (!c.homepage.isEmpty())
        meta << tr("<b>Homepage:</b> <a href=\"%1\">%1</a>")
                    .arg(c.homepage.toHtmlEscaped());
    m_metaLabel->setText(meta.join(QStringLiteral("<br>")));

    // License text
    if (!c.licenseFile.isEmpty())
    {
        QFile f(c.licenseFile);
        if (f.open(QIODevice::ReadOnly))
            m_licenseText->setPlainText(QString::fromUtf8(f.readAll()));
        else
            m_licenseText->setPlainText(tr("(license file not found: %1)").arg(c.licenseFile));
    }
    else
    {
        m_licenseText->setPlainText(tr("(no license file declared in the manifest)"));
    }

    m_copyButton->setEnabled(!m_licenseText->toPlainText().isEmpty());
    m_homepageButton->setEnabled(!c.homepage.isEmpty());
    m_sourceButton->setEnabled(!c.sourceUrl.isEmpty());
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void AboutDialog::onSelectionChanged()
{
    const QModelIndex proxyIdx = m_tree->currentIndex();
    if (!proxyIdx.isValid()) return;
    const QModelIndex srcIdx = m_treeProxy->mapToSource(proxyIdx);
    if (!srcIdx.data(RoleIsLeaf).toBool())
        return;
    const int idx = srcIdx.data(RoleComponentIndex).toInt();
    if (idx < 0 || idx >= m_components.size())
        return;
    m_currentComponent = idx;
    showComponent(m_components[idx]);
}

void AboutDialog::onSearchTextChanged(const QString &text)
{
    m_treeProxy->setFilterFixedString(text);
    m_tree->expandAll();
}

void AboutDialog::onCopyLicense()
{
    QApplication::clipboard()->setText(m_licenseText->toPlainText());
}

void AboutDialog::onOpenHomepage()
{
    if (m_currentComponent < 0) return;
    const QString url = m_components[m_currentComponent].homepage;
    if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
}

void AboutDialog::onOpenSource()
{
    if (m_currentComponent < 0) return;
    const QString url = m_components[m_currentComponent].sourceUrl;
    if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
}
