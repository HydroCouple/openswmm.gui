/*!
 * \file   lidcontroleditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/lidcontroleditordialog.h"

#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "lid/lidcontrolprovider.h"
#include "lid/lidcontrolregistry.h"
#include "ui/models/lidcontrollistmodel.h"
#include "ui/uiscrollhelpers.h"
#include "core/unitsystem.h"
#include "ui/sectionview/lidlayerdiagram.h"
#include "ui/sectionview/sectionpreviewwidget.h"
#include "ui/theme/iconfactory.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::lid::LidControlProvider;
using openswmmvis::lid::LidControlRegistry;

namespace {
QDoubleSpinBox *makeSpin(QWidget *parent, double minV, double maxV, double step)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(minV, maxV);
    s->setDecimals(4);
    s->setSingleStep(step);
    return s;
}
} // namespace

LidControlEditorDialog::LidControlEditorDialog(LidControlRegistry *registry,
                                               SWMMModelLayer *layer,
                                               QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("LID Controls"));
    resize(680, 480);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &LidControlRegistry::providerRenamed,
                this, &LidControlEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

LidControlEditorDialog::~LidControlEditorDialog() = default;

LidControlProvider *LidControlEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void LidControlEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("LidControlEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ── Left pane ───────────────────────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new LidControlListModel(this);
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

    // ── Right pane: name + type + layer tabs ────────────────────────────────
    auto *rightPane = new QWidget(m_splitter);
    auto *rightLay  = new QVBoxLayout(rightPane);
    rightLay->setContentsMargins(0, 0, 0, 0);

    auto *headForm = new QFormLayout;
    m_nameEdit = new QLineEdit(rightPane);
    headForm->addRow(tr("N&ame"), m_nameEdit);
    m_typeCombo = new QComboBox(rightPane);
    for (const char *t : { "Bio-Retention Cell", "Rain Garden", "Green Roof",
                            "Infiltration Trench", "Permeable Pavement",
                            "Rain Barrel", "Rooftop Disconnection",
                            "Vegetative Swale" })
        m_typeCombo->addItem(QString::fromLatin1(t));
    headForm->addRow(tr("T&ype"), m_typeCombo);
    rightLay->addLayout(headForm);

    auto *tabs = new QTabWidget(rightPane);
    m_tabs = tabs;

    // Surface tab.
    auto *surf = new QWidget;  auto *surfForm = new QFormLayout(surf);
    m_surfStorage = makeSpin(surf, 0.0, 1.0e6, 0.1);
    m_surfRough   = makeSpin(surf, 0.0, 1.0,   0.01);
    m_surfSlope   = makeSpin(surf, 0.0, 100.0, 0.1);
    surfForm->addRow(tr("Sto&rage Depth"),   m_surfStorage);
    surfForm->addRow(tr("Ro&ughness (n)"),   m_surfRough);
    surfForm->addRow(tr("Slope (%)"),       m_surfSlope);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(surf, tabs), tr("&Surface"));

    // Soil tab.
    auto *soil = new QWidget;  auto *soilForm = new QFormLayout(soil);
    m_soilThick  = makeSpin(soil, 0.0, 1.0e6, 0.1);
    m_soilPoro   = makeSpin(soil, 0.0, 1.0,   0.01);
    m_soilFc     = makeSpin(soil, 0.0, 1.0,   0.01);
    m_soilWp     = makeSpin(soil, 0.0, 1.0,   0.01);
    m_soilKsat   = makeSpin(soil, 0.0, 1.0e6, 0.1);
    m_soilKslope = makeSpin(soil, 0.0, 1.0e6, 1.0);
    soilForm->addRow(tr("T&hickness"),        m_soilThick);
    soilForm->addRow(tr("&Porosity"),         m_soilPoro);
    soilForm->addRow(tr("&Field Capacity"),   m_soilFc);
    soilForm->addRow(tr("&Wilting Point"),    m_soilWp);
    soilForm->addRow(tr("&Conductivity"),     m_soilKsat);
    soilForm->addRow(tr("Conduct&ivity Slope"), m_soilKslope);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(soil, tabs), tr("S&oil"));

    // Storage tab.
    auto *stor = new QWidget;  auto *storForm = new QFormLayout(stor);
    m_storThick = makeSpin(stor, 0.0, 1.0e6, 0.1);
    m_storVoid  = makeSpin(stor, 0.0, 1.0,   0.01);
    m_storKsat  = makeSpin(stor, 0.0, 1.0e6, 0.1);
    storForm->addRow(tr("Thic&kness"),     m_storThick);
    storForm->addRow(tr("&Void Fraction"), m_storVoid);
    storForm->addRow(tr("S&eepage Rate"),  m_storKsat);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(stor, tabs), tr("S&torage"));

    // Drain tab.
    auto *drain = new QWidget;  auto *drainForm = new QFormLayout(drain);
    m_drainCoeff  = makeSpin(drain, 0.0, 1.0e6, 0.1);
    m_drainExpon  = makeSpin(drain, 0.0, 100.0, 0.1);
    m_drainOffset = makeSpin(drain, 0.0, 1.0e6, 0.1);
    drainForm->addRow(tr("Coefficient"), m_drainCoeff);
    drainForm->addRow(tr("E&xponent"),    m_drainExpon);
    drainForm->addRow(tr("Offset"),      m_drainOffset);
    tabs->addTab(OpenSWMM::Ui::wrapInScrollArea(drain, tabs), tr("&Drain"));

    rightLay->addWidget(tabs, 1);

    auto *note = new QLabel(
        tr("The engine cannot read back existing LID layer values; loaded "
           "controls show defaults. Editing overwrites all layers."),
        rightPane);
    note->setWordWrap(true);
    note->setEnabled(false);
    rightLay->addWidget(note);

    // Slice SP.6 — third pane: the layer-stack diagram. Which layers exist is
    // a function of the LID type (a rain barrel has no soil, a green roof has
    // a drainage mat instead of storage), so the drawing is the fastest way to
    // see that the type and the numbers agree.
    m_diagram = new openswmmvis::sectionview::SectionPreviewWidget(m_splitter);
    m_diagram->setObjectName(QStringLiteral("lidLayerDiagram"));
    m_diagram->setPlaceholderText(tr("Select or create a LID control."));

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(rightPane);
    m_splitter->addWidget(m_diagram);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->setSizes({ 180, 420, 360 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LidControlEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &LidControlEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &LidControlEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &LidControlEditorDialog::onNameEdited_);
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &LidControlEditorDialog::onFieldEdited_);
    // The active tab drives which layer the diagram highlights; it changes no
    // data, so it refreshes the drawing directly rather than via onFieldEdited_.
    connect(tabs, &QTabWidget::currentChanged,
            this, [this](int) { refreshLayerDiagram_(); });
    for (QDoubleSpinBox *s : { m_surfStorage, m_surfRough, m_surfSlope,
                                m_soilThick, m_soilPoro, m_soilFc, m_soilWp,
                                m_soilKsat, m_soilKslope, m_storThick, m_storVoid,
                                m_storKsat, m_drainCoeff, m_drainExpon, m_drainOffset })
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &LidControlEditorDialog::onFieldEdited_);
}

void LidControlEditorDialog::bindProvider_(LidControlProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    m_nameEdit->setEnabled(enabled);
    m_typeCombo->setEnabled(enabled);
    for (QDoubleSpinBox *s : { m_surfStorage, m_surfRough, m_surfSlope,
                                m_soilThick, m_soilPoro, m_soilFc, m_soilWp,
                                m_soilKsat, m_soilKslope, m_storThick, m_storVoid,
                                m_storKsat, m_drainCoeff, m_drainExpon, m_drainOffset })
        s->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        m_typeCombo->setCurrentIndex(p->type() >= 0 && p->type() <= 7 ? p->type() : 0);
        m_surfStorage->setValue(p->surfStorage());
        m_surfRough->setValue(p->surfRoughness());
        m_surfSlope->setValue(p->surfSlope());
        m_soilThick->setValue(p->soilThick());
        m_soilPoro->setValue(p->soilPorosity());
        m_soilFc->setValue(p->soilFc());
        m_soilWp->setValue(p->soilWp());
        m_soilKsat->setValue(p->soilKsat());
        m_soilKslope->setValue(p->soilKslope());
        m_storThick->setValue(p->storThick());
        m_storVoid->setValue(p->storVoidFrac());
        m_storKsat->setValue(p->storKsat());
        m_drainCoeff->setValue(p->drainCoeff());
        m_drainExpon->setValue(p->drainExpon());
        m_drainOffset->setValue(p->drainOffset());
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;
    refreshLayerDiagram_();
}

void LidControlEditorDialog::selectProviderInList_(LidControlProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<LidControlProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
}

QString LidControlEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("LID%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void LidControlEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void LidControlEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    LidControlProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void LidControlEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete LID Control"),
        tr("Delete LID control \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes) return;

    LidControlProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void LidControlEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename LID Control"),
            tr("A LID control named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void LidControlEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    m_current->setType(m_typeCombo->currentIndex());
    m_current->setSurfStorage(m_surfStorage->value());
    m_current->setSurfRoughness(m_surfRough->value());
    m_current->setSurfSlope(m_surfSlope->value());
    m_current->setSoilThick(m_soilThick->value());
    m_current->setSoilPorosity(m_soilPoro->value());
    m_current->setSoilFc(m_soilFc->value());
    m_current->setSoilWp(m_soilWp->value());
    m_current->setSoilKsat(m_soilKsat->value());
    m_current->setSoilKslope(m_soilKslope->value());
    m_current->setStorThick(m_storThick->value());
    m_current->setStorVoidFrac(m_storVoid->value());
    m_current->setStorKsat(m_storKsat->value());
    m_current->setDrainCoeff(m_drainCoeff->value());
    m_current->setDrainExpon(m_drainExpon->value());
    m_current->setDrainOffset(m_drainOffset->value());
    refreshLayerDiagram_();
}

// Slice SP.6 — layer-stack diagram, built from the widgets rather than the
// provider so it tracks typing before the value is pushed to the model.
void LidControlEditorDialog::refreshLayerDiagram_()
{
    if (!m_diagram) return;

    namespace sv = openswmmvis::sectionview;

    if (!m_current) {
        m_diagram->setModel(sv::SectionDiagramModel{});
        return;
    }

    sv::LidDiagramInput in;
    in.name = m_nameEdit ? m_nameEdit->text() : QString();
    in.type = static_cast<sv::LidType>(
        m_typeCombo ? qBound(0, m_typeCombo->currentIndex(), 7) : 0);

    in.surfaceStorage   = m_surfStorage->value();
    in.surfaceRoughness = m_surfRough->value();
    in.surfaceSlope     = m_surfSlope->value();
    in.soilThickness    = m_soilThick->value();
    in.soilPorosity     = m_soilPoro->value();
    in.soilConductivity = m_soilKsat->value();
    in.storageThickness = m_storThick->value();
    in.storageVoidFrac  = m_storVoid->value();
    in.storageSeepage   = m_storKsat->value();
    in.drainCoeff       = m_drainCoeff->value();
    in.drainExponent    = m_drainExpon->value();
    in.drainOffset      = m_drainOffset->value();

    if (auto *us = UnitSystem::instance())
        in.lengthLabel = us->lengthLabel();

    // Highlight the layer whose tab is in front. Tab order is fixed at build
    // time (Surface / Soil / Storage / Drain); Drain is not a layer, so it
    // highlights the lowest storage-like layer instead of nothing.
    if (m_tabs) {
        switch (m_tabs->currentIndex()) {
        case 0: in.activeLayer = sv::LidLayer::Surface; in.hasActiveLayer = true; break;
        case 1: in.activeLayer = sv::LidLayer::Soil;    in.hasActiveLayer = true; break;
        case 2: in.activeLayer = sv::LidLayer::Storage; in.hasActiveLayer = true; break;
        default: in.hasActiveLayer = false; break;
        }
    }

    m_diagram->setModel(sv::buildLidLayerDiagram(in));
}

void LidControlEditorDialog::onProviderRenamed_(LidControlProvider *p,
                                                const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void LidControlEditorDialog::invokeNew()
{
    onAddClicked_();
}

LidControlEditorDialog *LidControlEditorDialog::createNew(LidControlRegistry *registry,
                                                          SWMMModelLayer *layer,
                                                          QWidget *parent)
{
    auto *dlg = new LidControlEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New LID Control"));
    dlg->invokeNew();
    return dlg;
}

} // namespace openswmmvis::ui
