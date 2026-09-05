/*!
 * \file   aquifereditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/aquifereditordialog.h"

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "pattern/patternregistry.h"
#include "ui/dialogs/patterneditordialog.h"
#include "ui/models/aquiferlistmodel.h"
#include "ui/sectionview/aquiferdiagram.h"
#include "ui/sectionview/sectionpreviewwidget.h"
#include "ui/theme/iconfactory.h"
#include "ui/uiscrollhelpers.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <initializer_list>

namespace openswmmvis::ui {

using openswmmvis::aquifer::AquiferProvider;
using openswmmvis::aquifer::AquiferRegistry;

namespace {

//! Per-parameter form spec, indexed by AquiferProvider::Param. Ranges are the
//! engine's hard bounds (fractions in [0,1]; elevations may be negative; the
//! rest ≥ 0). Unit kind selects the spin-box suffix from UnitSystem.
enum class Unit { None, Length, Rate, RainDepth };
struct FieldSpec {
    const char *label;
    const char *tip;
    double min, max, step;
    Unit unit;
};
const FieldSpec kFields[AquiferProvider::ParamCount] = {
    { QT_TR_NOOP("&Porosity"),
      QT_TR_NOOP("Volume of voids / total soil volume (volumetric fraction)."),
      0.0, 1.0, 0.01, Unit::None },
    { QT_TR_NOOP("&Wilting Point"),
      QT_TR_NOOP("Soil moisture content at which plants cannot survive (volumetric fraction)."),
      0.0, 1.0, 0.01, Unit::None },
    { QT_TR_NOOP("&Field Capacity"),
      QT_TR_NOOP("Soil moisture content after all free water has drained off (volumetric fraction)."),
      0.0, 1.0, 0.01, Unit::None },
    { QT_TR_NOOP("&Conductivity"),
      QT_TR_NOOP("Saturated hydraulic conductivity of the soil."),
      0.0, 1.0e6, 0.1, Unit::Rate },
    { QT_TR_NOOP("Conductivity &Slope"),
      QT_TR_NOOP("Average slope of log(conductivity) versus soil moisture deficit (porosity minus moisture content) curve (dimensionless)."),
      0.0, 1.0e6, 1.0, Unit::None },
    { QT_TR_NOOP("&Tension Slope"),
      QT_TR_NOOP("Average slope of soil tension versus soil moisture content curve."),
      0.0, 1.0e6, 1.0, Unit::RainDepth },
    { QT_TR_NOOP("&Upper Evap. Fraction"),
      QT_TR_NOOP("Fraction of total evaporation available for evapotranspiration in the upper unsaturated zone."),
      0.0, 1.0, 0.01, Unit::None },
    { QT_TR_NOOP("&Lower Evap. Depth"),
      QT_TR_NOOP("Maximum depth into the lower saturated zone over which evapotranspiration can occur."),
      0.0, 1.0e6, 0.1, Unit::Length },
    { QT_TR_NOOP("Lower &GW Loss Rate"),
      QT_TR_NOOP("Rate of percolation from the saturated zone to deep groundwater when the water table is at the ground surface."),
      0.0, 1.0e6, 0.01, Unit::Rate },
    { QT_TR_NOOP("&Bottom Elevation"),
      QT_TR_NOOP("Elevation of the bottom of the aquifer."),
      -1.0e6, 1.0e6, 1.0, Unit::Length },
    { QT_TR_NOOP("Water Table &Elevation"),
      QT_TR_NOOP("Elevation of the water table in the aquifer at the start of the simulation."),
      -1.0e6, 1.0e6, 1.0, Unit::Length },
    { QT_TR_NOOP("Unsaturated Zone &Moisture"),
      QT_TR_NOOP("Moisture content of the unsaturated upper zone at the start of the simulation (volumetric fraction; cannot exceed porosity)."),
      0.0, 1.0, 0.01, Unit::None },
};

QString rateLabel()
{
    auto *u = UnitSystem::instance();
    return (u && u->isSI()) ? QStringLiteral("mm/hr") : QStringLiteral("in/hr");
}

QString lengthLabel()
{
    auto *u = UnitSystem::instance();
    return u ? u->lengthLabel() : QStringLiteral("ft");
}

QString unitLabel(Unit unit)
{
    auto *u = UnitSystem::instance();
    switch (unit) {
    case Unit::None:      return {};
    case Unit::Length:    return lengthLabel();
    case Unit::Rate:      return rateLabel();
    case Unit::RainDepth: return (u && u->isSI()) ? QStringLiteral("mm")
                                                  : QStringLiteral("in");
    }
    return {};
}

} // namespace

AquiferEditorDialog::AquiferEditorDialog(AquiferRegistry *registry,
                                         SWMMModelLayer *layer,
                                         QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Aquifers"));
    resize(980, 560);
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

QDoubleSpinBox *AquiferEditorDialog::spinBox(int param) const noexcept
{
    return (param >= 0 && param < m_spins.size()) ? m_spins[param] : nullptr;
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
    m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")),
                               tr("New"), leftPane);
    m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                               tr("Delete"), leftPane);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    // ── Middle pane: name + grouped form + validation status ────────────────
    auto *formPane = new QWidget;
    auto *formLay  = new QVBoxLayout(formPane);

    auto *headForm = new QFormLayout;
    m_nameEdit = new QLineEdit(formPane);
    headForm->addRow(tr("N&ame"), m_nameEdit);
    formLay->addLayout(headForm);

    m_spins.resize(AquiferProvider::ParamCount);
    for (int k = 0; k < AquiferProvider::ParamCount; ++k) {
        const FieldSpec &fs = kFields[k];
        auto *s = new QDoubleSpinBox(formPane);
        s->setRange(fs.min, fs.max);
        s->setDecimals(4);
        s->setSingleStep(fs.step);
        s->setToolTip(tr(fs.tip));
        const QString unit = unitLabel(fs.unit);
        if (!unit.isEmpty()) s->setSuffix(QLatin1Char(' ') + unit);
        s->setMinimumWidth(OpenSWMM::Ui::kSpinMinWidthPx);
        // Focus drives which callout the diagram emphasises.
        s->installEventFilter(this);
        m_spins[k] = s;
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &AquiferEditorDialog::onFieldEdited_);
    }

    auto addGroup = [this, formPane, formLay](const QString &title,
                                              std::initializer_list<int> params)
        -> QFormLayout * {
        auto *box  = new QGroupBox(title, formPane);
        auto *form = new QFormLayout(box);
        for (int k : params) form->addRow(tr(kFields[k].label), m_spins[k]);
        formLay->addWidget(box);
        return form;
    };

    addGroup(tr("Soil"), { AquiferProvider::Porosity, AquiferProvider::WiltingPoint,
                           AquiferProvider::FieldCapacity, AquiferProvider::UpperMoisture });
    addGroup(tr("Conductivity"), { AquiferProvider::Conductivity,
                                   AquiferProvider::ConductSlope,
                                   AquiferProvider::TensionSlope });

    QFormLayout *evapForm = addGroup(tr("Evaporation"),
                                     { AquiferProvider::UpperEvapFrac,
                                       AquiferProvider::LowerEvapDepth });
    auto *patRow = new QHBoxLayout;
    m_patternCombo = new QComboBox(formPane);
    m_patternCombo->setToolTip(
        tr("Optional monthly pattern of adjustments to the upper evaporation fraction."));
    m_patternCombo->setMinimumWidth(OpenSWMM::Ui::kComboMinWidthPx);
    m_patternCombo->installEventFilter(this);
    m_patternBtn = new QPushButton(QStringLiteral("…"), formPane);
    m_patternBtn->setToolTip(tr("Create or edit patterns"));
    m_patternBtn->setFixedWidth(28);
    patRow->addWidget(m_patternCombo, 1);
    patRow->addWidget(m_patternBtn);
    evapForm->addRow(tr("Evap. Patte&rn"), patRow);

    addGroup(tr("Elevations && Losses"), { AquiferProvider::BottomElev,
                                           AquiferProvider::WaterTableElev,
                                           AquiferProvider::LowerLossCoeff });

    m_statusLabel = new QLabel(formPane);
    m_statusLabel->setObjectName(QStringLiteral("aquiferWarnings"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setVisible(false);
    formLay->addWidget(m_statusLabel);
    formLay->addStretch(1);

    // ── Right pane: two-zone groundwater illustration ───────────────────────
    m_diagram = new openswmmvis::sectionview::SectionPreviewWidget(m_splitter);
    m_diagram->setObjectName(QStringLiteral("aquiferDiagram"));
    m_diagram->setPlaceholderText(tr("Select or create an aquifer."));

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(OpenSWMM::Ui::wrapInScrollArea(formPane, m_splitter));
    m_splitter->addWidget(m_diagram);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setStretchFactor(2, 2);
    m_splitter->setSizes({ 180, 420, 380 });

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
    connect(m_nameEdit, &QLineEdit::textEdited,
            this, [this](const QString &) { refreshDiagram_(); });
    connect(m_patternCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &AquiferEditorDialog::onFieldEdited_);
    connect(m_patternBtn, &QPushButton::clicked,
            this, &AquiferEditorDialog::onPatternPickClicked_);
}

bool AquiferEditorDialog::eventFilter(QObject *watched, QEvent *event)
{
    // A focus move changes no data, only which callout is emphasised.
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
        refreshDiagram_();
    return QDialog::eventFilter(watched, event);
}

void AquiferEditorDialog::populatePatternCombo_(const QString &select)
{
    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    m_patternCombo->clear();
    m_patternCombo->addItem(tr("(none)"));

    // MONTHLY (type 0) is the only pattern kind [AQUIFERS] accepts. The
    // registry carries the engine handle it was loaded from, so the list works
    // with or without a layer (tests bind a bare registry).
    if (auto eng = static_cast<SWMM_Engine>(m_registry ? m_registry->engineHandle()
                                                        : nullptr)) {
        const int n = swmm_pattern_count(eng);
        for (int i = 0; i < n; ++i) {
            int t = -1;
            if (swmm_pattern_get_type(eng, i, &t) == SWMM_OK && t != 0) continue;
            if (const char *id = swmm_pattern_id(eng, i))
                if (*id) m_patternCombo->addItem(QString::fromUtf8(id));
        }
    }
    // Keep a name the engine does not (yet) know visible rather than
    // silently showing "(none)".
    if (!select.isEmpty() && m_patternCombo->findText(select) < 0)
        m_patternCombo->addItem(select);
    m_patternCombo->setCurrentIndex(
        select.isEmpty() ? 0 : m_patternCombo->findText(select));

    m_suppressFieldSync = prev;
}

void AquiferEditorDialog::bindProvider_(AquiferProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    m_nameEdit->setEnabled(enabled);
    for (QDoubleSpinBox *s : m_spins) s->setEnabled(enabled);
    m_patternCombo->setEnabled(enabled);
    // The "…" picker needs the layer's PatternRegistry.
    m_patternBtn->setEnabled(enabled && !m_layer.isNull());

    if (p) {
        m_nameEdit->setText(p->name());
        for (int k = 0; k < m_spins.size(); ++k)
            m_spins[k]->setValue(p->param(k));
        populatePatternCombo_(p->evapPattern());
    } else {
        m_nameEdit->clear();
        populatePatternCombo_(QString());
    }

    m_suppressFieldSync = prev;
    refreshDiagram_();
    // Binding a different aquifer is a new subject; field edits keep the view.
    if (m_diagram) m_diagram->zoomToExtents();
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
    // Plan D5 — say what the engine-side delete will clear before doing it.
    QString text = tr("Delete aquifer \"%1\"?").arg(m_current->name());
    const QString impact = m_registry->impactSummary(m_current);
    if (!impact.isEmpty()) text += QLatin1String("\n\n") + impact;
    const auto answer = QMessageBox::question(this, tr("Delete Aquifer"), text);
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
    m_current->setEvapPattern(m_patternCombo->currentIndex() > 0
                                  ? m_patternCombo->currentText() : QString());
    refreshDiagram_();
}

void AquiferEditorDialog::onPatternPickClicked_()
{
    if (!m_layer || !m_current) return;
    using openswmmvis::pattern::PatternRegistry;
    auto *reg = qobject_cast<PatternRegistry *>(m_layer->ensurePatternRegistry());
    if (!reg) return;
    const QString current = m_patternCombo->currentIndex() > 0
                                ? m_patternCombo->currentText() : QString();
    const QString chosen = PatternEditorDialog::pickPattern(
        reg, /*undoStack=*/nullptr, current, this);
    // The picker may have added or renamed patterns; rebuild the list either
    // way, then push the choice through the normal edit path.
    populatePatternCombo_(chosen.isEmpty() ? current : chosen);
    onFieldEdited_();
}

QStringList AquiferEditorDialog::validationWarnings() const
{
    QStringList w;
    if (!m_current) return w;
    const double por = m_spins[AquiferProvider::Porosity]->value();
    const double wp  = m_spins[AquiferProvider::WiltingPoint]->value();
    const double fc  = m_spins[AquiferProvider::FieldCapacity]->value();
    const double umc = m_spins[AquiferProvider::UpperMoisture]->value();
    const double ebot = m_spins[AquiferProvider::BottomElev]->value();
    const double egw  = m_spins[AquiferProvider::WaterTableElev]->value();
    if (wp > fc)   w << tr("Wilting Point exceeds Field Capacity.");
    if (fc > por)  w << tr("Field Capacity exceeds Porosity.");
    if (umc > por) w << tr("Unsaturated Zone Moisture exceeds Porosity.");
    if (egw < ebot) w << tr("Water Table Elevation is below Bottom Elevation.");
    return w;
}

int AquiferEditorDialog::activeParam_() const
{
    for (int k = 0; k < m_spins.size(); ++k)
        if (m_spins[k]->hasFocus()) return k;
    if (m_patternCombo && m_patternCombo->hasFocus())
        return AquiferProvider::UpperEvapFrac;
    return -1;
}

// Built from the widgets rather than the provider so it tracks typing before
// the value is pushed to the model (same as the LID layer diagram).
void AquiferEditorDialog::refreshDiagram_()
{
    if (!m_diagram) return;

    namespace sv = openswmmvis::sectionview;

    const QStringList warnings = validationWarnings();
    m_statusLabel->setText(warnings.join(QLatin1Char('\n')));
    m_statusLabel->setVisible(!warnings.isEmpty());

    if (!m_current) {
        m_diagram->setModel(sv::SectionDiagramModel{});
        return;
    }

    sv::AquiferDiagramInput in;
    in.name           = m_nameEdit->text();
    in.porosity       = m_spins[AquiferProvider::Porosity]->value();
    in.wiltingPoint   = m_spins[AquiferProvider::WiltingPoint]->value();
    in.fieldCapacity  = m_spins[AquiferProvider::FieldCapacity]->value();
    in.conductivity   = m_spins[AquiferProvider::Conductivity]->value();
    in.conductSlope   = m_spins[AquiferProvider::ConductSlope]->value();
    in.tensionSlope   = m_spins[AquiferProvider::TensionSlope]->value();
    in.upperEvapFrac  = m_spins[AquiferProvider::UpperEvapFrac]->value();
    in.lowerEvapDepth = m_spins[AquiferProvider::LowerEvapDepth]->value();
    in.lowerLossCoeff = m_spins[AquiferProvider::LowerLossCoeff]->value();
    in.bottomElev     = m_spins[AquiferProvider::BottomElev]->value();
    in.waterTableElev = m_spins[AquiferProvider::WaterTableElev]->value();
    in.upperMoisture  = m_spins[AquiferProvider::UpperMoisture]->value();
    in.evapPattern    = m_patternCombo->currentIndex() > 0
                            ? m_patternCombo->currentText() : QString();
    in.lengthLabel    = lengthLabel();
    in.rateLabel      = rateLabel();
    in.activeParam    = activeParam_();
    in.warnings       = warnings;

    m_diagram->setModel(sv::buildAquiferDiagram(in));
}

void AquiferEditorDialog::onProviderRenamed_(AquiferProvider *p,
                                             const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
        refreshDiagram_();
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
