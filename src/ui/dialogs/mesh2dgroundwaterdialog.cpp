/*!
 * \file   mesh2dgroundwaterdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/mesh2dgroundwaterdialog.h"
#include "ui/hydrothermalstate.h"

#include "core/unitsystem.h"
#include "ui/dialogs/mesh2daquifermodel.h"

#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_model.h>

#include <initializer_list>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QUndoCommand>
#include <QApplication>
#include <QItemSelectionModel>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemDelegate>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QScrollArea>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <QPushButton>
#include <QPointer>
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
    ComboDelegate(QStringList items, QObject *parent, bool inherited=false)
        : QStyledItemDelegate(parent), m_items(std::move(items)), m_inherited(inherited) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *cb = new QComboBox(parent);
        if(m_inherited)cb->addItem(tr("(model default)"),-1);
        for(int i=0;i<m_items.size();++i)cb->addItem(m_items[i],i);
        return cb;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor))
            cb->setCurrentIndex(cb->findData(index.data(Qt::EditRole).toInt()));
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor))
            model->setData(index, cb->currentData().toInt(), Qt::EditRole);
    }

private:
    QStringList m_items;
    bool m_inherited=false;
};

class NodeConnectionUndo final:public QUndoCommand {
    std::shared_ptr<Mesh2DAquiferNodeModel> draft_;SWMM_Engine engine_;
    std::function<bool()> current_;std::function<void()> edited_;bool first_=true;
    void install(bool forward) {
        QString error;
        if(!current_||!current_())error=QObject::tr("The model or mesh changed after this connection edit.");
        else error=forward?draft_->commit(engine_,false):draft_->restoreLoaded(engine_);
        if(!error.isEmpty()){setObsolete(true);QMessageBox::warning(QApplication::activeWindow(),QObject::tr("LID bottom connection"),error);return;}
        if(edited_)edited_();
    }
public:
    NodeConnectionUndo(std::shared_ptr<Mesh2DAquiferNodeModel> draft,SWMM_Engine engine,
        std::function<bool()> current,std::function<void()> edited):draft_(std::move(draft)),engine_(engine),current_(std::move(current)),edited_(std::move(edited)) {setText(QObject::tr("Edit node bottom connections"));}
    void undo()override{install(false);}void redo()override{if(first_){first_=false;return;}install(true);}
};

class BoundaryUndo final:public QUndoCommand {
    std::shared_ptr<Mesh2DAquiferBoundaryModel> draft_;SWMM_Engine engine_;
    std::function<bool()> current_;std::function<void()> edited_;bool first_=true;
    void install(bool forward) {
        QString error;
        if(!current_||!current_())error=QObject::tr("The model or mesh changed after this boundary edit.");
        else error=forward?draft_->commit(engine_,false):draft_->restoreLoaded(engine_);
        if(!error.isEmpty()){setObsolete(true);QMessageBox::warning(QApplication::activeWindow(),QObject::tr("Groundwater boundaries"),error);return;}
        if(edited_)edited_();
    }
public:
    BoundaryUndo(std::shared_ptr<Mesh2DAquiferBoundaryModel> draft,SWMM_Engine engine,
        std::function<bool()> current,std::function<void()> edited):draft_(std::move(draft)),engine_(engine),current_(std::move(current)),edited_(std::move(edited)){setText(QObject::tr("Edit groundwater boundaries"));}
    void undo()override{install(false);}void redo()override{if(first_){first_=false;return;}install(true);}
};

QString optionText(SWMM_Engine e, const char *key)
{
    char buf[128] = {0};
    const QString name=QString::fromUtf8(key);
    const int result=name.startsWith("GW_TRANSPORT:") ? swmm_gw_transport_option_get(e,name.mid(13).toUtf8().constData(),buf,sizeof buf) : swmm_gw2d_option_get(e,key,buf,sizeof buf);
    if (result != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

bool setOption(SWMM_Engine e, const char *key, const QString &value)
{
    const QString name=QString::fromUtf8(key);
    return (name.startsWith("GW_TRANSPORT:") ? swmm_gw_transport_option_set(e,name.mid(13).toUtf8().constData(),value.toUtf8().constData()) : swmm_gw2d_option_set(e,key,value.toUtf8().constData()))==SWMM_OK;
}

bool canEdit(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return swmm_engine_get_state(engine, &state) == SWMM_OK &&
        (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

bool supportsPorousThermalOptions(SWMM_Engine engine)
{
    if (!engine) return false;
    char value[64]{};
    const int result = swmm_gw_transport_option_get(
        engine, "THERMAL_MODEL", value, sizeof value);
    return porousSoilHeatOptionIsSupported(
        result == SWMM_OK, QString::fromUtf8(value).trimmed());
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

QStringList Mesh2DGroundwaterDialog::configurationOptionKeys()
{
    QStringList keys = {QStringLiteral("SOIL_CHAR"), QStringLiteral("CLOSURE"),
        QStringLiteral("M_LAYERS"), QStringLiteral("CAPILLARY_DIFF"),
        QStringLiteral("C_GW"), QStringLiteral("C_COL"),
        QStringLiteral("FORCE_CLOSED_FORM"), QStringLiteral("DUNNE"),
        QStringLiteral("MODE"), QStringLiteral("GW_ET")};
    const QStringList transportKeys = {QStringLiteral("UNSAT_TRANSPORT"),QStringLiteral("TRANSPORT_POLLUTANTS"),
        QStringLiteral("TRANSPORT_MSX"), QStringLiteral("TRANSPORT_AGE"),
        QStringLiteral("TRANSPORT_TEMPERATURE"), QStringLiteral("DISPERSION"),
        QStringLiteral("THERMAL_MODEL"), QStringLiteral("CONDUCTION"),
        QStringLiteral("THERMAL_MIXING"), QStringLiteral("LATERAL_CONDUCTION"),
        QStringLiteral("SOIL_ET_HEAT"), QStringLiteral("THERMAL_LAYERS"),
        QStringLiteral("THERMAL_MAX_STEP"), QStringLiteral("WATER_THERMAL_CONDUCTIVITY"),
        QStringLiteral("AIR_THERMAL_CONDUCTIVITY"), QStringLiteral("SURFACE_THERMAL_RESISTANCE"),
        QStringLiteral("C_DIFF"), QStringLiteral("SURFACE_THERMAL_BC"),
        QStringLiteral("DEEP_THERMAL_BC")};
    for (const auto &key : transportKeys) keys << QStringLiteral("GW_TRANSPORT:") + key;
    return keys;
}

void Mesh2DGroundwaterDialog::setThermalOptionsOnly(bool enabled)
{
    if (!m_tabs) return;
    m_tabs->setTabVisible(1, !enabled);
    m_tabs->setTabVisible(2, !enabled);
    m_tabs->setTabVisible(3, !enabled);
    m_tabs->setTabVisible(5, !enabled);
}

Mesh2DGroundwaterDialog::Mesh2DGroundwaterDialog(SWMM_Engine engine,
                                                 QWidget *parent,
                                                 Page initialPage,
                                                 const UnitSystem *units)
    : QDialog(parent), m_engine(engine), m_units(units),
      m_porousThermalOptionsSupported(supportsPorousThermalOptions(engine))
{
    setObjectName(QStringLiteral("Mesh2DGroundwaterDialog"));
    setWindowTitle(tr("2D Groundwater"));
    buildUi(initialPage);
    loadFromEngine();
    if(parent){if(parent->metaObject()->indexOfSignal("engineAboutToClose()")>=0)connect(parent,SIGNAL(engineAboutToClose()),this,SLOT(invalidateContext()));connect(parent,&QObject::destroyed,this,&Mesh2DGroundwaterDialog::invalidateContext);}
    if(units){connect(units,&UnitSystem::unitsChanged,this,&Mesh2DGroundwaterDialog::invalidateContext);connect(units,&QObject::destroyed,this,&Mesh2DGroundwaterDialog::invalidateContext);}
}
void Mesh2DGroundwaterDialog::invalidateContext(){m_engine=nullptr;m_editable=false;setEnabled(false);QDialog::reject();}

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
    m_tabs->addTab(buildTransportPage(), tr("Transport and thermal settings"));
    m_tabs->addTab(buildBoundaryPage(),tr("Edge boundaries"));
    switch (initialPage) {
    case Page::Options:            m_tabs->setCurrentIndex(0); break;
    case Page::InitialConditions:  m_tabs->setCurrentIndex(1); break;  // hg0 lives on the rows
    case Page::State:              m_tabs->setCurrentIndex(3); break;
    case Page::NodeBeds:           m_tabs->setCurrentIndex(2); break;
    case Page::EdgeBoundaries:     m_tabs->setCurrentIndex(5); break;
    default:                       m_tabs->setCurrentIndex(1); break;
    }
    outer->addWidget(m_tabs, 1);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
            QDialogButtonBox::Apply,
        this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (applyChanges()) accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onApply);
    outer->addWidget(m_buttons);

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
    form->addRow(tr("&Soil law:"), m_soilCombo);

    m_closureCombo = new QComboBox(page);
    m_closureCombo->addItems(closureTokens());
    m_closureCombo->setToolTip(
        tr("Default unsaturated-zone closure. AUTO chooses per cell from αL: "
           "below 1 the column tracks the table closely enough to be dropped "
           "(ENSLAVED), above 5 the quasi-steady assumption fails and the "
           "cell gets a real σ column."));
    form->addRow(tr("&Closure:"), m_closureCombo);

    m_layersSpin = new QSpinBox(page);
    m_layersSpin->setRange(2, 128);
    m_layersSpin->setToolTip(
        tr("Layers in each σ column. Fixed for the run: the layers stretch "
           "and compress with the water table instead of being regridded."));
    form->addRow(tr("σ &layers:"), m_layersSpin);

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
    form->addRow(tr("Saturated safety &factor:"), m_cgwSpin);

    m_ccolSpin = new QDoubleSpinBox(page);
    m_ccolSpin->setRange(0.01, 1.0);
    m_ccolSpin->setDecimals(3);
    m_ccolSpin->setSingleStep(0.05);
    m_ccolSpin->setToolTip(tr("Safety factor on the unsaturated column's "
                              "explicit stability step."));
    form->addRow(tr("Column safet&y factor:"), m_ccolSpin);

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
    form->addRow(tr("&Mode:"), m_modeCombo);

    m_gwEtCombo = new QComboBox(page);
    m_gwEtCombo->setObjectName("gwDetailedEt");
    m_gwEtCombo->addItem(tr("Automatic"),"AUTO");
    m_gwEtCombo->addItem(tr("None"),"NONE");
    m_gwEtCombo->addItem(tr("Capillary rise"),"CAPILLARY_RISE");
    m_gwEtCombo->addItem(tr("Boundary ET"),"BOUNDARY_ET");
    m_gwEtCombo->addItem(tr("Both"),"BOTH");
    m_gwEtCombo->setToolTip(
        tr("CAPILLARY_RISE lets recharge run upward when the column is drier "
           "than its equilibrium. BOUNDARY_ET takes evaporation from the top "
           "of the column with a smooth Feddes stress rather than an on/off "
           "gate."));
    form->addRow(tr("Groundwater &ET:"), m_gwEtCombo);
    auto* etStatus=new QLabel(page);etStatus->setWordWrap(true);etStatus->setObjectName("gwDetailedEtStatus");
    form->addRow(etStatus);
    auto refreshEt=[this,etStatus]{
        char configured[64]{},enabled[64]{};
        if(m_engine){swmm_gw2d_option_get(m_engine,"CONFIGURED",configured,sizeof configured);swmm_options_get_ext(m_engine,"GROUNDWATER",enabled,sizeof enabled);}
        etStatus->setText(groundwaterEtState(m_gwEtCombo->currentData().toString(),m_modeCombo->currentText(),QString::fromUtf8(enabled)!="NO",QString::fromUtf8(configured)=="YES").reason);
    };
    connect(m_gwEtCombo,&QComboBox::currentIndexChanged,this,refreshEt);
    connect(m_modeCombo,&QComboBox::currentIndexChanged,this,refreshEt);
    refreshEt();

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
    m_aquiferView->setAccessibleName(tr("Aquifer properties and initial conditions"));
    m_aquiferView->setAccessibleDescription(
        tr("Select a row with the arrow keys. Press F2 to edit the current cell."));
    m_aquiferView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_aquiferView->horizontalHeader()->setStretchLastSection(false);
    m_aquiferView->setItemDelegateForColumn(
        Mesh2DAquiferModel::ColScope,
        new ComboDelegate({tr("All cells"), tr("Tag"), tr("Cell")}, this));
    m_aquiferView->setItemDelegateForColumn(
        Mesh2DAquiferModel::ColSoil,
        new ComboDelegate(soilModelTokens(), this, true));
    m_aquiferView->setItemDelegateForColumn(
        Mesh2DAquiferModel::ColClosure,
        new ComboDelegate(closureTokens(), this, true));
    v->addWidget(m_aquiferView, 1);

    auto *row = new QHBoxLayout;
    m_addRowBtn = new QPushButton(tr("Add row"), page);
    m_addRowBtn->setAutoDefault(false);
    m_delRowBtn = new QPushButton(tr("Remove row"), page);
    m_delRowBtn->setAutoDefault(false);
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
    m_nodeView->setItemDelegateForColumn(Mesh2DAquiferNodeModel::ColBottom,new ComboDelegate(Mesh2DAquiferNodeModel::bottomLabels(),m_nodeView));
    m_nodeView->setAccessibleName(tr("Groundwater node beds"));
    m_nodeView->setAccessibleDescription(
        tr("Select a row with the arrow keys. Press F2 to edit the current cell. Press Space to toggle aquifer exchange."));
    m_nodeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeView->horizontalHeader()->setStretchLastSection(false);
    m_nodeView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_nodeView->horizontalHeader()->moveSection(Mesh2DAquiferNodeModel::ColExchange, 2);
    v->addWidget(m_nodeView, 1);

    auto *row = new QHBoxLayout;
    m_addBedBtn = new QPushButton(tr("Add bed"), page);
    m_addBedBtn->setAutoDefault(false);
    m_delBedBtn = new QPushButton(tr("Remove bed"), page);
    m_delBedBtn->setAutoDefault(false);
    connect(m_addBedBtn, &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onAddNodeBed);
    connect(m_delBedBtn, &QPushButton::clicked,
            this, &Mesh2DGroundwaterDialog::onRemoveNodeBed);
    row->addWidget(m_addBedBtn);
    row->addWidget(m_delBedBtn);
    row->addStretch(1);
    v->addLayout(row);

    auto* form=new QFormLayout;
    m_bottomNode=new QComboBox(page);m_bottomNode->setObjectName("lidBottomNode");m_bottomNode->setAccessibleName(tr("Storage LID node"));
    m_bottomDestination=new QComboBox(page);m_bottomDestination->setObjectName("lidBottomDestination");m_bottomDestination->setAccessibleName(tr("Bottom destination"));
    m_bottomDestination->addItems(Mesh2DAquiferNodeModel::bottomLabels());
    m_useFootprint=new QPushButton(tr("Use current LID footprint"),page);m_useFootprint->setObjectName("lidUseFootprint");m_useFootprint->setAutoDefault(false);
    form->addRow(tr("Storage LID &node"),m_bottomNode);form->addRow(tr("Bottom &destination"),m_bottomDestination);form->addRow(QString{},m_useFootprint);v->addLayout(form);
    m_bottomSummary=new QLabel(page);m_bottomSummary->setObjectName("lidBottomPreview");m_bottomSummary->setWordWrap(true);v->addWidget(m_bottomSummary);
    connect(m_bottomNode,qOverload<int>(&QComboBox::currentIndexChanged),this,&Mesh2DGroundwaterDialog::updateBottomConnection);
    connect(m_bottomDestination,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int mode){m_nodes->setBottomDestination(m_bottomNode->currentText(),mode);updateBottomConnection();});
    connect(m_useFootprint,&QPushButton::clicked,this,[this]{const auto error=m_nodes->useLidFootprint(m_bottomNode->currentText());updateBottomConnection();if(!error.isEmpty())m_bottomSummary->setText(error);});
    connect(m_nodeView->selectionModel(),&QItemSelectionModel::currentRowChanged,this,[this](const QModelIndex& index){
        const QString name=m_nodes->data(m_nodes->index(index.row(),0),Qt::DisplayRole).toString();
        const int at=m_bottomNode->findText(name);if(at>=0)m_bottomNode->setCurrentIndex(at);
    });
    connect(m_nodes,&QAbstractItemModel::dataChanged,this,[this]{updateBottomConnection();});

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
    m_stateText->setAccessibleName(tr("Groundwater state and water balance"));
    m_stateText->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = m_stateText->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("Monospace"));
    m_stateText->setFont(mono);
    v->addWidget(m_stateText, 1);

    auto *row = new QHBoxLayout;
    m_refreshBtn = new QPushButton(tr("Refresh"), page);
    m_refreshBtn->setAutoDefault(false);
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
        refreshState();
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
        int i = cb->findData(token);
        if(i<0)i=cb->findText(token, Qt::MatchFixedString);
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

    for(auto it=m_transportOptions.cbegin();it!=m_transportOptions.cend();++it){const auto value=optionText(m_engine,("GW_TRANSPORT:"+it.key()).toUtf8().constData());
        if(auto* box=qobject_cast<QCheckBox*>(it.value()))box->setChecked(value=="YES");
        else if(auto* combo=qobject_cast<QComboBox*>(it.value())){int index=combo->findData(value);if(index<0)index=combo->findText(value,Qt::MatchFixedString);if(index>=0)combo->setCurrentIndex(index);}
        else if(auto* integer=qobject_cast<QSpinBox*>(it.value()))integer->setValue(value.toInt());
        else if(auto* number=qobject_cast<QDoubleSpinBox*>(it.value()))number->setValue(value.toDouble());
        else if(auto* text=qobject_cast<QLineEdit*>(it.value()))text->setText(value);}
    auto loadBoundary=[&](const QString& key,QComboBox* kind,QLineEdit* argument,QDoubleSpinBox* depth){
        const auto tokens=optionText(m_engine,("GW_TRANSPORT:"+key).toUtf8().constData()).split(QLatin1Char(' '),Qt::SkipEmptyParts);
        if(tokens.isEmpty())return;
        const int index=kind->findData(tokens.first());if(index>=0)kind->setCurrentIndex(index);
        argument->clear();if(depth)depth->setValue(0);if(tokens.size()>1&&tokens[1]!=QLatin1String("DEPTH"))argument->setText(tokens[1]);
        if(depth)for(int i=2;i+1<tokens.size();++i)if(tokens[i]==QLatin1String("DEPTH")){depth->setValue(tokens[i+1].toDouble());break;}
    };
    loadBoundary("SURFACE_THERMAL_BC",m_surfaceThermalKind,m_surfaceThermalValue,nullptr);
    loadBoundary("DEEP_THERMAL_BC",m_deepThermalKind,m_deepThermalValue,m_deepThermalDepth);
    m_loadedOptions=readOptions();m_displayedOptions=optionDraft();
    m_aquifer->load(m_engine);
    m_nodes->load(m_engine);
    {QSignalBlocker block(m_bottomNode);m_bottomNode->clear();m_bottomNode->addItems(m_nodes->lidNodes());}
    updateBottomConnection();
    refreshState();

    // The setter also marks options authored; querying lifecycle avoids a
    // hidden model change when this dialog is only inspected or cancelled.
    const bool editable = canEdit(m_engine);
    setEditable(editable,
                editable ? QString()
                         : tr("A simulation is running. The aquifer rows seed "
                              "the kernel when it starts, so they cannot be "
                              "changed now — reset the run to edit them. The "
                              "State tab still updates."));
}

void Mesh2DGroundwaterDialog::setEditable(bool on, const QString &whyNot)
{
    m_editable = on;
    updateBottomConnection();
    if (!on) m_buttons->setStandardButtons(QDialogButtonBox::Close);
    m_refreshBtn->setEnabled(m_engine != nullptr);
    for(auto* field:m_transportOptions)field->setEnabled(on);
    if(m_boundaries)m_boundaries->setEditable(on);
    if(m_boundaryPage) {
        m_boundaryPage->setEnabled(on);
        for(auto* view:m_boundaryPage->findChildren<QTableView*>())
            view->setEditTriggers(on?QAbstractItemView::AllEditTriggers:QAbstractItemView::NoEditTriggers);
    }
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_soilCombo, m_closureCombo, m_layersSpin, m_capillaryCheck,
             m_cgwSpin, m_ccolSpin, m_forceCfCheck, m_dunneCheck,
             m_modeCombo, m_gwEtCombo, m_addRowBtn, m_delRowBtn,
             m_addBedBtn, m_delBedBtn,m_surfaceThermalKind,m_surfaceThermalValue,
             m_deepThermalKind,m_deepThermalValue,m_deepThermalDepth})
        if (w) w->setEnabled(on);
    refreshThermalGates();
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

QWidget* Mesh2DGroundwaterDialog::buildBoundaryPage()
{
    m_boundaryPage=new QWidget;auto* layout=new QVBoxLayout(m_boundaryPage);
    auto* note=new QLabel(tr("Add one Water row for each exterior groundwater edge, then its species rows. HEAD is a water-table elevation in project units; FLOW is total m³/s, positive inward. Potential inflow needs a concentration for every enabled groundwater species. Imposed MASSFLUX and HEATFLUX rows may accompany a concentration."),m_boundaryPage);
    note->setWordWrap(true);layout->addWidget(note);
    m_boundaries=new Mesh2DAquiferBoundaryModel(this);m_boundaries->setObjectName("gwBoundaryModel");
    auto* view=new QTableView(m_boundaryPage);view->setObjectName("gwBoundaryTable");view->setAccessibleName(tr("Groundwater hydraulic and chemistry boundaries"));view->setModel(m_boundaries);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);view->horizontalHeader()->setStretchLastSection(true);layout->addWidget(view);
    auto* buttons=new QHBoxLayout;auto* water=new QPushButton(tr("Add hydraulic edge"),m_boundaryPage);
    auto* chemistry=new QPushButton(tr("Add species row"),m_boundaryPage);auto* remove=new QPushButton(tr("Remove row"),m_boundaryPage);
    water->setObjectName("gwBoundaryAddWater");chemistry->setObjectName("gwBoundaryAddSpecies");remove->setObjectName("gwBoundaryRemove");
    for(auto* button:{water,chemistry,remove}){button->setAutoDefault(false);button->setDefault(false);}
    buttons->addWidget(water);buttons->addWidget(chemistry);buttons->addWidget(remove);buttons->addStretch();layout->addLayout(buttons);
    connect(water,&QPushButton::clicked,m_boundaries,&Mesh2DAquiferBoundaryModel::addHydraulic);
    connect(chemistry,&QPushButton::clicked,m_boundaries,&Mesh2DAquiferBoundaryModel::addChemistry);
    connect(remove,&QPushButton::clicked,this,[this,view]{m_boundaries->remove(view->currentIndex().row());});
    m_boundaries->load(m_engine);return m_boundaryPage;
}

QWidget* Mesh2DGroundwaterDialog::buildTransportPage()
{
    auto* content=new QWidget;auto* layout=new QVBoxLayout(content);
    auto* note=new QLabel(tr("These are saved groundwater transport settings. They apply only with an active mesh aquifer and the corresponding project process. "
        "Initial values and injection/extraction sources are edited in Assign Groundwater. Porous soil heat is opt-in and requires the porous-equilibrium model, active 2D groundwater, heat transport and groundwater temperature transport. "
        "Thermal settings remain saved while those parent processes are off."),content);
    note->setWordWrap(true);layout->addWidget(note);auto* form=new QFormLayout;
    const QStringList keys={"TRANSPORT_POLLUTANTS","TRANSPORT_MSX","TRANSPORT_AGE","TRANSPORT_TEMPERATURE","DISPERSION"};
    const QStringList labels={tr("Carry pollutants"),tr("Carry reaction species"),tr("Carry water age"),tr("Carry temperature"),tr("Solute dispersion")};
    for(int i=0;i<keys.size();++i){auto* box=new QCheckBox(labels[i],content);box->setObjectName("gwTransport_"+keys[i]);m_transportOptions[keys[i]]=box;form->addRow(box);}
    auto* chemistry=new QComboBox(content);chemistry->setObjectName("gwTransport_UNSAT_TRANSPORT");
    chemistry->addItem(tr("Well-mixed column"),"BULK");chemistry->addItem(tr("Separate soil layers"),"LAYERED");
    chemistry->setToolTip(tr("Separate chemistry follows the SIGMA water layers. Choose SIGMA as the aquifer closure. This profile currently uses water-only temperature storage."));
    chemistry->setAccessibleName(tr("Unsaturated chemistry"));m_transportOptions["UNSAT_TRANSPORT"]=chemistry;
    form->addRow(tr("Unsaturated &chemistry:"),chemistry);
    auto* diffusion=new QDoubleSpinBox(content);diffusion->setObjectName("gwTransport_C_DIFF");
    diffusion->setRange(1e-6,1);diffusion->setDecimals(6);diffusion->setKeyboardTracking(false);
    diffusion->setAccessibleName(tr("Diffusion safety factor"));
    diffusion->setToolTip(tr("Controls the explicit solute diffusion timestep and the porous thermal diffusion bound."));
    m_transportOptions["C_DIFF"]=diffusion;form->addRow(tr("Diffusion safety &factor:"),diffusion);
    connect(qobject_cast<QCheckBox*>(m_transportOptions.value("DISPERSION")),&QCheckBox::toggled,this,&Mesh2DGroundwaterDialog::refreshThermalGates);
    layout->addLayout(form);

    auto* thermalBox=new QGroupBox(tr("Porous soil heat and conduction"),content);
    auto* thermalForm=new QFormLayout(thermalBox);
    const auto setThermalAccessibility=[](QWidget* field,QString label,const QString& description=QString()) {
        label.remove('&');
        field->setAccessibleName(label.trimmed());
        if(!description.isEmpty())field->setAccessibleDescription(description);
    };
    auto addCombo=[&](const QString& key,const QString& label,const QStringList& names,const QStringList& values){
        auto* combo=new QComboBox(thermalBox);combo->setObjectName("gwTransport_"+key);
        for(int i=0;i<names.size();++i)combo->addItem(names[i],values[i]);
        setThermalAccessibility(combo,label,combo->toolTip());
        m_transportOptions[key]=combo;m_thermalFields.push_back(combo);thermalForm->addRow(label,combo);return combo;
    };
    auto addDouble=[&](const QString& key,const QString& label,double minimum,double maximum,int decimals,const QString& suffix){
        auto* spin=new QDoubleSpinBox(thermalBox);spin->setObjectName("gwTransport_"+key);spin->setRange(minimum,maximum);spin->setDecimals(decimals);spin->setKeyboardTracking(false);spin->setSuffix(suffix);
        setThermalAccessibility(spin,label,spin->toolTip());
        m_transportOptions[key]=spin;m_thermalFields.push_back(spin);thermalForm->addRow(label,spin);return spin;
    };
    addCombo("THERMAL_MODEL",tr("Soil heat &model:"),{tr("Water temperature only"),tr("Porous soil heat storage")},{"WATER_ONLY","POROUS_EQUILIBRIUM"});
    addCombo("CONDUCTION",tr("Vertical soil conduc&tion:"),{tr("Off"),tr("On")},{"NO","YES"});
    addCombo("THERMAL_MIXING",tr("Conductivity mi&xing:"),{tr("Arithmetic"),tr("Geometric")},{"ARITHMETIC","GEOMETRIC"});
    auto* lateral=new QCheckBox(tr("Enable lateral conduction between saturated cells"),thermalBox);lateral->setObjectName("gwTransport_LATERAL_CONDUCTION");setThermalAccessibility(lateral,lateral->text());m_transportOptions["LATERAL_CONDUCTION"]=lateral;m_thermalFields.push_back(lateral);thermalForm->addRow(QString(),lateral);
    auto* soilEt=addCombo("SOIL_ET_HEAT",tr("Soil ET heat treat&ment:"),{tr("Latent heat at surface"),tr("Sensible energy only")},{"LATENT_SURFACE","SENSIBLE_ONLY"});
    soilEt->setToolTip(tr("LATENT_SURFACE debits the latent heat of accepted soil ET from the surface boundary. SENSIBLE_ONLY carries water sensible heat without that latent debit."));
    soilEt->setAccessibleDescription(soilEt->toolTip());
    const QString layerLabel=tr("Vertical thermal resolu&tion:");
    auto* layers=new QSpinBox(thermalBox);layers->setObjectName("gwTransport_THERMAL_LAYERS");layers->setRange(1,128);layers->setSuffix(tr(" layers"));layers->setToolTip(tr("Number of fixed thermal cells from the surface to the bottom of the soil profile."));setThermalAccessibility(layers,layerLabel,layers->toolTip());m_transportOptions["THERMAL_LAYERS"]=layers;m_thermalFields.push_back(layers);thermalForm->addRow(layerLabel,layers);
    addDouble("THERMAL_MAX_STEP",tr("Maximum thermal &step:"),1e-6,1e9,6,tr(" s"));
    addDouble("WATER_THERMAL_CONDUCTIVITY",tr("&Water thermal conductivity:"),1e-9,1e6,8,tr(" W/m/K"));
    addDouble("AIR_THERMAL_CONDUCTIVITY",tr("&Air thermal conductivity:"),1e-9,1e6,8,tr(" W/m/K"));
    addDouble("SURFACE_THERMAL_RESISTANCE",tr("Surface thermal resist&ance:"),1e-9,1e12,8,tr(" m² K/W"));

    auto* surfaceKind=new QComboBox(thermalBox);surfaceKind->setObjectName("gwTransport_SURFACE_THERMAL_BC_KIND");
    const QStringList surfaceModes={"ADIABATIC","AIR","SURFACE_WATER","FIXED","TIMESERIES","HEAT_FLUX","FLUX_TIMESERIES"};
    const QStringList surfaceNames={tr("Insulated"),tr("Air temperature"),tr("Surface-water temperature"),tr("Fixed temperature"),tr("Temperature time series"),tr("Fixed heat flux"),tr("Heat-flux time series")};
    for(int i=0;i<surfaceModes.size();++i)surfaceKind->addItem(surfaceNames[i],surfaceModes[i]);
    const QString surfaceKindLabel=tr("Surface &boundary:");
    m_surfaceThermalKind=surfaceKind;setThermalAccessibility(surfaceKind,surfaceKindLabel);m_thermalFields.push_back(surfaceKind);thermalForm->addRow(surfaceKindLabel,surfaceKind);
    const QString surfaceValueLabel=tr("Surface boundary value / series:");
    m_surfaceThermalValue=new QLineEdit(thermalBox);m_surfaceThermalValue->setObjectName("gwTransport_SURFACE_THERMAL_BC_VALUE");m_surfaceThermalValue->setToolTip(tr("Temperature in °C or heat flux in W/m² for numeric modes; otherwise enter a time-series name."));setThermalAccessibility(m_surfaceThermalValue,surfaceValueLabel,m_surfaceThermalValue->toolTip());m_thermalFields.push_back(m_surfaceThermalValue);thermalForm->addRow(surfaceValueLabel,m_surfaceThermalValue);

    auto* deepKind=new QComboBox(thermalBox);deepKind->setObjectName("gwTransport_DEEP_THERMAL_BC_KIND");
    const QStringList deepModes={"ADIABATIC","GEOTHERMAL_FLUX","FIXED_TEMP","TIMESERIES","FLUX_TIMESERIES"};
    const QStringList deepNames={tr("Insulated"),tr("Geothermal heat flux"),tr("Fixed temperature"),tr("Temperature time series"),tr("Heat-flux time series")};
    for(int i=0;i<deepModes.size();++i)deepKind->addItem(deepNames[i],deepModes[i]);
    const QString deepKindLabel=tr("&Deep boundary:");
    m_deepThermalKind=deepKind;setThermalAccessibility(deepKind,deepKindLabel);m_thermalFields.push_back(deepKind);thermalForm->addRow(deepKindLabel,deepKind);
    const QString deepValueLabel=tr("Deep boundary value / series:");
    m_deepThermalValue=new QLineEdit(thermalBox);m_deepThermalValue->setObjectName("gwTransport_DEEP_THERMAL_BC_VALUE");m_deepThermalValue->setToolTip(tr("Temperature in °C or flux in W/m² for numeric modes; otherwise enter a time-series name."));setThermalAccessibility(m_deepThermalValue,deepValueLabel,m_deepThermalValue->toolTip());m_thermalFields.push_back(m_deepThermalValue);thermalForm->addRow(deepValueLabel,m_deepThermalValue);
    const QString deepDepthLabel=tr("Boundary &depth:");
    m_deepThermalDepth=new QDoubleSpinBox(thermalBox);m_deepThermalDepth->setObjectName("gwTransport_DEEP_THERMAL_BC_DEPTH");m_deepThermalDepth->setRange(0,1e9);m_deepThermalDepth->setDecimals(6);m_deepThermalDepth->setSuffix(tr(" m"));m_deepThermalDepth->setSpecialValueText(tr("Use groundwater bottom"));setThermalAccessibility(m_deepThermalDepth,deepDepthLabel,m_deepThermalDepth->toolTip());m_thermalFields.push_back(m_deepThermalDepth);thermalForm->addRow(deepDepthLabel,m_deepThermalDepth);
    auto updateBoundaryArguments=[this]{
        const auto surface=m_surfaceThermalKind->currentData().toString();const bool surfaceArg=surface=="FIXED"||surface=="TIMESERIES"||surface=="HEAT_FLUX"||surface=="FLUX_TIMESERIES";
        const auto model=qobject_cast<QComboBox*>(m_transportOptions.value("THERMAL_MODEL"));const bool porous=model&&model->currentData().toString()=="POROUS_EQUILIBRIUM";
        m_surfaceThermalValue->setEnabled(m_editable&&m_porousThermalOptionsSupported&&porous&&surfaceArg);m_surfaceThermalValue->setPlaceholderText(surface=="TIMESERIES"?tr("Series name"):surface=="FLUX_TIMESERIES"?tr("Flux series name"):surface=="HEAT_FLUX"?tr("W/m²"):tr("°C"));
        const auto deep=m_deepThermalKind->currentData().toString();const bool deepArg=deep!="ADIABATIC";m_deepThermalValue->setEnabled(m_editable&&m_porousThermalOptionsSupported&&porous&&deepArg);m_deepThermalDepth->setEnabled(m_editable&&m_porousThermalOptionsSupported&&porous&&deepArg);
        m_deepThermalValue->setPlaceholderText(deep=="TIMESERIES"?tr("Series name"):deep=="FLUX_TIMESERIES"?tr("Flux series name"):deep=="GEOTHERMAL_FLUX"?tr("W/m²"):tr("°C"));
    };
    connect(surfaceKind,&QComboBox::currentIndexChanged,this,updateBoundaryArguments);connect(deepKind,&QComboBox::currentIndexChanged,this,updateBoundaryArguments);updateBoundaryArguments();
    m_thermalStatus=new QLabel(thermalBox);m_thermalStatus->setObjectName("gwThermalActivationStatus");m_thermalStatus->setAccessibleName(tr("Porous soil heat run status"));m_thermalStatus->setWordWrap(true);thermalForm->addRow(tr("Run status:"),m_thermalStatus);
    connect(static_cast<QComboBox*>(m_transportOptions.value("THERMAL_MODEL")),&QComboBox::currentIndexChanged,this,&Mesh2DGroundwaterDialog::refreshThermalGates);
    connect(static_cast<QComboBox*>(m_transportOptions.value("CONDUCTION")),&QComboBox::currentIndexChanged,this,&Mesh2DGroundwaterDialog::refreshThermalGates);
    connect(static_cast<QCheckBox*>(m_transportOptions.value("TRANSPORT_TEMPERATURE")),&QCheckBox::toggled,this,&Mesh2DGroundwaterDialog::refreshThermalGates);
    layout->addWidget(thermalBox);layout->addStretch();auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(content);return scroll;
}

void Mesh2DGroundwaterDialog::refreshThermalGates()
{
    auto* model=qobject_cast<QComboBox*>(m_transportOptions.value("THERMAL_MODEL"));
    if(!model||!m_thermalStatus)return;
    const bool porous=model->currentData().toString()=="POROUS_EQUILIBRIUM";
    const auto* dispersion=qobject_cast<QCheckBox*>(m_transportOptions.value("DISPERSION"));
    if(auto* factor=m_transportOptions.value("C_DIFF"))factor->setEnabled(m_editable&&(porous||(dispersion&&dispersion->isChecked())));
    const auto setStatus=[this](const QString& text){m_thermalStatus->setText(text);m_thermalStatus->setAccessibleDescription(text);};
    for(auto* field:m_thermalFields)
        if(field)field->setEnabled(m_editable&&m_porousThermalOptionsSupported);
    if(!m_engine){
        setStatus(tr("Unavailable until a model is open."));
        return;
    }
    if(!m_porousThermalOptionsSupported){
        setStatus(tr("Unavailable: this engine does not expose the porous-soil heat option. These controls are disabled because this engine cannot validate or apply them."));
        return;
    }
    for(auto it=m_transportOptions.cbegin();it!=m_transportOptions.cend();++it) {
        if(it.key()=="THERMAL_MODEL"||it.key().startsWith("TRANSPORT_")||it.key()=="DISPERSION"||it.key()=="UNSAT_TRANSPORT"||it.key()=="C_DIFF")continue;
        it.value()->setEnabled(m_editable&&porous);
    }
    const auto surface=m_surfaceThermalKind->currentData().toString();
    const auto deep=m_deepThermalKind->currentData().toString();
    const bool surfaceArg=surface=="FIXED"||surface=="TIMESERIES"||surface=="HEAT_FLUX"||surface=="FLUX_TIMESERIES";
    m_surfaceThermalKind->setEnabled(m_editable&&porous);m_surfaceThermalValue->setEnabled(m_editable&&porous&&surfaceArg);
    m_deepThermalKind->setEnabled(m_editable&&porous);m_deepThermalValue->setEnabled(m_editable&&porous&&deep!="ADIABATIC");
    m_deepThermalDepth->setEnabled(m_editable&&porous&&deep!="ADIABATIC");
    if(!porous){setStatus(tr("Inactive: Water temperature only is selected. Porous heat settings are retained for later use."));return;}
    QStringList inactive;
    char groundwater[64]{};if(swmm_options_get_ext(m_engine,"GROUNDWATER",groundwater,sizeof groundwater)!=SWMM_OK||QString::fromUtf8(groundwater).compare("NO",Qt::CaseInsensitive)==0)inactive.append(tr("2D groundwater is off"));
    int heat=0;if(swmm_heat_get_enabled(m_engine,&heat)!=SWMM_OK||!heat)inactive.append(tr("heat transport is off"));
    const auto* temperature=qobject_cast<QCheckBox*>(m_transportOptions.value("TRANSPORT_TEMPERATURE"));if(!temperature||!temperature->isChecked())inactive.append(tr("groundwater temperature transport is off"));
    if(!inactive.isEmpty())setStatus(tr("Inactive until %1. These settings are saved and do not activate a disabled parent process.").arg(inactive.join(tr("; "))));
    else {const auto* conduction=qobject_cast<QComboBox*>(m_transportOptions.value("CONDUCTION"));
        if(!conduction||conduction->currentData().toString()=="NO")setStatus(tr("Porous heat transport is ready for covered aquifer cells; vertical conduction is turned off."));
        else setStatus(tr("Ready for covered aquifer cells. Actual activation still depends on mesh-to-aquifer coverage and valid material rows."));}
}

QMap<QString,QString> Mesh2DGroundwaterDialog::optionDraft()const
{
 QMap<QString,QString> values{{"SOIL_CHAR",m_soilCombo->currentText()},{"CLOSURE",m_closureCombo->currentText()},{"M_LAYERS",QString::number(m_layersSpin->value())},{"CAPILLARY_DIFF",m_capillaryCheck->isChecked()?"YES":"NO"},{"C_GW",QString::number(m_cgwSpin->value(),'g',17)},{"C_COL",QString::number(m_ccolSpin->value(),'g',17)},{"FORCE_CLOSED_FORM",m_forceCfCheck->isChecked()?"YES":"NO"},{"DUNNE",m_dunneCheck->isChecked()?"YES":"NO"},{"MODE",m_modeCombo->currentText()},{"GW_ET",m_gwEtCombo->currentData().toString()}};
 for(auto it=m_transportOptions.cbegin();it!=m_transportOptions.cend();++it){
     QString value;
     if(auto* box=qobject_cast<QCheckBox*>(it.value()))value=box->isChecked()?"YES":"NO";
     else if(auto* combo=qobject_cast<QComboBox*>(it.value()))value=combo->currentData().toString();
     else if(auto* integer=qobject_cast<QSpinBox*>(it.value()))value=QString::number(integer->value());
     else if(auto* number=qobject_cast<QDoubleSpinBox*>(it.value()))value=QString::number(number->value(),'g',17);
     else if(auto* text=qobject_cast<QLineEdit*>(it.value()))value=text->text().trimmed();
     values["GW_TRANSPORT:"+it.key()]=value;
 }
 auto surface=m_surfaceThermalKind->currentData().toString();const auto surfaceArg=m_surfaceThermalValue->text().trimmed();
 if((surface=="FIXED"||surface=="TIMESERIES"||surface=="HEAT_FLUX"||surface=="FLUX_TIMESERIES")&&!surfaceArg.isEmpty())surface+=QLatin1Char(' ')+surfaceArg;
 values["GW_TRANSPORT:SURFACE_THERMAL_BC"]=surface;
 auto deep=m_deepThermalKind->currentData().toString();const auto deepArg=m_deepThermalValue->text().trimmed();
 if(deep!="ADIABATIC"&&!deepArg.isEmpty())deep+=QLatin1Char(' ')+deepArg;
 if(deep!="ADIABATIC"&&m_deepThermalDepth->value()>0)deep+=QStringLiteral(" DEPTH ")+QString::number(m_deepThermalDepth->value(),'g',17);
 values["GW_TRANSPORT:DEEP_THERMAL_BC"]=deep;
 return values;
}
QMap<QString,QString> Mesh2DGroundwaterDialog::readOptions()const
{
 QMap<QString,QString> values;for(const auto&key:optionDraft().keys())values[key]=optionText(m_engine,key.toUtf8().constData());return values;
}
QString Mesh2DGroundwaterDialog::applyOptions()
{
 const auto draft=optionDraft();for(auto it=draft.cbegin();it!=draft.cend();++it)if(it.value()!=m_displayedOptions.value(it.key()))if(!setOption(m_engine,it.key().toUtf8().constData(),it.value()))return tr("The engine refused %1 = %2.").arg(it.key(),it.value());return {};
}

void Mesh2DGroundwaterDialog::onApply()
{
    applyChanges();
}

bool Mesh2DGroundwaterDialog::applyChanges()
{
    if(!m_engine||!m_editable)return false;
    if(!canEdit(m_engine)){QMessageBox::warning(this,tr("2D Groundwater"),tr("Reset the simulation before applying edits. Your draft remains here."));return false;}
    if(auto*focus=focusWidget())focus->clearFocus();
    const auto draft=optionDraft();const bool optionsChanged=draft!=m_displayedOptions,aqChanged=m_aquifer->isDirty(),nodesChanged=m_nodes->isDirty(),edgesChanged=m_boundaries->isDirty();
    if(!optionsChanged&&!aqChanged&&!nodesChanged&&!edgesChanged)return true;
    QString error;
    if(draft.value("GW_TRANSPORT:UNSAT_TRANSPORT")=="LAYERED") {
        if(draft.value("CLOSURE")!="SIGMA")error=tr("Choose SIGMA as the aquifer closure for separate soil-layer chemistry.");
    }
    if(readOptions()!=m_loadedOptions)error=tr("Groundwater options changed outside this editor. Reopen it to preserve newer edits.");
    if(error.isEmpty())error=m_aquifer->validateCommit(m_engine);
    if(error.isEmpty())error=m_nodes->validateCommit(m_engine,!optionsChanged&&!aqChanged);
    if(error.isEmpty()&&edgesChanged)error=m_boundaries->validateCommit(m_engine,!optionsChanged&&!aqChanged);
    if(error.isEmpty()&&optionsChanged){SWMM_Engine check=swmm_engine_new();if(!check)error=tr("Cannot allocate option validation model.");else {for(auto it=draft.cbegin();it!=draft.cend();++it)if(it.value()!=m_displayedOptions.value(it.key())&&!setOption(check,it.key().toUtf8().constData(),it.value())){error=tr("Invalid groundwater option %1.").arg(it.key());break;}swmm_engine_destroy(check);}}
    if(!error.isEmpty()){QMessageBox::warning(this,tr("2D Groundwater"),error);return false;}
    QPointer<Mesh2DGroundwaterDialog> self=this;emit changesMayHaveBeenApplied();if(!self||!m_engine||!canEdit(m_engine))return false;
    // Dirty observers may close or modify a model synchronously. Recheck after
    // that signal, before the first write, without restoring over their edits.
    if(readOptions()!=m_loadedOptions)error=tr("Groundwater options changed before commit.");
    if(error.isEmpty())error=m_aquifer->validateCommit(m_engine);
    if(error.isEmpty())error=m_nodes->validateCommit(m_engine,!optionsChanged&&!aqChanged);
    if(error.isEmpty()&&edgesChanged)error=m_boundaries->validateCommit(m_engine,!optionsChanged&&!aqChanged);
    if(!error.isEmpty()){QMessageBox::warning(this,tr("2D Groundwater"),error);return false;}
    error=applyOptions();if(error.isEmpty())error=m_aquifer->commit(m_engine,false);if(error.isEmpty())error=m_nodes->commit(m_engine,false);if(error.isEmpty()&&edgesChanged)error=m_boundaries->commit(m_engine,false);
    if(!error.isEmpty()){
        QStringList failed;if(edgesChanged){auto e=m_boundaries->restoreLoaded(m_engine);if(!e.isEmpty())failed.append(e);}if(aqChanged){auto e=m_aquifer->restoreLoaded(m_engine);if(!e.isEmpty())failed.append(e);}if(nodesChanged){auto e=m_nodes->restoreLoaded(m_engine);if(!e.isEmpty())failed.append(e);}
        for(auto it=m_loadedOptions.cbegin();it!=m_loadedOptions.cend();++it)if(draft.value(it.key())!=m_displayedOptions.value(it.key())&&!setOption(m_engine,it.key().toUtf8().constData(),it.value()))failed.append(it.key());
        error+=failed.isEmpty()?tr(" Previous values restored; your draft remains available."):tr(" Rollback incomplete: %1. Reload the saved model before further editing.").arg(failed.join(", "));
        QMessageBox::warning(this,tr("2D Groundwater"),error);return false;
    }
    if(nodesChanged&&m_nodeUndo)m_nodeUndo->push(new NodeConnectionUndo(m_nodes->transactionCopy(),m_engine,m_undoCurrent,m_undoEdited));
    if(edgesChanged&&m_nodeUndo)m_nodeUndo->push(new BoundaryUndo(m_boundaries->transactionCopy(),m_engine,m_undoCurrent,m_undoEdited));
    m_boundaries->acceptChanges();
    m_aquifer->acceptChanges();m_nodes->acceptChanges();m_loadedOptions=readOptions();m_displayedOptions=draft;
    refreshState();emit applied();return true;
}

void Mesh2DGroundwaterDialog::setNodeUndoStack(QUndoStack* stack,std::function<bool()> current,std::function<void()> edited) {
    m_nodeUndo=stack;m_undoCurrent=std::move(current);m_undoEdited=std::move(edited);
}
void Mesh2DGroundwaterDialog::focusNode(const QString& node) {
    m_tabs->setCurrentIndex(2);const int at=m_bottomNode->findText(node);
    if(at>=0)m_bottomNode->setCurrentIndex(at);
    const int row=m_nodes->bedRow(node);if(row>=0)m_nodeView->selectRow(row);
    updateBottomConnection();
}
void Mesh2DGroundwaterDialog::updateBottomConnection() {
    if(!m_bottomDestination)return;
    m_bottomNode->setEnabled(m_engine&&!m_nodes->lidNodes().isEmpty());
    const auto node=m_bottomNode->currentText();const int mode=m_nodes->bottomDestination(node);
    {QSignalBlocker block(m_bottomDestination);m_bottomDestination->setCurrentIndex(std::max(0,mode));}
    m_bottomDestination->setEnabled(m_editable&&mode>=0);m_useFootprint->setEnabled(m_editable&&mode>=0);
    m_bottomSummary->setText(m_nodes->connectionSummary(node));
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
    QList<int> rows;for(const auto&i:sel)rows.append(i.row());std::sort(rows.begin(),rows.end(),std::greater<int>());for(int row:rows)m_aquifer->removeRow(row);
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
    QList<int> rows;for(const auto&i:sel)rows.append(i.row());std::sort(rows.begin(),rows.end(),std::greater<int>());for(int row:rows)m_nodes->removeRow(row);
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
        {SWMM_GW2D_LED_LATERAL,      "Lateral net in (includes edges)"},
        {SWMM_GW2D_LED_BOUNDARY_IN, "Boundary in (within lateral)"},
        {SWMM_GW2D_LED_BOUNDARY_OUT,"Boundary out (within lateral)"},
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

    const int boundaries=swmm_gw2d_boundary_count(m_engine);
    if(boundaries>0) {
        double requestedIn=0,requestedOut=0,acceptedIn=0,acceptedOut=0;
        bool valid=true;
        for(int edge=0;edge<boundaries;++edge) {
            double ri=0,ro=0,ai=0,ao=0;
            if(swmm_gw2d_boundary_water(m_engine,edge,&ri,&ro,&ai,&ao)!=SWMM_OK){valid=false;break;}
            requestedIn+=ri;requestedOut+=ro;acceptedIn+=ai;acceptedOut+=ao;
        }
        if(valid)s+=tr("\nHydraulic edges (gross m³, cumulative)\n  In: requested %1, accepted %2\n  Out: requested %3, accepted %4\n")
            .arg(requestedIn,0,'g',8).arg(acceptedIn,0,'g',8).arg(requestedOut,0,'g',8).arg(acceptedOut,0,'g',8);
    }
    int species=0;
    if(swmm_gw2d_species_count(m_engine,&species)==SWMM_OK&&species>0) {
        s+=tr("\nSpecies balance (native concentration × m³; reaction loss may be negative)\n");
        for(int row=0;row<species;++row) {
            char name[4096]{};double loss=0,residual=0,in=0,out=0,fluxIn=0,fluxOut=0;
            if(swmm_gw2d_species_name(m_engine,row,name,sizeof name)!=SWMM_OK)continue;
            const auto term=[&](int code,double& value){return swmm_gw2d_get_species_ledger(m_engine,row,code,&value)==SWMM_OK;};
            if(!term(SWMM_GW2D_SPL_REACTED,loss)||!term(SWMM_GW2D_SPL_RESIDUAL,residual))continue;
            term(SWMM_GW2D_SPL_BOUNDARY_IN,in);term(SWMM_GW2D_SPL_BOUNDARY_OUT,out);
            term(SWMM_GW2D_SPL_BOUNDARY_FLUX_IN,fluxIn);term(SWMM_GW2D_SPL_BOUNDARY_FLUX_OUT,fluxOut);
            s+=tr("  %1: advective edge in %2, out %3; imposed in %4, out %5; reaction loss %6; residual %7\n")
                .arg(QString::fromUtf8(name)).arg(in,0,'g',8).arg(out,0,'g',8).arg(fluxIn,0,'g',8).arg(fluxOut,0,'g',8).arg(loss,0,'g',8).arg(residual,0,'g',8);
        }
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
