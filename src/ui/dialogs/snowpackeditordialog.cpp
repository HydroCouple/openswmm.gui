/*!
 * \file   snowpackeditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/snowpackeditordialog.h"

#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "snowpack/snowpackprovider.h"
#include "snowpack/snowpackregistry.h"
#include "ui/models/snowpacklistmodel.h"
#include "ui/theme/iconfactory.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
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

using openswmmvis::snowpack::SnowpackProvider;
using openswmmvis::snowpack::SnowpackRegistry;

namespace {
// Per-parameter form spec, ordered to match SnowpackProvider::Param.
struct FieldSpec { const char *label; double min; double max; int decimals; double step; };
const FieldSpec kFields[SnowpackProvider::ParamCount] = {
    // PLOWABLE
    { "Min. Melt Coefficient (in or mm/hr/deg)", 0.0,    1.0e6, 4, 0.01 },
    { "Max. Melt Coefficient (in or mm/hr/deg)", 0.0,    1.0e6, 4, 0.01 },
    { "Base Temperature (deg F or C)",          -1.0e6,  1.0e6, 4, 1.0  },
    { "Free Water Capacity (fraction of depth)", 0.0,    1.0,   4, 0.01 },
    { "Initial Snow Depth (in or mm)",           0.0,    1.0e6, 4, 0.1  },
    { "Initial Free Water (in or mm)",           0.0,    1.0e6, 4, 0.1  },
    { "Fraction of Impervious Area Plowable",    0.0,    1.0,   4, 0.01 },
    // IMPERVIOUS
    { "Min. Melt Coefficient (in or mm/hr/deg)", 0.0,    1.0e6, 4, 0.01 },
    { "Max. Melt Coefficient (in or mm/hr/deg)", 0.0,    1.0e6, 4, 0.01 },
    { "Base Temperature (deg F or C)",          -1.0e6,  1.0e6, 4, 1.0  },
    { "Free Water Capacity (fraction of depth)", 0.0,    1.0,   4, 0.01 },
    { "Initial Snow Depth (in or mm)",           0.0,    1.0e6, 4, 0.1  },
    { "Initial Free Water (in or mm)",           0.0,    1.0e6, 4, 0.1  },
    { "Depth at 100% Cover (in or mm)",          0.0,    1.0e6, 4, 0.1  },
    // PERVIOUS
    { "Min. Melt Coefficient (in or mm/hr/deg)", 0.0,    1.0e6, 4, 0.01 },
    { "Max. Melt Coefficient (in or mm/hr/deg)", 0.0,    1.0e6, 4, 0.01 },
    { "Base Temperature (deg F or C)",          -1.0e6,  1.0e6, 4, 1.0  },
    { "Free Water Capacity (fraction of depth)", 0.0,    1.0,   4, 0.01 },
    { "Initial Snow Depth (in or mm)",           0.0,    1.0e6, 4, 0.1  },
    { "Initial Free Water (in or mm)",           0.0,    1.0e6, 4, 0.1  },
    { "Depth at 100% Cover (in or mm)",          0.0,    1.0e6, 4, 0.1  },
    // REMOVAL
    { "Depth at Which Removal Begins (in or mm)", 0.0,   1.0e6, 4, 0.1  },
    { "Fraction Transferred Out of Watershed",    0.0,   1.0,   4, 0.01 },
    { "Fraction Transferred to Impervious Area",  0.0,   1.0,   4, 0.01 },
    { "Fraction Transferred to Pervious Area",    0.0,   1.0,   4, 0.01 },
    { "Fraction Converted to Immediate Melt",     0.0,   1.0,   4, 0.01 },
    { "Fraction Transferred to Subcatchment",     0.0,   1.0,   4, 0.01 },
};

struct GroupSpec { const char *title; int first; int count; };
const GroupSpec kGroups[] = {
    { "Plowable Snow",   SnowpackProvider::PlowableCmin,   7 },
    { "Impervious Area", SnowpackProvider::ImperviousCmin, 7 },
    { "Pervious Area",   SnowpackProvider::PerviousCmin,   7 },
    { "Snow Removal",    SnowpackProvider::RemovalDsnow,   6 },
};
} // namespace

SnowpackEditorDialog::SnowpackEditorDialog(SnowpackRegistry *registry,
                                           SWMMModelLayer *layer,
                                           QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Snow Packs"));
    resize(700, 620);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &SnowpackRegistry::providerRenamed,
                this, &SnowpackEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

SnowpackEditorDialog::~SnowpackEditorDialog() = default;

SnowpackProvider *SnowpackEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void SnowpackEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("SnowpackEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new SnowpackListModel(this);
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

    // ── Right pane: scrollable field form, one group box per engine call ────
    auto *formPane = new QWidget;
    auto *paneLay  = new QVBoxLayout(formPane);

    auto *nameForm = new QFormLayout;
    m_nameEdit = new QLineEdit(formPane);
    nameForm->addRow(tr("N&ame"), m_nameEdit);
    paneLay->addLayout(nameForm);

    m_spins.resize(SnowpackProvider::ParamCount);
    for (const GroupSpec &gs : kGroups) {
        auto *box     = new QGroupBox(tr(gs.title), formPane);
        auto *boxForm = new QFormLayout(box);
        for (int k = gs.first; k < gs.first + gs.count; ++k) {
            const FieldSpec &fs = kFields[k];
            auto *s = new QDoubleSpinBox(box);
            s->setRange(fs.min, fs.max);
            s->setDecimals(fs.decimals);
            s->setSingleStep(fs.step);
            boxForm->addRow(tr(fs.label), s);
            m_spins[k] = s;
            connect(s, &QDoubleSpinBox::valueChanged,
                    this, &SnowpackEditorDialog::onFieldEdited_);
        }
        if (gs.first == SnowpackProvider::RemovalDsnow) {
            m_removalSubcatchEdit = new QLineEdit(box);
            boxForm->addRow(tr("Destination Subcatchment"), m_removalSubcatchEdit);
        }
        paneLay->addWidget(box);
    }
    paneLay->addStretch(1);

    auto *scroll = new QScrollArea(m_splitter);
    scroll->setWidgetResizable(true);
    scroll->setWidget(formPane);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(scroll);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({ 180, 480 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SnowpackEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &SnowpackEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &SnowpackEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &SnowpackEditorDialog::onNameEdited_);
    connect(m_removalSubcatchEdit, &QLineEdit::editingFinished,
            this, &SnowpackEditorDialog::onRemovalSubcatchEdited_);
}

void SnowpackEditorDialog::bindProvider_(SnowpackProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    m_nameEdit->setEnabled(enabled);
    m_removalSubcatchEdit->setEnabled(enabled);
    for (QDoubleSpinBox *s : m_spins) s->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        m_removalSubcatchEdit->setText(p->removalSubcatch());
        for (int k = 0; k < m_spins.size(); ++k)
            m_spins[k]->setValue(p->param(k));
    } else {
        m_nameEdit->clear();
        m_removalSubcatchEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void SnowpackEditorDialog::selectProviderInList_(SnowpackProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<SnowpackProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
}

QString SnowpackEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("Snowpack%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void SnowpackEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void SnowpackEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    SnowpackProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void SnowpackEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Snow Pack"),
        tr("Delete snow pack \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes) return;

    SnowpackProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void SnowpackEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Snow Pack"),
            tr("A snow pack named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void SnowpackEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    for (int k = 0; k < m_spins.size(); ++k)
        m_current->setParam(k, m_spins[k]->value());
}

void SnowpackEditorDialog::onRemovalSubcatchEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    m_current->setRemovalSubcatch(m_removalSubcatchEdit->text().trimmed());
}

void SnowpackEditorDialog::onProviderRenamed_(SnowpackProvider *p,
                                              const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void SnowpackEditorDialog::invokeNew()
{
    onAddClicked_();
}

SnowpackEditorDialog *SnowpackEditorDialog::createNew(SnowpackRegistry *registry,
                                                      SWMMModelLayer *layer,
                                                      QWidget *parent)
{
    auto *dlg = new SnowpackEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Snow Pack"));
    dlg->invokeNew();
    return dlg;
}

} // namespace openswmmvis::ui
