/*!
 * \file   symbologydialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/symbologydialog.h"

#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/attributecandidates.h"   // Slice CTX.1 — per-(layer,kind) attr lists
#include "render/categoricalpalette.h"    // Slice BI.3-α — Classify color assignment
#include "render/colorramp.h"             // Slice BB-α — built-in ramp catalogue
#include "render/datadefined.h"           // Slice BI Phase 8.13.43-α — DataDefinedScalar
#include "render/intervalbinner.h"        // Slice BB-α — Graduated binner
#include "ui/widgets/colorrampcombobox.h" // Slice BB-β — gradient-swatch dropdown
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <optional>

namespace openswmmvis::ui {

SymbologyDialog::SymbologyDialog(OpenSWMMVisLayer *layer, QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Layer Symbology"));
    resize(820, 540);  // wider to accommodate the kind picker pane
    m_singleColor = QColor(64, 128, 240);
    // Slice OUT.3 — both SWMMModelLayer and SWMMResultsLayer carry the
    // 11 kind sub-rows, so both can be kind-scoped.
    if (qobject_cast<SWMMModelLayer *>(layer)
        || qobject_cast<SWMMResultsLayer *>(layer))
        m_activeKind = static_cast<int>(SWMMModelLayer::CatJunctions);
    buildUi();
    readFromLayer();
}

SymbologyDialog::SymbologyDialog(OpenSWMMVisLayer *layer,
                                 int kindOrdinal,
                                 const QString &rendererId,
                                 QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Layer Symbology"));
    resize(820, 540);
    m_singleColor = QColor(64, 128, 240);
    const bool isKindLayer = qobject_cast<SWMMModelLayer *>(layer)
                          || qobject_cast<SWMMResultsLayer *>(layer);
    if (isKindLayer
        && kindOrdinal >= 0
        && kindOrdinal < SWMMModelLayer::NumCategories)
    {
        m_activeKind = kindOrdinal;
    } else if (isKindLayer) {
        m_activeKind = static_cast<int>(SWMMModelLayer::CatJunctions);
    }
    buildUi();
    readFromLayer();

    // Pre-select the right tab based on the requested renderer class.
    if (m_tabs) {
        if      (rendererId == QLatin1String("single"))      m_tabs->setCurrentIndex(0);
        else if (rendererId == QLatin1String("graduated"))   m_tabs->setCurrentIndex(1);
        else if (rendererId == QLatin1String("categorized")) m_tabs->setCurrentIndex(2);
    }
}

SymbologyDialog::~SymbologyDialog() = default;

bool SymbologyDialog::isKindScoped() const
{
    if (m_activeKind < 0) return false;
    // Slice OUT.3 — both SWMMModelLayer and SWMMResultsLayer expose
    // per-kind sub-rows and renderer slots.
    return qobject_cast<SWMMModelLayer  *>(m_layer.data()) != nullptr
        || qobject_cast<SWMMResultsLayer *>(m_layer.data()) != nullptr;
}

void SymbologyDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Horizontal split: 130-px kind picker on the left (when isKindScoped),
    // tabs on the right. Single-kind layers (GIS / mesh / raster) skip the
    // left pane entirely — backward-compatible with the prior dialog shape.
    auto *body = new QHBoxLayout;
    if (isKindScoped()) {
        buildKindPicker();
        body->addWidget(m_kindList, 0);
    }

    m_tabs = new QTabWidget(this);
    buildSingleTab(m_tabs);
    buildGraduatedTab(m_tabs);
    buildCategorizedTab(m_tabs);
    buildLabelsTab(m_tabs);
    buildArrowsTab(m_tabs);
    body->addWidget(m_tabs, 1);
    root->addLayout(body, 1);

    // Apply-to-all-kinds button — only meaningful for kind-scoped dialogs.
    auto *btnRow = new QHBoxLayout;
    if (isKindScoped()) {
        m_applyAllBtn = new QPushButton(tr("Apply to all kinds"), this);
        m_applyAllBtn->setToolTip(tr("Write the active tab's config into every kind's renderer."));
        connect(m_applyAllBtn, &QPushButton::clicked,
                this, &SymbologyDialog::onApplyToAllKindsClicked);
        btnRow->addWidget(m_applyAllBtn);
    }
    btnRow->addStretch(1);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SymbologyDialog::onApplyClicked);
    btnRow->addWidget(bb);
    root->addLayout(btnRow);

    // Slice CTX.1 — initial population must happen after every tab
    // is built (the combos exist now) but before readFromLayer() so
    // selectComboText() in the renderer-replay path hits the correct
    // candidate list.
    populateAttributeCombos();
}

void SymbologyDialog::buildKindPicker()
{
    m_kindList = new QListWidget(this);
    m_kindList->setFixedWidth(150);
    m_kindList->setAlternatingRowColors(true);

    // 11 entries + 3 non-selectable separators ("Nodes", "Links", "Areal")
    // for scannability. Display order matches SWMMModelLayer::Category.
    auto addRow = [this](const QString &label, int kind) {
        auto *it = new QListWidgetItem(label);
        it->setData(Qt::UserRole, kind);
        m_kindList->addItem(it);
    };
    auto addSep = [this](const QString &label) {
        auto *it = new QListWidgetItem(QStringLiteral("── %1 ──").arg(label));
        it->setFlags(Qt::NoItemFlags);   // non-selectable
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
        m_kindList->addItem(it);
    };

    addSep(tr("Nodes"));
    addRow(tr("Junctions"),     SWMMModelLayer::CatJunctions);
    addRow(tr("Outfalls"),      SWMMModelLayer::CatOutfalls);
    addRow(tr("Storage"),       SWMMModelLayer::CatStorage);
    addRow(tr("Dividers"),      SWMMModelLayer::CatDividers);
    addSep(tr("Links"));
    addRow(tr("Conduits"),      SWMMModelLayer::CatConduits);
    addRow(tr("Pumps"),         SWMMModelLayer::CatPumps);
    addRow(tr("Orifices"),      SWMMModelLayer::CatOrifices);
    addRow(tr("Weirs"),         SWMMModelLayer::CatWeirs);
    addRow(tr("Outlets"),       SWMMModelLayer::CatOutlets);
    addSep(tr("Areal"));
    addRow(tr("Subcatchments"), SWMMModelLayer::CatSubcatchments);
    addRow(tr("Rain Gages"),    SWMMModelLayer::CatRainGages);

    // Select the row matching m_activeKind (pre-set in the ctor).
    for (int i = 0; i < m_kindList->count(); ++i) {
        const auto *it = m_kindList->item(i);
        if ((it->flags() & Qt::ItemIsSelectable)
            && it->data(Qt::UserRole).toInt() == m_activeKind) {
            m_kindList->setCurrentRow(i);
            break;
        }
    }

    connect(m_kindList, &QListWidget::currentRowChanged,
            this, &SymbologyDialog::onKindChanged);
}

void SymbologyDialog::onKindChanged(int row)
{
    if (row < 0 || !m_kindList) return;
    const auto *it = m_kindList->item(row);
    if (!it || !(it->flags() & Qt::ItemIsSelectable)) return;   // separator
    m_activeKind = it->data(Qt::UserRole).toInt();
    // Slice CTX.1 — repopulate attribute combos for the new kind BEFORE
    // re-reading the renderer so selectComboText() in readFromLayer hits
    // the correct candidate list.
    populateAttributeCombos();
    readFromLayer();
}

namespace {

// Refill a QComboBox while preserving the user's current selection if
// it's still a candidate.
void refillComboPreservingChoice(QComboBox *combo, const QStringList &items)
{
    if (!combo) return;
    const QString prev = combo->currentText().trimmed();
    combo->clear();
    combo->addItems(items);
    if (!prev.isEmpty()) {
        const int idx = combo->findText(prev, Qt::MatchFixedString);
        if (idx >= 0) combo->setCurrentIndex(idx);
        else if (combo->isEditable()) combo->setEditText(prev);
    }
}

} // namespace

void SymbologyDialog::populateAttributeCombos()
{
    auto *swmm    = qobject_cast<SWMMModelLayer *>(m_layer.data());
    auto *results = qobject_cast<SWMMResultsLayer *>(m_layer.data());

    QStringList numeric;
    QStringList strings;

    if (swmm && isKindScoped()) {
        const auto cat = static_cast<SWMMModelLayer::Category>(m_activeKind);
        numeric = OpenSWMM::Render::AttributeCandidates::modelLayerNumeric(cat);
        strings = OpenSWMM::Render::AttributeCandidates::modelLayerString(cat);
    } else if (results) {
        // Pre-OUT.3 the dialog isn't kind-scoped on the output layer, so
        // m_activeKind stays at -1 and we expose every result variable.
        numeric = OpenSWMM::Render::AttributeCandidates::resultsLayerNumeric(m_activeKind);
        strings.clear();   // output is always numeric
    } else if (swmm) {
        // Layer-scope SWMMModelLayer fallback (no kind picker visible) —
        // unlikely path but keep something reasonable.
        numeric = {QStringLiteral("invertElev"), QStringLiteral("maxDepth"),
                   QStringLiteral("length"),     QStringLiteral("area")};
        strings = {QStringLiteral("tag"),        QStringLiteral("group")};
    }

    refillComboPreservingChoice(m_gradAttr,       numeric);
    refillComboPreservingChoice(m_singleSizeAttr, numeric);
    refillComboPreservingChoice(m_catAttr,        strings);
}

void SymbologyDialog::onApplyToAllKindsClicked()
{
    if (!isKindScoped()) return;
    for (int k = 0; k < SWMMModelLayer::NumCategories; ++k)
        applyCurrentTabToKind(k);
}

void SymbologyDialog::buildSingleTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_singleColorBtn = new QPushButton(tr("Pick…"), w);
    m_singleColorBtn->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_singleColor.name()));
    connect(m_singleColorBtn, &QPushButton::clicked, this, &SymbologyDialog::onColorClicked);
    form->addRow(tr("Colour:"), m_singleColorBtn);

    m_singleSize = new QDoubleSpinBox(w);
    m_singleSize->setRange(0.5, 50.0);
    m_singleSize->setValue(6.0);
    m_singleSize->setSuffix(tr(" px"));
    form->addRow(tr("Symbol size:"), m_singleSize);

    m_singleWidth = new QDoubleSpinBox(w);
    m_singleWidth->setRange(0.1, 10.0);
    m_singleWidth->setValue(1.2);
    m_singleWidth->setSuffix(tr(" px"));
    form->addRow(tr("Stroke width:"), m_singleWidth);

    // Slice BI Phase 8.13.43-α — data-defined size controls. Hidden by
    // default; revealed when the checkbox is toggled.
    m_singleSizeByAttr = new QCheckBox(tr("Size by attribute"), w);
    form->addRow(m_singleSizeByAttr);

    m_singleSizeAttr = new QComboBox(w);
    m_singleSizeAttr->setEditable(true);
    // Slice CTX.1 — candidates filled by populateAttributeCombos() per
    // the active layer + kind. Empty until buildUi completes.
    form->addRow(tr("Attribute:"), m_singleSizeAttr);

    auto *vRow = new QHBoxLayout;
    m_singleSizeValueMin = new QDoubleSpinBox(w);
    m_singleSizeValueMin->setRange(-1e9, 1e9);
    m_singleSizeValueMin->setValue(0.0);
    m_singleSizeValueMax = new QDoubleSpinBox(w);
    m_singleSizeValueMax->setRange(-1e9, 1e9);
    m_singleSizeValueMax->setValue(10.0);
    vRow->addWidget(m_singleSizeValueMin);
    vRow->addWidget(new QLabel(tr("→"), w));
    vRow->addWidget(m_singleSizeValueMax);
    vRow->addStretch(1);
    form->addRow(tr("Value range:"), vRow);

    auto *oRow = new QHBoxLayout;
    m_singleSizeOutMin = new QDoubleSpinBox(w);
    m_singleSizeOutMin->setRange(0.5, 100.0);
    m_singleSizeOutMin->setValue(2.0);
    m_singleSizeOutMin->setSuffix(tr(" px"));
    m_singleSizeOutMax = new QDoubleSpinBox(w);
    m_singleSizeOutMax->setRange(0.5, 100.0);
    m_singleSizeOutMax->setValue(14.0);
    m_singleSizeOutMax->setSuffix(tr(" px"));
    oRow->addWidget(m_singleSizeOutMin);
    oRow->addWidget(new QLabel(tr("→"), w));
    oRow->addWidget(m_singleSizeOutMax);
    oRow->addStretch(1);
    form->addRow(tr("Size range:"), oRow);

    m_singleSizeCurve = new QComboBox(w);
    m_singleSizeCurve->addItem(tr("Linear"), static_cast<int>(OpenSWMM::Render::DDCurve::Linear));
    m_singleSizeCurve->addItem(tr("Sqrt"),   static_cast<int>(OpenSWMM::Render::DDCurve::Sqrt));
    m_singleSizeCurve->addItem(tr("Log"),    static_cast<int>(OpenSWMM::Render::DDCurve::Log));
    form->addRow(tr("Curve:"), m_singleSizeCurve);

    // Wire the checkbox to enable/disable the four sub-controls. Default off.
    auto setDDEnabled = [this](bool on) {
        m_singleSizeAttr->setEnabled(on);
        m_singleSizeValueMin->setEnabled(on);
        m_singleSizeValueMax->setEnabled(on);
        m_singleSizeOutMin->setEnabled(on);
        m_singleSizeOutMax->setEnabled(on);
        m_singleSizeCurve->setEnabled(on);
    };
    setDDEnabled(false);
    connect(m_singleSizeByAttr, &QCheckBox::toggled, w, setDDEnabled);

    tabs->addTab(w, tr("Single symbol"));
}

void SymbologyDialog::buildGraduatedTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_gradAttr = new QComboBox(w);
    m_gradAttr->setEditable(true);   // allow arbitrary attribute keys
    // Slice CTX.1 — candidates filled by populateAttributeCombos() per
    // the active layer + kind. The hardcoded {Depth/Head/Flow/...} list
    // was wrong for static .inp layers and unhelpful for output layers.
    form->addRow(tr("Attribute:"), m_gradAttr);

    // Slice BB-β — ColorRampComboBox renders gradient swatches per item
    // and auto-populates from RasterColorRamp::builtinNames() + the user's
    // saved custom ramps in PreferencesManager. Last item is the
    // "Edit Custom Ramp…" sentinel that opens LegendEditorDialog.
    m_gradRamp = new ColorRampComboBox(w);
    m_gradRamp->setCurrentRampByName(QStringLiteral("Viridis"));
    form->addRow(tr("Ramp:"), m_gradRamp);

    m_gradClasses = new QSpinBox(w);
    m_gradClasses->setRange(2, 12);
    m_gradClasses->setValue(5);
    form->addRow(tr("Classes:"), m_gradClasses);

    auto *sizeRow = new QHBoxLayout;
    m_gradMinSize = new QDoubleSpinBox(w);
    m_gradMinSize->setRange(0.5, 50.0);
    m_gradMinSize->setValue(3.0);
    m_gradMaxSize = new QDoubleSpinBox(w);
    m_gradMaxSize->setRange(0.5, 50.0);
    m_gradMaxSize->setValue(12.0);
    sizeRow->addWidget(m_gradMinSize);
    sizeRow->addWidget(new QLabel(tr("→"), w));
    sizeRow->addWidget(m_gradMaxSize);
    sizeRow->addStretch(1);
    form->addRow(tr("Size range:"), sizeRow);

    // Slice BI Phase 8.13.43-α — Output axes (the QGIS "double encoding"
    // idiom). Color is on by default; Size off. Both can be on
    // simultaneously — the renderer writes the bin-mapped color into
    // props["color"] and the bin-mapped size into props["size"].
    auto *axesRow = new QHBoxLayout;
    m_gradOutputColor = new QCheckBox(tr("Color"), w);
    m_gradOutputColor->setChecked(true);
    m_gradOutputSize  = new QCheckBox(tr("Size"), w);
    m_gradOutputSize->setChecked(false);
    axesRow->addWidget(m_gradOutputColor);
    axesRow->addWidget(m_gradOutputSize);
    axesRow->addStretch(1);
    form->addRow(tr("Output axes:"), axesRow);

    tabs->addTab(w, tr("Graduated"));
}

void SymbologyDialog::buildCategorizedTab(QTabWidget *tabs)
{
    auto *w    = new QWidget(tabs);
    auto *root = new QVBoxLayout(w);

    // Slice BI.3-α — per-value editor body.
    //
    // Top form: attribute + colour scheme + Classify button.
    auto *form = new QFormLayout;
    m_catAttr  = new QComboBox(w);
    m_catAttr->setEditable(true);   // allow arbitrary attribute keys
    // Slice CTX.1 — candidates filled by populateAttributeCombos() per
    // the active layer + kind. Output layer leaves this empty (no
    // string-valued result variables).
    form->addRow(tr("Attribute:"), m_catAttr);

    // Slice BB-γ — populate from CategoricalPalette::builtinNames() so
    // the dropdown stays in sync with what byName() will accept. Default
    // selection = "Default" (Tab10). Plotly palettes (Plotly / D3 / G10
    // / T10 / Alphabet / Dark24 / Light24) follow.
    m_catScheme = new QComboBox(w);
    for (const QString &name : CategoricalPalette::builtinNames())
        m_catScheme->addItem(name);
    form->addRow(tr("Colour scheme:"), m_catScheme);
    root->addLayout(form);

    m_catClassifyBtn = new QPushButton(tr("Classify (auto-assign colors)"), w);
    m_catClassifyBtn->setToolTip(tr("Scan the active kind, collect unique attribute "
                                    "values, and create one category per value."));
    connect(m_catClassifyBtn, &QPushButton::clicked,
            this, &SymbologyDialog::onCategorizedClassify);
    root->addWidget(m_catClassifyBtn);

    // Table: (Color | Value | Label | Visible). Color is a per-row QPushButton
    // that opens QColorDialog. Value / Label are inline-editable. Visible is
    // a Qt::CheckStateRole on the Value cell (rendered as a checkbox).
    m_catTable = new QTableWidget(0, 4, w);
    m_catTable->setHorizontalHeaderLabels({tr("Color"),
                                            tr("Value"),
                                            tr("Label"),
                                            tr("")});
    m_catTable->horizontalHeader()->setStretchLastSection(false);
    m_catTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_catTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_catTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_catTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_catTable->verticalHeader()->setVisible(false);
    m_catTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_catTable->setEditTriggers(QAbstractItemView::DoubleClicked |
                                 QAbstractItemView::SelectedClicked |
                                 QAbstractItemView::EditKeyPressed);
    root->addWidget(m_catTable, 1);

    auto *btnRow = new QHBoxLayout;
    m_catAddBtn    = new QPushButton(tr("Add row"), w);
    m_catRemoveBtn = new QPushButton(tr("Remove row"), w);
    connect(m_catAddBtn,    &QPushButton::clicked, this, &SymbologyDialog::onCategorizedAddRow);
    connect(m_catRemoveBtn, &QPushButton::clicked, this, &SymbologyDialog::onCategorizedRemoveRow);
    btnRow->addWidget(m_catAddBtn);
    btnRow->addWidget(m_catRemoveBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    tabs->addTab(w, tr("Categorized"));
}

void SymbologyDialog::buildLabelsTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_labelEnabled = new QCheckBox(tr("Show labels"), w);
    form->addRow(m_labelEnabled);

    m_labelExpr = new QLineEdit(w);
    m_labelExpr->setPlaceholderText(QStringLiteral("$name"));
    form->addRow(tr("Expression:"), m_labelExpr);

    m_labelSize = new QDoubleSpinBox(w);
    m_labelSize->setRange(6.0, 36.0);
    m_labelSize->setValue(10.0);
    m_labelSize->setSuffix(tr(" pt"));
    form->addRow(tr("Font size:"), m_labelSize);

    m_labelHalo = new QCheckBox(tr("White halo"), w);
    m_labelHalo->setChecked(true);
    form->addRow(m_labelHalo);

    tabs->addTab(w, tr("Labels"));
}

void SymbologyDialog::buildArrowsTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_arrowEnabled = new QCheckBox(tr("Show flow-direction arrows on links"), w);
    form->addRow(m_arrowEnabled);

    m_arrowSize = new QDoubleSpinBox(w);
    m_arrowSize->setRange(2.0, 30.0);
    m_arrowSize->setValue(10.0);
    m_arrowSize->setSuffix(tr(" px"));
    form->addRow(tr("Arrow size:"), m_arrowSize);

    // Slice FX.1 — arrow colour picker (was previously not exposed).
    m_arrowColorBtn = new QPushButton(tr("Pick…"), w);
    m_arrowColorBtn->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(m_arrowColor.name()));
    connect(m_arrowColorBtn, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(
            m_arrowColor, this, tr("Arrow colour"));
        if (!c.isValid()) return;
        m_arrowColor = c;
        m_arrowColorBtn->setStyleSheet(
            QStringLiteral("background-color: %1;").arg(c.name()));
    });
    form->addRow(tr("Arrow colour:"), m_arrowColorBtn);

    // Slice FX.1 — opt-in flow-positive filter (was hardcoded ON, hiding
    // arrows pre-simulation). Now defaults OFF so arrows appear whenever
    // showArrows is enabled, regardless of sim state.
    m_arrowOnlyFlowPos = new QCheckBox(tr("Only when flow > 0"), w);
    m_arrowOnlyFlowPos->setToolTip(tr(
        "When checked, arrows hide on links with non-positive flow. "
        "Requires an open .out file (no effect pre-simulation)."));
    m_arrowOnlyFlowPos->setChecked(false);
    form->addRow(m_arrowOnlyFlowPos);

    tabs->addTab(w, tr("Arrows"));
}

void SymbologyDialog::onColorClicked()
{
    const QColor c = QColorDialog::getColor(m_singleColor, this, tr("Symbol colour"));
    if (!c.isValid()) return;
    m_singleColor = c;
    m_singleColorBtn->setStyleSheet(QStringLiteral("background-color: %1;").arg(c.name()));
}

namespace {

// Slice BI.3-α — helper to populate one (color, value, label, visible) row
// in the Categorized table. The color cell is a QPushButton that holds the
// QColor in its dynamic property "currentColor" so onCategorizedColorClicked
// can read it back without scanning a parallel storage.
QPushButton *makeCategoryColorButton(const QColor &c)
{
    auto *btn = new QPushButton;
    btn->setProperty("currentColor", c);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid #555; min-width: 32px; min-height: 18px; }")
        .arg(c.name()));
    return btn;
}

} // namespace

void SymbologyDialog::onCategorizedColorClicked()
{
    auto *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    const QColor cur = btn->property("currentColor").value<QColor>();
    const QColor next = QColorDialog::getColor(cur.isValid() ? cur : Qt::gray,
                                                this,
                                                tr("Category colour"));
    if (!next.isValid()) return;
    btn->setProperty("currentColor", next);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid #555; min-width: 32px; min-height: 18px; }")
        .arg(next.name()));
}

void SymbologyDialog::onCategorizedAddRow()
{
    if (!m_catTable) return;
    const int row = m_catTable->rowCount();
    m_catTable->insertRow(row);

    // Slice BB-γ — pick the row's default colour from the active
    // categorical palette (cycles with modulo when row >= palette size).
    const QList<QColor> palette = m_catScheme
        ? CategoricalPalette::byName(m_catScheme->currentText())
        : CategoricalPalette::palette();
    const int paletteN = palette.isEmpty() ? 1 : palette.size();
    const QColor seed = palette.isEmpty() ? CategoricalPalette::at(row)
                                          : palette.at(row % paletteN);
    auto *colorBtn = makeCategoryColorButton(seed);
    connect(colorBtn, &QPushButton::clicked,
            this, &SymbologyDialog::onCategorizedColorClicked);
    m_catTable->setCellWidget(row, 0, colorBtn);

    // Column 1 — value (editable text + visibility check on the Value cell).
    auto *valueItem = new QTableWidgetItem(QStringLiteral("value%1").arg(row + 1));
    valueItem->setFlags(valueItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
    valueItem->setCheckState(Qt::Checked);
    m_catTable->setItem(row, 1, valueItem);

    // Column 2 — label (defaults to the value).
    auto *labelItem = new QTableWidgetItem(valueItem->text());
    m_catTable->setItem(row, 2, labelItem);

    // Column 3 — placeholder (selection / drag handle could go here later).
    m_catTable->setItem(row, 3, new QTableWidgetItem(QString()));
}

void SymbologyDialog::onCategorizedRemoveRow()
{
    if (!m_catTable) return;
    const auto sel = m_catTable->selectionModel()->selectedRows();
    QList<int> rows;
    rows.reserve(sel.size());
    for (const QModelIndex &idx : sel) rows.append(idx.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());   // delete from the bottom
    for (int r : rows) m_catTable->removeRow(r);
}

void SymbologyDialog::onCategorizedClassify()
{
    if (!m_catTable || !m_catAttr) return;
    auto *swmm = qobject_cast<SWMMModelLayer *>(m_layer.data());
    if (!swmm) return;
    if (!isKindScoped()) return;
    const auto cat = static_cast<SWMMModelLayer::Category>(m_activeKind);
    const QString attr = m_catAttr->currentText().trimmed();
    if (attr.isEmpty()) return;

    // Collect unique non-empty attribute values from every feature in the
    // active kind. identifyByName is the cheapest cached path to the
    // QVariantMap. O(N) per Classify, not per frame.
    const int n = swmm->categoryCount(cat);
    QStringList ordered;
    QSet<QString> seen;
    ordered.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QString name = swmm->objectNameAt(cat, i);
        const QVariantMap attrs = swmm->identifyByName(name);
        const QString val = attrs.value(attr).toString().trimmed();
        if (val.isEmpty() || seen.contains(val)) continue;
        seen.insert(val);
        ordered.append(val);
    }

    // Slice BB-γ — Classify auto-assigns colors from the active palette
    // picked in the Colour-scheme combo (Plotly / D3 / Tab10 / ...).
    const QList<QColor> palette = m_catScheme
        ? CategoricalPalette::byName(m_catScheme->currentText())
        : CategoricalPalette::palette();
    const int paletteN = palette.isEmpty() ? 1 : palette.size();

    // Replace table contents.
    m_catTable->setRowCount(0);
    int paletteIdx = 0;
    for (const QString &val : ordered) {
        const int row = m_catTable->rowCount();
        m_catTable->insertRow(row);

        const QColor color = palette.isEmpty()
            ? CategoricalPalette::at(paletteIdx)
            : palette.at(paletteIdx % paletteN);
        ++paletteIdx;
        auto *colorBtn = makeCategoryColorButton(color);
        connect(colorBtn, &QPushButton::clicked,
                this, &SymbologyDialog::onCategorizedColorClicked);
        m_catTable->setCellWidget(row, 0, colorBtn);

        auto *valueItem = new QTableWidgetItem(val);
        valueItem->setFlags(valueItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        valueItem->setCheckState(Qt::Checked);
        m_catTable->setItem(row, 1, valueItem);

        auto *labelItem = new QTableWidgetItem(val);
        m_catTable->setItem(row, 2, labelItem);

        m_catTable->setItem(row, 3, new QTableWidgetItem(QString()));
    }
}

void SymbologyDialog::onApplyClicked()
{
    applyToLayer();
}

void SymbologyDialog::accept()
{
    applyToLayer();
    QDialog::accept();
}

namespace {

using namespace OpenSWMM::Render;

// One SimpleMarker SymbolLayer carrying colour + size + a thin stroke
// width. v1 default — any layer kind (markers, lines, fills) accepts a
// "color" / "size" / "width" lookup, so a single shape works as a
// neutral starting point. Editor for marker/line/fill stack composition
// lands in Slice BI.3.
SymbolStyle makeSimpleMarkerStyle(const QColor &c, double size, double strokeWidth)
{
    SymbolStyle style;
    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleMarker;
    sl.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
    sl.props.insert(QStringLiteral("color"), c.name(QColor::HexArgb));
    sl.props.insert(QStringLiteral("size"),  size);
    sl.props.insert(QStringLiteral("width"), strokeWidth);
    style.layers.append(sl);
    return style;
}

// Interpolates between two QColors in RGB space at t in [0,1].
QColor lerpColor(const QColor &a, const QColor &b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        a.redF()   + t * (b.redF()   - a.redF()),
        a.greenF() + t * (b.greenF() - a.greenF()),
        a.blueF()  + t * (b.blueF()  - a.blueF()));
}

// Samples `count` evenly-spaced colours from a named ramp. The ramp
// catalogue is intentionally inline — the user-facing combo strings in
// buildGraduatedTab() are the ground truth; keep both in sync.
QList<QColor> sampleRamp(const QString &name, int count)
{
    // Stops as (position, QColor) — viridis from gisrasterlayer.cpp;
    // plasma / turbo / blue-red / grayscale chosen for visual distinction.
    QList<QPair<double, QColor>> stops;
    if (name == QLatin1String("Viridis")) {
        stops = {
            {0.000, QColor( 68,   1,  84)}, {0.250, QColor( 62,  83, 137)},
            {0.500, QColor( 53, 153, 122)}, {0.750, QColor(163, 214,  63)},
            {1.000, QColor(253, 231,  37)},
        };
    } else if (name == QLatin1String("Plasma")) {
        stops = {
            {0.000, QColor( 13,   8, 135)}, {0.250, QColor(126,   3, 167)},
            {0.500, QColor(204,  71, 120)}, {0.750, QColor(248, 149,  64)},
            {1.000, QColor(240, 249,  33)},
        };
    } else if (name == QLatin1String("Turbo")) {
        stops = {
            {0.000, QColor( 48,  18,  59)}, {0.250, QColor( 40, 142, 232)},
            {0.500, QColor( 42, 218, 168)}, {0.750, QColor(241, 215,  56)},
            {1.000, QColor(122,   4,   3)},
        };
    } else if (name.startsWith(QLatin1String("Blue-Red"))) {
        stops = {
            {0.0, QColor( 33, 102, 172)}, {0.5, QColor(247, 247, 247)},
            {1.0, QColor(178,  24,  43)},
        };
    } else { // Grayscale and unknowns
        stops = { {0.0, Qt::black}, {1.0, Qt::white} };
    }

    QList<QColor> out;
    out.reserve(count);
    if (count <= 0) return out;
    if (count == 1) { out.append(stops.first().second); return out; }
    for (int i = 0; i < count; ++i) {
        const double t = double(i) / double(count - 1);
        // Find bracketing stops.
        int hi = 1;
        while (hi < stops.size() - 1 && stops[hi].first < t) ++hi;
        const auto &s0 = stops[hi - 1];
        const auto &s1 = stops[hi];
        const double span = (s1.first - s0.first);
        const double local = span > 0.0 ? (t - s0.first) / span : 0.0;
        out.append(lerpColor(s0.second, s1.second, local));
    }
    return out;
}

// Canonical attribute key used by the renderer's classifyAttribute /
// the QVariantMap passed to symbolFor(). Match the user-visible combo
// labels so the renderer JSON round-trip stays human-readable.
QString attributeKey(const QString &uiLabel)
{
    return uiLabel.toLower();
}

// Pull the first SymbolLayer's "color" prop out of a SymbolStyle. Mirrors
// the writer convention (hex string in props["color"]). Returns an
// invalid QColor if no layer in the stack advertises a colour.
QColor firstStyleColor(const SymbolStyle &style)
{
    for (const SymbolLayer &sl : style.layers) {
        const auto it = sl.props.constFind(QStringLiteral("color"));
        if (it != sl.props.constEnd()) {
            QColor c(it.value().toString());
            if (c.isValid()) return c;
        }
    }
    return {};
}

// Same shape as firstStyleColor() but for numeric properties (size,
// width). Returns std::nullopt when no layer carries the key so the
// caller can keep the dialog's default rather than overwriting with 0.
std::optional<double> firstStyleNumber(const SymbolStyle &style, const QString &key)
{
    for (const SymbolLayer &sl : style.layers) {
        const auto it = sl.props.constFind(key);
        if (it != sl.props.constEnd()) {
            bool ok = false;
            const double v = it.value().toDouble(&ok);
            if (ok) return v;
        }
    }
    return std::nullopt;
}

// Select the combo entry whose text matches `value` case-insensitively.
// No-op when the combo is empty or no entry matches — callers fall back
// to the combo's current (default) selection.
void selectComboText(QComboBox *combo, const QString &value)
{
    if (!combo || value.isEmpty()) return;
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemText(i).compare(value, Qt::CaseInsensitive) == 0) {
            combo->setCurrentIndex(i);
            return;
        }
    }
}

// The renderer interface is the same for every concrete vector layer
// type — pull the renderer pointer through whichever qobject_cast hits.
// Returns nullptr for raster / basemap layers.
const IFeatureRenderer *currentRendererOf(const OpenSWMMVisLayer *layer)
{
    if (auto *l = qobject_cast<const GISVectorLayer *>(layer))      return l->renderer();
    if (auto *l = qobject_cast<const SWMMResultsLayer *>(layer))    return l->renderer();
    if (auto *l = qobject_cast<const SWMM2DResultsLayer *>(layer))  return l->renderer();
    if (auto *l = qobject_cast<const SWMM2DMeshLayer *>(layer))     return l->renderer();
    if (auto *l = qobject_cast<const SWMMModelLayer *>(layer))      return l->renderer();
    return nullptr;
}

} // namespace

void SymbologyDialog::readFromLayer()
{
    using namespace OpenSWMM::Render;

    if (!m_layer || !m_tabs) return;

    // Slice BI-MK.1.42 — when we're kind-scoped, read from the active kind's
    // per-kind renderer (Slice 2-α slot) instead of the layer's renderer.
    // Slice OUT.3 — same path for SWMMResultsLayer per-kind slot.
    const IFeatureRenderer *r = nullptr;
    if (isKindScoped()) {
        auto *swmm    = qobject_cast<SWMMModelLayer   *>(m_layer.data());
        auto *results = qobject_cast<SWMMResultsLayer *>(m_layer.data());
        const auto cat = static_cast<SWMMModelLayer::Category>(m_activeKind);
        if (swmm)         r = swmm->kindRenderer(cat);
        else if (results) r = results->kindRenderer(cat);
        // Read the per-kind arrow state into the Arrows tab.
        if (swmm && m_arrowEnabled) {
            m_arrowEnabled->setChecked(swmm->linkArrowsEnabled(cat));
            if (m_arrowSize)
                m_arrowSize->setValue(swmm->linkArrowSize(cat));
            if (m_arrowColorBtn) {
                m_arrowColor = swmm->linkArrowColor(cat);
                m_arrowColorBtn->setStyleSheet(
                    QStringLiteral("background-color: %1;").arg(m_arrowColor.name()));
            }
            if (m_arrowOnlyFlowPos)
                m_arrowOnlyFlowPos->setChecked(swmm->linkArrowOnlyWhenFlowPos(cat));
        }
    } else {
        r = currentRendererOf(m_layer.data());
    }
    if (!r) return;

    const QString id = r->rendererId();

    if (id == QLatin1String("single")) {
        if (auto *sr = dynamic_cast<const SingleSymbolRenderer *>(r)) {
            if (const QColor c = firstStyleColor(sr->symbol()); c.isValid()) {
                m_singleColor = c;
                if (m_singleColorBtn)
                    m_singleColorBtn->setStyleSheet(
                        QStringLiteral("background-color: %1;").arg(c.name()));
            }
            if (auto v = firstStyleNumber(sr->symbol(), QStringLiteral("size")))
                m_singleSize->setValue(*v);
            if (auto v = firstStyleNumber(sr->symbol(), QStringLiteral("width")))
                m_singleWidth->setValue(*v);

            // Slice BI Phase 8.13.43-α — surface the data-defined size
            // config back into the UI so re-opening the dialog reflects
            // what the renderer actually has.
            const auto &dd = sr->sizeData();
            if (m_singleSizeByAttr) {
                const bool active = dd.isValid();
                m_singleSizeByAttr->setChecked(active);
                if (active) {
                    if (m_singleSizeAttr) {
                        int idx = m_singleSizeAttr->findText(dd.attribute);
                        if (idx >= 0) m_singleSizeAttr->setCurrentIndex(idx);
                        else m_singleSizeAttr->setEditText(dd.attribute);
                    }
                    if (m_singleSizeValueMin) m_singleSizeValueMin->setValue(dd.valueMin);
                    if (m_singleSizeValueMax) m_singleSizeValueMax->setValue(dd.valueMax);
                    if (m_singleSizeOutMin)   m_singleSizeOutMin->setValue(dd.outMin);
                    if (m_singleSizeOutMax)   m_singleSizeOutMax->setValue(dd.outMax);
                    if (m_singleSizeCurve) {
                        const int cidx = m_singleSizeCurve->findData(static_cast<int>(dd.curve));
                        if (cidx >= 0) m_singleSizeCurve->setCurrentIndex(cidx);
                    }
                }
            }
        }
        m_tabs->setCurrentIndex(0);

    } else if (id == QLatin1String("graduated")) {
        if (auto *gr = dynamic_cast<const GraduatedRenderer *>(r)) {
            selectComboText(m_gradAttr, gr->classifyAttribute());
            const int bins = gr->binCount();
            if (bins >= m_gradClasses->minimum() && bins <= m_gradClasses->maximum())
                m_gradClasses->setValue(bins);
            if (auto v = firstStyleNumber(gr->baseSymbol(), QStringLiteral("size")))
                m_gradMaxSize->setValue(*v);
            // Slice BI Phase 8.13.43-α — output-axis toggles + size range.
            if (m_gradOutputColor) m_gradOutputColor->setChecked(gr->outputColorEnabled());
            if (m_gradOutputSize)  m_gradOutputSize->setChecked(gr->outputSizeEnabled());
            if (m_gradMinSize)     m_gradMinSize->setValue(gr->outputSizeMin());
            if (m_gradMaxSize)     m_gradMaxSize->setValue(gr->outputSizeMax());
        }
        m_tabs->setCurrentIndex(1);

    } else if (id == QLatin1String("categorized")) {
        if (auto *cr = dynamic_cast<const CategorizedRenderer *>(r)) {
            selectComboText(m_catAttr, cr->classifyAttribute());

            // Slice BI.3-α — repopulate the table from the renderer's
            // current categories so re-opening the dialog reflects what
            // the renderer actually has.
            if (m_catTable) {
                m_catTable->setRowCount(0);
                for (const auto &c : cr->categories()) {
                    const int row = m_catTable->rowCount();
                    m_catTable->insertRow(row);

                    QColor color = firstStyleColor(c.symbol);
                    if (!color.isValid()) color = CategoricalPalette::at(row);
                    auto *colorBtn = makeCategoryColorButton(color);
                    connect(colorBtn, &QPushButton::clicked,
                            this, &SymbologyDialog::onCategorizedColorClicked);
                    m_catTable->setCellWidget(row, 0, colorBtn);

                    auto *valueItem = new QTableWidgetItem(c.value);
                    valueItem->setFlags(valueItem->flags()
                                        | Qt::ItemIsUserCheckable
                                        | Qt::ItemIsEditable);
                    valueItem->setCheckState(Qt::Checked);
                    m_catTable->setItem(row, 1, valueItem);

                    auto *labelItem = new QTableWidgetItem(
                        c.label.isEmpty() ? c.value : c.label);
                    m_catTable->setItem(row, 2, labelItem);

                    m_catTable->setItem(row, 3, new QTableWidgetItem(QString()));
                }
            }
        }
        m_tabs->setCurrentIndex(2);
    }
    // Other rendererId() values (e.g. "rule") fall through — the dialog
    // doesn't have a tab for those yet, so leave the default tab visible.
}

// Slice BI-MK.1.42 — build a renderer from the current tab's form fields.
// Returns nullptr when the active tab is Labels/Arrows (not a renderer
// class) so the caller can short-circuit.
//
// Slice BI.3-α — Categorized tab now reads from a QTableWidget of
// (color, value, label, visible) rows; the table is owned by the dialog
// and passed in so this helper stays free of dialog-state coupling.
// Slice BI Phase 8.13.43-α — additional `singleSizeData` carries the
// data-defined-size config for the Single tab (invalid → no DD output),
// `gradOutputColor` / `gradOutputSize` / `gradSizeMin` / `gradSizeMax`
// carry the Graduated tab's output-axis toggles.
struct RenderConfig
{
    QColor                              singleColor;
    double                              singleSize  = 6.0;
    double                              singleWidth = 1.2;
    OpenSWMM::Render::DataDefinedScalar singleSizeData;   // invalid by default
    QString                             gradAttr;
    RasterColorRamp                     gradRamp = RasterColorRamp::viridis();  // Slice BB-β — full ramp value
    int                                 gradClasses = 5;
    double                              gradMaxSize = 12.0;
    bool                                gradOutputColor = true;
    bool                                gradOutputSize  = false;
    double                              gradSizeMin = 2.0;
    double                              gradSizeMax = 14.0;
    QString                             catAttr;
    const QTableWidget                 *catTable = nullptr;
};

static std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>
buildRendererFromTabs(int activeTabIdx, const RenderConfig &cfg)
{
    using namespace OpenSWMM::Render;
    switch (activeTabIdx) {
    case 0: {
        auto r = std::make_unique<SingleSymbolRenderer>();
        SymbolStyle style;
        SymbolLayer sl;
        sl.kind = SymbolLayerKind::SimpleMarker;
        sl.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
        sl.props.insert(QStringLiteral("color"), cfg.singleColor.name(QColor::HexArgb));
        sl.props.insert(QStringLiteral("size"),  cfg.singleSize);
        sl.props.insert(QStringLiteral("width"), cfg.singleWidth);
        style.layers.append(sl);
        r->setSymbol(std::move(style));
        // Slice BI Phase 8.13.43-α — data-defined size override.
        if (cfg.singleSizeData.isValid())
            r->setSizeData(cfg.singleSizeData);
        return r;
    }
    case 1: {
        auto r = std::make_unique<GraduatedRenderer>();
        r->setClassifyAttribute(cfg.gradAttr.toLower());
        r->setRamp(cfg.gradRamp);
        IntervalBinner b;
        b.setMethod(BinMethod::EqualInterval);
        b.setBinCount(cfg.gradClasses);
        r->setBinner(b);
        // Base symbol — Graduated overrides color (and optionally size)
        // per bin via the renderer; the template's stroke width still
        // applies.
        SymbolStyle base;
        SymbolLayer sl;
        sl.kind = SymbolLayerKind::SimpleMarker;
        sl.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
        sl.props.insert(QStringLiteral("color"), cfg.singleColor.name(QColor::HexArgb));
        sl.props.insert(QStringLiteral("size"),  cfg.gradMaxSize);
        sl.props.insert(QStringLiteral("width"), cfg.singleWidth);
        base.layers.append(sl);
        r->setBaseSymbol(std::move(base));
        // Slice BI Phase 8.13.43-α — output-axis toggles.
        r->setOutputColorEnabled(cfg.gradOutputColor);
        r->setOutputSizeEnabled(cfg.gradOutputSize);
        r->setOutputSizeRange(cfg.gradSizeMin, cfg.gradSizeMax);
        return r;
    }
    case 2: {
        auto r = std::make_unique<CategorizedRenderer>();
        r->setClassifyAttribute(cfg.catAttr.toLower());

        // Build a Category per visible row in the editor table.
        QList<CategorizedRenderer::Category> cats;
        if (cfg.catTable) {
            for (int row = 0; row < cfg.catTable->rowCount(); ++row) {
                auto *colorBtn  = qobject_cast<QPushButton *>(cfg.catTable->cellWidget(row, 0));
                auto *valueItem = cfg.catTable->item(row, 1);
                auto *labelItem = cfg.catTable->item(row, 2);
                if (!valueItem) continue;
                if (valueItem->checkState() != Qt::Checked) continue;
                const QString value = valueItem->text().trimmed();
                if (value.isEmpty()) continue;

                const QColor color = colorBtn
                    ? colorBtn->property("currentColor").value<QColor>()
                    : CategoricalPalette::at(row);

                SymbolStyle s;
                SymbolLayer sl;
                sl.kind = SymbolLayerKind::SimpleMarker;
                sl.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
                sl.props.insert(QStringLiteral("color"), color.name(QColor::HexArgb));
                sl.props.insert(QStringLiteral("size"),  cfg.singleSize);
                sl.props.insert(QStringLiteral("width"), cfg.singleWidth);
                s.layers.append(sl);

                CategorizedRenderer::Category c;
                c.value  = value;
                c.label  = labelItem ? labelItem->text() : value;
                c.symbol = std::move(s);
                cats.append(std::move(c));
            }
        }
        r->setCategories(cats);

        // Fallback symbol.
        SymbolStyle fb;
        SymbolLayer slf;
        slf.kind = SymbolLayerKind::SimpleMarker;
        slf.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
        slf.props.insert(QStringLiteral("color"), cfg.singleColor.name(QColor::HexArgb));
        slf.props.insert(QStringLiteral("size"),  cfg.singleSize);
        slf.props.insert(QStringLiteral("width"), cfg.singleWidth);
        fb.layers.append(slf);
        r->setFallbackSymbol(std::move(fb));
        return r;
    }
    default:
        return nullptr;   // Labels / Arrows tabs aren't renderer classes
    }
}

void SymbologyDialog::applyCurrentTabToKind(int kindOrdinal)
{
    if (!m_layer || !m_tabs) return;
    auto *swmm    = qobject_cast<SWMMModelLayer   *>(m_layer.data());
    auto *results = qobject_cast<SWMMResultsLayer *>(m_layer.data());
    if (!swmm && !results) return;
    if (kindOrdinal < 0 || kindOrdinal >= SWMMModelLayer::NumCategories) return;
    const auto cat = static_cast<SWMMModelLayer::Category>(kindOrdinal);

    const int tabIdx = m_tabs->currentIndex();

    // Arrows tab — writes through to the per-kind arrow toggle; doesn't
    // construct a renderer. Output layer skips this (no static arrows).
    // Slice FX.1 — also propagates size / color / flow-positive filter.
    if (tabIdx == 4 && m_arrowEnabled) {
        if (swmm) {
            swmm->setLinkArrowsEnabled(cat, m_arrowEnabled->isChecked());
            if (m_arrowSize)
                swmm->setLinkArrowSize(cat, m_arrowSize->value());
            if (m_arrowColor.isValid())
                swmm->setLinkArrowColor(cat, m_arrowColor);
            if (m_arrowOnlyFlowPos)
                swmm->setLinkArrowOnlyWhenFlowPos(cat, m_arrowOnlyFlowPos->isChecked());
        }
        return;
    }

    RenderConfig cfg;
    cfg.singleColor = m_singleColor;
    cfg.singleSize  = m_singleSize->value();
    cfg.singleWidth = m_singleWidth->value();
    if (m_singleSizeByAttr && m_singleSizeByAttr->isChecked()) {
        OpenSWMM::Render::DataDefinedScalar d;
        d.attribute = m_singleSizeAttr->currentText().trimmed();
        d.valueMin  = m_singleSizeValueMin->value();
        d.valueMax  = m_singleSizeValueMax->value();
        d.outMin    = m_singleSizeOutMin->value();
        d.outMax    = m_singleSizeOutMax->value();
        const int cv = m_singleSizeCurve->currentData().toInt();
        d.curve = (cv == 1) ? OpenSWMM::Render::DDCurve::Sqrt
                : (cv == 2) ? OpenSWMM::Render::DDCurve::Log
                            : OpenSWMM::Render::DDCurve::Linear;
        cfg.singleSizeData = d;
    }
    cfg.gradAttr     = m_gradAttr->currentText();
    cfg.gradRamp     = m_gradRamp->currentRamp();
    cfg.gradClasses  = m_gradClasses->value();
    cfg.gradMaxSize  = m_gradMaxSize->value();
    cfg.gradOutputColor = m_gradOutputColor ? m_gradOutputColor->isChecked() : true;
    cfg.gradOutputSize  = m_gradOutputSize  ? m_gradOutputSize->isChecked()  : false;
    cfg.gradSizeMin  = m_gradMinSize->value();
    cfg.gradSizeMax  = m_gradMaxSize->value();
    cfg.catAttr      = m_catAttr->currentText();
    cfg.catTable     = m_catTable;

    auto next = buildRendererFromTabs(tabIdx, cfg);
    if (!next) return;
    if (swmm)         swmm->setKindRenderer(cat, std::move(next));
    else if (results) results->setKindRenderer(cat, std::move(next));
}

void SymbologyDialog::applyToLayer()
{
    if (!m_layer || !m_tabs) return;

    // Slice BI-MK.1.42 — kind-scoped dialog routes through the active
    // kind's per-kind renderer slot instead of replacing the layer's
    // renderer wholesale.
    if (isKindScoped()) {
        applyCurrentTabToKind(m_activeKind);
        return;
    }

    RenderConfig cfg;
    cfg.singleColor = m_singleColor;
    cfg.singleSize  = m_singleSize->value();
    cfg.singleWidth = m_singleWidth->value();
    if (m_singleSizeByAttr && m_singleSizeByAttr->isChecked()) {
        OpenSWMM::Render::DataDefinedScalar d;
        d.attribute = m_singleSizeAttr->currentText().trimmed();
        d.valueMin  = m_singleSizeValueMin->value();
        d.valueMax  = m_singleSizeValueMax->value();
        d.outMin    = m_singleSizeOutMin->value();
        d.outMax    = m_singleSizeOutMax->value();
        const int cv = m_singleSizeCurve->currentData().toInt();
        d.curve = (cv == 1) ? OpenSWMM::Render::DDCurve::Sqrt
                : (cv == 2) ? OpenSWMM::Render::DDCurve::Log
                            : OpenSWMM::Render::DDCurve::Linear;
        cfg.singleSizeData = d;
    }
    cfg.gradAttr     = m_gradAttr->currentText();
    cfg.gradRamp     = m_gradRamp->currentRamp();
    cfg.gradClasses  = m_gradClasses->value();
    cfg.gradMaxSize  = m_gradMaxSize->value();
    cfg.gradOutputColor = m_gradOutputColor ? m_gradOutputColor->isChecked() : true;
    cfg.gradOutputSize  = m_gradOutputSize  ? m_gradOutputSize->isChecked()  : false;
    cfg.gradSizeMin  = m_gradMinSize->value();
    cfg.gradSizeMax  = m_gradMaxSize->value();
    cfg.catAttr      = m_catAttr->currentText();
    cfg.catTable     = m_catTable;

    auto renderer = buildRendererFromTabs(m_tabs->currentIndex(), cfg);
    if (!renderer) return;

    // Each concrete layer owns its own typed setRenderer(unique_ptr<...>).
    // Raster / basemap layers don't carry an IFeatureRenderer, so they
    // fall through unchanged.
    if (auto *l = qobject_cast<GISVectorLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMMResultsLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMM2DResultsLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMM2DMeshLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMMModelLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    }
}

} // namespace openswmmvis::ui
