/*!
 * \file   landuseeditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/landuseeditordialog.h"

#include "landuse/landuseprovider.h"
#include "landuse/landuseregistry.h"
#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "pollutant/pollutantregistry.h"
#include "ui/models/landuselistmodel.h"
#include "ui/models/qualityfunctiontablemodels.h"
#include "ui/theme/iconfactory.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_quality.h>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::landuse::LandUseProvider;
using openswmmvis::landuse::LandUseRegistry;

LandUseEditorDialog::LandUseEditorDialog(LandUseRegistry *registry,
                                         SWMMModelLayer *layer,
                                         QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Land Uses"));
    resize(760, 460);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &LandUseRegistry::providerRenamed,
                this, &LandUseEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }

}

void LandUseEditorDialog::trackPollutantRegistry(
    openswmmvis::pollutant::PollutantRegistry *registry)
{
    // Rows in the Buildup/Washoff tables track the pollutant set live —
    // "sections dynamically added and removed" as pollutants change. The
    // launch site (ComprehensiveEditorRegistry) passes the model layer's
    // pollutant registry; tests may call refreshPollutants() directly.
    if (!registry) return;
    using openswmmvis::pollutant::PollutantRegistry;
    connect(registry, &PollutantRegistry::providerAdded, this,
            [this](auto *) { refreshPollutants(); });
    connect(registry, &PollutantRegistry::providerAboutToBeRemoved, this,
            [this](auto *) { refreshPollutants(); });
    connect(registry, &PollutantRegistry::providerRenamed, this,
            [this](auto *, const QString &, const QString &) {
                refreshPollutants();
            });
}

LandUseEditorDialog::~LandUseEditorDialog() = default;

LandUseProvider *LandUseEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void LandUseEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("LandUseEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ── Left pane: list + add/delete ────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new LandUseListModel(this);
    m_listView->setModel(m_listModel);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")),
                               tr("New"), leftPane);
    m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                               tr("Delete"), leftPane);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    // ── Right pane: tabbed detail for the selected land use ─────────────────
    // Iteration 4 — the unified editor: sweeping scalars, per-pollutant
    // buildup, and per-pollutant washoff (incl. sweep/BMP efficiencies) in
    // one place.
    m_tabs = new QTabWidget(m_splitter);

    auto *formPane = new QWidget(m_tabs);
    auto *form     = new QFormLayout(formPane);

    m_nameEdit = new QLineEdit(formPane);
    form->addRow(tr("N&ame"), m_nameEdit);

    m_intervalSpin = new QDoubleSpinBox(formPane);
    m_intervalSpin->setRange(0.0, 1.0e6);
    m_intervalSpin->setDecimals(3);
    m_intervalSpin->setSingleStep(1.0);
    form->addRow(tr("&Sweep Interval (days)"), m_intervalSpin);

    m_removalSpin = new QDoubleSpinBox(formPane);
    m_removalSpin->setRange(0.0, 1.0);
    m_removalSpin->setDecimals(3);
    m_removalSpin->setSingleStep(0.05);
    form->addRow(tr("S&weep Removal (fraction)"), m_removalSpin);

    auto *sweepNote = new QLabel(
        tr("Per-pollutant sweeping removal efficiency is the Sweep Effic "
           "column on the Washoff tab."), formPane);
    sweepNote->setWordWrap(true);
    form->addRow(QString(), sweepNote);
    m_tabs->addTab(formPane, tr("General && Sweeping"));

    const auto makeGrid = [this](QAbstractTableModel *model) {
        auto *view = new QTableView(m_tabs);
        view->setModel(model);
        view->setItemDelegate(new EnumComboDelegate(view));
        view->horizontalHeader()->setSectionResizeMode(
            QHeaderView::ResizeToContents);
        view->horizontalHeader()->setStretchLastSection(true);
        view->verticalHeader()->setVisible(false);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        return view;
    };
    m_buildupModel = new BuildupTableModel(this);
    m_buildupView  = makeGrid(m_buildupModel);
    m_tabs->addTab(m_buildupView, tr("Buildup"));

    m_washoffModel = new WashoffTableModel(this);
    m_washoffView  = makeGrid(m_washoffModel);
    m_tabs->addTab(m_washoffView, tr("Washoff"));

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(m_tabs);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({ 180, 420 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LandUseEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &LandUseEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &LandUseEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &LandUseEditorDialog::onNameEdited_);
    for (QDoubleSpinBox *s : { m_intervalSpin, m_removalSpin })
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &LandUseEditorDialog::onFieldEdited_);
}

void LandUseEditorDialog::bindProvider_(LandUseProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    for (QWidget *w : { static_cast<QWidget*>(m_nameEdit),
                        static_cast<QWidget*>(m_intervalSpin),
                        static_cast<QWidget*>(m_removalSpin) })
        w->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        m_intervalSpin->setValue(p->sweepInterval());
        m_removalSpin->setValue(p->sweepRemoval());
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;

    // Point the Buildup/Washoff tables at the engine-side land use. A
    // just-created provider is materialised by onAddClicked_'s save, so
    // the index resolves; -1 (no engine / unknown) leaves the tables empty.
    void *engine = m_registry ? m_registry->engineHandle() : nullptr;
    int luIndex = -1;
    if (engine && p) {
        luIndex = swmm_landuse_index(static_cast<SWMM_Engine>(engine),
                                     p->name().toUtf8().constData());
    }
    if (m_buildupModel) m_buildupModel->bind(engine, luIndex);
    if (m_washoffModel) m_washoffModel->bind(engine, luIndex);
}

void LandUseEditorDialog::refreshPollutants()
{
    if (m_buildupModel) m_buildupModel->refresh();
    if (m_washoffModel) m_washoffModel->refresh();
}

void LandUseEditorDialog::selectProviderInList_(LandUseProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<LandUseProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
}

QString LandUseEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("LandUse%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void LandUseEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void LandUseEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    LandUseProvider *p = m_registry->create(suggestUniqueName_());
    if (!p) return;
    // Materialise in the engine right away so the Buildup/Washoff tables
    // (which edit the engine matrices directly) are live for the new row.
    m_registry->saveToEngine();
    selectProviderInList_(p);
}

void LandUseEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    // Impact-aware confirmation (iteration 4): list what the engine will
    // cascade (buildup/washoff rows) or clear (subcatchment coverages).
    const QString impact = m_registry->impactSummary(m_current);
    const QString detail = impact.isEmpty()
        ? tr("Delete land use \"%1\"?").arg(m_current->name())
        : tr("Delete land use \"%1\"?\n\nThis also removes: %2.")
              .arg(m_current->name(), impact);
    const auto answer = QMessageBox::question(
        this, tr("Delete Land Use"), detail);
    if (answer != QMessageBox::Yes) return;

    LandUseProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void LandUseEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Land Use"),
            tr("A land use named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void LandUseEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    m_current->setSweepInterval(m_intervalSpin->value());
    m_current->setSweepRemoval(m_removalSpin->value());
}

void LandUseEditorDialog::onProviderRenamed_(LandUseProvider *p,
                                             const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void LandUseEditorDialog::invokeNew()
{
    onAddClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Static entry points
// ─────────────────────────────────────────────────────────────────────────────

LandUseEditorDialog *LandUseEditorDialog::createNew(LandUseRegistry *registry,
                                                    SWMMModelLayer *layer,
                                                    QWidget *parent)
{
    auto *dlg = new LandUseEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Land Use"));
    dlg->invokeNew();
    return dlg;
}

QString LandUseEditorDialog::pickLandUse(LandUseRegistry *registry,
                                         SWMMModelLayer *layer,
                                         const QString  &initialName,
                                         QWidget        *parent)
{
    if (!registry) return {};

    LandUseEditorDialog dlg(registry, layer, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        dlg.m_mode = Mode::CreateNew;
        dlg.setWindowTitle(tr("New Land Use"));
        dlg.invokeNew();
    } else {
        dlg.setWindowTitle(tr("Edit Land Use"));
        if (auto *p = registry->findByName(initialName))
            dlg.selectProviderInList_(p);
    }
    dlg.exec();
    registry->saveToEngine();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
