/*!
 * \file   mesh2dgroundwaterdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/mesh2dgroundwaterdialog.h"

#include "core/unitsystem.h"
#include "ui/dialogs/mesh2daquifermodel.h"

#include <openswmm/engine/openswmm_gw2d.h>

#include <initializer_list>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemDelegate>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui {

namespace {

/*! Combo-box editor for a column whose value IS the combo's row index.
 *  Used for the soil law and the closure, where the model deliberately keeps
 *  index == enum code so a reorder cannot silently remap authored rows. */
class ComboDelegate : public QStyledItemDelegate
{
public:
    ComboDelegate(QStringList items, QObject *parent)
        : QStyledItemDelegate(parent), m_items(std::move(items)) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *cb = new QComboBox(parent);
        cb->addItems(m_items);
        return cb;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor))
            cb->setCurrentIndex(index.data(Qt::EditRole).toInt());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor))
            model->setData(index, cb->currentIndex(), Qt::EditRole);
    }

private:
    QStringList m_items;
};

QString optionText(SWMM_Engine e, const char *key)
{
    char buf[128] = {0};
    if (swmm_gw2d_option_get(e, key, buf, sizeof buf) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

bool setOption(SWMM_Engine e, const char *key, const QString &value)
{
    return swmm_gw2d_option_set(e, key, value.toUtf8().constData()) == SWMM_OK;
}

} // namespace

QStringList Mesh2DGroundwaterDialog::soilModelTokens()
{
    return Mesh2DAquiferModel::soilTokens();
}

QStringList Mesh2DGroundwaterDialog::closureTokens()
{
    return Mesh2DAquiferModel::closureTokens();
}

Mesh2DGroundwaterDialog::Mesh2DGroundwaterDialog(SWMM_Engine engine,
                                                 QWidget *parent,
                                                 Page initialPage,
                                                 const UnitSystem *units)
    : QDialog(parent), m_engine(engine), m_units(units)
{
    setWindowTitle(tr("2D Groundwater"));
    buildUi(initialPage);
    loadFromEngine();
}

void Mesh2DGroundwaterDialog::buildUi(Page initialPage)
{
    auto *outer = new QVBoxLayout(this);

    m_banner = new QLabel(this);
    m_banner->setWordWrap(true);
    m_banner->setFrameShape(QFrame::StyledPanel);
    m_banner->setContentsMargins(8, 6, 8, 6);
    m_banner->hide();
    outer->addWidget(m_banner);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildOptionsPage(),  tr("Options"));
    m_tabs->addTab(buildAquiferPage(),  tr("Aquifer"));
    m_tabs->addTab(buildNodeBedPage(),  tr("Node Beds"));
    m_tabs->addTab(buildStatePage(),    tr("State"));
    switch (initialPage) {
    case Page::Options:            m_tabs->setCurrentIndex(0); break;
    case Page::InitialConditions:  m_tabs->setCurrentIndex(1); break;  // hg0 lives on the rows
    case Page::State:              m_tabs->setCurrentIndex(3); break;
    default:                       m_tabs->setCurrentIndex(1); break;
    }
    outer->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
            QDialogButtonBox::Apply,
        this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        onApply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onApply);
    outer->addWidget(buttons);

    resize(880, 520);
}

// ---------------------------------------------------------------------------
// GG1 — [2D_AQUIFER_OPTIONS]
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildOptionsPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_soilCombo = new QComboBox(page);
    m_soilCombo->addItems(soilModelTokens());
    m_soilCombo->setToolTip(
        tr("Default soil characteristic law. Gardner is the only one whose "
           "quasi-steady recharge is a published closed form; the other three "
           "reproduce the right equilibrium and sign, but their relaxation "
           "rate is this engine's modelling choice. Prefer the σ column "
           "closure where the answer matters."));
    form->addRow(tr("Soil law:"), m_soilCombo);

    m_closureCombo = new QComboBox(page);
    m_closureCombo->addItems(closureTokens());
    m_closureCombo->setToolTip(
        tr("Default unsaturated-zone closure. AUTO chooses per cell from αL: "
           "below 1 the column tracks the table closely enough to be dropped "
           "(ENSLAVED), above 5 the quasi-steady assumption fails and the "
           "cell gets a real σ column."));
    form->addRow(tr("Closure:"), m_closureCombo);

    m_layersSpin = new QSpinBox(page);
    m_layersSpin->setRange(2, 128);
    m_layersSpin->setToolTip(
        tr("Layers in each σ column. Fixed for the run: the layers stretch "
           "and compress with the water table instead of being regridded."));
    form->addRow(tr("σ layers:"), m_layersSpin);

    m_capillaryCheck = new QCheckBox(tr("Include the capillary diffusion term"),
                                     page);
    m_capillaryCheck->setToolTip(
        tr("Off by default. It tightens the column's stability step "
           "considerably and changes little in most storm-scale runs."));
    form->addRow(QString(), m_capillaryCheck);

    m_cgwSpin = new QDoubleSpinBox(page);
    m_cgwSpin->setRange(0.01, 1.0);
    m_cgwSpin->setDecimals(3);
    m_cgwSpin->setSingleStep(0.05);
    m_cgwSpin->setToolTip(tr("Safety factor on the saturated zone's explicit "
                             "stability step."));
    form->addRow(tr("Saturated safety factor:"), m_cgwSpin);

    m_ccolSpin = new QDoubleSpinBox(page);
    m_ccolSpin->setRange(0.01, 1.0);
    m_ccolSpin->setDecimals(3);
    m_ccolSpin->setSingleStep(0.05);
    m_ccolSpin->setToolTip(tr("Safety factor on the unsaturated column's "
                              "explicit stability step."));
    form->addRow(tr("Column safety factor:"), m_ccolSpin);

    m_forceCfCheck = new QCheckBox(
        tr("Keep the closed form even when AUTO would switch to σ"), page);
    m_forceCfCheck->setToolTip(
        tr("For like-for-like comparisons against a closed-form run. It does "
           "not make the closed form valid at large αL — it makes it "
           "reachable."));
    form->addRow(QString(), m_forceCfCheck);

    m_dunneCheck = new QCheckBox(
        tr("Return saturation excess to the surface (Dunne)"), page);
    m_dunneCheck->setToolTip(
        tr("On by default, and it is not an option in the usual sense: with "
           "it off, water that reaches a saturated table has nowhere to go "
           "and leaves the continuity balance. Turn it off only to compare "
           "against a model that also loses it."));
    form->addRow(QString(), m_dunneCheck);

    m_modeCombo = new QComboBox(page);
    m_modeCombo->addItems({QStringLiteral("MESH"),
                           QStringLiteral("PER_SUBCATCH")});
    m_modeCombo->setToolTip(
        tr("MESH runs one aquifer per mesh cell with lateral Darcy between "
           "them. PER_SUBCATCH runs one degenerate cell per subcatchment with "
           "no lateral flux — the same kernel, as a drop-in for the legacy "
           "subcatchment aquifer."));
    form->addRow(tr("Mode:"), m_modeCombo);

    m_gwEtCombo = new QComboBox(page);
    m_gwEtCombo->addItems({QStringLiteral("NONE"),
                           QStringLiteral("CAPILLARY_RISE"),
                           QStringLiteral("BOUNDARY_ET"),
                           QStringLiteral("BOTH")});
    m_gwEtCombo->setToolTip(
        tr("CAPILLARY_RISE lets recharge run upward when the column is drier "
           "than its equilibrium. BOUNDARY_ET takes evaporation from the top "
           "of the column with a smooth Feddes stress rather than an on/off "
           "gate."));
    form->addRow(tr("Groundwater ET:"), m_gwEtCombo);

    return page;
}

// ---------------------------------------------------------------------------
// GG2 — [2D_AQUIFER]
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildAquiferPage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *hint = new QLabel(
        tr("Rows resolve least-specific first: <b>*</b> then <b>Tag</b> then "
           "<b>Cell</b>, and within one scope the lower row wins. Leave the "
           "soil law and closure at \"(model default)\" to follow the Options "
           "tab."),
        page);
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_aquifer = new Mesh2DAquiferModel(this);
    m_aquiferView = new QTableView(page);
    m_aquiferView->setModel(m_aquifer);
    m_aquiferView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_aquiferView->horizontalHeader()->setStretchLastSection(true);
    m_aquiferView->setItemDelegateForColumn(
        Mesh2DAquiferModel::ColScope,
        new ComboDelegate({tr("All cells"), tr("Tag"), tr("Cell")}, this));
    m_aquiferView->setItemDelegateForColumn(
        Mesh2DAquiferModel::ColSoil,
        new ComboDelegate(soilModelTokens(), this));
    m_aquiferView->setItemDelegateForColumn(
        Mesh2DAquiferModel::ColClosure,
        new ComboDelegate(closureTokens(), this));
    v->addWidget(m_aquiferView, 1);

    auto *row = new QHBoxLayout;
    m_addRowBtn = new QPushButton(tr("Add row"), page);
    m_delRowBtn = new QPushButton(tr("Remove row"), page);
    connect(m_addRowBtn, &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onAddAquiferRow);
    connect(m_delRowBtn, &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onRemoveAquiferRow);
    row->addWidget(m_addRowBtn);
    row->addWidget(m_delRowBtn);
    row->addStretch(1);
    v->addLayout(row);

    return page;
}

// ---------------------------------------------------------------------------
// GG3 — [2D_AQUIFER_NODE]
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildNodeBedPage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *hint = new QLabel(
        tr("Each row lets one 1D node exchange with the aquifer under one "
           "mesh cell — infiltration into a leaky pipe, or exfiltration out "
           "of a surcharged one. A node may have at most one bed."),
        page);
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_nodes = new Mesh2DAquiferNodeModel(this);
    m_nodeView = new QTableView(page);
    m_nodeView->setModel(m_nodes);
    m_nodeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeView->horizontalHeader()->setStretchLastSection(true);
    v->addWidget(m_nodeView, 1);

    auto *row = new QHBoxLayout;
    m_addBedBtn = new QPushButton(tr("Add bed"), page);
    m_delBedBtn = new QPushButton(tr("Remove bed"), page);
    connect(m_addBedBtn, &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onAddNodeBed);
    connect(m_delBedBtn, &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onRemoveNodeBed);
    row->addWidget(m_addBedBtn);
    row->addWidget(m_delBedBtn);
    row->addStretch(1);
    v->addLayout(row);

    return page;
}

// ---------------------------------------------------------------------------
// GG4 + GG6 — live state and diagnostics
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildStatePage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *hint = new QLabel(
        tr("Read-only. Everything here is in SI (metres, seconds, m³), which "
           "is what the kernel holds — it is not converted, so a number "
           "quoted from here means the same thing on any project."),
        page);
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_stateText = new QPlainTextEdit(page);
    m_stateText->setReadOnly(true);
    m_stateText->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = m_stateText->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("Monospace"));
    m_stateText->setFont(mono);
    v->addWidget(m_stateText, 1);

    auto *row = new QHBoxLayout;
    m_refreshBtn = new QPushButton(tr("Refresh"), page);
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::refreshState);
    row->addStretch(1);
    row->addWidget(m_refreshBtn);
    v->addLayout(row);

    return page;
}

// ---------------------------------------------------------------------------
// Load / apply
// ---------------------------------------------------------------------------

void Mesh2DGroundwaterDialog::loadFromEngine()
{
    if (!m_engine) {
        setEditable(false, tr("No model is open."));
        return;
    }

    const QString lengthLabel = m_units ? m_units->lengthLabel()
                                        : QStringLiteral("m");
    const bool si = lengthLabel == QStringLiteral("m");
    const QString rateLabel = si ? QStringLiteral("mm/hr")
                                 : QStringLiteral("in/hr");
    const QString areaLabel = si ? QStringLiteral("m²") : QStringLiteral("ft²");
    m_aquifer->setUnitLabels(lengthLabel, rateLabel);
    m_nodes->setUnitLabels(lengthLabel, rateLabel, areaLabel);

    auto pick = [](QComboBox *cb, const QString &token) {
        const int i = cb->findText(token, Qt::MatchFixedString);
        if (i >= 0) cb->setCurrentIndex(i);
    };
    pick(m_soilCombo,    optionText(m_engine, "SOIL_CHAR"));
    pick(m_closureCombo, optionText(m_engine, "CLOSURE"));
    pick(m_modeCombo,    optionText(m_engine, "MODE"));
    pick(m_gwEtCombo,    optionText(m_engine, "GW_ET"));
    m_layersSpin->setValue(optionText(m_engine, "M_LAYERS").toInt());
    m_capillaryCheck->setChecked(optionText(m_engine, "CAPILLARY_DIFF") ==
                                 QLatin1String("YES"));
    m_forceCfCheck->setChecked(optionText(m_engine, "FORCE_CLOSED_FORM") ==
                               QLatin1String("YES"));
    m_dunneCheck->setChecked(optionText(m_engine, "DUNNE") ==
                             QLatin1String("YES"));
    m_cgwSpin->setValue(optionText(m_engine, "C_GW").toDouble());
    m_ccolSpin->setValue(optionText(m_engine, "C_COL").toDouble());

    m_aquifer->load(m_engine);
    m_nodes->load(m_engine);
    refreshState();

    // The C API refuses authoring outside BUILDING/OPENED. Probe it once with
    // a harmless idempotent write rather than guessing from the GUI's own
    // idea of the run state — the engine is the authority, and a disabled
    // editor with a reason beats an Apply that half-succeeds.
    const QString mode = optionText(m_engine, "MODE");
    const bool editable = !mode.isEmpty() && setOption(m_engine, "MODE", mode);
    setEditable(editable,
                editable ? QString()
                         : tr("A simulation is running. The aquifer rows seed "
                              "the kernel when it starts, so they cannot be "
                              "changed now — reset the run to edit them. The "
                              "State tab still updates."));
}

void Mesh2DGroundwaterDialog::setEditable(bool on, const QString &whyNot)
{
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_soilCombo, m_closureCombo, m_layersSpin, m_capillaryCheck,
             m_cgwSpin, m_ccolSpin, m_forceCfCheck, m_dunneCheck,
             m_modeCombo, m_gwEtCombo, m_addRowBtn, m_delRowBtn,
             m_addBedBtn, m_delBedBtn})
        if (w) w->setEnabled(on);
    if (m_aquiferView)
        m_aquiferView->setEditTriggers(on ? QAbstractItemView::AllEditTriggers
                                          : QAbstractItemView::NoEditTriggers);
    if (m_nodeView)
        m_nodeView->setEditTriggers(on ? QAbstractItemView::AllEditTriggers
                                       : QAbstractItemView::NoEditTriggers);
    if (!whyNot.isEmpty()) {
        m_banner->setText(whyNot);
        m_banner->show();
    } else {
        m_banner->hide();
    }
}

QString Mesh2DGroundwaterDialog::applyOptions()
{
    if (!m_engine) return tr("No model is open.");
    struct KV { const char *key; QString value; };
    const QList<KV> kv = {
        {"SOIL_CHAR",         m_soilCombo->currentText()},
        {"CLOSURE",           m_closureCombo->currentText()},
        {"M_LAYERS",          QString::number(m_layersSpin->value())},
        {"CAPILLARY_DIFF",    m_capillaryCheck->isChecked() ? "YES" : "NO"},
        {"C_GW",              QString::number(m_cgwSpin->value())},
        {"C_COL",             QString::number(m_ccolSpin->value())},
        {"FORCE_CLOSED_FORM", m_forceCfCheck->isChecked() ? "YES" : "NO"},
        {"DUNNE",             m_dunneCheck->isChecked() ? "YES" : "NO"},
        {"MODE",              m_modeCombo->currentText()},
        {"GW_ET",             m_gwEtCombo->currentText()},
    };
    for (const KV &e : kv)
        if (!setOption(m_engine, e.key, e.value))
            return tr("The engine refused %1 = %2.")
                       .arg(QString::fromLatin1(e.key), e.value);
    return {};
}

void Mesh2DGroundwaterDialog::onApply()
{
    if (!m_engine) return;
    QString err = applyOptions();
    if (err.isEmpty()) err = m_aquifer->commit(m_engine);
    if (err.isEmpty()) err = m_nodes->commit(m_engine);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("2D Groundwater"), err);
        return;
    }
    refreshState();
}

void Mesh2DGroundwaterDialog::onAddAquiferRow()
{
    const int at = m_aquifer->appendRow();
    m_aquiferView->selectRow(at);
    m_aquiferView->scrollTo(m_aquifer->index(at, 0));
}

void Mesh2DGroundwaterDialog::onRemoveAquiferRow()
{
    const auto sel = m_aquiferView->selectionModel()->selectedRows();
    for (int i = sel.size() - 1; i >= 0; --i)
        m_aquifer->removeRow(sel.at(i).row());
}

void Mesh2DGroundwaterDialog::onAddNodeBed()
{
    const int at = m_nodes->appendRow(tr("<node>"), 0);
    m_nodeView->selectRow(at);
    m_nodeView->edit(m_nodes->index(at, Mesh2DAquiferNodeModel::ColNode));
}

void Mesh2DGroundwaterDialog::onRemoveNodeBed()
{
    const auto sel = m_nodeView->selectionModel()->selectedRows();
    for (int i = sel.size() - 1; i >= 0; --i)
        m_nodes->removeRow(sel.at(i).row());
}

void Mesh2DGroundwaterDialog::refreshState()
{
    if (!m_stateText) return;
    if (!m_engine) {
        m_stateText->setPlainText(tr("No model is open."));
        return;
    }
    int active = 0;
    swmm_gw2d_is_active(m_engine, &active);
    if (!active) {
        m_stateText->setPlainText(
            tr("The groundwater kernel is not running.\n\n"
               "It starts when a [2D_AQUIFER] row resolves onto the mesh and "
               "the simulation begins. Add a row on the Aquifer tab, then "
               "run."));
        return;
    }

    int nCells = 0, mLayers = 0;
    swmm_gw2d_get_dimensions(m_engine, &nCells, &mLayers);

    QString s;
    s += tr("Cells: %1    σ layers: %2\n\n").arg(nCells).arg(mLayers);

    double residual = 0.0;
    if (swmm_gw2d_get_continuity_error(m_engine, &residual) == SWMM_OK) {
        double storage = 0.0;
        swmm_gw2d_get_ledger(m_engine, SWMM_GW2D_LED_STORAGE, &storage);
        s += tr("Continuity residual: %1 m³")
                 .arg(residual, 0, 'e', 3);
        if (storage > 0.0)
            s += tr("   (%1 of storage)")
                     .arg(std::abs(residual) / storage, 0, 'e', 2);
        s += QStringLiteral("\n\n");
    }

    struct Term { int code; const char *label; };
    const Term terms[] = {
        {SWMM_GW2D_LED_INIT_STORAGE, "Initial storage"},
        {SWMM_GW2D_LED_STORAGE,      "Storage now"},
        {SWMM_GW2D_LED_INFIL_IN,     "Infiltration in"},
        {SWMM_GW2D_LED_RECHARGE,     "Recharge (internal)"},
        {SWMM_GW2D_LED_CAPRISE,      "Capillary rise (internal)"},
        {SWMM_GW2D_LED_LATERAL,      "Lateral Darcy in"},
        {SWMM_GW2D_LED_DEEP,         "Deep percolation out"},
        {SWMM_GW2D_LED_NODE,         "Node exchange out"},
        {SWMM_GW2D_LED_ET,           "Subsurface ET out"},
        {SWMM_GW2D_LED_DUNNE,        "Saturation excess to surface"},
    };
    s += tr("Water balance (m³, cumulative)\n");
    for (const Term &t : terms) {
        double v = 0.0;
        if (swmm_gw2d_get_ledger(m_engine, t.code, &v) != SWMM_OK) continue;
        s += QStringLiteral("  %1 %2\n")
                 .arg(QString::fromLatin1(t.label).leftJustified(32, ' '))
                 .arg(v, 14, 'g', 8);
    }

    long hist[8] = {0};
    int written = 0;
    if (swmm_gw2d_get_tier_histogram(m_engine, hist, 8, &written) == SWMM_OK &&
        written > 0) {
        s += tr("\nLTS firings per tier (tier k fires every 2^k substeps)\n");
        for (int i = 0; i < written; ++i)
            s += QStringLiteral("  tier %1: %2\n").arg(i).arg(hist[i]);
    }

    // A short census rather than every cell: a mesh has too many to read, and
    // the numbers that matter are the extremes and the closure mix.
    if (nCells > 0) {
        QVector<double> hg(nCells), tier(nCells), closure(nCells);
        int got = 0;
        const bool ok =
            swmm_gw2d_get_cell_bulk(m_engine, SWMM_GW2D_VAR_HG, hg.data(),
                                    nCells, &got) == SWMM_OK &&
            swmm_gw2d_get_cell_bulk(m_engine, SWMM_GW2D_VAR_TIER, tier.data(),
                                    nCells, &got) == SWMM_OK &&
            swmm_gw2d_get_cell_bulk(m_engine, SWMM_GW2D_VAR_CLOSURE,
                                    closure.data(), nCells, &got) == SWMM_OK;
        if (ok) {
            double lo = hg.at(0), hi = hg.at(0), sum = 0.0;
            int nEns = 0, nCf = 0, nSig = 0;
            for (int i = 0; i < nCells; ++i) {
                lo = std::min(lo, hg.at(i));
                hi = std::max(hi, hg.at(i));
                sum += hg.at(i);
                switch (static_cast<int>(closure.at(i))) {
                case SWMM_GW2D_CLOSURE_ENSLAVED: ++nEns; break;
                case SWMM_GW2D_CLOSURE_SIGMA:    ++nSig; break;
                default:                         ++nCf;  break;
                }
            }
            s += tr("\nSaturated thickness (m): min %1  mean %2  max %3\n")
                     .arg(lo, 0, 'g', 6)
                     .arg(sum / nCells, 0, 'g', 6)
                     .arg(hi, 0, 'g', 6);
            s += tr("Closures resolved: %1 ENSLAVED, %2 CLOSED_FORM, %3 SIGMA\n")
                     .arg(nEns).arg(nCf).arg(nSig);
        }
    }

    m_stateText->setPlainText(s);
}

} // namespace openswmmvis::ui
