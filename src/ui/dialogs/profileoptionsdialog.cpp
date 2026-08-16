/*!
 * \file   profileoptionsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/profileoptionsdialog.h"

#include "animation/animationcontroller.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "plot/profileattributetrackoptions.h"
#include "plot/profileplotoptions.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/profileresultsources.h"
#include "ui/dialogs/profilesourcestyleadapter.h"
#include "ui/uiscrollhelpers.h"

#ifdef HAVE_QPROPERTYMODEL
#include <qpropertymodel.h>
#include <qpropertyitemdelegate.h>
#endif

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

QIcon colorChip(const QColor &c, int sizePx = 14)
{
    QPixmap p(sizePx, sizePx);
    p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing, true);
    g.setBrush(c);
    g.setPen(QPen(c.darker(140), 1));
    g.drawEllipse(1, 1, sizePx - 2, sizePx - 2);
    return QIcon(p);
}

// Column layout for the sources QTreeView.  Keep in sync with the model
// builder below — kColCount serves as the source of truth.
enum SourceColumn {
    ColVisible  = 0,
    ColColor    = 1,
    ColName     = 2,
    ColFile     = 3,
    ColProject  = 4,
    kColCount
};

} // namespace

ProfileOptionsDialog::ProfileOptionsDialog(ProfilePlotOptions   *options,
                                            AnimationController  *anim,
                                            SWMMVisProjectWindow *projectWindow,
                                            QWidget              *parent)
    : QDialog(parent),
      m_options(options),
      m_anim(anim),
      m_projectWindow(projectWindow)
{
    setWindowTitle(tr("Profile Display Options"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("ProfileOptionsDialog"));
    setModal(false);
    setWindowFlags(Qt::Window | Qt::WindowSystemMenuHint
                   | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    resize(560, 640);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, /*stretch=*/1);

    buildDisplayTab();
    buildSourcesTab();

    if (m_projectWindow && m_projectWindow->canvas()) {
        auto refreshForResultLayerChange = [this](OpenSWMMVisLayer *layer) {
            if (!qobject_cast<SWMMResultsLayer *>(layer)) return;
            refreshSources();
        };
        connect(m_projectWindow->canvas(), &MapCanvas::layerAdded,
                this, refreshForResultLayerChange);
        connect(m_projectWindow->canvas(), &MapCanvas::layerRemoved,
                this, refreshForResultLayerChange);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    restoreSourcesState();
}

void ProfileOptionsDialog::buildDisplayTab()
{
    auto *page   = new QWidget(this);
    auto *vbox   = new QVBoxLayout(page);
    vbox->setContentsMargins(6, 6, 6, 6);

    m_tree = new QTreeView(page);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    vbox->addWidget(m_tree, /*stretch=*/1);

#ifdef HAVE_QPROPERTYMODEL
    auto *pm = new QPropertyModel(this);
    m_model    = pm;
    m_delegate = new QPropertyItemDelegate(this);
    m_tree->setModel(m_model);
    m_tree->setItemDelegate(m_delegate);
    m_tree->header()->setDefaultSectionSize(180);
    if (m_options)
        pm->setData(QVariant::fromValue<QObject *>(m_options.data()));
    m_tree->expandAll();

    if (m_options) {
        connect(m_options.data(), &ProfilePlotOptions::changed, pm,
                [pm] { pm->refreshValues(); });
    }
#else
    m_model = new QStandardItemModel(this);
    m_tree->setModel(m_model);
    Q_UNUSED(m_delegate);
#endif

    m_tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(page, m_tabs), tr("&Display"));
}

void ProfileOptionsDialog::setTrackOptions(
    ProfileAttributeTrackOptions *trackOptions)
{
    if (!trackOptions || !m_tabs) return;

    auto *page = new QWidget(this);
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(6, 6, 6, 6);

    auto *tree = new QTreeView(page);
    tree->setAlternatingRowColors(true);
    tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    vbox->addWidget(tree, /*stretch=*/1);

#ifdef HAVE_QPROPERTYMODEL
    auto *pm       = new QPropertyModel(this);
    auto *delegate = new QPropertyItemDelegate(this);
    tree->setModel(pm);
    tree->setItemDelegate(delegate);
    tree->header()->setDefaultSectionSize(180);
    pm->setData(QVariant::fromValue<QObject *>(trackOptions));
    tree->expandAll();
    connect(trackOptions, &ProfileAttributeTrackOptions::changed, pm,
            [pm] { pm->refreshValues(); });
#else
    vbox->addWidget(new QLabel(
        tr("Property editor unavailable (QPropertyModel not built in)."),
        page));
#endif

    m_tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(page, m_tabs),
                   tr("Attribute &Tracks"));
}

void ProfileOptionsDialog::buildSourcesTab()
{
    auto *page = new QWidget(this);
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(6, 6, 6, 6);

    auto *hint = new QLabel(tr("Select a source on the left to edit its profile style on the right.  "
                               "Toggle Visible, double-click the colour swatch, or edit the Scenario "
                               "name in place."),
                            page);
    hint->setWordWrap(true);
    vbox->addWidget(hint);

    // Horizontal splitter: sources list on the left, property-model editor
    // on the right.  The splitter is the source of truth for the tab's
    // resize behaviour — both sides shrink/grow freely.
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName(QStringLiteral("main"));
    splitter->setChildrenCollapsible(false);
    vbox->addWidget(splitter, /*stretch=*/1);

    // ── Left: sources list ──────────────────────────────────────────────
    m_sourcesView  = new QTreeView(splitter);
    m_sourcesModel = new QStandardItemModel(this);
    m_sourcesModel->setColumnCount(kColCount);
    m_sourcesModel->setHorizontalHeaderLabels(
        { tr("Visible"), tr("Colour"), tr("Scenario"), tr("File"), tr("Project") });
    m_sourcesView->setModel(m_sourcesModel);
    m_sourcesView->setRootIsDecorated(false);
    m_sourcesView->setAlternatingRowColors(true);
    m_sourcesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sourcesView->setEditTriggers(QAbstractItemView::DoubleClicked
                                   | QAbstractItemView::EditKeyPressed);
    m_sourcesView->header()->setStretchLastSection(false);
    m_sourcesView->header()->setSectionResizeMode(ColName,    QHeaderView::Stretch);
    m_sourcesView->header()->setSectionResizeMode(ColFile,    QHeaderView::Stretch);
    m_sourcesView->header()->setSectionResizeMode(ColProject, QHeaderView::ResizeToContents);
    m_sourcesView->header()->setSectionResizeMode(ColVisible, QHeaderView::ResizeToContents);
    m_sourcesView->header()->setSectionResizeMode(ColColor,   QHeaderView::ResizeToContents);
    splitter->addWidget(m_sourcesView);

    // ── Right: per-source style editor (QPropertyModel-backed) ───────────
    auto *rightPane     = new QWidget(splitter);
    auto *rightLayout   = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(6, 0, 0, 0);
    auto *styleHeading  = new QLabel(tr("<b>Style</b>"), rightPane);
    rightLayout->addWidget(styleHeading);
    m_styleView         = new QTreeView(rightPane);
    m_styleView->setAlternatingRowColors(true);
    m_styleView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_styleView->setEnabled(false);   // disabled until a source is selected
    rightLayout->addWidget(m_styleView, /*stretch=*/1);
    splitter->addWidget(rightPane);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

#ifdef HAVE_QPROPERTYMODEL
    m_styleAdapter  = new ProfileSourceStyleAdapter(this);
    auto *stylePM   = new QPropertyModel(this);
    m_styleModel    = stylePM;
    m_styleDelegate = new QPropertyItemDelegate(this);
    m_styleView->setModel(m_styleModel);
    m_styleView->setItemDelegate(m_styleDelegate);
    m_styleView->header()->setDefaultSectionSize(160);
    // Bind adapter as a QObject — model lazily builds rows from
    // metaProperties() when setData is called.  Adapter starts unbound so
    // the model renders an empty (disabled) editor.
    stylePM->setData(QVariant::fromValue<QObject *>(m_styleAdapter));
    m_styleView->expandAll();
    // Adapter forwards layer profileStyleChanged / profileLineColorChanged
    // as a single `changed()` — drive the model refresh from there so swatch
    // changes elsewhere (the plot itself, hypothetically) reflect here.
    connect(m_styleAdapter, &ProfileSourceStyleAdapter::changed, stylePM,
            [stylePM] { stylePM->refreshValues(); });
    // Property edits propagate to the layer through the adapter, which fires
    // profileStyleChanged() — that triggers our `changed()` and we also need
    // to persist & notify the plot dialog.
    connect(m_styleAdapter, &ProfileSourceStyleAdapter::changed, this,
            [this] {
        persistSourcesState();
        emit sourcesChanged();
    });
#else
    m_styleModel = new QStandardItemModel(this);
    m_styleView->setModel(m_styleModel);
    Q_UNUSED(m_styleDelegate);
    Q_UNUSED(m_styleAdapter);
#endif

    auto *buttonRow = new QHBoxLayout;
    auto *addBtn    = new QPushButton(tr("Add output file…"), page);
    addBtn->setToolTip(tr("Browse for a SWMM .out file and add it as a comparison source."));
    buttonRow->addWidget(addBtn);
    buttonRow->addStretch(1);
    vbox->addLayout(buttonRow);

    // Selection drives the right pane: bind the adapter to the selected
    // layer, or detach (disable editor) when no row is selected.
    connect(m_sourcesView->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
        SWMMResultsLayer *layer = nullptr;
        if (cur.isValid()) {
            QStandardItem *visItem = m_sourcesModel->item(cur.row(), ColVisible);
            if (visItem)
                layer = qobject_cast<SWMMResultsLayer *>(
                    visItem->data(Qt::UserRole + 1).value<QObject *>());
        }
#ifdef HAVE_QPROPERTYMODEL
        if (m_styleAdapter) m_styleAdapter->setLayer(layer);
#endif
        m_styleView->setEnabled(layer != nullptr);
    });

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        QString dir;
        if (m_projectWindow) {
            const QString p = (m_projectWindow->modelLayer() ? m_projectWindow->modelLayer()->modelFilePath() : QString());
            if (!p.isEmpty()) dir = QFileInfo(p).absolutePath();
        }
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Add Comparison Output"), dir,
            tr("SWMM Output Files (*.out);;All Files (*)"));
        if (path.isEmpty()) return;
        emit addOutputFileRequested(path);
        refreshSourcesModel();
    });

    // Edits → emit sourcesChanged.  The model carries the layer pointer in
    // the visibility column's UserRole so the slot can mutate the layer.
    connect(m_sourcesModel, &QStandardItemModel::itemChanged, this,
            [this](QStandardItem *item) {
        if (!item) return;
        QStandardItem *visItem = m_sourcesModel->item(item->row(), ColVisible);
        if (!visItem) return;
        auto *layer = qobject_cast<SWMMResultsLayer *>(
            visItem->data(Qt::UserRole + 1).value<QObject *>());
        if (!layer) return;
        switch (item->column()) {
        case ColVisible:
            // Stored visibility carried by Qt::CheckStateRole — read directly.
            persistSourcesState();
            emit sourcesChanged();
            break;
        case ColName: {
            const QString newName = item->text();
            if (!newName.isEmpty() && newName != layer->scenarioName())
                layer->setScenarioName(newName);
            persistSourcesState();
            emit sourcesChanged();
            break;
        }
        default:
            break;
        }
    });

    // Color column is opened via double-click on the swatch.
    connect(m_sourcesView, &QTreeView::doubleClicked, this,
            [this](const QModelIndex &idx) {
        if (!idx.isValid() || idx.column() != ColColor) return;
        QStandardItem *visItem = m_sourcesModel->item(idx.row(), ColVisible);
        if (!visItem) return;
        auto *layer = qobject_cast<SWMMResultsLayer *>(
            visItem->data(Qt::UserRole + 1).value<QObject *>());
        if (!layer) return;
        const QColor picked = QColorDialog::getColor(layer->profileLineColor(),
                                                     this, tr("Source colour"));
        if (!picked.isValid()) return;
        layer->setProfileLineColor(picked);
        refreshSourcesModel();
        persistSourcesState();
        emit sourcesChanged();
    });

    refreshSourcesModel();

    m_tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(page, m_tabs), tr("&Sources"));
}

void ProfileOptionsDialog::refreshSources()
{
    refreshSourcesModel();
    restoreSourcesState();
}

void ProfileOptionsDialog::refreshSourcesModel()
{
    if (!m_sourcesModel) return;
    QSignalBlocker block(m_sourcesModel);
    m_sourcesModel->removeRows(0, m_sourcesModel->rowCount());

    const auto layers =
        openswmmvis::ui::profileResultSources(m_anim.data(),
                                              m_projectWindow.data());
    for (auto *layer : layers) {
        if (!layer) continue;
        auto *visible = new QStandardItem();
        visible->setCheckable(true);
        visible->setCheckState(Qt::Checked);
        visible->setEditable(false);
        visible->setData(QVariant::fromValue<QObject *>(layer), Qt::UserRole + 1);

        auto *colorItem = new QStandardItem(colorChip(layer->profileLineColor()),
                                            layer->profileLineColor().name());
        colorItem->setEditable(false);
        colorItem->setToolTip(tr("Double-click to change colour"));

        auto *nameItem = new QStandardItem(layer->scenarioName());
        nameItem->setEditable(true);

        const QString filePath = layer->resultsFilePath();
        auto *fileItem = new QStandardItem(filePath.isEmpty()
                                               ? tr("(unsaved)")
                                               : QFileInfo(filePath).fileName());
        fileItem->setToolTip(filePath);
        fileItem->setEditable(false);

        QString projectName;
        // Walk the layer's parent chain to find a SWMMVisProjectWindow.
        for (QObject *p = layer->parent(); p; p = p->parent()) {
            if (auto *pw = qobject_cast<SWMMVisProjectWindow *>(p)) {
                const QString fp = pw->modelLayer()
                                       ? pw->modelLayer()->modelFilePath()
                                       : QString();
                projectName = fp.isEmpty() ? pw->windowTitle()
                                           : QFileInfo(fp).fileName();
                break;
            }
        }
        auto *projectItem = new QStandardItem(projectName);
        projectItem->setEditable(false);

        m_sourcesModel->appendRow({ visible, colorItem, nameItem,
                                    fileItem, projectItem });
    }
}

QString ProfileOptionsDialog::persistenceKey() const
{
    QString projectKey = QStringLiteral("default");
    if (m_projectWindow) {
        const QString p = (m_projectWindow->modelLayer() ? m_projectWindow->modelLayer()->modelFilePath() : QString());
        if (!p.isEmpty()) projectKey = p;
    }
    return QStringLiteral("ProfilePlot/Comparison/") + projectKey;
}

void ProfileOptionsDialog::persistSourcesState() const
{
    if (!m_sourcesModel) return;
    QSettings settings;
    settings.beginGroup(persistenceKey());
    settings.remove(QStringLiteral("sources"));
    settings.beginWriteArray(QStringLiteral("sources"));
    int idx = 0;
    for (int row = 0; row < m_sourcesModel->rowCount(); ++row) {
        QStandardItem *vis = m_sourcesModel->item(row, ColVisible);
        if (!vis) continue;
        auto *layer = qobject_cast<SWMMResultsLayer *>(
            vis->data(Qt::UserRole + 1).value<QObject *>());
        if (!layer) continue;
        const QString fp = layer->resultsFilePath();
        if (fp.isEmpty()) continue;
        settings.setArrayIndex(idx++);
        settings.setValue(QStringLiteral("file"),    fp);
        settings.setValue(QStringLiteral("visible"), vis->checkState() == Qt::Checked);
        settings.setValue(QStringLiteral("color"),   layer->profileLineColor().name());
        settings.setValue(QStringLiteral("name"),    layer->scenarioName());
        // Per-source HGL/EGL/Max-HGL/Max-EGL pens & brushes — pushed
        // through SWMMResultsLayer's helper so the same keys are
        // produced from both this dialog and ProfilePlotDialog.
        layer->writeProfileStyle(settings);
    }
    settings.endArray();
    settings.endGroup();
}

void ProfileOptionsDialog::restoreSourcesState()
{
    if (!m_sourcesModel) return;
    QSignalBlocker block(m_sourcesModel);

    // Build a file-path → (layer, row, model-items) lookup once so we can
    // apply each settings entry directly while QSettings is still
    // positioned at that array index — this is necessary because the
    // per-source style helper (readProfileStyle) reads multiple keys
    // from the current position.
    struct RowRef {
        SWMMResultsLayer *layer    = nullptr;
        QStandardItem    *visItem  = nullptr;
        QStandardItem    *colItem  = nullptr;
        QStandardItem    *nameItem = nullptr;
    };
    QHash<QString, RowRef> byFile;
    for (int row = 0; row < m_sourcesModel->rowCount(); ++row) {
        QStandardItem *visItem = m_sourcesModel->item(row, ColVisible);
        if (!visItem) continue;
        auto *layer = qobject_cast<SWMMResultsLayer *>(
            visItem->data(Qt::UserRole + 1).value<QObject *>());
        if (!layer) continue;
        RowRef r;
        r.layer    = layer;
        r.visItem  = visItem;
        r.colItem  = m_sourcesModel->item(row, ColColor);
        r.nameItem = m_sourcesModel->item(row, ColName);
        byFile.insert(layer->resultsFilePath(), r);
    }

    QSettings settings;
    settings.beginGroup(persistenceKey());
    const int n = settings.beginReadArray(QStringLiteral("sources"));
    for (int i = 0; i < n; ++i) {
        settings.setArrayIndex(i);
        const QString fp = settings.value(QStringLiteral("file")).toString();
        if (fp.isEmpty()) continue;
        const auto it = byFile.constFind(fp);
        if (it == byFile.constEnd()) continue;
        const RowRef &r = it.value();
        if (settings.contains(QStringLiteral("visible"))) {
            r.visItem->setCheckState(
                settings.value(QStringLiteral("visible")).toBool()
                    ? Qt::Checked : Qt::Unchecked);
        }
        const QString colorName = settings.value(QStringLiteral("color")).toString();
        if (!colorName.isEmpty()) {
            QColor c(colorName);
            if (c.isValid()) r.layer->setProfileLineColor(c);
            if (r.colItem) {
                r.colItem->setIcon(colorChip(c));
                r.colItem->setText(c.name());
            }
        }
        const QString name = settings.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            r.layer->setScenarioName(name);
            if (r.nameItem) r.nameItem->setText(name);
        }
        // Per-source profile-plot pens & brushes.  Missing keys leave
        // the layer's defaults (derived from profileLineColor) untouched.
        r.layer->readProfileStyle(settings);
    }
    settings.endArray();
    settings.endGroup();

    emit sourcesChanged();
}
