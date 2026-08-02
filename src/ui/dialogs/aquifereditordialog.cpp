/*!
 * \file   aquifereditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/aquifereditordialog.h"

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "ui/models/aquiferlistmodel.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::aquifer::AquiferProvider;
using openswmmvis::aquifer::AquiferRegistry;

namespace {
// Per-parameter form spec, ordered to match AquiferProvider::Param.
struct FieldSpec { const char *label; double min; double max; int decimals; double step; };
const FieldSpec kFields[AquiferProvider::ParamCount] = {
    { "Porosity (fraction)",            0.0,    1.0,    4, 0.01 },  // Porosity
    { "Wilting Point (fraction)",       0.0,    1.0,    4, 0.01 },  // WiltingPoint
    { "Field Capacity (fraction)",      0.0,    1.0,    4, 0.01 },  // FieldCapacity
    { "Conductivity (in/hr or mm/hr)",  0.0,    1.0e6,  4, 0.1  },  // Conductivity
    { "Conductivity Slope",             0.0,    1.0e6,  4, 1.0  },  // ConductSlope
    { "Tension Slope",                  0.0,    1.0e6,  4, 1.0  },  // TensionSlope
    { "Upper Evaporation Fraction",     0.0,    1.0,    4, 0.01 },  // UpperEvapFrac
    { "Lower Evaporation Depth",        0.0,    1.0e6,  4, 0.1  },  // LowerEvapDepth
    { "Lower Loss Coefficient",         0.0,    1.0e6,  4, 0.01 },  // LowerLossCoeff
    { "Bottom Elevation",              -1.0e6,  1.0e6,  4, 1.0  },  // BottomElev
    { "Water Table Elevation",         -1.0e6,  1.0e6,  4, 1.0  },  // WaterTableElev
    { "Upper Moisture (fraction)",      0.0,    1.0,    4, 0.01 },  // UpperMoisture
};
} // namespace

AquiferEditorDialog::AquiferEditorDialog(AquiferRegistry *registry,
                                         SWMMModelLayer *layer,
                                         QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Aquifers"));
    resize(640, 520);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &AquiferRegistry::providerRenamed,
                this, &AquiferEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

AquiferEditorDialog::~AquiferEditorDialog() = default;

AquiferProvider *AquiferEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void AquiferEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("AquiferEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ── Left pane: list + add/delete ────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new AquiferListModel(this);
    m_listView->setModel(m_listModel);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("+ New"), leftPane);
    m_delBtn = new QPushButton(tr("− Delete"), leftPane);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    // ── Right pane: scrollable field form ───────────────────────────────────
    auto *formPane = new QWidget;
    auto *form     = new QFormLayout(formPane);

    m_nameEdit = new QLineEdit(formPane);
    form->addRow(tr("Na&me"), m_nameEdit);

    m_spins.reserve(AquiferProvider::ParamCount);
    for (int k = 0; k < AquiferProvider::ParamCount; ++k) {
        const FieldSpec &fs = kFields[k];
        auto *s = new QDoubleSpinBox(formPane);
        s->setRange(fs.min, fs.max);
        s->setDecimals(fs.decimals);
        s->setSingleStep(fs.step);
        form->addRow(tr(fs.label), s);
        m_spins.push_back(s);
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &AquiferEditorDialog::onFieldEdited_);
    }

    auto *scroll = new QScrollArea(m_splitter);
    scroll->setWidgetResizable(true);
    scroll->setWidget(formPane);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(scroll);
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
            this, &AquiferEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &AquiferEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &AquiferEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &AquiferEditorDialog::onNameEdited_);
}

void AquiferEditorDialog::bindProvider_(AquiferProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    m_nameEdit->setEnabled(enabled);
    for (QDoubleSpinBox *s : m_spins) s->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        for (int k = 0; k < m_spins.size(); ++k)
            m_spins[k]->setValue(p->param(k));
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void AquiferEditorDialog::selectProviderInList_(AquiferProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<AquiferProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
}

QString AquiferEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("Aquifer%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void AquiferEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void AquiferEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    AquiferProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void AquiferEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Aquifer"),
        tr("Delete aquifer \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes) return;

    AquiferProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void AquiferEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Aquifer"),
            tr("An aquifer named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void AquiferEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    for (int k = 0; k < m_spins.size(); ++k)
        m_current->setParam(k, m_spins[k]->value());
}

void AquiferEditorDialog::onProviderRenamed_(AquiferProvider *p,
                                             const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void AquiferEditorDialog::invokeNew()
{
    onAddClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Static entry points
// ─────────────────────────────────────────────────────────────────────────────

AquiferEditorDialog *AquiferEditorDialog::createNew(AquiferRegistry *registry,
                                                    SWMMModelLayer *layer,
                                                    QWidget *parent)
{
    auto *dlg = new AquiferEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Aquifer"));
    dlg->invokeNew();
    return dlg;
}

QString AquiferEditorDialog::pickAquifer(AquiferRegistry *registry,
                                         SWMMModelLayer *layer,
                                         const QString  &initialName,
                                         QWidget        *parent)
{
    if (!registry) return {};

    AquiferEditorDialog dlg(registry, layer, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        dlg.m_mode = Mode::CreateNew;
        dlg.setWindowTitle(tr("New Aquifer"));
        dlg.invokeNew();
    } else {
        dlg.setWindowTitle(tr("Edit Aquifer"));
        if (auto *p = registry->findByName(initialName))
            dlg.selectProviderInList_(p);
    }
    dlg.exec();
    registry->saveToEngine();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
