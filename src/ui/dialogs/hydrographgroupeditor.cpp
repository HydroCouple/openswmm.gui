/*!
 * \file   hydrographgroupeditor.cpp
 * \brief  Slice BS Phase 6.9.2 — HydrographGroupEditor implementation.
 */

#include "ui/dialogs/hydrographgroupeditor.h"

#include "layers/swmmmodellayer.h"
#include "layers/hydrographmodels.h"
#include "ui/widgets/chartaxisformatcontroller.h"
#include "ui/widgets/interactivechartview.h"

#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_inflows.h>

#include <QAbstractItemView>
#include <QAreaSeries>
#include <QChart>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLegendMarker>
#include <QLineEdit>
#include <QLineSeries>
#include <QListView>
#include <QColorDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QValueAxis>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kPlotSamples = 200;

constexpr const char *kSettingsKey       = "HydrographGroupEditor/geometry";
constexpr const char *kSettingsSplitter  = "HydrographGroupEditor/splitter";

// Slice AT.3 palette colours (matches the comparison-plot conventions). For
// a non-aggressive editor we lower the alpha so triangle interiors don't
// drown the composite line.
const std::array<QColor, 3> kTrianglePens = {
    QColor(0x1f, 0x77, 0xb4),   // short — blue
    QColor(0xff, 0x7f, 0x0e),   // medium — orange
    QColor(0x2c, 0xa0, 0x2c),   // long — green
};

QColor withAlpha(QColor c, int a) { c.setAlpha(a); return c; }

}  // namespace

// =========================================================================
// Construction
// =========================================================================

HydrographGroupEditor::HydrographGroupEditor(SWMMModelLayer *layer, QWidget *parent)
    : QDialog(parent)
    , m_layer(layer)
{
    setWindowTitle(tr("Unit Hydrographs"));
    setModal(false);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    resize(1280, 720);

    if (m_layer) {
        m_groupListModel = m_layer->hydrographGroupListModel();
        m_rtkModel       = m_layer->hydrographRtkModel();
        m_iaModel        = m_layer->hydrographIaModel();
        m_decayModel     = m_layer->hydrographDecayModel();
    }

    m_filterProxy = new QSortFilterProxyModel(this);
    m_filterProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_filterProxy->setFilterKeyColumn(0);
    if (m_groupListModel) m_filterProxy->setSourceModel(m_groupListModel);

    buildUi();

    if (m_layer) {
        connect(m_layer, &SWMMModelLayer::hydrographChanged,
                this, &HydrographGroupEditor::onHydrographChanged);
    }

    restoreState();

    // Select the first group (if any) so the middle / right panes are
    // populated rather than blank on open.
    if (m_groupList && m_filterProxy->rowCount() > 0) {
        m_groupList->setCurrentIndex(m_filterProxy->index(0, 0));
    } else {
        rebindModelsToCurrentSelection();
        refreshPreview();
        updateGroupSummary();
    }
}

HydrographGroupEditor::~HydrographGroupEditor() = default;

// =========================================================================
// Window/event plumbing
// =========================================================================

void HydrographGroupEditor::closeEvent(QCloseEvent *e)
{
    saveState();
    QDialog::closeEvent(e);
}

void HydrographGroupEditor::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
}

void HydrographGroupEditor::openForGroup(const QString &name)
{
    show();
    raise();
    activateWindow();

    if (!m_groupListModel || name.isEmpty()) return;
    const int sourceRow = m_groupListModel->indexOf(name);
    if (sourceRow < 0) return;
    const QModelIndex proxyIdx = m_filterProxy->mapFromSource(
        m_groupListModel->index(sourceRow));
    if (proxyIdx.isValid()) m_groupList->setCurrentIndex(proxyIdx);
}

QString HydrographGroupEditor::pickGroup(SWMMModelLayer *layer,
                                          const QString &initialName,
                                          QWidget *parent)
{
    HydrographGroupEditor editor(layer, parent);
    editor.setModal(true);
    editor.setWindowTitle(tr("Pick Unit Hydrograph"));
    // Pre-select the requested group; if it doesn't exist (e.g. empty
    // picker) the constructor already selected the first available group.
    if (!initialName.isEmpty()) editor.openForGroup(initialName);
    editor.exec();
    // Return whatever's currently highlighted — applies to all three
    // exit paths (Apply, OK, Close). All edits are already persisted
    // through the MVC layer regardless of which button was used.
    return editor.currentGroupName();
}

void HydrographGroupEditor::saveState()
{
    QSettings s;
    s.setValue(kSettingsKey,      saveGeometry());
    if (m_splitter) s.setValue(kSettingsSplitter, m_splitter->saveState());
}

void HydrographGroupEditor::restoreState()
{
    QSettings s;
    const auto geo = s.value(kSettingsKey).toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    if (m_splitter) {
        const auto st = s.value(kSettingsSplitter).toByteArray();
        if (!st.isEmpty()) m_splitter->restoreState(st);
        else m_splitter->setSizes({220, 540, 520});
    }
}

// =========================================================================
// UI construction
// =========================================================================

void HydrographGroupEditor::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    m_splitter->addWidget(buildLeftPane());
    m_splitter->addWidget(buildMiddlePane());
    m_splitter->addWidget(buildRightPane());
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 4);
    m_splitter->setStretchFactor(2, 3);
    root->addWidget(m_splitter, /*stretch=*/1);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setForegroundRole(QPalette::PlaceholderText);
    root->addWidget(m_summaryLabel);

    // Action bar — Apply commits any in-flight cell editor + refreshes the
    // plot; OK does the same + closes; Close just closes (changes already
    // landed live through the MVC layer).
    m_buttonBox = new QDialogButtonBox(this);
    auto *applyBtn = m_buttonBox->addButton(QDialogButtonBox::Apply);
    auto *okBtn    = m_buttonBox->addButton(QDialogButtonBox::Ok);
    auto *closeBtn = m_buttonBox->addButton(QDialogButtonBox::Close);
    applyBtn->setToolTip(tr("Commit any open cell editor and refresh the preview plot"));
    okBtn->setToolTip(tr("Commit any open cell editor, refresh, and close"));
    closeBtn->setToolTip(tr("Close the editor (edits commit live as you type)"));
    connect(applyBtn, &QPushButton::clicked, this, &HydrographGroupEditor::onApplyClicked);
    connect(okBtn,    &QPushButton::clicked, this, &HydrographGroupEditor::onOkClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    root->addWidget(m_buttonBox);
}

QWidget *HydrographGroupEditor::buildLeftPane()
{
    auto *host = new QWidget(this);
    auto *v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    // Filter line edit at the top — modern style with a placeholder.
    m_filterEdit = new QLineEdit(host);
    m_filterEdit->setPlaceholderText(tr("Search groups…"));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &HydrographGroupEditor::onFilterTextChanged);
    v->addWidget(m_filterEdit);

    // List view — bound to the filter proxy over the layer-owned list model.
    m_groupList = new QListView(host);
    m_groupList->setModel(m_filterProxy);
    m_groupList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_groupList->setEditTriggers(QAbstractItemView::SelectedClicked
                                  | QAbstractItemView::EditKeyPressed);
    m_groupList->setAlternatingRowColors(true);
    connect(m_groupList->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &) {
        onGroupSelectionChanged();
    });
    v->addWidget(m_groupList, /*stretch=*/1);

    // Flat-style toolbar of New / Delete / Rename actions. Uses the
    // shared icon resource so the editor matches the rest of the GUI.
    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(2);

    auto makeIconBtn = [host](const QString &iconPath, const QString &tip) {
        auto *b = new QToolButton(host);
        b->setIcon(QIcon(iconPath));
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(true);
        b->setIconSize({18, 18});
        return b;
    };

    m_addBtn    = makeIconBtn(QStringLiteral(":/swmmvis/New"),
                                tr("Create a new unit hydrograph"));
    m_removeBtn = makeIconBtn(QStringLiteral(":/swmmvis/Clear"),
                                tr("Delete the selected unit hydrograph"));
    m_renameBtn = makeIconBtn(QStringLiteral(":/swmmvis/SelectEdit"),
                                tr("Rename the selected unit hydrograph"));
    connect(m_addBtn,    &QToolButton::clicked, this, &HydrographGroupEditor::onNewGroup);
    connect(m_removeBtn, &QToolButton::clicked, this, &HydrographGroupEditor::onDeleteGroup);
    connect(m_renameBtn, &QToolButton::clicked, this, &HydrographGroupEditor::onRenameGroup);

    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addWidget(m_renameBtn);
    btnRow->addStretch(1);
    v->addLayout(btnRow);

    return host;
}

QWidget *HydrographGroupEditor::buildMiddlePane()
{
    auto *host = new QWidget(this);
    auto *v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    // Name label — bold, large.
    m_nameLabel = new QLabel(host);
    auto nameFont = m_nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSizeF(nameFont.pointSizeF() + 1.5);
    m_nameLabel->setFont(nameFont);
    v->addWidget(m_nameLabel);

    // Gage + season form.
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(4);

    m_gageCombo = new QComboBox(host);
    connect(m_gageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HydrographGroupEditor::onGageChanged);
    form->addRow(tr("Rain Gage:"), m_gageCombo);

    m_seasonCombo = new QComboBox(host);
    populateSeasonCombo();
    connect(m_seasonCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HydrographGroupEditor::onSeasonChanged);
    form->addRow(tr("Season:"), m_seasonCombo);

    v->addLayout(form);

    // Tabs.
    m_tabs = new QTabWidget(host);
    m_tabs->setDocumentMode(true);
    v->addWidget(m_tabs, /*stretch=*/1);

    // Tab 1 — RTK.
    {
        auto *page = new QWidget(m_tabs);
        auto *pv = new QVBoxLayout(page);
        pv->setContentsMargins(6, 6, 6, 6);

        auto *hint = new QLabel(tr(
            "R = fraction of rainfall that becomes I&&I. T = time to peak (h). "
            "K = ratio of recession-limb time to time-to-peak (≥ 0). Base time = "
            "T·(1+K); the falling limb spans K·T hours."), page);
        hint->setWordWrap(true);
        hint->setForegroundRole(QPalette::PlaceholderText);
        pv->addWidget(hint);

        m_rtkView = new QTableView(page);
        m_rtkView->setModel(m_rtkModel);
        m_rtkView->verticalHeader()->setVisible(false);
        m_rtkView->horizontalHeader()->setStretchLastSection(true);
        m_rtkView->setSelectionBehavior(QAbstractItemView::SelectItems);
        m_rtkView->setAlternatingRowColors(true);
        pv->addWidget(m_rtkView, /*stretch=*/1);

        m_tabs->addTab(page, tr("RTK"));
    }

    // Tab 2 — Initial Abstraction.
    {
        auto *page = new QWidget(m_tabs);
        auto *pv = new QVBoxLayout(page);
        pv->setContentsMargins(6, 6, 6, 6);
        pv->setSpacing(8);

        // Linear IA group (season-aware).
        auto *linGroup = new QGroupBox(tr("Linear IA (per season)"), page);
        auto *linV = new QVBoxLayout(linGroup);
        linV->setContentsMargins(8, 4, 8, 8);
        auto *linHint = new QLabel(tr(
            "Dmax — max initial-abstraction depth. Drec — recovery rate "
            "(depth/day). Do — initial depth already used at simulation start."),
            linGroup);
        linHint->setWordWrap(true);
        linHint->setForegroundRole(QPalette::PlaceholderText);
        linV->addWidget(linHint);
        m_iaView = new QTableView(linGroup);
        m_iaView->setModel(m_iaModel);
        m_iaView->verticalHeader()->setVisible(false);
        m_iaView->horizontalHeader()->setStretchLastSection(true);
        m_iaView->setSelectionBehavior(QAbstractItemView::SelectItems);
        m_iaView->setAlternatingRowColors(true);
        linV->addWidget(m_iaView);
        pv->addWidget(linGroup);

        // Exponential decay group (season-agnostic).
        auto *expGroup = new QGroupBox(tr("Exponential IA decay (season-agnostic)"),
                                         page);
        auto *expV = new QVBoxLayout(expGroup);
        expV->setContentsMargins(8, 4, 8, 8);
        auto *expHint = new QLabel(tr(
            "Independent of season — these parameters apply year-round; "
            "temperature drives recovery variation. Tick \"Active\" on a "
            "response to enable the exponential model for that response; "
            "untick to fall back to the linear model above.\n"
            "k_dep — depletion rate (1/depth). k_0 — base recovery rate "
            "(1/hr, gravity drainage / capillary). k_T — thermal recovery "
            "rate at T_ref (1/hr, ET-driven). T_ref — reference temperature "
            "(°C). theta_rec — temperature sensitivity of the thermal term "
            "(1/°C). T_freeze — recovery is suppressed when air temperature "
            "≤ T_freeze (°C)."), expGroup);
        expHint->setWordWrap(true);
        expHint->setForegroundRole(QPalette::PlaceholderText);
        expV->addWidget(expHint);
        m_decayView = new QTableView(expGroup);
        m_decayView->setModel(m_decayModel);
        m_decayView->verticalHeader()->setVisible(false);
        m_decayView->horizontalHeader()->setStretchLastSection(true);
        m_decayView->setSelectionBehavior(QAbstractItemView::SelectItems);
        m_decayView->setAlternatingRowColors(true);
        expV->addWidget(m_decayView);
        pv->addWidget(expGroup);

        m_tabs->addTab(page, tr("Initial Abstraction"));
    }

    return host;
}

QWidget *HydrographGroupEditor::buildRightPane()
{
    auto *host = new QWidget(this);
    auto *v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto *heading = new QLabel(tr("<b>Unit Hydrograph Preview</b>"), host);
    v->addWidget(heading);

    // Hover tooltip — a small floating label that updates as the cursor
    // moves over a series. Lives inside the right-pane host so it can
    // overlay the chart cleanly without being clipped.
    m_hoverLabel = new QLabel(host);
    m_hoverLabel->setStyleSheet(QStringLiteral(
        "QLabel { background-color: rgba(40,40,40,200); color: white; "
        "padding: 2px 6px; border-radius: 3px; font-size: 11px; }"));
    m_hoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hoverLabel->hide();

    m_chart = new QChart;
    m_chart->setTitle(QString());
    m_chart->setBackgroundRoundness(0);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->legend()->setVisible(true);
    m_chart->setMargins(QMargins(2, 2, 2, 2));

    m_xAxis = new QValueAxis;
    m_xAxis->setTitleText(tr("Time (h)"));
    m_xAxis->setTickCount(7);
    m_xAxis->setMinorTickCount(1);
    m_xAxis->setRange(0.0, 1.0);
    m_chart->addAxis(m_xAxis, Qt::AlignBottom);

    m_yAxis = new QValueAxis;
    m_yAxis->setTitleText(tr("Unit flow"));
    m_yAxis->setTickCount(7);
    m_yAxis->setMinorTickCount(1);
    m_yAxis->setRange(0.0, 1.0);
    m_chart->addAxis(m_yAxis, Qt::AlignLeft);

    // Per-chart axis number format (decimals / sig figs / custom printf),
    // editable via the chart's right-click "Chart Properties…" entry. Seeded
    // from the global Preferences default (replaces the former hardcoded "%g").
    m_axisFmt = new openswmmvis::ui::ChartAxisFormatController(m_chart, this);

    // Composite (summation) series — added FIRST so it draws behind the
    // triangles. Dashed grey line.
    m_sumSeries = new QLineSeries;
    m_sumSeries->setName(tr("Composite"));
    QPen sumPen(QColor(0x35, 0x35, 0x35));
    sumPen.setWidth(2);
    sumPen.setStyle(Qt::DashLine);
    m_sumSeries->setPen(sumPen);
    m_chart->addSeries(m_sumSeries);
    m_sumSeries->attachAxis(m_xAxis);
    m_sumSeries->attachAxis(m_yAxis);
    connect(m_sumSeries, &QLineSeries::hovered,
            this, &HydrographGroupEditor::onPlotHover);

    // Three filled triangles via QAreaSeries — added LONG → MEDIUM → SHORT
    // so the short-term triangle (narrowest, often inside the medium one)
    // renders on top and stays visible. The internal `m_areaSeries[r]`
    // array still indexes 0=Short / 1=Medium / 2=Long so the rest of the
    // refresh logic is index-symmetric.
    const QStringList responseNames = { tr("Short-Term"),
                                          tr("Medium-Term"),
                                          tr("Long-Term") };
    for (int r = 2; r >= 0; --r) {
        m_areaUpper[r] = new QLineSeries;
        m_areaSeries[r] = new QAreaSeries(m_areaUpper[r]);
        m_areaSeries[r]->setName(responseNames.at(r));
        QPen pen(kTrianglePens[r]);
        pen.setWidth(2);
        m_areaSeries[r]->setPen(pen);
        m_areaSeries[r]->setBrush(QBrush(withAlpha(kTrianglePens[r], 90)));
        m_chart->addSeries(m_areaSeries[r]);
        m_areaSeries[r]->attachAxis(m_xAxis);
        m_areaSeries[r]->attachAxis(m_yAxis);
        // QAreaSeries hover fires when the cursor enters/exits the filled
        // area; carries the upper-line point at that x. Lets us show
        // (t, q) at the peak/limbs.
        connect(m_areaSeries[r], &QAreaSeries::hovered, this,
                [this](const QPointF &pt, bool state) { onPlotHover(pt, state); });
    }

    // Click a legend marker to toggle the series on/off. Hidden series
    // grey out their marker so the user can tell what's off.
    const auto markers = m_chart->legend()->markers();
    for (auto *mk : markers) {
        connect(mk, &QLegendMarker::clicked,
                this, &HydrographGroupEditor::onLegendMarkerClicked);
    }

    m_chartView = new openswmmvis::ui::InteractiveChartView(m_chart, host);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(200);
    v->addWidget(m_chartView, /*stretch=*/1);
    m_hoverLabel->setParent(m_chartView);
    m_hoverLabel->raise();

    // Plot toolbar — small flat buttons for zoom / pan / style. Uses the
    // shared icon resource so the editor matches the rest of the GUI.
    auto *toolRow = new QHBoxLayout;
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(2);

    auto makeIconBtn = [host](const QString &iconPath, const QString &tip) {
        auto *b = new QToolButton(host);
        b->setIcon(QIcon(iconPath));
        b->setToolTip(tip);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(true);
        b->setIconSize({18, 18});
        return b;
    };
    auto *zoomInBtn   = makeIconBtn(QStringLiteral(":/swmmvis/ZoomIn"),
                                      tr("Zoom in (also: scroll wheel up)"));
    auto *zoomOutBtn  = makeIconBtn(QStringLiteral(":/swmmvis/ZoomOut"),
                                      tr("Zoom out (also: scroll wheel down)"));
    auto *panBtn      = makeIconBtn(QStringLiteral(":/swmmvis/Move"),
                                      tr("Pan mode (drag the chart)"));
    panBtn->setCheckable(true);
    auto *extentBtn   = makeIconBtn(QStringLiteral(":/swmmvis/Extent"),
                                      tr("Zoom to extent — reset to full UH range"));
    auto *styleBtn    = makeIconBtn(QStringLiteral(":/swmmvis/Style"),
                                      tr("Plot style — same options as right-clicking the chart"));

    using Mode = openswmmvis::ui::InteractiveChartView::Mode;
    connect(zoomInBtn,  &QToolButton::clicked, this, [this]{
        if (m_chartView) m_chartView->setMode(Mode::ZoomIn);
    });
    connect(zoomOutBtn, &QToolButton::clicked, this, [this]{
        if (m_chartView) m_chartView->setMode(Mode::ZoomOut);
    });
    connect(panBtn, &QToolButton::toggled, this, [this](bool on){
        if (m_chartView) m_chartView->setMode(on ? Mode::Pan : Mode::Select);
    });
    connect(extentBtn,  &QToolButton::clicked, this, [this]{
        if (m_chartView) m_chartView->resetZoom();
        refreshPreview();
    });
    connect(styleBtn, &QToolButton::clicked, this, [this, styleBtn]{
        // Open the same context menu as right-click, anchored to the button.
        showPlotStyleMenu(styleBtn->mapToGlobal(QPoint(0, styleBtn->height())));
    });

    toolRow->addWidget(zoomInBtn);
    toolRow->addWidget(zoomOutBtn);
    toolRow->addWidget(panBtn);
    toolRow->addWidget(extentBtn);
    toolRow->addSpacing(8);
    toolRow->addWidget(styleBtn);
    toolRow->addStretch(1);
    v->addLayout(toolRow);

    // Right-click context menu on the chart view for styling. The
    // InteractiveChartView emits chartContextMenuRequested(globalPos) on
    // right-click; we route it to the same menu builder so toolbar and
    // right-click stay in sync.
    if (m_chartView) {
        connect(m_chartView,
                &openswmmvis::ui::InteractiveChartView::chartContextMenuRequested,
                this, &HydrographGroupEditor::showPlotStyleMenu);
    }

    return host;
}

void HydrographGroupEditor::populateSeasonCombo()
{
    if (!m_seasonCombo) return;
    m_suppressSeasonSignal = true;
    m_seasonCombo->clear();
    m_seasonCombo->addItem(tr("All"), -1);
    const QStringList months = {
        tr("January"), tr("February"), tr("March"),     tr("April"),
        tr("May"),     tr("June"),     tr("July"),      tr("August"),
        tr("September"),tr("October"), tr("November"),  tr("December")
    };
    for (int i = 0; i < 12; ++i) m_seasonCombo->addItem(months.at(i), i);
    m_suppressSeasonSignal = false;
}

void HydrographGroupEditor::populateGageCombo()
{
    if (!m_gageCombo || !m_layer || !m_layer->engine()) return;
    m_suppressGageSignal = true;
    const QString currentGage = m_gageCombo->currentText();
    m_gageCombo->clear();
    m_gageCombo->addItem(tr("(none)"), QString());
    SWMM_Engine eng = m_layer->engine();
    const int n = swmm_gage_count(eng);
    for (int i = 0; i < n; ++i) {
        const char *id = swmm_gage_id(eng, i);
        if (id) m_gageCombo->addItem(QString::fromUtf8(id),
                                       QString::fromUtf8(id));
    }
    if (!currentGage.isEmpty()) {
        const int restore = m_gageCombo->findText(currentGage);
        if (restore >= 0) m_gageCombo->setCurrentIndex(restore);
    }
    m_suppressGageSignal = false;
}

// =========================================================================
// Slots
// =========================================================================

QString HydrographGroupEditor::currentGroupName() const
{
    if (!m_groupList || !m_filterProxy) return {};
    const QModelIndex proxyIdx = m_groupList->currentIndex();
    if (!proxyIdx.isValid()) return {};
    return m_filterProxy->data(proxyIdx, Qt::DisplayRole).toString();
}

int HydrographGroupEditor::currentMonth() const
{
    if (!m_seasonCombo) return -1;
    return m_seasonCombo->currentData().toInt();
}

void HydrographGroupEditor::onGroupSelectionChanged()
{
    const QString name = currentGroupName();
    if (m_nameLabel) {
        m_nameLabel->setText(name.isEmpty()
            ? tr("(no group selected)")
            : name);
    }
    populateGageCombo();
    // Reflect the engine's current rain-gage assignment in the combo.
    if (m_gageCombo && m_layer && m_layer->engine() && !name.isEmpty()) {
        m_suppressGageSignal = true;
        const int gn = swmm_hydrograph_gage_count(m_layer->engine());
        QString currentGage;
        for (int i = 0; i < gn; ++i) {
            char uh[64], gage[64];
            if (swmm_hydrograph_get_gage(m_layer->engine(), i,
                                          uh, sizeof(uh),
                                          gage, sizeof(gage)) != SWMM_OK)
                continue;
            if (name == QString::fromUtf8(uh)) {
                currentGage = QString::fromUtf8(gage);
                break;
            }
        }
        const int idx = currentGage.isEmpty()
            ? 0
            : std::max(0, m_gageCombo->findText(currentGage));
        m_gageCombo->setCurrentIndex(idx);
        m_suppressGageSignal = false;
    }
    rebindModelsToCurrentSelection();
    refreshPreview();
    updateGroupSummary();

    const bool hasSelection = !name.isEmpty();
    if (m_removeBtn) m_removeBtn->setEnabled(hasSelection);
    if (m_renameBtn) m_renameBtn->setEnabled(hasSelection);
    if (m_gageCombo) m_gageCombo->setEnabled(hasSelection);
    if (m_seasonCombo) m_seasonCombo->setEnabled(hasSelection);
    if (m_tabs)      m_tabs->setEnabled(hasSelection);
}

void HydrographGroupEditor::onSeasonChanged(int /*index*/)
{
    if (m_suppressSeasonSignal) return;
    // Commit any in-flight editor before swapping context so a half-typed
    // value doesn't land in the wrong (month, response) cell.
    if (m_rtkView) m_rtkView->setCurrentIndex({});
    if (m_iaView)  m_iaView->setCurrentIndex({});
    rebindModelsToCurrentSelection();
    refreshPreview();
}

void HydrographGroupEditor::onGageChanged(int /*index*/)
{
    if (m_suppressGageSignal) return;
    const QString name = currentGroupName();
    if (name.isEmpty() || !m_layer) return;
    const QString newGage = m_gageCombo->currentData().toString();
    m_layer->applyHydrographSetGage(name, newGage);
}

void HydrographGroupEditor::onNewGroup()
{
    if (!m_layer) return;

    // Slice BM.0-Add-New — replaces the prior NewDataObjectDialog flow.
    // The editor's middle pane already exposes the rain-gage combo + RTK
    // tables, so we only prompt for a name here and let the user fill in
    // the gage + R/T/K via the existing in-pane controls (live MVC sync).
    const QString suggested =
        m_layer->suggestUniqueDataObjectName(SWMMModelLayer::DataHydrographs);
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New Unit Hydrograph"),
        tr("Name:"), QLineEdit::Normal, suggested, &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    if (!m_layer->applyHydrographAddGroup(name, /*gageName=*/QString(),
                                           /*initialResponse=*/0)) {
        QMessageBox::warning(this, tr("New Unit Hydrograph"),
            tr("Engine rejected the create. The name may already be in use."));
        return;
    }
    // hydrographChanged() fires → onHydrographChanged → refresh.
    openForGroup(name);
}

void HydrographGroupEditor::beginNewGroup()
{
    show();
    raise();
    activateWindow();
    onNewGroup();
}

void HydrographGroupEditor::onDeleteGroup()
{
    const QString name = currentGroupName();
    if (name.isEmpty() || !m_layer) return;

    const auto reply = QMessageBox::question(this, tr("Delete Unit Hydrograph"),
        tr("Delete unit hydrograph \"%1\"?\n\n"
           "This will remove all per-month / per-response parameter rows, "
           "the rain-gage assignment, any RDII decay rows, and any [RDII] "
           "node assignments referencing this group.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    if (!m_layer->applyHydrographRemoveGroup(name)) {
        QMessageBox::warning(this, tr("Delete Unit Hydrograph"),
            tr("Engine rejected the delete."));
    }
}

void HydrographGroupEditor::onRenameGroup()
{
    const QString name = currentGroupName();
    if (name.isEmpty() || !m_layer) return;
    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Rename Unit Hydrograph"),
        tr("New name:"), QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || newName.isEmpty() || newName == name) return;
    if (!m_layer->applyHydrographRenameGroup(name, newName)) {
        QMessageBox::warning(this, tr("Rename Unit Hydrograph"),
            tr("Engine rejected the rename. The name may already be in use."));
        return;
    }
    openForGroup(newName);
}

void HydrographGroupEditor::onHydrographChanged(const QString &uhName)
{
    // Models refresh themselves (they're also subscribed to this signal).
    // We still need to update view chrome that doesn't come from a model:
    //   - the bold name label (in case of rename)
    //   - the gage combo (the user might have changed the gage from another UI)
    //   - the preview plot
    //   - the summary strip
    // If the changed name doesn't match our current group, only update the
    // summary; everything else keeps its current state.
    const QString cur = currentGroupName();
    if (uhName.isEmpty() || uhName == cur) {
        if (!cur.isEmpty()) {
            // Mirror gage from engine.
            populateGageCombo();
            if (m_layer && m_layer->engine() && m_gageCombo) {
                m_suppressGageSignal = true;
                const int gn = swmm_hydrograph_gage_count(m_layer->engine());
                QString currentGage;
                for (int i = 0; i < gn; ++i) {
                    char uh[64], gage[64];
                    if (swmm_hydrograph_get_gage(m_layer->engine(), i,
                                                  uh, sizeof(uh),
                                                  gage, sizeof(gage)) != SWMM_OK)
                        continue;
                    if (cur == QString::fromUtf8(uh)) {
                        currentGage = QString::fromUtf8(gage);
                        break;
                    }
                }
                const int idx = currentGage.isEmpty()
                    ? 0 : std::max(0, m_gageCombo->findText(currentGage));
                m_gageCombo->setCurrentIndex(idx);
                m_suppressGageSignal = false;
            }
        }
        refreshPreview();
    }
    updateGroupSummary();
}

void HydrographGroupEditor::onFilterTextChanged(const QString &text)
{
    if (m_filterProxy) m_filterProxy->setFilterFixedString(text);
}

// =========================================================================
// Model rebinding + preview math
// =========================================================================

void HydrographGroupEditor::rebindModelsToCurrentSelection()
{
    const QString name = currentGroupName();
    const int month = currentMonth();
    if (m_rtkModel)   m_rtkModel->setContext(name, month);
    if (m_iaModel)    m_iaModel->setContext(name, month);
    if (m_decayModel) m_decayModel->setContext(name);
}

void HydrographGroupEditor::refreshPreview()
{
    if (!m_chart || !m_layer || !m_layer->engine()) return;
    const QString name = currentGroupName();
    const int month = currentMonth();

    // Read R/T/K for each response from the engine. The engine's RDII
    // semantics are: a per-month entry overrides the ALL (-1) entry for
    // that response. We mirror that here so the preview always shows a
    // triangle if there's enough data anywhere — falling back to ALL
    // when the selected month has no per-month override.
    //
    // First pass: capture both ALL and the selected month into separate
    // arrays. Second pass: pick the per-month value when present,
    // otherwise the ALL value. Triangle vertices are (0, 0) → (T, peak)
    // → (K·T, 0) with peak = 2R / (K·T).
    std::array<double, 3> R = {0, 0, 0}, T = {0, 0, 0}, K = {0, 0, 0};
    std::array<bool, 3>   present = { false, false, false };
    // Render every row that has a usable time-to-peak. R and K can be 0 —
    // R=0 draws a flat zero triangle (visible as a line on the axis,
    // signals "this response is defined but contributes nothing"); K=0
    // draws a degenerate rising-spike with no recession limb. T > 0 is
    // the one hard requirement — without it the triangle has zero
    // horizontal extent and there's nothing to draw.
    //
    // Engine convention (see RDII.cpp:144, legacy rdii.c:373): K is the
    // recession-limb-to-peak-time ratio, so tBase = T*(1+K). K can be in
    // [0, ∞) — values < 1 (e.g. sample data's K=0.9) are legitimate and
    // were previously rejected by an over-strict gate.
    auto isUsable = [](double r_, double t_, double k_) {
        return r_ >= 0.0 && t_ > 0.0 && k_ >= 0.0;
    };
    if (!name.isEmpty()) {
        const QByteArray uh = name.toUtf8();
        const int n = swmm_hydrograph_count(m_layer->engine());

        std::array<double, 3> aR{}, aT{}, aK{};
        std::array<bool,   3> aP{ false, false, false };
        std::array<double, 3> mR{}, mT{}, mK{};
        std::array<bool,   3> mP{ false, false, false };

        char buf[64]; int em, er;
        double r_, t_, k_, dmax_, drec_, dinit_;
        for (int i = 0; i < n; ++i) {
            if (swmm_hydrograph_get(m_layer->engine(), i, buf, sizeof(buf),
                                    &em, &er, &r_, &t_, &k_,
                                    &dmax_, &drec_, &dinit_) != SWMM_OK)
                continue;
            if (std::strcmp(buf, uh.constData()) != 0) continue;
            if (er < 0 || er > 2) continue;
            if (em == -1) {
                aR[er] = r_; aT[er] = t_; aK[er] = k_;
                aP[er] = isUsable(r_, t_, k_);
            } else if (em == month) {
                mR[er] = r_; mT[er] = t_; mK[er] = k_;
                mP[er] = isUsable(r_, t_, k_);
            }
        }
        for (int r = 0; r < 3; ++r) {
            if (mP[r])      { R[r] = mR[r]; T[r] = mT[r]; K[r] = mK[r]; present[r] = true; }
            else if (aP[r]) { R[r] = aR[r]; T[r] = aT[r]; K[r] = aK[r]; present[r] = true; }
        }
    }

    // Decide plot extent: largest T·(1+K) across present responses, or 1.0
    // fallback to keep axes well-formed when nothing is present.
    double tMax = 0.0;
    for (int r = 0; r < 3; ++r) if (present[r]) tMax = std::max(tMax, T[r] * (1.0 + K[r]));
    if (tMax <= 0.0) tMax = 1.0;

    // Update each triangle and accumulate the composite ordinate-wise.
    // Use QLineSeries::replace(QList) — fires a single pointsReplaced()
    // signal that QAreaSeries (and the chart) observe for an atomic redraw.
    // append()/clear() per-point did NOT reliably re-render on cell edits.
    std::array<QList<QPointF>, 3> tri;
    for (int r = 0; r < 3; ++r) {
        if (!present[r]) {
            m_areaUpper[r]->replace(QList<QPointF>{});
            continue;
        }
        // Engine convention: tBase = T·(1+K). See RDII.cpp:144.
        const double tBase = T[r] * (1.0 + K[r]);
        const double peak  = (tBase > 0.0) ? (2.0 * R[r] / tBase) : 0.0;
        tri[r] = { QPointF(0.0, 0.0), QPointF(T[r], peak), QPointF(tBase, 0.0) };
        m_areaUpper[r]->replace(tri[r]);
    }

    // Build composite series sampled across [0, tMax].
    auto ordinate = [&](int r, double t) -> double {
        if (!present[r]) return 0.0;
        const double tBase = T[r] * (1.0 + K[r]);
        if (tBase <= 0.0) return 0.0;
        const double peak  = 2.0 * R[r] / tBase;
        if (t <= 0.0 || t >= tBase) return 0.0;
        if (t <= T[r]) return peak * (t / T[r]);
        return peak * (1.0 - (t - T[r]) / (tBase - T[r]));
    };
    QList<QPointF> sum;
    sum.reserve(kPlotSamples + 1);
    double yMax = 0.0;
    for (int s = 0; s <= kPlotSamples; ++s) {
        const double t = (tMax * s) / kPlotSamples;
        const double y = ordinate(0, t) + ordinate(1, t) + ordinate(2, t);
        sum.append(QPointF(t, y));
        if (y > yMax) yMax = y;
    }
    m_sumSeries->replace(sum);
    // Force the chart's scene to invalidate and redraw — without this, the
    // QAreaSeries fill sometimes ghosts the previous triangle until the
    // next mouse-over event arrives.
    if (m_chartView) m_chartView->viewport()->update();
    // Also include individual triangle peaks in yMax (so partial single-response
    // cases set a sane axis range).
    for (int r = 0; r < 3; ++r) {
        if (!present[r]) continue;
        const double peak = 2.0 * R[r] / (K[r] * T[r]);
        if (peak > yMax) yMax = peak;
    }
    if (yMax <= 0.0) yMax = 1.0;

    m_xAxis->setRange(0.0, tMax);
    m_yAxis->setRange(0.0, yMax * 1.05);

    // Update legend visibility per series so empty rows drop out.
    for (int r = 0; r < 3; ++r) {
        m_areaSeries[r]->setVisible(present[r]);
        const auto markers = m_chart->legend()->markers(m_areaSeries[r]);
        for (auto *mk : markers) mk->setVisible(present[r]);
    }
}

void HydrographGroupEditor::updateGroupSummary()
{
    if (!m_summaryLabel) return;
    if (!m_layer || !m_layer->engine()) {
        m_summaryLabel->setText({});
        return;
    }
    const int groups = swmm_hydrograph_group_count(m_layer->engine());
    const QString cur = currentGroupName();

    int rows = 0;
    int decayRows = 0;
    if (!cur.isEmpty()) {
        const QByteArray uh = cur.toUtf8();
        const int n = swmm_hydrograph_count(m_layer->engine());
        char buf[64]; int m, r;
        double r_, t_, k_, dmax_, drec_, dinit_;
        for (int i = 0; i < n; ++i) {
            if (swmm_hydrograph_get(m_layer->engine(), i, buf, sizeof(buf),
                                    &m, &r, &r_, &t_, &k_,
                                    &dmax_, &drec_, &dinit_) != SWMM_OK)
                continue;
            if (std::strcmp(buf, uh.constData()) == 0) ++rows;
        }
        const int dn = swmm_rdii_decay_count(m_layer->engine());
        char dbuf[64]; int rr;
        double a, b, c, d, e, f;
        for (int i = 0; i < dn; ++i) {
            if (swmm_rdii_decay_get(m_layer->engine(), i, dbuf, sizeof(dbuf),
                                    &rr, &a, &b, &c, &d, &e, &f) != SWMM_OK)
                continue;
            if (std::strcmp(dbuf, uh.constData()) == 0) ++decayRows;
        }
    }

    if (cur.isEmpty()) {
        m_summaryLabel->setText(tr("%n group(s) defined.", nullptr, groups));
    } else {
        m_summaryLabel->setText(tr("%1 group(s) defined  ·  \"%2\" has %3 parameter row(s)  ·  Active decay rows: %4")
            .arg(groups).arg(cur).arg(rows).arg(decayRows));
    }
}

// =========================================================================
// Action-bar slots + helper
// =========================================================================

void HydrographGroupEditor::commitOpenEditors()
{
    // Each QTableView holds at most one cell editor at a time. Calling
    // commitData(currentEditor) requires reaching into the view's private
    // state; the supported workaround is to move keyboard focus away from
    // the view, which causes the focused editor (if any) to commit and
    // close via the standard QAbstractItemDelegate close path.
    for (auto *view : { m_rtkView, m_iaView, m_decayView }) {
        if (!view) continue;
        QWidget *e = view->indexWidget(view->currentIndex());
        if (e) { e->clearFocus(); continue; }
        // Editors created via the default item-delegate path are not
        // findable through indexWidget; focus-shifting the view itself
        // forces the open editor's editorEvent to fire commitData.
        view->setFocus();
    }
    // Defensive — push focus to a neutral widget so any straggler editor
    // is flushed.
    if (m_summaryLabel) m_summaryLabel->setFocus();
}

void HydrographGroupEditor::onApplyClicked()
{
    commitOpenEditors();
    // The model's setData fires hydrographChanged → refreshPreview via the
    // signal chain. Refresh again here defensively in case the user clicked
    // Apply without any open editor (no-op edit, still refreshes the plot).
    refreshPreview();
}

void HydrographGroupEditor::onOkClicked()
{
    commitOpenEditors();
    refreshPreview();
    close();
}

void HydrographGroupEditor::onLegendMarkerClicked()
{
    auto *marker = qobject_cast<QLegendMarker *>(sender());
    if (!marker || !marker->series()) return;
    QAbstractSeries *series = marker->series();
    const bool show = !series->isVisible();
    series->setVisible(show);
    marker->setVisible(true);  // keep the marker itself visible (greyed)
    // Dim the marker label when its series is hidden so the user can tell.
    QBrush b = marker->labelBrush();
    QColor c = b.color();
    c.setAlpha(show ? 255 : 110);
    b.setColor(c);
    marker->setLabelBrush(b);
}

void HydrographGroupEditor::showPlotStyleMenu(const QPoint &globalPos)
{
    if (!m_chart) return;
    QMenu menu(this);
    const QStringList names = { tr("Short-Term"), tr("Medium-Term"), tr("Long-Term") };

    // Per-series submenu: toggle visibility + change fill color.
    for (int r = 0; r < 3; ++r) {
        if (!m_areaSeries[r]) continue;
        auto *sub = menu.addMenu(names.at(r));

        auto *visAct = sub->addAction(tr("Visible"));
        visAct->setCheckable(true);
        visAct->setChecked(m_areaSeries[r]->isVisible());
        connect(visAct, &QAction::toggled, this, [this, r](bool on) {
            if (m_areaSeries[r]) m_areaSeries[r]->setVisible(on);
        });

        auto *colorAct = sub->addAction(tr("Fill color…"));
        connect(colorAct, &QAction::triggered, this, [this, r]() {
            if (!m_areaSeries[r]) return;
            QColor cur = m_areaSeries[r]->brush().color();
            const QColor c = QColorDialog::getColor(cur, this,
                tr("Pick fill color"), QColorDialog::ShowAlphaChannel);
            if (!c.isValid()) return;
            m_areaSeries[r]->setBrush(QBrush(c));
            // Keep the outline pen in sync with the chosen colour but
            // fully opaque so the triangle edges stay crisp.
            QColor outline = c; outline.setAlpha(255);
            QPen pen = m_areaSeries[r]->pen();
            pen.setColor(outline);
            m_areaSeries[r]->setPen(pen);
        });

        auto *lineWidthMenu = sub->addMenu(tr("Outline width"));
        for (int w : {1, 2, 3, 4}) {
            auto *act = lineWidthMenu->addAction(QString::number(w));
            act->setCheckable(true);
            act->setChecked(m_areaSeries[r]->pen().width() == w);
            connect(act, &QAction::triggered, this, [this, r, w]() {
                if (!m_areaSeries[r]) return;
                QPen pen = m_areaSeries[r]->pen();
                pen.setWidth(w);
                m_areaSeries[r]->setPen(pen);
            });
        }
    }

    // Composite series style.
    if (m_sumSeries) {
        auto *sub = menu.addMenu(tr("Composite"));
        auto *visAct = sub->addAction(tr("Visible"));
        visAct->setCheckable(true);
        visAct->setChecked(m_sumSeries->isVisible());
        connect(visAct, &QAction::toggled, this, [this](bool on) {
            if (m_sumSeries) m_sumSeries->setVisible(on);
        });

        auto *colorAct = sub->addAction(tr("Line color…"));
        connect(colorAct, &QAction::triggered, this, [this]() {
            if (!m_sumSeries) return;
            const QColor c = QColorDialog::getColor(m_sumSeries->pen().color(),
                this, tr("Pick line color"));
            if (!c.isValid()) return;
            QPen pen = m_sumSeries->pen();
            pen.setColor(c);
            m_sumSeries->setPen(pen);
        });

        auto *styleMenu = sub->addMenu(tr("Line style"));
        const QList<QPair<QString, Qt::PenStyle>> styles = {
            { tr("Solid"),       Qt::SolidLine },
            { tr("Dash"),        Qt::DashLine },
            { tr("Dot"),         Qt::DotLine },
            { tr("DashDot"),     Qt::DashDotLine },
            { tr("DashDotDot"),  Qt::DashDotDotLine },
        };
        for (const auto &kv : styles) {
            auto *act = styleMenu->addAction(kv.first);
            act->setCheckable(true);
            act->setChecked(m_sumSeries->pen().style() == kv.second);
            connect(act, &QAction::triggered, this, [this, ps = kv.second]() {
                if (!m_sumSeries) return;
                QPen pen = m_sumSeries->pen();
                pen.setStyle(ps);
                m_sumSeries->setPen(pen);
            });
        }
    }

    menu.addSeparator();

    // Chart-level: legend visibility, antialias toggle, axis label format.
    auto *legendAct = menu.addAction(tr("Show legend"));
    legendAct->setCheckable(true);
    legendAct->setChecked(m_chart->legend()->isVisible());
    connect(legendAct, &QAction::toggled, this, [this](bool on) {
        if (m_chart) m_chart->legend()->setVisible(on);
    });

    auto *antialiasAct = menu.addAction(tr("Antialiasing"));
    antialiasAct->setCheckable(true);
    antialiasAct->setChecked(m_chartView && m_chartView->renderHints()
                              .testFlag(QPainter::Antialiasing));
    connect(antialiasAct, &QAction::toggled, this, [this](bool on) {
        if (!m_chartView) return;
        m_chartView->setRenderHint(QPainter::Antialiasing, on);
    });

    auto *fmtMenu = menu.addMenu(tr("Axis label format"));
    const QList<QPair<QString, QString>> fmts = {
        { tr("Adaptive (%g — recommended)"),    QStringLiteral("%g")   },
        { tr("2 decimals (%.2f)"),               QStringLiteral("%.2f") },
        { tr("4 decimals (%.4f)"),               QStringLiteral("%.4f") },
        { tr("Scientific (%e)"),                 QStringLiteral("%e")   },
    };
    for (const auto &kv : fmts) {
        auto *act = fmtMenu->addAction(kv.first);
        act->setCheckable(true);
        act->setChecked(m_xAxis && m_xAxis->labelFormat() == kv.second);
        connect(act, &QAction::triggered, this, [this, fmt = kv.second]() {
            if (m_xAxis) m_xAxis->setLabelFormat(fmt);
            if (m_yAxis) m_yAxis->setLabelFormat(fmt);
        });
    }

    menu.addSeparator();
    auto *resetAct = menu.addAction(tr("Reset zoom"));
    connect(resetAct, &QAction::triggered, this, [this]() {
        if (m_chartView) m_chartView->resetZoom();
    });
    auto *extentAct = menu.addAction(tr("Zoom to extent"));
    connect(extentAct, &QAction::triggered, this, [this]() {
        if (m_chartView) m_chartView->resetZoom();
        refreshPreview();
    });

    menu.addSeparator();
    auto *propsAct = menu.addAction(tr("Chart Properties…"));
    connect(propsAct, &QAction::triggered, this, [this]() {
        if (m_axisFmt) m_axisFmt->openDialog(this);
    });

    menu.exec(globalPos);
}

void HydrographGroupEditor::onPlotHover(const QPointF &point, bool state)
{
    if (!m_hoverLabel || !m_chartView || !m_chart) return;
    if (!state) { m_hoverLabel->hide(); return; }

    auto *series = qobject_cast<QAbstractSeries *>(sender());
    const QString name = series ? series->name() : QString();

    // Format with %g for adaptive significant figures.
    m_hoverLabel->setText(QStringLiteral("%1\nt = %2 h\nq = %3")
        .arg(name)
        .arg(point.x(), 0, 'g', 4)
        .arg(point.y(), 0, 'g', 4));
    m_hoverLabel->adjustSize();

    // Position the label slightly above-right of the data point. Map the
    // chart-space coordinate to viewport pixels via the chart's value-
    // mapping; clamp so the label doesn't fall outside the chart view.
    const QPointF scenePos = m_chart->mapToPosition(point);
    const QPoint  viewPos  = m_chartView->mapFromScene(scenePos);
    QPoint pos = viewPos + QPoint(10, -28);
    const int maxX = m_chartView->width() - m_hoverLabel->width() - 4;
    const int maxY = m_chartView->height() - m_hoverLabel->height() - 4;
    pos.setX(std::clamp(pos.x(), 4, std::max(4, maxX)));
    pos.setY(std::clamp(pos.y(), 4, std::max(4, maxY)));
    m_hoverLabel->move(pos);
    m_hoverLabel->show();
    m_hoverLabel->raise();
}
