#include <openswmm/engine/openswmm_edit.h>
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

#include "ui/models/lidnodelayermodel.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QTableView>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QInputDialog>
#include "ui/widgets/treatmentexpressionedit.h"
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <QSignalBlocker>
#include <limits>
#include <QHeaderView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
#include <QTabWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::lid::LidControlProvider;
using openswmmvis::lid::LidControlRegistry;
namespace {
class RetentionCurves : public QWidget {
public:
    explicit RetentionCurves(QWidget* parent) : QWidget(parent) { setMinimumHeight(185); setObjectName("lidRetentionCurves"); }
    SWMM_LidRichardsMaterial material{}; double porosity = 0;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base()); p.setPen(palette().text().color());
        if (!(material.alpha > 0 && material.n > 1 && porosity > material.theta_r && material.l >= 0)) {
            p.drawText(rect().adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap, tr("Enter retention parameters to preview the moisture and conductivity curves.")); return;
        }
        QRectF graph(50, 25, width() - 75, height() - 65);
        p.drawLine(graph.bottomLeft(), graph.bottomRight()); p.drawLine(graph.bottomLeft(), graph.topLeft());
        p.drawText(QRectF(0, 0, width(), 22), Qt::AlignCenter, tr("Blue: water content · Orange: relative conductivity"));
        p.drawText(QRectF(0, height() - 28, width(), 24), Qt::AlignCenter, tr("Suction head (m), logarithmic: 0.001 → 1,000"));
        p.drawText(QRectF(0, graph.top(), 43, 20), Qt::AlignRight, "1");
        p.drawText(QRectF(0, graph.bottom() - 20, 43, 20), Qt::AlignRight, "0");
        for (int curve = 0; curve < 2; ++curve) {
            QPainterPath path; const double m = 1 - 1 / material.n;
            for (int i = 0; i <= 200; ++i) {
                double suction = std::pow(10., -3 + 6. * i / 200);
                double se = std::pow(1 + std::pow(material.alpha * suction, material.n), -m);
                double value = curve ? std::pow(se, material.l) * std::pow(1 - std::pow(1 - std::pow(se, 1 / m), m), 2) : material.theta_r + (porosity - material.theta_r) * se;
                QPointF point(graph.left() + graph.width() * i / 200., graph.bottom() - graph.height() * value);
                if (i) path.lineTo(point); else path.moveTo(point);
            }
            p.setPen(QPen(curve ? QColor("#c87818") : QColor("#287fb8"), 2)); p.drawPath(path);
        }
    }
};
}

namespace {
// Use the same native numeric controls as the physical property forms.
class TreatmentRateDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override {
        auto* spin = new QDoubleSpinBox(parent);
        spin->setDecimals(8);
        spin->setRange(0.0, index.column() == 1 ? 100.0 : 1.e12);
        spin->setSingleStep(index.column() == 1 ? 1.0 : .01);
        spin->setKeyboardTracking(false);
        return spin;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        static_cast<QDoubleSpinBox*>(editor)->setValue(index.data(Qt::EditRole).toDouble());
    }
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
        auto* spin = static_cast<QDoubleSpinBox*>(editor);
        spin->interpretText();
        model->setData(index, spin->value(), Qt::EditRole);
    }
};
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
    resize(1180, 760);
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

void LidControlEditorDialog::openForLidControl(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty()) return;
    if (auto *p = m_registry->findByName(name)) selectProviderInList_(p);
}

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
    auto *newLayered = new QPushButton(tr("New layered LID"), leftPane);
    newLayered->setObjectName(QStringLiteral("newLayeredLid"));
    newLayered->setToolTip(tr("Create a storage-node LID with any number of media and aggregate layers."));
    leftLay->addWidget(newLayered);
    connect(newLayered, &QPushButton::clicked, this, &LidControlEditorDialog::addLayeredControl_);

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
                            "Vegetative Swale", "Storage Node (ordered layers)" })
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

    m_nodeLayerPage = new QWidget(tabs);
    auto *stackLayout = new QVBoxLayout(m_nodeLayerPage);
    auto *countForm = new QFormLayout;
    m_mediaCount = new QSpinBox(m_nodeLayerPage);
    m_mediaCount->setObjectName(QStringLiteral("lidMediaLayerCount"));
    m_mediaCount->setRange(1, std::numeric_limits<int>::max());
    m_mediaCount->setKeyboardTracking(false);
    countForm->addRow(tr("Media / aggregate layers"), m_mediaCount);
    m_flowModel = new QComboBox(m_nodeLayerPage);
    m_flowModel->setObjectName("lidFlowModel");
    m_flowModel->addItems({tr("Existing formulation (with backwater)"), tr("Richards 1D")});
    countForm->addRow(tr("Flow model"), m_flowModel);
    m_richardsBoundaryNotice = new QLabel(tr("Richards supports sealed bottoms and native soil drainage. Active 2D aquifer beds require the forthcoming bottom-interface coupling."), m_nodeLayerPage);
    m_richardsBoundaryNotice->setWordWrap(true);
    m_richardsBoundaryNotice->setVisible(false);
    countForm->addRow(m_richardsBoundaryNotice);
    auto* numericsGroup = new QGroupBox(tr("Numerical settings"), m_nodeLayerPage);
    numericsGroup->setObjectName("lidRichardsNumerics");
    numericsGroup->setCheckable(true); numericsGroup->setChecked(false);
    auto* numericsPage = new QWidget(numericsGroup);
    auto* numericsForm = new QFormLayout(numericsPage);
    auto* numericsLayout = new QVBoxLayout(numericsGroup); numericsLayout->addWidget(numericsPage);
    numericsPage->setVisible(false);
    connect(numericsGroup, &QGroupBox::toggled, numericsPage, &QWidget::setVisible);
    m_richardsSettings = numericsGroup;
    m_richardsCells = new QSpinBox(m_nodeLayerPage); m_richardsCells->setRange(1, 256); m_richardsCells->setValue(8);
    m_richardsCells->setObjectName("lidRichardsCells");
    numericsForm->addRow(tr("Numerical cells per porous layer"), m_richardsCells);
    m_richardsAtol = makeSpin(m_nodeLayerPage, 1.e-12, .1, 1.e-7); m_richardsAtol->setDecimals(12); m_richardsAtol->setValue(1.e-7);
    m_richardsRtol = makeSpin(m_nodeLayerPage, 1.e-12, .1, 1.e-5); m_richardsRtol->setDecimals(12); m_richardsRtol->setValue(1.e-5);
    m_richardsMaxStep = makeSpin(m_nodeLayerPage, .001, 86400, 1); m_richardsMaxStep->setValue(30);
    numericsForm->addRow(tr("Absolute water-content tolerance"), m_richardsAtol);
    numericsForm->addRow(tr("Relative tolerance"), m_richardsRtol);
    numericsForm->addRow(tr("Maximum internal step (s)"), m_richardsMaxStep);
    auto flowChanged = [this] {
        if (!m_suppressFieldSync) m_layerDraftDirty = true;
        const bool richards = m_flowModel->currentIndex() == 1;
        m_richardsSettings->setVisible(richards);
        m_richardsBoundaryNotice->setVisible(richards);
        if (m_layerDetails) m_layerDetails->setTabVisible(1, richards);
        for (auto* widget : {static_cast<QWidget*>(m_richardsCells), static_cast<QWidget*>(m_richardsAtol), static_cast<QWidget*>(m_richardsRtol), static_cast<QWidget*>(m_richardsMaxStep)}) widget->setEnabled(richards);
        for (int col = 10; col < 15; ++col) m_nodeLayerTable->setColumnHidden(col, !richards);
        refreshLayerFields_();
    };
    m_layerSummary = new QLabel(m_nodeLayerPage);
    countForm->addRow(m_layerSummary);
    stackLayout->addLayout(countForm);
    stackLayout->addWidget(numericsGroup);
    auto *units = new QLabel(tr("Top to bottom. Thickness and suction: mm (SI) or in (US). Conductivity: mm/hr or in/hr. BOTTOM is the native-soil boundary."));
    units->setWordWrap(true); stackLayout->addWidget(units);
    m_nodeLayerModel = new LidNodeLayerModel(this);
    m_nodeLayerTable = new QTableView(m_nodeLayerPage);
    m_nodeLayerTable->setObjectName(QStringLiteral("lidNodeLayers"));
    m_nodeLayerTable->setModel(m_nodeLayerModel);
    m_nodeLayerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeLayerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    stackLayout->addWidget(m_nodeLayerTable, 1);
    auto *details = new QTabWidget(m_nodeLayerPage);
    m_layerDetails = details;
    auto *retentionPage = new QWidget(details);
    auto *retentionLayout = new QHBoxLayout(retentionPage);
    m_retentionFields = new QFormLayout;
    retentionLayout->addLayout(m_retentionFields);
    auto *physicalPage = new QWidget(details);
    m_layerFields = new QFormLayout(physicalPage);
    for (int column = 1; column < 15; ++column) {
        auto *field = makeSpin(m_nodeLayerPage, 0.0, 1.e12, 0.1);
        field->setDecimals(6);
        if (column >= 10) field->setDecimals(10);
        field->setObjectName(QStringLiteral("lidLayerParameter%1").arg(column));
        m_layerValues[column - 1] = field;
        (column >= 10 ? m_retentionFields : m_layerFields)->addRow(m_nodeLayerModel->headerData(column, Qt::Horizontal).toString(), field);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, column](double value) {
            if (!m_syncLayerFields)
                m_nodeLayerModel->setData(m_nodeLayerModel->index(m_nodeLayerTable->currentIndex().row(), column), value);
        });
    }
    m_retentionCurves = new RetentionCurves(retentionPage);
    retentionLayout->addWidget(m_retentionCurves, 1);
    details->addTab(OpenSWMM::Ui::wrapInScrollArea(physicalPage, details), tr("Physical properties"));
    details->addTab(OpenSWMM::Ui::wrapInScrollArea(retentionPage, details), tr("Retention and conductivity"));
    auto *treatmentPage = new QWidget(details);
    auto *treatmentLayout = new QVBoxLayout(treatmentPage);
    auto *treatmentHelp = new QLabel(tr("Treatment for the selected layer. Removal acts on outgoing pollutant mass. Decay is a first-order rate per day. An optional R = or C = expression acts after removal; leave it blank for rates only."), treatmentPage);
    treatmentHelp->setWordWrap(true); treatmentLayout->addWidget(treatmentHelp);
    m_treatmentTable = new QTableView(treatmentPage);
    m_treatmentTable->setObjectName(QStringLiteral("lidLayerTreatment"));
    m_treatmentModel = new QStandardItemModel(0, 4, this);
    m_treatmentModel->setHorizontalHeaderLabels({tr("Pollutant"),tr("Removal (%)"),tr("Decay (1/day)"),tr("Expression")});
    m_treatmentTable->setModel(m_treatmentModel);
    m_treatmentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treatmentTable->setAlternatingRowColors(true);
    m_treatmentTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    auto* rateDelegate = new TreatmentRateDelegate(m_treatmentTable);
    m_treatmentTable->setItemDelegateForColumn(1, rateDelegate);
    m_treatmentTable->setItemDelegateForColumn(2, rateDelegate);
    m_treatmentTable->setToolTip(tr("Double-click a rate or expression to edit. Use Ctrl+Space for expression completion."));
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_treatmentTable->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Interactive);
    auto *expressionDelegate = new TreatmentExpressionDelegate(m_registry ? m_registry->engineHandle() : nullptr, m_treatmentTable);
    m_treatmentTable->setItemDelegateForColumn(3, expressionDelegate);
    treatmentLayout->addWidget(m_treatmentTable);
    m_treatmentMessage = new QLabel(treatmentPage);
    m_treatmentMessage->setWordWrap(true); treatmentLayout->addWidget(m_treatmentMessage);
    connect(expressionDelegate,&TreatmentExpressionDelegate::validationChanged,this,[this](bool valid,const QString& message,int) {
        m_treatmentMessage->setText(valid ? tr("Expression is valid.") : message);
    });
    auto *treatmentButtons = new QHBoxLayout;
    auto *addTreatment = new QPushButton(tr("Add pollutant"), treatmentPage);
    auto *removeTreatment = new QPushButton(tr("Remove pollutant"), treatmentPage);
    treatmentButtons->addWidget(addTreatment);treatmentButtons->addWidget(removeTreatment);treatmentButtons->addStretch();
    treatmentLayout->addLayout(treatmentButtons);
    details->addTab(treatmentPage,tr("Pollutant treatment"));
    stackLayout->addWidget(details,1);
    connect(m_flowModel, &QComboBox::currentIndexChanged, this, flowChanged);
    connect(m_richardsCells, &QSpinBox::valueChanged, this, flowChanged);
    for (auto* field : {m_richardsAtol, m_richardsRtol, m_richardsMaxStep}) connect(field, &QDoubleSpinBox::valueChanged, this, flowChanged);
    flowChanged();
    connect(m_treatmentModel,&QStandardItemModel::dataChanged,this,[this]{storeTreatmentRows_();});
    connect(addTreatment,&QPushButton::clicked,this,[this] {
        const int row=m_nodeLayerTable->currentIndex().row();
        if(row<0||m_nodeLayerModel->layers[row].kind==3)return;
        auto eng=m_registry ? static_cast<SWMM_Engine>(m_registry->engineHandle()) : nullptr;
        QStringList names;
        if(eng)for(int p=0;p<swmm_pollutant_count(eng);++p) {
            const QString name=QString::fromUtf8(swmm_pollutant_id(eng,p));
            bool used=false;for(int j=0;j<m_treatmentModel->rowCount();++j)used|=m_treatmentModel->index(j,0).data().toString()==name;
            if(!used)names.append(name);
        }
        if(names.isEmpty()) {m_treatmentMessage->setText(tr("Define pollutants in Model → Pollutant first, or edit the pollutants already listed."));return;}
        bool ok=false;const QString name=QInputDialog::getItem(this,tr("Layer treatment"),tr("Pollutant"),names,0,false,&ok);
        if(!ok)return;
        auto *id=new QStandardItem(name);id->setEditable(false);
        m_treatmentModel->appendRow({id,new QStandardItem(QStringLiteral("0")),new QStandardItem(QStringLiteral("0")),new QStandardItem});
        storeTreatmentRows_();
    });
    connect(removeTreatment,&QPushButton::clicked,this,[this] {
        m_treatmentModel->removeRow(m_treatmentTable->currentIndex().row());storeTreatmentRows_();
    });
    connect(m_nodeLayerTable->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this] { refreshLayerFields_(); refreshTreatmentRows_(); });
    connect(m_mediaCount, &QSpinBox::valueChanged, this, [this](int count) {
        if (!m_syncLayerFields) m_nodeLayerModel->setMediaCount(count, UnitSystem::instance() && UnitSystem::instance()->isSI());
    });
    auto changedLayers = [this] { if(!m_suppressFieldSync) m_layerDraftDirty=true; refreshLayerFields_(); refreshTreatmentRows_(); refreshLayerDiagram_(); };
    connect(m_nodeLayerModel, &QAbstractItemModel::rowsInserted, this, changedLayers);
    connect(m_nodeLayerModel, &QAbstractItemModel::rowsRemoved, this, changedLayers);
    connect(m_nodeLayerModel, &QAbstractItemModel::rowsMoved, this, changedLayers);
    connect(m_nodeLayerModel, &QAbstractItemModel::modelReset, this, changedLayers);
    auto *rowActions = new QHBoxLayout;
    for (int kind = 0; kind < 4; ++kind) {
        auto *add = new QPushButton(tr("Add %1").arg(QStringList{"SURFACE", "MEDIA", "AGGREGATE", "BOTTOM"}[kind]));
        rowActions->addWidget(add);
        connect(add, &QPushButton::clicked, this, [this, kind] { m_nodeLayerModel->append(kind, UnitSystem::instance() && UnitSystem::instance()->isSI());
            const auto& rows = m_nodeLayerModel->layers;
            int selected = kind == 0 ? 0 : rows.size() - 1;
            if (kind != 3 && kind != 0 && !rows.isEmpty() && rows.back().kind == 3) --selected;
            m_nodeLayerTable->selectRow(selected); refreshLayerDiagram_(); });
    }
    stackLayout->addLayout(rowActions);
    auto *editActions = new QHBoxLayout;
    auto *remove = new QPushButton(tr("Remove"));
    auto *up = new QPushButton(tr("Move up")); auto *down = new QPushButton(tr("Move down"));
    auto *apply = new QPushButton(tr("Apply layers and treatment"));
    for (auto *b : {remove, up, down, apply}) editActions->addWidget(b);
    stackLayout->addLayout(editActions);
    connect(remove, &QPushButton::clicked, this, [this] { m_nodeLayerModel->remove(m_nodeLayerTable->currentIndex().row()); refreshLayerDiagram_(); });
    for (auto pair : {qMakePair(up, -1), qMakePair(down, 1)})
        connect(pair.first, &QPushButton::clicked, this, [this, delta=pair.second] {
            const int r = m_nodeLayerTable->currentIndex().row(); m_nodeLayerModel->move(r, delta);
            m_nodeLayerTable->selectRow(qBound(0, r + delta, m_nodeLayerModel->rowCount() - 1)); refreshLayerDiagram_();
        });
    connect(m_nodeLayerModel, &QAbstractItemModel::dataChanged, this, [this] { if(!m_suppressFieldSync)m_layerDraftDirty=true; refreshLayerFields_(); refreshLayerDiagram_(); });
    connect(apply, &QPushButton::clicked, this, [this] { applyLayers_(); });
    tabs->addTab(m_nodeLayerPage, tr("Ordered layers"));
    rightLay->addWidget(tabs, 1);

    auto *note = new QLabel(
        tr("NODE controls use the ordered layer table. Apply layers updates every assigned storage node and its anchored outlets."),
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
    m_splitter->setSizes({ 170, 650, 300 });

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
    // Changing the LID type swaps the whole layer stack — reset the view with
    // it, unlike an ordinary field edit.
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { if (m_diagram) m_diagram->zoomToExtents(); });
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

bool LidControlEditorDialog::applyLayers_()
{
        if (!m_current || !m_registry) return false;
        auto eng = static_cast<SWMM_Engine>(m_registry->engineHandle());
        if (!eng) return false;
        int idx = swmm_lid_index(eng, m_current->name().toUtf8().constData());
        const bool created = idx < 0;
        if (idx < 0) {
            if (swmm_lid_add(eng, m_current->name().toUtf8().constData(), 8) != SWMM_OK) return false;
            idx = swmm_lid_index(eng, m_current->name().toUtf8().constData());
        }
        const auto& rows = m_nodeLayerModel->layers;
        const auto treatment = m_nodeLayerModel->treatmentRows();
        QVector<QByteArray> expressions; expressions.reserve(treatment.size());
        QVector<SWMM_LidLayerTreatment> rules; rules.reserve(treatment.size());
        for(const auto& t:treatment) {
            expressions.append(t.expression.toUtf8());
            rules.append({t.layer,swmm_pollutant_index(eng,t.pollutant.toUtf8().constData()),t.removal,t.decay,expressions.back().constData()});
        }
        const SWMM_LidRichardsOptions flow{m_flowModel->currentIndex(), m_richardsCells->value(), m_richardsAtol->value(), m_richardsRtol->value(), m_richardsMaxStep->value()};
        if (swmm_lid_node_configure_flow(eng, idx, rows.constData(), rows.size(), rules.constData(), rules.size(), &flow, m_nodeLayerModel->retention.constData()) != SWMM_OK) {
            if (created) swmm_lid_delete(eng, idx, nullptr);
            QMessageBox::warning(this, tr("Invalid layer stack"), tr("Check layer order, parameters and outlet anchors. Richards requires SURFACE first and explicit retention parameters on every porous layer: residual content below porosity (and media wilting point), alpha > 0, n > 1, l ≥ 0, and specific storage > 0. Alpha and specific storage use 1/m in both unit systems. Treatment requires known pollutants and valid rates/expressions."));
            return false;
        }
        m_current->treatments=treatment; m_current->setNodeLayers(rows); m_current->setNodeFlow(flow, m_nodeLayerModel->retention); m_current->clearDirty(); m_layerDraftDirty=false;
        if (m_layer) m_layer->markEdited();
        return true;
}
bool LidControlEditorDialog::finishLayerDraft_()
{
    if(!m_layerDraftDirty || !m_current || m_current->type()!=8)return true;
    const auto answer=QMessageBox::question(this,tr("Unsaved layer changes"),tr("Apply the layer and treatment changes to %1?").arg(m_current->name()),QMessageBox::Apply|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Apply);
    if(answer==QMessageBox::Cancel)return false;
    if(answer==QMessageBox::Apply)return applyLayers_();
    m_layerDraftDirty=false;return true;
}
void LidControlEditorDialog::done(int result)
{
    if(finishLayerDraft_())QDialog::done(result);
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
        m_typeCombo->setCurrentIndex(p->type() >= 0 && p->type() <= 8 ? p->type() : 0);
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

    m_nodeLayerModel->setLayers(p ? p->nodeLayers() : QVector<SWMM_LidNodeLayer>{});
    if (p && p->retention.size() == m_nodeLayerModel->layers.size()) m_nodeLayerModel->retention = p->retention;
    const auto flow = p ? p->flowOptions : SWMM_LidRichardsOptions{0, 8, 1.e-7, 1.e-5, 30};
    m_flowModel->setCurrentIndex(flow.model); m_richardsSettings->setVisible(flow.model == 1); m_richardsBoundaryNotice->setVisible(flow.model == 1); m_layerDetails->setTabVisible(1, flow.model == 1); m_richardsCells->setValue(flow.cells_per_layer);
    m_richardsAtol->setValue(flow.atol); m_richardsRtol->setValue(flow.rtol); m_richardsMaxStep->setValue(flow.max_step);
    for (int col = 10; col < 15; ++col) m_nodeLayerTable->setColumnHidden(col, flow.model != 1);
    m_nodeLayerModel->setTreatments(p ? p->treatments : QVector<openswmmvis::lid::LidLayerTreatment>{});
    const bool node = p && p->type() == 8;
    for (int i = 0; i < 4; ++i) m_tabs->setTabVisible(i, !node);
    m_tabs->setTabVisible(4, node);
    if (node) { m_tabs->setCurrentIndex(4); m_nodeLayerTable->selectRow(0); }
    m_nodeLayerPage->setEnabled(node);
    // Existing controls keep their type; create a new control for a new type.
    const auto eng = m_registry ? static_cast<SWMM_Engine>(m_registry->engineHandle()) : nullptr;
    m_typeCombo->setEnabled(p && (!eng || swmm_lid_index(eng, p->name().toUtf8().constData()) < 0));
    m_suppressFieldSync = prev;
    m_layerDraftDirty=false;
    refreshTreatmentRows_();
    refreshLayerDiagram_();
    // Binding a different control is a new subject; field edits keep the view.
    if (m_diagram) m_diagram->zoomToExtents();
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
    if(!finishLayerDraft_()) {
        QSignalBlocker blocker(m_listView->selectionModel());
        selectProviderInList_(m_current);return;
    }
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void LidControlEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    LidControlProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void LidControlEditorDialog::addLayeredControl_()
{
    if (!m_registry) return;
    auto *provider = m_registry->create(suggestUniqueName_());
    if (!provider) return;
    provider->setType(8);
    LidNodeLayerModel defaults;
    const bool si = UnitSystem::instance() && UnitSystem::instance()->isSI();
    for (int kind : {0, 1, 2, 3}) defaults.append(kind, si);
    provider->setNodeLayers(defaults.layers);
    selectProviderInList_(provider);
}

void LidControlEditorDialog::refreshLayerFields_()
{
    if (!m_mediaCount || !m_layerFields) return;
    m_syncLayerFields = true;
    m_mediaCount->setValue(qMax(1, m_nodeLayerModel->mediaCount()));
    int physical = 0;
    for (const auto& row : m_nodeLayerModel->layers) if (row.kind != 3) ++physical;
    m_layerSummary->setText(tr("%1 physical layers · top to bottom. Select a row to edit its parameters below.").arg(physical));
    const int row = m_nodeLayerTable->currentIndex().row();
    const bool si = UnitSystem::instance() && UnitSystem::instance()->isSI();
    const bool richards = m_flowModel && m_flowModel->currentIndex() == 1;
    for (int column = 1; column < 15; ++column) {
        const auto index = m_nodeLayerModel->index(row, column);
        const bool visible = index.isValid() && (m_nodeLayerModel->flags(index) & Qt::ItemIsEditable) &&
            (column < 10 || richards) && !(richards && (column == 6 || column == 7));
        auto *field = m_layerValues[column - 1];
        auto* form = column >= 10 ? m_retentionFields : m_layerFields;
        form->setRowVisible(field, visible);
        if (!visible) continue;
        QString label = m_nodeLayerModel->headerData(column, Qt::Horizontal).toString();
        if (richards && column == 3) label += tr(" (existing model)");
        if (column == 1 || column == 7) label += si ? tr(" (mm)") : tr(" (in)");
        if (column == 5) label += si ? tr(" (mm/hr)") : tr(" (in/hr)");
        qobject_cast<QLabel *>(form->labelForField(field))->setText(label);
        field->setValue(index.data(Qt::EditRole).toDouble());
    }
    auto* curves = static_cast<RetentionCurves*>(m_retentionCurves);
    const bool porous = row >= 0 && row < m_nodeLayerModel->layers.size() && (m_nodeLayerModel->layers[row].kind == 1 || m_nodeLayerModel->layers[row].kind == 2);
    curves->setVisible(richards && porous);
    if (porous && row < m_nodeLayerModel->retention.size()) { curves->material = m_nodeLayerModel->retention[row]; curves->porosity = m_nodeLayerModel->layers[row].params[1]; curves->update(); }
    m_syncLayerFields = false;
}

void LidControlEditorDialog::refreshTreatmentRows_()
{
    if(!m_treatmentModel)return;
    m_syncTreatment=true;
    m_treatmentModel->removeRows(0,m_treatmentModel->rowCount());
    const int row=m_nodeLayerTable->currentIndex().row();
    const bool valid=row>=0&&row<m_nodeLayerModel->treatments.size()&&m_nodeLayerModel->layers[row].kind!=3;
    m_treatmentTable->setEnabled(valid);
    if(valid)for(const auto& t:m_nodeLayerModel->treatments[row]) {
        auto *id=new QStandardItem(t.pollutant);id->setEditable(false);
        m_treatmentModel->appendRow({id,new QStandardItem(QString::number(t.removal,'g',15)),new QStandardItem(QString::number(t.decay,'g',15)),new QStandardItem(t.expression)});
    }
    m_treatmentMessage->setText(valid ? tr("Layer %1 — double-click a value to edit. Expressions use the existing treatment syntax and completion.").arg(row+1) : tr("Select a physical layer. BOTTOM is a seepage boundary, not a treatment layer."));
    m_syncTreatment=false;
}
void LidControlEditorDialog::storeTreatmentRows_()
{
    if(m_syncTreatment)return;
    m_layerDraftDirty=true;
    const int row=m_nodeLayerTable->currentIndex().row();
    if(row<0||row>=m_nodeLayerModel->treatments.size())return;
    auto& rules=m_nodeLayerModel->treatments[row];rules.clear();
    for(int i=0;i<m_treatmentModel->rowCount();++i) {
        bool r=false,k=false;
        const double removal=m_treatmentModel->index(i,1).data().toDouble(&r), decay=m_treatmentModel->index(i,2).data().toDouble(&k);
        rules.append({row+1,m_treatmentModel->index(i,0).data().toString(),r?removal:std::numeric_limits<double>::quiet_NaN(),k?decay:std::numeric_limits<double>::quiet_NaN(),m_treatmentModel->index(i,3).data().toString()});
    }
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
    const bool node = m_current->type() == 8;
    m_nodeLayerPage->setEnabled(node);
    for (int i = 0; i < 4; ++i) m_tabs->setTabVisible(i, !node);
    m_tabs->setTabVisible(4, node);
    if (node) { m_tabs->setCurrentIndex(4); refreshLayerDiagram_(); return; }
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
        m_typeCombo ? qBound(0, m_typeCombo->currentIndex(), 8) : 0);

    if (in.type == sv::LidType::Node) {
        for (const auto& row : m_nodeLayerModel->layers) {
            if (row.kind == 3) continue;
            const auto layer = row.kind == 0 ? sv::LidLayer::Surface : row.kind == 1 ? sv::LidLayer::Soil : sv::LidLayer::Storage;
            in.orderedLayers.append({layer, row.params[0], row.kind == 0 ? 1.0 - row.params[1] : row.params[1], row.kind == 1 ? row.params[4] : row.kind == 2 ? row.params[2] : 0.0});
        }
        in.lengthLabel = UnitSystem::instance() && UnitSystem::instance()->isSI() ? QStringLiteral("mm") : QStringLiteral("in");
        m_diagram->setModel(sv::buildLidLayerDiagram(in));
        return;
    }
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
