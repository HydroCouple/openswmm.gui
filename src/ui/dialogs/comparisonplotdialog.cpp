/*!
 * \file   comparisonplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/comparisonplotdialog.h"
#include "ui/theme/iconfactory.h"
#include "ui/theme/themehelpers.h"

#include "core/preferencesmanager.h"
#include "core/swmmdatetime.h"
#include "plot/swmmoutrunlayer.h"
#include "plot/mesh2drunlayer.h"
#include "plot/observedcsvrunlayer.h"
#include "plot/fitmetrics.h"
#include "plot/seriespairing.h"
#include "plot/chartproperties.h"
#include "ui/dialogs/chartpropertiesdialog.h"
#include "ui/dialogs/comparisonpairsdialog.h"
#include "ui/widgets/interactivechartview.h"
#include "ui/widgets/rangeslider.h"
#include "ui/widgets/seriesstyleeditor.h"
#include "ui/widgets/statssummarypanel.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"

#include <QAction>
#include <QActionGroup>
#include <QChart>
#include <QChartView>
#include <QCursor>
#include <QFileDialog>
#include <QPainter>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTimeAxis>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLegendMarker>
#include <QLineEdit>
#include <QLineSeries>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScatterSeries>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace openswmmvis::ui {

using namespace openswmmvis::plot;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ComparisonPlotDialog::ComparisonPlotDialog(QWidget *parent)
    : QDialog(parent),
      m_model(std::make_unique<ComparisonPlotModel>())
{
    setWindowTitle(tr("Comparison Plot"));
    // Iteration 2 (D2) — naming wires the app-wide layout persistence
    // (geometry, the three splitters, the checkable view toggles).
    setObjectName(QStringLiteral("ComparisonPlotDialog"));
    resize(1100, 720);
    buildUi();

    connect(m_model.get(), &ComparisonPlotModel::runSourceAdded,
            this, &ComparisonPlotDialog::onRunSourceAdded);
    connect(m_model.get(), &ComparisonPlotModel::runSourceRemoved,
            this, &ComparisonPlotDialog::onRunSourceRemoved);
    connect(m_model.get(), &ComparisonPlotModel::seriesAdded,
            this, &ComparisonPlotDialog::onSeriesAdded);
    connect(m_model.get(), &ComparisonPlotModel::seriesRemoved,
            this, &ComparisonPlotDialog::onSeriesRemoved);
    connect(m_model.get(), &ComparisonPlotModel::styleChanged,
            this, &ComparisonPlotDialog::onStyleChanged);
    connect(m_model.get(), &ComparisonPlotModel::rowsChanged,
            this, &ComparisonPlotDialog::onRowsChanged);
    connect(m_model.get(), &ComparisonPlotModel::animationTimeChanged,
            this, &ComparisonPlotDialog::onAnimationTimeChanged);
    // Phase 5 — 1v1 pair list edits rebuild the scatter column.
    connect(m_model.get(), &ComparisonPlotModel::pairsChanged,
            this, [this]() { rebuildCharts(); });
}

ComparisonPlotDialog::~ComparisonPlotDialog() = default;

void ComparisonPlotDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // ----- Top toolbar (Slice AT.2) ----------------------------------------
    buildToolBar();
    root->addWidget(m_toolBar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("main"));

    // ----- Left pane: series tree + add/remove ------------------------------
    m_leftHost = new QWidget(this);
    auto *leftCol = new QVBoxLayout(m_leftHost);
    leftCol->setContentsMargins(0, 0, 0, 0);

    m_seriesTree = new QTreeWidget(m_leftHost);
    m_seriesTree->setHeaderLabels({tr("Series")});
    m_seriesTree->header()->setStretchLastSection(true);
    m_seriesTree->setRootIsDecorated(true);
    // Slice AT.3 — right-click context menu on tree items.
    m_seriesTree->setContextMenuPolicy(Qt::CustomContextMenu);
    leftCol->addWidget(m_seriesTree, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn     = new QPushButton(tr("Add Series…"), m_leftHost);
    m_loadObsBtn = new QPushButton(tr("Load Observed…"), m_leftHost);
    m_removeBtn  = new QPushButton(tr("Remove"), m_leftHost);
    m_removeBtn->setEnabled(false);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_loadObsBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch(1);
    leftCol->addLayout(btnRow);

    m_splitter->addWidget(m_leftHost);
    // CP.1 — keep the left pane reachable: minimum width ensures the
    // handle stays grabbable when sized small, and a non-collapsible
    // policy prevents the user from snapping it to zero (the View →
    // Show Series Panel toggle is now the only way to fully hide it,
    // and it remembers the prior width on restore).
    m_leftHost->setMinimumWidth(60);
    m_splitter->setChildrenCollapsible(false);

    // ----- Right pane: vertical splitter of chart rows ----------------------
    // Slice AT.2: rows live in a Qt::Vertical QSplitter so the user can
    // drag handles between them; on add we equalise sizes so the first
    // chart fills the entire pane until a second one shows up.
    // Slice AT.3: wrap the chart splitter in an outer vertical splitter
    // that also hosts the RangeSliderWidget and StatsSummaryPanel.
    m_chartsOuter = new QSplitter(Qt::Vertical, this);
    m_chartsOuter->setObjectName(QStringLiteral("chartsOuter"));
    m_chartsOuter->setChildrenCollapsible(true);
    m_chartsOuter->setHandleWidth(4);

    m_chartsScroll = new QScrollArea(m_chartsOuter);
    m_chartsScroll->setWidgetResizable(true);

    m_chartsSplitter = new QSplitter(Qt::Vertical, m_chartsScroll);
    m_chartsSplitter->setObjectName(QStringLiteral("charts"));
    m_chartsSplitter->setChildrenCollapsible(false);
    m_chartsSplitter->setHandleWidth(6);
    m_chartsScroll->setWidget(m_chartsSplitter);
    m_chartsOuter->addWidget(m_chartsScroll);

    // Slice AT.3 — X-range slider + datetime label, wrapped in a thin row.
    m_sliderHost = new QWidget(m_chartsOuter);
    auto *sliderRow  = new QHBoxLayout(m_sliderHost);
    sliderRow->setContentsMargins(2, 0, 2, 0);
    sliderRow->setSpacing(8);
    m_rangeSlider = new RangeSliderWidget(m_sliderHost);
    m_rangeSlider->setMinimumHeight(20);
    m_rangeLabel  = new QLabel(m_sliderHost);
    m_rangeLabel->setMinimumWidth(220);
    m_rangeLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    sliderRow->addWidget(m_rangeSlider, 1);
    sliderRow->addWidget(m_rangeLabel,  0);
    m_sliderHost->setMinimumHeight(24);
    m_chartsOuter->addWidget(m_sliderHost);

    // Slice AT.3 — Statistics Summary panel (per-attribute tabs).
    // CP.1 — relax minimum height (was 160px) so the user can drag the
    // splitter handle down to a thin sliver; "View → Show Stats Panel"
    // gives a full collapse + restore that remembers the prior height.
    m_statsPanel = new StatsSummaryPanel(m_chartsOuter);
    m_statsPanel->setModel(m_model.get());
    m_statsPanel->setMinimumHeight(60);
    m_chartsOuter->addWidget(m_statsPanel);

    // Charts dominate; slider is fixed at 20 px; stats panel resizable
    // around its 160 px minimum.
    m_chartsOuter->setStretchFactor(0, 4);  // charts
    m_chartsOuter->setStretchFactor(1, 0);  // slider — fixed-height
    m_chartsOuter->setStretchFactor(2, 1);  // stats

    // Wire range-slider → row 0's xAxis + update the datetime label.
    connect(m_rangeSlider, &RangeSliderWidget::rangeChanged,
            this, [this](qreal lo, qreal hi) {
                if (m_syncingSlider) return;
                if (m_rowWidgets.isEmpty() || !m_xFullMin.isValid()) return;
                const qint64 fullMs = m_xFullMax.toMSecsSinceEpoch()
                                       - m_xFullMin.toMSecsSinceEpoch();
                if (fullMs <= 0) return;
                const QDateTime loDt = m_xFullMin.addMSecs(qint64(lo * fullMs));
                const QDateTime hiDt = m_xFullMin.addMSecs(qint64(hi * fullMs));
                if (auto *xAxis = m_rowWidgets[0].xAxis)
                    xAxis->setRange(loDt, hiDt);
                if (m_rangeLabel) {
                    m_rangeLabel->setText(
                        QStringLiteral("%1  →  %2")
                            .arg(loDt.toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                                 hiDt.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
                }
            });

    m_splitter->addWidget(m_chartsOuter);
    m_splitter->setSizes({280, 820});
    root->addWidget(m_splitter, 1);

    // ----- Bottom: close button ---------------------------------------------
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    // Wiring
    connect(m_addBtn,     &QPushButton::clicked,
            this,         &ComparisonPlotDialog::onAddSeriesClicked);
    connect(m_loadObsBtn, &QPushButton::clicked,
            this,         &ComparisonPlotDialog::onLoadObservedClicked);
    connect(m_removeBtn,  &QPushButton::clicked,
            this,         &ComparisonPlotDialog::onRemoveSelectedClicked);
    connect(m_seriesTree, &QTreeWidget::itemSelectionChanged,
            this,         &ComparisonPlotDialog::onSeriesItemSelectionChanged);
    connect(m_seriesTree, &QTreeWidget::itemDoubleClicked,
            this,         &ComparisonPlotDialog::onSeriesItemDoubleClicked);
    // Slice AT.3 — checkbox toggle + context menu.
    connect(m_seriesTree, &QTreeWidget::itemChanged,
            this,         &ComparisonPlotDialog::onSeriesItemChanged);
    connect(m_seriesTree, &QTreeWidget::customContextMenuRequested,
            this,         &ComparisonPlotDialog::onSeriesTreeContextMenu);
}

// ---------------------------------------------------------------------------
// Slice AT.2 — toolbar
// ---------------------------------------------------------------------------

void ComparisonPlotDialog::buildToolBar()
{
    m_toolBar = new QToolBar(this);
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(20, 20));

    // Mode group — mutually exclusive checkables (Select / Pan / ZoomIn / ZoomOut).
    m_modeActions = new QActionGroup(this);
    m_modeActions->setExclusive(true);

    auto makeModeAction = [this](const QString& text, const QString& tip)
    {
        auto *a = new QAction(text, this);
        a->setCheckable(true);
        a->setStatusTip(tip);
        a->setToolTip(tip);
        m_modeActions->addAction(a);
        m_toolBar->addAction(a);
        connect(a, &QAction::triggered,
                this, &ComparisonPlotDialog::onModeActionTriggered);
        return a;
    };
    m_actSelect  = makeModeAction(tr("Select"),   tr("Identify values under cursor"));
    m_actPan     = makeModeAction(tr("Pan"),      tr("Drag to pan the view"));
    m_actZoomIn  = makeModeAction(tr("Zoom In"),  tr("Click or drag a rectangle to zoom in"));
    m_actZoomOut = makeModeAction(tr("Zoom Out"), tr("Click or drag a rectangle to zoom out"));
    // Slice AT.3 — SVG icons sourced from the existing :/swmmvis/ resource set.
    m_actSelect ->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Select")));
    m_actPan    ->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Move")));
    m_actZoomIn ->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomIn")));
    m_actZoomOut->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("ZoomOut")));
    m_actSelect->setChecked(true);

    m_toolBar->addSeparator();

    m_actFit = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Extent")),
                            tr("Fit"), this);
    m_actFit->setStatusTip(tr("Reset axes to data extents on all chart rows"));
    m_actFit->setToolTip(m_actFit->statusTip());
    connect(m_actFit, &QAction::triggered,
            this, &ComparisonPlotDialog::onFitClicked);
    m_toolBar->addAction(m_actFit);

    m_actExport = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportImage")),
                               tr("Export PNG…"), this);
    m_actExport->setStatusTip(tr("Save the chart pane as a PNG image"));
    m_actExport->setToolTip(m_actExport->statusTip());
    connect(m_actExport, &QAction::triggered,
            this, &ComparisonPlotDialog::onExportPngClicked);
    m_toolBar->addAction(m_actExport);

    m_toolBar->addSeparator();

    m_actAnimCursor = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("TimeCursor")),
                                    tr("Show Animation Cursor"), this);
    m_actAnimCursor->setObjectName(QStringLiteral("showAnimCursor"));
    m_actAnimCursor->setCheckable(true);
    m_actAnimCursor->setChecked(true);
    m_actAnimCursor->setShortcut(QKeySequence(tr("Ctrl+Shift+C")));
    m_actAnimCursor->setStatusTip(tr("Toggle the vertical line at the current animation time"));
    m_actAnimCursor->setToolTip(m_actAnimCursor->statusTip());
    connect(m_actAnimCursor, &QAction::toggled,
            this, &ComparisonPlotDialog::onAnimationCursorToggled);
    m_toolBar->addAction(m_actAnimCursor);

    m_actAddSystem = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("SystemSeries")),
                                   tr("Add System Series…"), this);
    m_actAddSystem->setStatusTip(tr("Plot a system-wide variable (rainfall, runoff, flooding, …)"));
    m_actAddSystem->setToolTip(m_actAddSystem->statusTip());
    connect(m_actAddSystem, &QAction::triggered,
            this, &ComparisonPlotDialog::onAddSystemSeriesClicked);
    m_toolBar->addAction(m_actAddSystem);

    m_actAddFromMap = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("PickFromMap")),
                                    tr("Add from Map…"), this);
    m_actAddFromMap->setObjectName(QStringLiteral("addFromMap"));
    m_actAddFromMap->setCheckable(true);
    m_actAddFromMap->setStatusTip(tr("Click objects on the map to add them as series"));
    m_actAddFromMap->setToolTip(m_actAddFromMap->statusTip());
    connect(m_actAddFromMap, &QAction::toggled,
            this, &ComparisonPlotDialog::addFromMapToggled);
    m_toolBar->addAction(m_actAddFromMap);

    // ----- CP.1 — View toggles ---------------------------------------------
    // Three independent show/hide actions for each piece of chrome plus a
    // convenience "Charts Only" master toggle. Each remembers the prior
    // splitter sizing so re-show restores the user's width/height instead
    // of resetting to defaults. Solves the "I collapsed the left pane
    // and now I can't grab the handle" trap.
    m_toolBar->addSeparator();

    m_actShowSeries = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("PanelLeft")),
                                tr("Series Panel"), this);
    m_actShowSeries->setObjectName(QStringLiteral("showSeriesPanel"));
    m_actShowSeries->setCheckable(true);
    m_actShowSeries->setChecked(true);
    m_actShowSeries->setStatusTip(tr("Show/hide the series tree on the left"));
    m_actShowSeries->setToolTip(m_actShowSeries->statusTip());
    connect(m_actShowSeries, &QAction::toggled,
            this, &ComparisonPlotDialog::onShowSeriesToggled);
    m_toolBar->addAction(m_actShowSeries);

    m_actShowSlider = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("PanelBottomSlider")),
                                tr("Range Slider"), this);
    m_actShowSlider->setObjectName(QStringLiteral("showRangeSlider"));
    m_actShowSlider->setCheckable(true);
    m_actShowSlider->setChecked(true);
    m_actShowSlider->setStatusTip(tr("Show/hide the X-range slider under the charts"));
    m_actShowSlider->setToolTip(m_actShowSlider->statusTip());
    connect(m_actShowSlider, &QAction::toggled,
            this, &ComparisonPlotDialog::onShowSliderToggled);
    m_toolBar->addAction(m_actShowSlider);

    m_actShowStats = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("PanelStats")),
                               tr("Stats Panel"), this);
    m_actShowStats->setObjectName(QStringLiteral("showStatsPanel"));
    m_actShowStats->setCheckable(true);
    m_actShowStats->setChecked(true);
    m_actShowStats->setStatusTip(tr("Show/hide the statistics summary at the bottom"));
    m_actShowStats->setToolTip(m_actShowStats->statusTip());
    connect(m_actShowStats, &QAction::toggled,
            this, &ComparisonPlotDialog::onShowStatsToggled);
    m_toolBar->addAction(m_actShowStats);

    // COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 4 — optional 1v1 column.
    m_actShow1v1 = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Plot1v1")),
                             tr("1v1 Plots"), this);
    m_actShow1v1->setObjectName(QStringLiteral("show1v1Plots"));
    m_actShow1v1->setCheckable(true);
    m_actShow1v1->setChecked(true);
    m_actShow1v1->setStatusTip(tr("Show/hide the 1v1 comparison scatter column"));
    m_actShow1v1->setToolTip(m_actShow1v1->statusTip());
    connect(m_actShow1v1, &QAction::toggled,
            this, &ComparisonPlotDialog::onShow1v1Toggled);
    m_toolBar->addAction(m_actShow1v1);

    // COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5 — configure the pairs.
    m_actConfig1v1 = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("Configure1v1")),
                               tr("Configure 1v1…"), this);
    m_actConfig1v1->setStatusTip(tr("Choose which series pair up in the 1v1 "
                                    "comparison plots"));
    m_actConfig1v1->setToolTip(m_actConfig1v1->statusTip());
    connect(m_actConfig1v1, &QAction::triggered,
            this, &ComparisonPlotDialog::onConfigure1v1Clicked);
    m_toolBar->addAction(m_actConfig1v1);

    m_actChartsOnly = new QAction(openswmmvis::ui::IconFactory::icon(QStringLiteral("ChartsOnly")),
                                tr("Charts Only"), this);
    m_actChartsOnly->setObjectName(QStringLiteral("chartsOnly"));
    m_actChartsOnly->setCheckable(true);
    m_actChartsOnly->setStatusTip(tr("Hide series, slider, and stats panels to show only the charts"));
    m_actChartsOnly->setToolTip(m_actChartsOnly->statusTip());
    m_actChartsOnly->setShortcut(QKeySequence(tr("Ctrl+Shift+F")));
    connect(m_actChartsOnly, &QAction::toggled,
            this, &ComparisonPlotDialog::onChartsOnlyToggled);
    m_toolBar->addAction(m_actChartsOnly);
}

void ComparisonPlotDialog::setAddFromMapChecked(bool checked)
{
    if (m_actAddFromMap)
        m_actAddFromMap->setChecked(checked);
}

void ComparisonPlotDialog::onModeActionTriggered()
{
    propagateModeToRows();
}

void ComparisonPlotDialog::propagateModeToRows()
{
    using Mode = InteractiveChartView::Mode;
    Mode m = Mode::Select;
    if      (m_actPan     && m_actPan->isChecked())     m = Mode::Pan;
    else if (m_actZoomIn  && m_actZoomIn->isChecked())  m = Mode::ZoomIn;
    else if (m_actZoomOut && m_actZoomOut->isChecked()) m = Mode::ZoomOut;
    for (RowWidgets &rw : m_rowWidgets) {
        if (rw.view)        rw.view->setMode(m);
        // COMPARISON_PLOT_1V1_AND_TREE_PLAN — 1v1 scatter views follow
        // the same toolbar mode (Select/Pan/ZoomIn/ZoomOut).
        if (rw.scatterView) rw.scatterView->setMode(m);
    }
}

void ComparisonPlotDialog::onFitClicked()
{
    // AT.3 polish — when a Shift-drag X selection is active, refit Y to
    // the windowed samples per row instead of rebuilding to full extent.
    // Otherwise fall back to the canonical rebuildCharts.
    if (!m_xSelLo.isValid() || !m_xSelHi.isValid()
        || m_xSelLo >= m_xSelHi
        || m_rowWidgets.isEmpty())
    {
        rebuildCharts();
        return;
    }

    const QVector<plot::AttributeRow> &rows = m_model->rows();
    for (int r = 0; r < m_rowWidgets.size() && r < rows.size(); ++r) {
        const auto &row = rows.at(r);
        RowWidgets &rw  = m_rowWidgets[r];
        if (!rw.yAxis || !rw.xAxis) continue;

        double yMin = std::numeric_limits<double>::infinity();
        double yMax = -std::numeric_limits<double>::infinity();
        for (int sIdx : row.seriesIndices) {
            plot::SeriesData data;
            m_model->resolveSeries(sIdx, data);
            if (!data.ok) continue;
            for (std::size_t i = 0; i < data.values.size(); ++i) {
                const QDateTime t = core::swmmDateTimeToQDateTime(data.timesJulian[i]);
                if (t < m_xSelLo || t > m_xSelHi) continue;
                const double v = data.values[i];
                if (!std::isfinite(v)) continue;
                if (v < yMin) yMin = v;
                if (v > yMax) yMax = v;
            }
        }
        if (std::isfinite(yMin) && std::isfinite(yMax) && yMax > yMin) {
            const double pad = 0.05 * (yMax - yMin);
            rw.yAxis->setRange(yMin - pad, yMax + pad);
        }
        rw.xAxis->setRange(m_xSelLo, m_xSelHi);   // X follows the selection too
    }
}

void ComparisonPlotDialog::onExportPngClicked()
{
    if (m_rowWidgets.isEmpty()) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PNG"), QString(), tr("PNG image (*.png)"));
    if (path.isEmpty()) return;

    // Vertically stitch all row frames into one image.
    int totalH = 0;
    int maxW   = 0;
    QVector<QPixmap> pieces;
    pieces.reserve(m_rowWidgets.size());
    for (RowWidgets &rw : m_rowWidgets) {
        if (!rw.rowFrame) continue;
        QPixmap pm = rw.rowFrame->grab();
        pieces.push_back(pm);
        totalH += pm.height();
        maxW    = std::max(maxW, pm.width());
    }
    if (totalH == 0) return;
    QPixmap combined(maxW, totalH);
    combined.fill(Qt::white);
    QPainter p(&combined);
    int y = 0;
    for (const QPixmap &pm : pieces) {
        p.drawPixmap(0, y, pm);
        y += pm.height();
    }
    p.end();
    if (!combined.save(path, "PNG")) {
        QMessageBox::warning(this, tr("Export failed"),
                              tr("Could not save the image to %1").arg(path));
    }
}

void ComparisonPlotDialog::onAnimationCursorToggled(bool checked)
{
    m_showCursor = checked;
    for (RowWidgets &rw : m_rowWidgets) {
        if (!rw.cursorLine) continue;
        rw.cursorLine->setVisible(checked);
        // CP.1 — keep the cursor's entry IN the legend so the user can
        // see which dashed line is the animation cursor (previously
        // hidden). Marker tracks the line's visibility.
        const auto markers = rw.chart ? rw.chart->legend()->markers(rw.cursorLine)
                                      : QList<QLegendMarker*>{};
        for (auto *m : markers)
            if (m) m->setVisible(checked);
    }
}

// ---------------------------------------------------------------------------
// CP.1 — View toggles (Series / Slider / Stats panels + Charts Only)
//
// Pattern: each toggle hides/shows the target widget. When hiding, the
// current splitter sizes are cached so re-show restores the user's
// previous width/height (matching what most IDEs do for dockable panes).
// The "Charts Only" master toggle just drives the three independent
// toggles together; the per-pane menu items remain authoritative so the
// user can mix-and-match without re-toggling the master.
// ---------------------------------------------------------------------------

void ComparisonPlotDialog::onShowSeriesToggled(bool show)
{
    if (!m_splitter || !m_leftHost) return;
    if (!show) {
        // Snapshot sizes before hiding so we can restore them.
        m_savedSplitterSizes = m_splitter->sizes();
        m_leftHost->hide();
    } else {
        m_leftHost->show();
        // If we have a snapshot, restore it. Otherwise fall back to a
        // sensible default (~280px series pane, charts taking the rest).
        if (m_savedSplitterSizes.size() == m_splitter->count() &&
            m_savedSplitterSizes[0] > 0) {
            m_splitter->setSizes(m_savedSplitterSizes);
        } else {
            const int total = m_splitter->width();
            m_splitter->setSizes({280, std::max(0, total - 280)});
        }
    }
}

void ComparisonPlotDialog::onShowSliderToggled(bool show)
{
    if (m_sliderHost) m_sliderHost->setVisible(show);
}

void ComparisonPlotDialog::onShowStatsToggled(bool show)
{
    if (!m_chartsOuter || !m_statsPanel) return;
    if (!show) {
        m_savedChartsOuterSizes = m_chartsOuter->sizes();
        m_statsPanel->hide();
    } else {
        m_statsPanel->show();
        if (m_savedChartsOuterSizes.size() == m_chartsOuter->count() &&
            m_savedChartsOuterSizes.last() > 0) {
            m_chartsOuter->setSizes(m_savedChartsOuterSizes);
        } else {
            // Restore the AT.3 default ratio: charts 4× stats; slider fixed.
            const int total = m_chartsOuter->height() - 24 /*slider*/;
            const int stats = std::max(60, total / 5);
            m_chartsOuter->setSizes({std::max(0, total - stats), 24, stats});
        }
    }
}

void ComparisonPlotDialog::onChartsOnlyToggled(bool chartsOnly)
{
    // Drive the three independent toggles. Block their signals so the
    // master doesn't fight with three change events and we get one clean
    // layout pass at the end.
    if (m_actShowSeries) m_actShowSeries->setChecked(!chartsOnly);
    if (m_actShowSlider) m_actShowSlider->setChecked(!chartsOnly);
    if (m_actShowStats)  m_actShowStats ->setChecked(!chartsOnly);
}

void ComparisonPlotDialog::onShow1v1Toggled(bool /*show*/)
{
    // COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 4 — the toggle gates the
    // scatter build inside rebuildCharts (off = skip the pairing work
    // entirely, not just hide the panes).
    rebuildCharts();
}

void ComparisonPlotDialog::onConfigure1v1Clicked()
{
    // Phase 5 — modal editor over the model's pair list. The model emits
    // pairsChanged on every edit, which rebuilds the scatter column live.
    ComparisonPairsDialog dlg(m_model.get(), this);
    dlg.exec();
}

void ComparisonPlotDialog::onAddSystemSeriesClicked()
{
    // Find a 1D run source to host the series (system attrs are global per run).
    int runIdx = -1;
    for (int i = 0; i < m_model->runSourceCount(); ++i) {
        const auto &rs = m_model->runSource(i);
        if (!rs.layer) continue;
        // Skip mesh-only runs; system series come from 1D .out.
        if (rs.layer->supportsAttribute(PlotAttribute::SystemRunoff)) {
            runIdx = i;
            break;
        }
    }
    if (runIdx < 0) {
        QMessageBox::information(this, tr("No 1D run loaded"),
            tr("Load a SWMM .out result first — system-wide series come from "
               "the 1D engine output, not from observed CSVs or 2D mesh runs."));
        return;
    }

    // Single-pick QMenu of all 14 system attributes (shared canonical order).
    QMenu menu(this);
    QHash<QAction*, PlotAttribute> bind;
    for (PlotAttribute attr : systemPlotAttributes()) {
        const auto unit = m_model->runSource(runIdx).layer->unitSystem();
        auto *a = menu.addAction(labelWithUnits(attr, unit));
        bind.insert(a, attr);
    }
    QAction *picked = menu.exec(QCursor::pos());
    if (!picked) return;
    addSeries(runIdx, ObjectRef::forSystem(), bind.value(picked));
}

// ---------------------------------------------------------------------------
// Splitter sizing helpers
// ---------------------------------------------------------------------------

void ComparisonPlotDialog::equaliseChartSplitterSizes()
{
    if (!m_chartsSplitter) return;
    const int n = m_chartsSplitter->count();
    if (n == 0) return;
    const int total = std::max(m_chartsSplitter->height(),
                                n * 220);   // honour per-row min height
    const int per   = total / n;
    QList<int> sizes;
    sizes.reserve(n);
    for (int i = 0; i < n; ++i) sizes.push_back(per);
    m_chartsSplitter->setSizes(sizes);
}

void ComparisonPlotDialog::wireXAxisSync(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= m_rowWidgets.size()) return;
    RowWidgets &rw = m_rowWidgets[rowIndex];
    if (!rw.xAxis) return;
    connect(rw.xAxis, &QDateTimeAxis::rangeChanged,
            this, [this, rowIndex](QDateTime lo, QDateTime hi) {
                if (m_syncingX) return;
                m_syncingX = true;
                for (int r = 0; r < m_rowWidgets.size(); ++r) {
                    if (r == rowIndex) continue;
                    QDateTimeAxis *ax = m_rowWidgets[r].xAxis;
                    if (!ax) continue;
                    QSignalBlocker block(ax);
                    ax->setRange(lo, hi);
                }
                m_syncingX = false;
            });
}

// ---------------------------------------------------------------------------
// Convenience: add 1D run from a SWMMResultsLayer + add a series
// ---------------------------------------------------------------------------

int ComparisonPlotDialog::ensureRunSourceForLayer(SWMMResultsLayer *layer,
                                                  bool makeBaseline)
{
    if (!layer)
        return -1;

    // De-dup: scan existing RunSources for a SwmmOutRunLayer wrapping the
    // same on-canvas layer (same persistenceKey() == resultsFilePath()).
    const QString key = layer->resultsFilePath();
    for (int i = 0; i < m_model->runSourceCount(); ++i) {
        const RunSource &rs = m_model->runSource(i);
        if (rs.layer && rs.layer->persistenceKey() == key)
            return i;
    }

    RunSource rs;
    rs.layer = std::make_shared<SwmmOutRunLayer>(layer);
    rs.isBaseline = makeBaseline;
    const int idx = m_model->addRunSource(std::move(rs));
    if (makeBaseline)
        m_model->setBaseline(idx);
    return idx;
}

int ComparisonPlotDialog::addSeries(int runIndex,
                                    const ObjectRef& ref,
                                    PlotAttribute attr)
{
    if (runIndex < 0 || runIndex >= m_model->runSourceCount())
        return -1;
    if (attr == PlotAttribute::Unknown || !ref.isValid())
        return -1;

    SeriesSpec spec;
    spec.runIndex  = runIndex;
    spec.objectRef = ref;
    spec.attribute = attr;
    return m_model->addSeries(std::move(spec));
}

int ComparisonPlotDialog::ensureRunSourceForMeshLayer(SWMM2DResultsLayer *layer)
{
    if (!layer)
        return -1;

    const QString key = QStringLiteral("mesh2d://") + layer->name();
    for (int i = 0; i < m_model->runSourceCount(); ++i) {
        const RunSource &rs = m_model->runSource(i);
        if (rs.layer && rs.layer->persistenceKey() == key)
            return i;
    }

    RunSource rs;
    rs.layer = std::make_shared<Mesh2DRunLayer>(layer);
    return m_model->addRunSource(std::move(rs));
}

int ComparisonPlotDialog::addCellSeries(int runIndex,
                                         const QVector<int>& triIdxList,
                                         const QVector<PlotAttribute>& attributes)
{
    if (runIndex < 0 || runIndex >= m_model->runSourceCount())
        return 0;
    if (triIdxList.isEmpty() || attributes.isEmpty())
        return 0;

    int added = 0;
    for (int triIdx : triIdxList) {
        const ObjectRef ref = ObjectRef::forMesh2DCell(triIdx);
        for (PlotAttribute a : attributes) {
            if (a == PlotAttribute::Unknown) continue;
            if (addSeries(runIndex, ref, a) >= 0) ++added;
        }
    }
    return added;
}

// ---------------------------------------------------------------------------
// Model signal handlers
// ---------------------------------------------------------------------------

void ComparisonPlotDialog::onRunSourceAdded(int /*runIndex*/) { rebuildSeriesTree(); }
void ComparisonPlotDialog::onRunSourceRemoved(int /*runIndex*/) { rebuildSeriesTree(); rebuildCharts(); }
void ComparisonPlotDialog::onSeriesAdded(int /*seriesIndex*/)   { rebuildSeriesTree(); rebuildCharts(); }
void ComparisonPlotDialog::onSeriesRemoved(int /*seriesIndex*/) { rebuildSeriesTree(); rebuildCharts(); }
void ComparisonPlotDialog::onRowsChanged() { /* charts rebuilt by series/runSource handlers */ }

void ComparisonPlotDialog::onStyleChanged(int seriesIndex)
{
    // Find which row + which child series corresponds to this spec, and
    // restyle without a full chart rebuild. Delegates to the shared
    // applySeriesStyle() helper so every visual field (pen cap/join,
    // marker shape/border, point labels, opacity, area-fill brush, ...)
    // takes effect — not just the legacy {color, dash, width, visible,
    // opacity} subset.
    for (int r = 0; r < m_rowWidgets.size(); ++r) {
        const AttributeRow &row = m_model->rows().at(r);
        for (int i = 0; i < row.seriesIndices.size(); ++i) {
            if (row.seriesIndices[i] != seriesIndex) continue;
            QLineSeries *line = m_rowWidgets[r].series.value(i, nullptr);
            if (!line) return;
            const SeriesSpec &spec = m_model->spec(seriesIndex);
            // Reapply the legend name first; applySeriesStyle overrides it
            // only when style.legendName is non-empty.
            line->setName(legendNameFor(spec));
            openswmmvis::plot::applySeriesStyle(spec.style, line);
            // Repaint the swatch row in the series tree so the legend label
            // there matches when legendOverride changed.
            rebuildSeriesTree();
            return;
        }
    }
}

QString ComparisonPlotDialog::legendNameFor(const SeriesSpec& spec) const
{
    if (!spec.legendOverride.isEmpty())
        return spec.legendOverride;
    return QStringLiteral("%1 — %2 (%3)")
        .arg(m_model->runSource(spec.runIndex).label,
             spec.objectRef.kind == ObjectRef::Kind::Mesh2DCell
                ? tr("Cell %1").arg(spec.objectRef.triIdx)
                : spec.objectRef.name,
             labelFor(spec.attribute));
}

void ComparisonPlotDialog::onAnimationTimeChanged(QDateTime t)
{
    m_curAnimTime = std::move(t);
    applyAnimationCursorToCharts();
}

// ---------------------------------------------------------------------------
// Series tree
// ---------------------------------------------------------------------------

void ComparisonPlotDialog::rebuildSeriesTree()
{
    if (!m_seriesTree)
        return;
    // Slice AT.3 — block itemChanged signals while rebuilding so we don't
    // mistake population for user-initiated checkbox toggles.
    QSignalBlocker block(m_seriesTree);
    m_seriesTree->clear();

    // Group by RunSource. Each top-level item is a run; children are series.
    QVector<QTreeWidgetItem*> runItems(m_model->runSourceCount(), nullptr);
    for (int r = 0; r < m_model->runSourceCount(); ++r) {
        const RunSource &rs = m_model->runSource(r);
        QString label = rs.label;
        if (r == m_model->baselineRunIndex())
            label = tr("⊙ %1  (baseline)").arg(label);
        else
            label = QStringLiteral("● ") + label;
        auto *it = new QTreeWidgetItem({label});
        it->setData(0, Qt::UserRole, QVariant(r));   // run index
        it->setData(0, Qt::UserRole + 1, QVariant(-1)); // -1 = run row, not series
        m_seriesTree->addTopLevelItem(it);
        runItems[r] = it;
    }

    for (int s = 0; s < m_model->seriesCount(); ++s) {
        const SeriesSpec &spec = m_model->spec(s);
        if (spec.runIndex < 0 || spec.runIndex >= runItems.size()) continue;

        QString objLabel;
        switch (spec.objectRef.kind) {
        case ObjectRef::Kind::Mesh2DCell:
            objLabel = tr("Cell %1").arg(spec.objectRef.triIdx);
            break;
        case ObjectRef::Kind::Observed:
            objLabel = tr("Observed: %1").arg(spec.objectRef.name);
            break;
        default:
            objLabel = spec.objectRef.name;
            break;
        }
        const QString itemLabel = tr("%1 — %2")
                                     .arg(objLabel, labelFor(spec.attribute));
        auto *child = new QTreeWidgetItem({itemLabel});
        child->setData(0, Qt::UserRole, QVariant(spec.runIndex));
        child->setData(0, Qt::UserRole + 1, QVariant(s));
        // Show series color swatch via Qt::DecorationRole.
        QPixmap pm(12, 12);
        pm.fill(spec.style.color);
        child->setData(0, Qt::DecorationRole, pm);
        // Slice AT.3 — user-checkable; check state mirrors showLine.
        child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
        child->setCheckState(0, spec.style.showLine ? Qt::Checked : Qt::Unchecked);
        runItems[spec.runIndex]->addChild(child);
    }

    m_seriesTree->expandAll();
}

// Slice AT.3 — checkbox toggle slot.
void ComparisonPlotDialog::onSeriesItemChanged(QTreeWidgetItem *item, int column)
{
    if (!item || column != 0) return;
    const int seriesIdx = item->data(0, Qt::UserRole + 1).toInt();
    if (seriesIdx < 0 || seriesIdx >= m_model->seriesCount()) return;
    SeriesStyle style = m_model->spec(seriesIdx).style;
    const bool shouldShow = (item->checkState(0) == Qt::Checked);
    if (style.showLine == shouldShow) return;   // no change
    style.showLine = shouldShow;
    m_model->updateStyle(seriesIdx, style);
}

// Slice AT.3 — right-click context menu.
void ComparisonPlotDialog::onSeriesTreeContextMenu(const QPoint &pos)
{
    if (!m_seriesTree) return;
    QTreeWidgetItem *item = m_seriesTree->itemAt(pos);
    if (!item) return;
    const int runIdx    = item->data(0, Qt::UserRole).toInt();
    const int seriesIdx = item->data(0, Qt::UserRole + 1).toInt();
    const bool isSeries = (seriesIdx >= 0);

    QMenu menu(this);
    if (isSeries) {
        // Slice AT.3 polish — Plot This Only is meaningless on a one-series
        // row; grey it out so the user knows it's a no-op.
        const auto &targetSpec = m_model->spec(seriesIdx);
        int rowSiblingCount = 0;
        for (const auto &row : m_model->rows()) {
            if (row.attribute == targetSpec.attribute) {
                rowSiblingCount = row.seriesIndices.size();
                break;
            }
        }
        QAction *plotOnly = menu.addAction(tr("Plot This Only"));
        plotOnly->setEnabled(rowSiblingCount > 1);
        if (rowSiblingCount <= 1)
            plotOnly->setToolTip(tr("Only one series on this chart row"));
        QAction *editStyle = menu.addAction(tr("Edit Properties…"));
        menu.addSeparator();
        QAction *removeSeries = menu.addAction(tr("Remove Series"));
        QAction *chosen = menu.exec(m_seriesTree->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == plotOnly) {
            // Hide every other series sharing this attribute row.
            const auto &rows = m_model->rows();
            const auto &targetSpec = m_model->spec(seriesIdx);
            for (const auto &row : rows) {
                if (row.attribute != targetSpec.attribute) continue;
                for (int sIdx : row.seriesIndices) {
                    auto style = m_model->spec(sIdx).style;
                    const bool show = (sIdx == seriesIdx);
                    if (style.showLine != show) {
                        style.showLine = show;
                        m_model->updateStyle(sIdx, style);
                    }
                }
            }
        } else if (chosen == editStyle) {
            onSeriesItemDoubleClicked(item, 0);
        } else if (chosen == removeSeries) {
            m_model->removeSeries(seriesIdx);
        }
    } else if (runIdx >= 0) {
        QAction *addSeries  = menu.addAction(tr("Add Series…"));
        QAction *removeRun  = menu.addAction(tr("Remove Run"));
        QAction *chosen = menu.exec(m_seriesTree->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == addSeries) onAddSeriesClicked();
        else if (chosen == removeRun) m_model->removeRunSource(runIdx);
    }
}

void ComparisonPlotDialog::onSeriesItemSelectionChanged()
{
    const auto items = m_seriesTree->selectedItems();
    bool removable = false;
    for (auto *it : items) {
        const int seriesIdx = it->data(0, Qt::UserRole + 1).toInt();
        if (seriesIdx >= 0) { removable = true; break; }
    }
    m_removeBtn->setEnabled(removable);
}

void ComparisonPlotDialog::onSeriesItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (!item) return;
    const int seriesIdx = item->data(0, Qt::UserRole + 1).toInt();
    if (seriesIdx < 0 || seriesIdx >= m_model->seriesCount()) return;

    // Full series-property editor in a small modal dialog. Live-updates the
    // model on every change so the user sees feedback as they edit.
    SeriesStyle original           = m_model->spec(seriesIdx).style;
    QString     originalLegendOver = m_model->spec(seriesIdx).legendOverride;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit series properties"));
    auto *vbox = new QVBoxLayout(&dlg);
    auto *editor = new openswmmvis::ui::SeriesStyleEditor(&dlg);
    editor->setStyle(original);
    editor->setLegendOverride(originalLegendOver);
    vbox->addWidget(editor);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vbox->addWidget(bb);

    // Live preview: any control change pushes through the model so the
    // chart restyles immediately (onStyleChanged() updates the QLineSeries
    // pen incrementally).
    QObject::connect(editor, &openswmmvis::ui::SeriesStyleEditor::styleChanged,
                     this, [this, seriesIdx](const SeriesStyle& s) {
                         m_model->updateStyle(seriesIdx, s);
                     });
    QObject::connect(editor, &openswmmvis::ui::SeriesStyleEditor::legendOverrideChanged,
                     this, [this, seriesIdx](const QString& s) {
                         m_model->updateLegendOverride(seriesIdx, s);
                     });

    if (dlg.exec() != QDialog::Accepted) {
        // Revert to the pre-edit state on cancel.
        m_model->updateStyle(seriesIdx, original);
        m_model->updateLegendOverride(seriesIdx, originalLegendOver);
    } else {
        // Final commit (the live-preview already wrote it, but call once
        // more so any debounced views catch up).
        m_model->updateStyle(seriesIdx, editor->style());
        m_model->updateLegendOverride(seriesIdx, editor->legendOverride());
    }
    rebuildSeriesTree();
}

void ComparisonPlotDialog::onRemoveSelectedClicked()
{
    const auto items = m_seriesTree->selectedItems();
    QVector<int> toRemove;
    for (auto *it : items) {
        const int s = it->data(0, Qt::UserRole + 1).toInt();
        if (s >= 0) toRemove.push_back(s);
    }
    // Remove from highest index to lowest so earlier removals don't shift later ones.
    std::sort(toRemove.begin(), toRemove.end(), std::greater<int>());
    for (int s : toRemove)
        m_model->removeSeries(s);
}

void ComparisonPlotDialog::onLoadObservedClicked()
{
    // 1) File picker.
    const QString path = QFileDialog::getOpenFileName(this,
        tr("Load observed time series"),
        QString(),
        tr("CSV / TSV / DAT (*.csv *.tsv *.dat);;All files (*)"));
    if (path.isEmpty()) return;

    // 2) Pick the chart attribute this CSV's columns plot against.
    static const QPair<PlotAttribute, QString> kAttrs[] = {
        { PlotAttribute::NodeDepth,         tr("Depth (node)") },
        { PlotAttribute::NodeHead,          tr("Head") },
        { PlotAttribute::NodeTotalInflow,   tr("Total inflow") },
        { PlotAttribute::NodeOverflow,      tr("Overflow") },
        { PlotAttribute::LinkFlow,          tr("Flow") },
        { PlotAttribute::LinkDepth,         tr("Depth (link)") },
        { PlotAttribute::LinkVelocity,      tr("Velocity (link)") },
        { PlotAttribute::SubcatchRainfall,  tr("Rainfall") },
        { PlotAttribute::SubcatchRunoff,    tr("Runoff") },
    };
    QStringList items;
    for (const auto &p : kAttrs) items << p.second;

    bool ok = false;
    const QString picked = QInputDialog::getItem(this,
        tr("Observed attribute"),
        tr("Which attribute do this file's value columns represent?"),
        items, /*current=*/0, /*editable=*/false, &ok);
    if (!ok) return;

    PlotAttribute chosenAttr = PlotAttribute::Unknown;
    for (const auto &p : kAttrs) {
        if (p.second == picked) { chosenAttr = p.first; break; }
    }
    if (chosenAttr == PlotAttribute::Unknown) return;

    // 3) Parse the file.
    QString err;
    auto layer = openswmmvis::plot::ObservedCsvRunLayer::load(
        path, chosenAttr, openswmmvis::plot::UnitSystem::SI, &err);
    if (!layer) {
        QMessageBox::warning(this,
            tr("Couldn't load observed series"),
            tr("Failed to load %1:\n%2").arg(path, err));
        return;
    }

    // 4) Register as a RunSource and add one series per CSV column.
    QStringList labels = layer->columnLabels();
    if (labels.isEmpty()) {
        QMessageBox::warning(this,
            tr("Empty file"),
            tr("No data columns found in %1.").arg(path));
        return;
    }

    RunSource rs;
    rs.layer = std::shared_ptr<openswmmvis::plot::ObservedCsvRunLayer>(
        layer.release());
    const int runIdx = m_model->addRunSource(std::move(rs));

    int added = 0;
    for (const QString& colLabel : labels) {
        ObjectRef ref = ObjectRef::forObserved(colLabel);
        SeriesSpec spec;
        spec.runIndex  = runIdx;
        spec.objectRef = ref;
        spec.attribute = chosenAttr;
        if (m_model->addSeries(std::move(spec)) >= 0) ++added;
    }

    if (added == 0) {
        QMessageBox::warning(this,
            tr("Nothing added"),
            tr("Couldn't add any series from %1.").arg(path));
    }
}

void ComparisonPlotDialog::onAddSeriesClicked()
{
    if (m_model->runSourceCount() == 0) {
        QMessageBox::information(this, tr("No runs loaded"),
            tr("Open a project with results before adding series."));
        return;
    }

    // Build a small popup menu: Run → Object kind → Attribute. For 1D runs
    // we list the SWMM IDs from the underlying SWMMResultsLayer via the
    // adapter; for 2D runs the user should drag-box on the map instead.
    // Slice AT.2: also includes a "(System)" pseudo-object entry per run
    // that pops the 14-attribute system menu — skips the object-name prompt
    // since system series are global per run.
    QMenu menu(this);
    for (int r = 0; r < m_model->runSourceCount(); ++r) {
        const RunSource &rs = m_model->runSource(r);
        QMenu *runMenu = menu.addMenu(rs.label);
        const auto *layer = rs.layer.get();
        if (!layer) continue;

        // Per-object attributes — only attributes the layer supports.
        // Deliberately a curated subset of the canonical lists in
        // plot/plotattribute.h (this menu is a quick-add, not the full
        // picker); keep any additions consistent with those lists.
        const PlotAttribute attrs[] = {
            PlotAttribute::NodeDepth, PlotAttribute::NodeHead, PlotAttribute::NodeTotalInflow,
            PlotAttribute::NodeOverflow,
            PlotAttribute::LinkFlow, PlotAttribute::LinkDepth, PlotAttribute::LinkVelocity,
            PlotAttribute::SubcatchRainfall, PlotAttribute::SubcatchRunoff,
        };
        for (PlotAttribute a : attrs) {
            if (!layer->supportsAttribute(a)) continue;
            QString text = labelFor(a);
            QAction *act = runMenu->addAction(text);
            act->setData(QVariantList{ r, static_cast<int>(a) });
        }

        // Slice AT.2 — (System) pseudo-object submenu listing all 14 system
        // attrs, in the shared canonical order.
        if (layer->supportsAttribute(PlotAttribute::SystemRunoff)) {
            runMenu->addSeparator();
            QMenu *sysSub = runMenu->addMenu(tr("(System)"));
            for (PlotAttribute a : systemPlotAttributes()) {
                QAction *act = sysSub->addAction(labelWithUnits(a, layer->unitSystem()));
                // Encode (runIdx, attr, isSystem=true) by reusing the same
                // QVariantList shape with a 3rd flag.
                act->setData(QVariantList{ r, static_cast<int>(a), true });
            }
        }
    }

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    const QVariantList payload = chosen->data().toList();
    if (payload.size() < 2) return;
    const int r = payload[0].toInt();
    const PlotAttribute a = static_cast<PlotAttribute>(payload[1].toInt());
    const bool isSystem = (payload.size() >= 3 && payload[2].toBool());

    // System series — global per run, no object name needed.
    if (isSystem) {
        addSeries(r, ObjectRef::forSystem(), a);
        return;
    }

    // Prompt for the SWMM object ID. The user can paste any name; if the
    // .out doesn't have it, getSeriesAt will report an error in the chart.
    bool ok = false;
    const QString objName = QInputDialog::getText(this,
        tr("Add series"),
        tr("Enter SWMM object ID (node / link / subcatchment) for %1:").arg(labelFor(a)),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || objName.isEmpty()) return;

    // Infer the ObjectRef::Kind from the attribute's prefix.
    ObjectRef::Kind kind = ObjectRef::Kind::Node;
    switch (a) {
    case PlotAttribute::LinkFlow:
    case PlotAttribute::LinkDepth:
    case PlotAttribute::LinkVelocity:
    case PlotAttribute::LinkVolume:
    case PlotAttribute::LinkCapacity:
        kind = ObjectRef::Kind::Link;
        break;
    case PlotAttribute::SubcatchRainfall:
    case PlotAttribute::SubcatchSnowDepth:
    case PlotAttribute::SubcatchEvap:
    case PlotAttribute::SubcatchInfil:
    case PlotAttribute::SubcatchRunoff:
        kind = ObjectRef::Kind::Subcatch;
        break;
    default:
        kind = ObjectRef::Kind::Node;
        break;
    }

    ObjectRef ref(kind, objName);
    addSeries(r, ref, a);
}

// ---------------------------------------------------------------------------
// Charts area
// ---------------------------------------------------------------------------

void ComparisonPlotDialog::rebuildCharts()
{
    // Tear down existing row frames (splitter children — deleting frame
    // also deletes its child views, charts, and series via Qt parenting).
    for (RowWidgets &rw : m_rowWidgets) {
        if (rw.rowFrame)
            rw.rowFrame->deleteLater();
    }
    m_rowWidgets.clear();

    // Scatter column is visible only when the "1v1 Plots" toolbar toggle
    // is on (Phase 4) AND there's something to pair: ≥2 runs for auto
    // mode, or user-configured pairs (Phase 5 — these may pair two
    // series of a single run, e.g. two objects).
    const bool show1v1     = !m_actShow1v1 || m_actShow1v1->isChecked();
    const bool haveScatter = show1v1 &&
        (m_model->runSourceCount() >= 2 || !m_model->pairs().isEmpty());
    const int baselineIdx  = m_model->baselineRunIndex();

    // Build one chart per row.
    const QVector<AttributeRow> &rows = m_model->rows();
    for (int r = 0; r < rows.size(); ++r) {
        const AttributeRow &row = rows.at(r);

        RowWidgets rw;
        rw.chart = new QChart;
        rw.chart->setTitle(labelWithUnits(row.attribute, row.unitSystem));
        rw.chart->legend()->setVisible(true);

        rw.xAxis = new QDateTimeAxis;
        rw.xAxis->setTitleText(tr("Time"));
        rw.xAxis->setFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
        rw.chart->addAxis(rw.xAxis, Qt::AlignBottom);

        rw.yAxis = new QValueAxis;
        rw.yAxis->setTitleText(row.unitsLabel);
        // Inherit the global default Y precision (X is a time axis → unchanged).
        rw.yAxis->setLabelFormat(PreferencesManager::instance()->plotYAxisFormat().printfSpec());
        rw.chart->addAxis(rw.yAxis, Qt::AlignLeft);

        // Aggregate y range across all child series; track X range too.
        double yMin = std::numeric_limits<double>::infinity();
        double yMax = -std::numeric_limits<double>::infinity();
        qint64 xMinMs = std::numeric_limits<qint64>::max();
        qint64 xMaxMs = std::numeric_limits<qint64>::min();

        for (int sIdx : row.seriesIndices) {
            const SeriesSpec &spec = m_model->spec(sIdx);
            auto *line = new QLineSeries;

            line->setName(legendNameFor(spec));
            openswmmvis::plot::applySeriesStyle(spec.style, line);

            SeriesData data;
            m_model->resolveSeries(sIdx, data);
            if (data.ok) {
                for (std::size_t i = 0; i < data.timesJulian.size(); ++i) {
                    const QDateTime dt = openswmmvis::core::swmmDateTimeToQDateTime(data.timesJulian[i]);
                    const double v = data.values[i];
                    if (!dt.isValid() || !std::isfinite(v)) continue;
                    const qint64 ms = dt.toMSecsSinceEpoch();
                    line->append(ms, v);
                    if (v < yMin) yMin = v;
                    if (v > yMax) yMax = v;
                    if (ms < xMinMs) xMinMs = ms;
                    if (ms > xMaxMs) xMaxMs = ms;
                }
            }

            rw.chart->addSeries(line);
            line->attachAxis(rw.xAxis);
            line->attachAxis(rw.yAxis);
            rw.series.push_back(line);
        }

        // CP.1 — Animation-cursor vertical line: dashed black, named so
        // the legend reads "Current time" (user request). Visibility is
        // governed by the toolbar toggle + presence of a valid time;
        // applyAnimationCursorToCharts() places the line at (t, [ymin,
        // ymax]) once the model emits a time.
        rw.cursorLine = new QLineSeries;
        QPen cursorPen(Qt::black);
        cursorPen.setStyle(Qt::DashLine);
        cursorPen.setWidthF(1.5);
        rw.cursorLine->setPen(cursorPen);
        rw.cursorLine->setName(tr("Current time"));
        rw.chart->addSeries(rw.cursorLine);
        rw.cursorLine->attachAxis(rw.xAxis);
        rw.cursorLine->attachAxis(rw.yAxis);
        // Legend marker tracks the master toggle — user can see which
        // dashed line is the cursor instead of guessing.
        const auto cursorMarkers = rw.chart->legend()->markers(rw.cursorLine);
        for (auto *m : cursorMarkers)
            if (m) m->setVisible(m_showCursor);

        // Axis ranges
        if (std::isfinite(yMin) && std::isfinite(yMax) && yMax > yMin) {
            const double pad = 0.05 * (yMax - yMin);
            rw.yAxis->setRange(yMin - pad, yMax + pad);
        } else {
            rw.yAxis->setRange(0, 1);
        }
        if (xMinMs < xMaxMs) {
            rw.xAxis->setRange(QDateTime::fromMSecsSinceEpoch(xMinMs),
                                QDateTime::fromMSecsSinceEpoch(xMaxMs));
        }

        // Slice AT.2 — InteractiveChartView replaces plain QChartView so the
        // toolbar Mode actions (Select/Pan/ZoomIn/ZoomOut) drive interaction.
        rw.view = new InteractiveChartView(rw.chart);
        rw.view->setRenderHint(QPainter::Antialiasing);
        rw.view->setMinimumHeight(220);
        connect(rw.view, &InteractiveChartView::chartContextMenuRequested,
                this, [this, r](const QPoint &globalPos) {
                    // AT.3 — adds Chart Properties… to the row context menu.
                    QMenu menu(this);
                    QAction *reset    = menu.addAction(tr("Reset Zoom"));
                    QAction *fit      = menu.addAction(tr("Fit All Rows"));
                    menu.addSeparator();
                    QAction *props    = menu.addAction(tr("Chart Properties…"));
                    QAction *chosen = menu.exec(globalPos);
                    if (!chosen) return;
                    if (chosen == reset && r < m_rowWidgets.size()
                        && m_rowWidgets[r].view) {
                        m_rowWidgets[r].view->resetZoom();
                    } else if (chosen == fit) {
                        rebuildCharts();
                    } else if (chosen == props && r < m_rowWidgets.size()
                               && m_rowWidgets[r].chart) {
                        auto *cp = new openswmmvis::plot::ChartProperties(
                            m_rowWidgets[r].chart);
                        if (m_statsPanel)
                            cp->setStatisticsNumberFormat(
                                m_statsPanel->statisticNumberFormat());
                        auto applyStatisticsFormat = [this, cp]() {
                            if (m_statsPanel)
                                m_statsPanel->setStatisticNumberFormat(
                                    cp->statisticsNumberFormat());
                        };
                        connect(cp,
                                &openswmmvis::plot::ChartProperties::statisticsFormatModeChanged,
                                this,
                                applyStatisticsFormat);
                        connect(cp,
                                &openswmmvis::plot::ChartProperties::statisticsPrecisionChanged,
                                this,
                                applyStatisticsFormat);
                        connect(cp,
                                &openswmmvis::plot::ChartProperties::statisticsFormatChanged,
                                this,
                                applyStatisticsFormat);
                        auto *dlg = new ChartPropertiesDialog(cp, this);
                        dlg->show();
                    }
                });
        // Slice AT.3 — Shift-drag X-range selection feeds the StatsSummaryPanel
        // and is cached so Fit can refit Y to the windowed data.
        connect(rw.view, &InteractiveChartView::xRangeSelectionChanged,
                this, [this](QDateTime lo, QDateTime hi) {
                    m_xSelLo = lo;
                    m_xSelHi = hi;
                    if (m_statsPanel) m_statsPanel->setSelectionRange(lo, hi);
                });

        // ----- Column 1: 1v1 scatter (FitMetrics in title) ----------------
        // Built only when ≥2 runs are loaded AND this row has a baseline
        // series plus at least one comparison series that actually pairs
        // with it. Pairing is computed FIRST; the chart is only created
        // when at least one pairing produced points, so rows without a
        // baseline/comparison overlap show the time series full-width
        // instead of an empty 0..1 scatter.
        if (haveScatter) {
            // Deferred chart creation: collect the non-empty pairings
            // first; the chart only exists when at least one pairing
            // produced points.
            struct PendingScatter {
                int           xSpecIdx;   ///< -1 in auto mode (baseline column).
                int           ySpecIdx;   ///< Styles the scatter points.
                PairedSamples samples;
            };
            std::vector<PendingScatter> pending;

            const QVector<ComparisonPair> &userPairs = m_model->pairs();
            if (!userPairs.isEmpty()) {
                // Phase 5 — user-configured pairs override auto pairing.
                // Pairs live on the row whose attribute they target.
                for (const ComparisonPair &cp : userPairs) {
                    if (cp.xSeriesIndex < 0 || cp.xSeriesIndex >= m_model->seriesCount() ||
                        cp.ySeriesIndex < 0 || cp.ySeriesIndex >= m_model->seriesCount())
                        continue;
                    if (m_model->spec(cp.xSeriesIndex).attribute != row.attribute)
                        continue;   // belongs to another row

                    SeriesData xData, yData;
                    m_model->resolveSeries(cp.xSeriesIndex, xData);
                    m_model->resolveSeries(cp.ySeriesIndex, yData);
                    if (!xData.ok || !yData.ok) continue;

                    PairedSamples ps = pairSamplesNearest(
                        xData.timesJulian, xData.values,
                        yData.timesJulian, yData.values);
                    if (ps.x.empty()) continue;
                    pending.push_back({cp.xSeriesIndex, cp.ySeriesIndex,
                                       std::move(ps)});
                }
            } else if (baselineIdx >= 0) {
                // Auto mode — baseline vs every other run, matched by
                // objectRef. Index baseline samples by objectRef so we can
                // pair them with comparison-run samples for the same object.
                struct BaselineCol {
                    std::vector<double> t, v;
                };
                QMap<QString, BaselineCol> baseByObj;   // key = obj name|kind|triIdx

                auto refKey = [](const ObjectRef &r) -> QString {
                    return QStringLiteral("%1|%2|%3")
                        .arg(static_cast<int>(r.kind)).arg(r.name).arg(r.triIdx);
                };

                // First pass: collect baseline samples for this row.
                for (int sIdx : row.seriesIndices) {
                    const SeriesSpec &spec = m_model->spec(sIdx);
                    if (spec.runIndex != baselineIdx) continue;
                    SeriesData data;
                    m_model->resolveSeries(sIdx, data);
                    if (!data.ok) continue;
                    BaselineCol bc;
                    bc.t.assign(data.timesJulian.begin(), data.timesJulian.end());
                    bc.v.assign(data.values.begin(),       data.values.end());
                    baseByObj[refKey(spec.objectRef)] = std::move(bc);
                }

                // Second pass: pair every comparison series against its
                // baseline column (timestep nearest-match — see
                // plot/seriespairing.h).
                for (int sIdx : row.seriesIndices) {
                    const SeriesSpec &spec = m_model->spec(sIdx);
                    if (spec.runIndex == baselineIdx) continue;

                    auto baseIt = baseByObj.find(refKey(spec.objectRef));
                    if (baseIt == baseByObj.end()) continue;   // no baseline for this object on this row

                    SeriesData data;
                    m_model->resolveSeries(sIdx, data);
                    if (!data.ok) continue;

                    PairedSamples ps = pairSamplesNearest(
                        baseIt.value().t, baseIt.value().v,
                        data.timesJulian, data.values);
                    if (ps.x.empty()) continue;
                    pending.push_back({-1, sIdx, std::move(ps)});
                }
            }

            if (!pending.empty()) {
            // Axis titles: auto mode keeps Baseline/Comparison; a single
            // configured pair names the actual series; multiple pairs on
            // one row stay generic.
            QString xTitle = tr("Baseline");
            QString yTitle = tr("Comparison");
            if (!userPairs.isEmpty()) {
                if (pending.size() == 1 && pending.front().xSpecIdx >= 0) {
                    xTitle = legendNameFor(m_model->spec(pending.front().xSpecIdx));
                    yTitle = legendNameFor(m_model->spec(pending.front().ySpecIdx));
                } else {
                    xTitle = tr("X series");
                    yTitle = tr("Y series");
                }
            }

            rw.scatterChart = new QChart;
            rw.scatterChart->legend()->setVisible(false);

            rw.scatterXAxis = new QValueAxis;
            rw.scatterXAxis->setTitleText(xTitle);
            rw.scatterXAxis->setLabelFormat(PreferencesManager::instance()->plotXAxisFormat().printfSpec());
            rw.scatterChart->addAxis(rw.scatterXAxis, Qt::AlignBottom);

            rw.scatterYAxis = new QValueAxis;
            rw.scatterYAxis->setTitleText(yTitle);
            rw.scatterYAxis->setLabelFormat(PreferencesManager::instance()->plotYAxisFormat().printfSpec());
            rw.scatterChart->addAxis(rw.scatterYAxis, Qt::AlignLeft);

            double scMin = std::numeric_limits<double>::infinity();
            double scMax = -std::numeric_limits<double>::infinity();

            // Aggregate fit metrics across all (baseline-paired) comparison
            // series in this row — best-of so the title carries the
            // closest-fitting run.
            FitMetrics bestFit;
            bool       haveAnyFit = false;

            for (const PendingScatter &p : pending) {
                const SeriesSpec &spec = m_model->spec(p.ySpecIdx);
                const auto &xs = p.samples.x;
                const auto &ys = p.samples.y;

                auto *scatter = new QScatterSeries;
                scatter->setMarkerSize(5.0);
                scatter->setColor(spec.style.color);
                scatter->setBorderColor(spec.style.color);

                for (std::size_t k = 0; k < xs.size(); ++k) {
                    scatter->append(xs[k], ys[k]);
                    const double mn = std::min(xs[k], ys[k]);
                    const double mx = std::max(xs[k], ys[k]);
                    if (mn < scMin) scMin = mn;
                    if (mx > scMax) scMax = mx;
                }

                rw.scatterChart->addSeries(scatter);
                scatter->attachAxis(rw.scatterXAxis);
                scatter->attachAxis(rw.scatterYAxis);
                rw.scatterSeries.push_back(scatter);

                // FitMetrics on this baseline↔comparison pair. Keep the
                // best NSE — that wins the title slot.
                FitMetrics fm = FitMetrics::compute(xs, ys);
                if (std::isfinite(fm.nse) && (!haveAnyFit || fm.nse > bestFit.nse)) {
                    bestFit = fm;
                    haveAnyFit = true;
                }
            }

            // Identity line.
            rw.identityLine = new QLineSeries;
            QPen idPen(Qt::black);
            idPen.setStyle(Qt::DashLine);
            rw.identityLine->setPen(idPen);
            rw.scatterChart->addSeries(rw.identityLine);
            rw.identityLine->attachAxis(rw.scatterXAxis);
            rw.identityLine->attachAxis(rw.scatterYAxis);

            if (std::isfinite(scMin) && std::isfinite(scMax) && scMax > scMin) {
                const double pad = 0.05 * (scMax - scMin);
                rw.scatterXAxis->setRange(scMin - pad, scMax + pad);
                rw.scatterYAxis->setRange(scMin - pad, scMax + pad);
                rw.identityLine->append(scMin, scMin);
                rw.identityLine->append(scMax, scMax);
            } else {
                rw.scatterXAxis->setRange(0, 1);
                rw.scatterYAxis->setRange(0, 1);
            }

            // Title: include fit metrics if we have any.
            QString scatterTitle = labelFor(row.attribute);
            if (haveAnyFit) {
                scatterTitle += QStringLiteral("  (NSE=%1  R²=%2  RMSE=%3  PBIAS=%4%)")
                    .arg(bestFit.nse,   0, 'f', 2)
                    .arg(bestFit.r2,    0, 'f', 2)
                    .arg(bestFit.rmse,  0, 'f', 3)
                    .arg(bestFit.pbias, 0, 'f', 1);
            }
            rw.scatterChart->setTitle(scatterTitle);

            // Also include fit metrics in the time-series chart title.
            if (haveAnyFit) {
                rw.chart->setTitle(labelWithUnits(row.attribute, row.unitSystem) +
                    QStringLiteral("  (NSE=%1)").arg(bestFit.nse, 0, 'f', 2));
            }

            // COMPARISON_PLOT_1V1_AND_TREE_PLAN — InteractiveChartView
            // (not plain QChartView) so the toolbar Mode actions and
            // wheel-zoom work on the 1v1 plots too. The Shift-drag
            // X-selection signal is NOT connected here: it carries
            // QDateTime bounds, which don't apply to value-value axes.
            rw.scatterView = new InteractiveChartView(rw.scatterChart);
            rw.scatterView->setRenderHint(QPainter::Antialiasing);
            rw.scatterView->setMinimumHeight(220);
            connect(rw.scatterView,
                    &InteractiveChartView::chartContextMenuRequested,
                    this, [this, r](const QPoint &globalPos) {
                        QMenu menu(this);
                        QAction *reset  = menu.addAction(tr("Reset Zoom"));
                        QAction *chosen = menu.exec(globalPos);
                        if (chosen == reset && r < m_rowWidgets.size()
                            && m_rowWidgets[r].scatterView) {
                            m_rowWidgets[r].scatterView->resetZoom();
                        }
                    });
            } // if (!pending.empty()) — no pairs → no scatter pane
        }

        // Slice AT.2 — wrap the time-series + (optional) scatter into a
        // per-row frame and push that into the vertical splitter. The
        // frame sits at index `r` in m_chartsSplitter; user-draggable
        // handles between frames let users resize rows arbitrarily.
        rw.rowFrame = new QWidget;       // QWidget is enough; we don't paint a frame
        rw.rowFrame->setMinimumHeight(220);
        auto *hLay = new QHBoxLayout(rw.rowFrame);
        hLay->setContentsMargins(0, 0, 0, 0);
        hLay->setSpacing(8);
        hLay->addWidget(rw.view, 2);     // time-series wider
        if (rw.scatterView) hLay->addWidget(rw.scatterView, 1);

        m_chartsSplitter->addWidget(rw.rowFrame);
        m_rowWidgets.push_back(rw);

        // X-axis link sync — added AFTER push_back so the lambda's
        // `rowIndex` matches m_rowWidgets[rowIndex].
        wireXAxisSync(m_rowWidgets.size() - 1);
    }

    // Equal stretch on add — handles drag to taste from there.
    equaliseChartSplitterSizes();

    // Propagate current toolbar mode to the freshly built views.
    propagateModeToRows();

    // Apply the cursor visibility toggle to any new rows.
    onAnimationCursorToggled(m_actAnimCursor ? m_actAnimCursor->isChecked() : true);
    // CP.1 — Re-emit the cached animation time so freshly-rebuilt
    // cursor lines actually get their (t, ymin)→(t, ymax) endpoints.
    // Without this the cursor line stays empty until the animation
    // controller fires its next tick, which can be a long wait if the
    // user hasn't started playback yet.
    applyAnimationCursorToCharts();

    // Slice AT.3 — capture the union X-range across all rows so the
    // range slider maps [0..1] correctly. Also reset slider to full.
    m_xFullMin = QDateTime();
    m_xFullMax = QDateTime();
    for (const RowWidgets &rw : m_rowWidgets) {
        if (!rw.xAxis) continue;
        const QDateTime lo = rw.xAxis->min();
        const QDateTime hi = rw.xAxis->max();
        if (!m_xFullMin.isValid() || lo < m_xFullMin) m_xFullMin = lo;
        if (!m_xFullMax.isValid() || hi > m_xFullMax) m_xFullMax = hi;
    }
    if (m_rangeSlider) {
        QSignalBlocker block(m_rangeSlider);
        m_rangeSlider->setRange(0.0, 1.0);
        m_rangeSlider->setEnabled(m_xFullMin.isValid() && m_xFullMax.isValid()
                                   && m_xFullMin < m_xFullMax);
    }
    if (m_rangeLabel) {
        if (m_xFullMin.isValid() && m_xFullMax.isValid()) {
            m_rangeLabel->setText(
                QStringLiteral("%1  →  %2")
                    .arg(m_xFullMin.toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                         m_xFullMax.toString(QStringLiteral("yyyy-MM-dd hh:mm"))));
        } else {
            m_rangeLabel->clear();
        }
    }

    applyAnimationCursorToCharts();
}

void ComparisonPlotDialog::updateChartForRow(int rowIndex)
{
    if (rowIndex < 0 || rowIndex >= m_rowWidgets.size()) return;
    // Coarse approach: rebuild all. A future polish can incrementally update.
    rebuildCharts();
}

void ComparisonPlotDialog::applyAnimationCursorToCharts()
{
    if (!m_curAnimTime.isValid()) {
        for (RowWidgets &rw : m_rowWidgets) {
            if (rw.cursorLine) rw.cursorLine->clear();
        }
        return;
    }
    const qint64 ms = m_curAnimTime.toMSecsSinceEpoch();
    for (RowWidgets &rw : m_rowWidgets) {
        if (!rw.cursorLine || !rw.yAxis) continue;
        rw.cursorLine->clear();
        rw.cursorLine->append(ms, rw.yAxis->min());
        rw.cursorLine->append(ms, rw.yAxis->max());
    }
}

} // namespace openswmmvis::ui
