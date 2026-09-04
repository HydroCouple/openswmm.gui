/*!
 * \file   rainfallvisualizationdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/rainfallvisualizationdialog.h"

#include "core/swmmdatetimeformat.h"
#include "layers/swmmmodellayer.h"
#include "plot/seriesstyle.h"
#include "ui/dialogs/dialoglayoutpersistence.h"
#include "ui/theme/themehelpers.h"
#include "ui/widgets/chartaxisformatcontroller.h"
#include "ui/widgets/interactivechartview.h"

#include <QAction>
#include <QActionGroup>
#include <QChart>
#include <QComboBox>
#include <QDateTimeAxis>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineSeries>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QValueAxis>

#include <algorithm>
#include <limits>

namespace openswmmvis::ui {

using openswmmvis::plot::RainfallSeriesModel;
using openswmmvis::plot::RainGageRainfall;
using openswmmvis::plot::RainGageStats;

namespace {

QString fmtDuration(qint64 secs)
{
    if (secs <= 0) return QStringLiteral("—");
    const qint64 h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
    if (h > 0)  return QStringLiteral("%1:%2:%3").arg(h)
                        .arg(m, 2, 10, QLatin1Char('0'))
                        .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString sourceLabel(const RainGageRainfall &g)
{
    if (g.dataSource == 0)
        return QObject::tr("TIMESERIES \"%1\"").arg(g.timeseriesName);
    switch (g.fileFormat) {
        case 5: return QObject::tr("FILE (rain file, station %1)").arg(g.stationId);
        case 6: return g.fileColumn.isEmpty()
                    ? QObject::tr("FILE (CSV/TSF)")
                    : QObject::tr("FILE (CSV/TSF, column %1)").arg(g.fileColumn);
        default: return QObject::tr("FILE");
    }
}

} // namespace

RainfallVisualizationDialog::RainfallVisualizationDialog(SWMMModelLayer *layer,
                                                         QWidget *parent)
    : QDialog(parent, floatingPanelFlags())
    , m_layer(layer)
{
    setWindowTitle(tr("Rainfall Visualization"));
    // objectName drives app-wide geometry/splitter persistence
    // (DialogLayoutWatcher) and the singleton findChild lookup.
    setObjectName(QStringLiteral("RainfallVisualizationDialog"));
    applyAlwaysOnTopPolicy(this);
    resize(1100, 720);

    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(300);
    connect(m_refreshDebounce, &QTimer::timeout,
            this, &RainfallVisualizationDialog::onAutoRefresh_);

    buildUi_();
    setLayer(layer);
}

RainfallVisualizationDialog::~RainfallVisualizationDialog() = default;

void RainfallVisualizationDialog::setLayer(SWMMModelLayer *layer)
{
    if (m_layer && m_layer != layer) disconnect(m_layer, nullptr, this, nullptr);
    m_layer = layer;
    m_model.setEngine(layer ? layer->engine() : nullptr);
    if (layer) {
        // Debounced auto-refresh: gage/source edits arrive as modelEdited,
        // simulation-date edits (which re-window FILE gage data) as
        // optionsChanged. One timer coalesces bursts.
        connect(layer, &SWMMModelLayer::optionsChanged, this,
                [this] { m_refreshDebounce->start(); });
        connect(layer, &SWMMModelLayer::modelEdited, this,
                [this] { m_refreshDebounce->start(); });
    }
    refresh();
}

RainfallSeriesModel::Basis RainfallVisualizationDialog::basis() const
{
    if (!m_basisCombo) return RainfallSeriesModel::Basis::Intensity;
    return static_cast<RainfallSeriesModel::Basis>(
        m_basisCombo->currentData().toInt());
}

QString RainfallVisualizationDialog::rainUnitLabel_() const
{
    // Rain depth follows the FLOW_UNITS system: inches for US-customary
    // (CFS/GPM/MGD), millimetres for SI (CMS/LPS/MLD).
    const QString fu = m_layer
        ? m_layer->getOption(QByteArrayLiteral("FLOW_UNITS"),
                             QStringLiteral("CFS")).toUpper()
        : QStringLiteral("CFS");
    const bool isSI = (fu == QLatin1String("CMS") || fu == QLatin1String("LPS")
                       || fu == QLatin1String("MLD"));
    return isSI ? tr("mm") : tr("in");
}

QString RainfallVisualizationDialog::basisAxisTitle_() const
{
    const QString u = rainUnitLabel_();
    switch (basis()) {
        case RainfallSeriesModel::Basis::DepthPerInterval:
            return tr("Depth per interval (%1)").arg(u);
        case RainfallSeriesModel::Basis::CumulativeDepth:
            return tr("Cumulative depth (%1)").arg(u);
        case RainfallSeriesModel::Basis::Intensity:
        default:
            return tr("Intensity (%1/hr)").arg(u);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────

void RainfallVisualizationDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(4);

    // Toolbar: interaction modes shared by every chart + Fit + Refresh +
    // the display-basis selector.
    m_toolbar = new QToolBar(this);
    auto *modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    // Same vector icons as the main map toolbar's navigation actions
    // (swmmvis.ui: Pan uses Move, Fit uses Extent), so the modes read the
    // same everywhere. QToolBar shows the icon; the text stays as tooltip.
    auto addMode = [&](const QString &iconRes, const QString &text,
                       bool checked) {
        QAction *a = m_toolbar->addAction(QIcon(iconRes), text);
        a->setCheckable(true);
        a->setChecked(checked);
        modeGroup->addAction(a);
        connect(a, &QAction::toggled, this,
                [this](bool on) { if (on) applyMode_(); });
        return a;
    };
    m_actSelect  = addMode(QStringLiteral(":/swmmvis/Select"),  tr("Select"), true);
    m_actPan     = addMode(QStringLiteral(":/swmmvis/Move"),    tr("Pan"), false);
    m_actZoomIn  = addMode(QStringLiteral(":/swmmvis/ZoomIn"),  tr("Zoom In"), false);
    m_actZoomOut = addMode(QStringLiteral(":/swmmvis/ZoomOut"), tr("Zoom Out"), false);
    m_toolbar->addSeparator();
    QAction *actFit = m_toolbar->addAction(
        QIcon(QStringLiteral(":/swmmvis/Extent")), tr("Fit"));
    connect(actFit, &QAction::triggered, this, [this] {
        if (m_overlay.view) m_overlay.view->resetZoom();
        for (auto &p : m_panels) if (p.view) p.view->resetZoom();
        rebuildCharts_();   // recompute full-extent axis ranges
    });
    QAction *actRefresh = m_toolbar->addAction(
        QIcon(QStringLiteral(":/swmmvis/Refresh")), tr("Refresh"));
    connect(actRefresh, &QAction::triggered,
            this, &RainfallVisualizationDialog::refresh);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(new QLabel(tr(" Basis: "), this));
    m_basisCombo = new QComboBox(this);
    m_basisCombo->setObjectName(QStringLiteral("basisCombo"));
    m_basisCombo->addItem(QString(), int(RainfallSeriesModel::Basis::Intensity));
    m_basisCombo->addItem(QString(), int(RainfallSeriesModel::Basis::DepthPerInterval));
    m_basisCombo->addItem(QString(), int(RainfallSeriesModel::Basis::CumulativeDepth));
    connect(m_basisCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &RainfallVisualizationDialog::onBasisChanged_);
    m_toolbar->addWidget(m_basisCombo);
    outer->addWidget(m_toolbar);

    // Views (tabs) over stats table.
    m_mainSplit = new QSplitter(Qt::Vertical, this);
    m_mainSplit->setObjectName(QStringLiteral("main"));

    m_tabs = new QTabWidget(m_mainSplit);
    m_tabs->setObjectName(QStringLiteral("views"));

    // Overlay tab.
    {
        m_overlay.chart = new QChart();
        m_overlay.chart->legend()->setVisible(true);
        m_overlay.chart->legend()->setAlignment(Qt::AlignBottom);
        m_overlay.axisX = new QDateTimeAxis(m_overlay.chart);
        m_overlay.axisX->setFormat(openswmmvis::core::swmmDateTimeDisplayFormat());
        m_overlay.axisY = new QValueAxis(m_overlay.chart);
        m_overlay.chart->addAxis(m_overlay.axisX, Qt::AlignBottom);
        m_overlay.chart->addAxis(m_overlay.axisY, Qt::AlignLeft);
        m_overlay.view = new InteractiveChartView(m_overlay.chart, m_tabs);
        m_overlay.fmt  = new ChartAxisFormatController(m_overlay.chart, this);
        m_tabs->addTab(m_overlay.view, tr("Overlay"));
    }

    // Per-gage tab: a scroll area whose content is rebuilt per refresh.
    {
        auto *scroll = new QScrollArea(m_tabs);
        scroll->setWidgetResizable(true);
        m_panelsHost = new QWidget(scroll);
        auto *v = new QVBoxLayout(m_panelsHost);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);
        scroll->setWidget(m_panelsHost);
        m_tabs->addTab(scroll, tr("Per Gage"));
    }

    m_emptyLabel = new QLabel(tr("No rain gages in this project."), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(theme::hintItalicStyle());
    m_emptyLabel->hide();

    m_mainSplit->addWidget(m_tabs);

    m_statsTable = new QTableWidget(m_mainSplit);
    m_statsTable->setObjectName(QStringLiteral("statsTable"));
    m_statsTable->setColumnCount(12);
    m_statsTable->setHorizontalHeaderLabels(
        {tr("Gage"), tr("Source"), tr("Total depth"), tr("Peak intensity"),
         tr("Peak time"), tr("Interval"), tr("First"), tr("Last"),
         tr("Gaps"), tr("Longest gap"), tr("Points"), tr("Status")});
    m_statsTable->horizontalHeader()->setStretchLastSection(true);
    m_statsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_statsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_statsTable->verticalHeader()->setVisible(false);
    // The Gage column carries a visibility checkbox per gage (only the
    // focused / first gage starts checked). rebuildStatsTable_ blocks this
    // signal while it programmatically sets check states.
    connect(m_statsTable, &QTableWidget::itemChanged,
            this, &RainfallVisualizationDialog::onStatsItemChanged_);
    m_mainSplit->addWidget(m_statsTable);
    m_mainSplit->setStretchFactor(0, 3);
    m_mainSplit->setStretchFactor(1, 1);

    outer->addWidget(m_emptyLabel);
    outer->addWidget(m_mainSplit, /*stretch=*/1);
}

void RainfallVisualizationDialog::applyMode_()
{
    InteractiveChartView::Mode m = InteractiveChartView::Mode::Select;
    if (m_actPan && m_actPan->isChecked())          m = InteractiveChartView::Mode::Pan;
    else if (m_actZoomIn && m_actZoomIn->isChecked())  m = InteractiveChartView::Mode::ZoomIn;
    else if (m_actZoomOut && m_actZoomOut->isChecked()) m = InteractiveChartView::Mode::ZoomOut;
    if (m_overlay.view) m_overlay.view->setMode(m);
    for (auto &p : m_panels) if (p.view) p.view->setMode(m);
}

// ─────────────────────────────────────────────────────────────────────────────
// Data → views
// ─────────────────────────────────────────────────────────────────────────────

void RainfallVisualizationDialog::refresh()
{
    m_model.reload(/*reloadRainFiles=*/true);
    // Basis labels carry the unit suffix, which follows FLOW_UNITS.
    {
        QSignalBlocker b(m_basisCombo);
        const QString u = rainUnitLabel_();
        m_basisCombo->setItemText(0, tr("Intensity (%1/hr)").arg(u));
        m_basisCombo->setItemText(1, tr("Depth per interval (%1)").arg(u));
        m_basisCombo->setItemText(2, tr("Cumulative depth (%1)").arg(u));
    }
    // Visibility: drop ids that no longer exist; when nothing remains
    // (first open, or a full gage turnover) default to ONE gage — the
    // first with data — and let the stats-table checkboxes opt the rest in.
    {
        QSet<QString> ids;
        for (const auto &g : m_model.gages()) ids.insert(g.id);
        m_visibleGages.intersect(ids);
        if (m_visibleGages.isEmpty()) {
            for (const auto &g : m_model.gages())
                if (g.hasData()) { m_visibleGages.insert(g.id); break; }
            if (m_visibleGages.isEmpty() && !m_model.gages().isEmpty())
                m_visibleGages.insert(m_model.gages().first().id);
        }
    }
    rebuildCharts_();
    rebuildStatsTable_();

    const bool empty = m_model.gages().isEmpty();
    m_emptyLabel->setVisible(empty);
    m_mainSplit->setVisible(!empty);
}

void RainfallVisualizationDialog::onAutoRefresh_() { refresh(); }

void RainfallVisualizationDialog::onBasisChanged_(int)
{
    rebuildCharts_();
}

void RainfallVisualizationDialog::rebuildCharts_()
{
    const auto b = basis();
    const auto &gages = m_model.gages();

    // Aggregate extents across every VISIBLE gage with data.
    double xMin = std::numeric_limits<double>::max();
    double xMax = std::numeric_limits<double>::lowest();
    double yMax = 0.0;

    // ── Overlay ─────────────────────────────────────────────────────────────
    // Style cycle is keyed by the gage's position in the full list (not the
    // plotted subset) so a gage keeps its colour when others are toggled.
    m_overlay.chart->removeAllSeries();
    int gi = -1;
    for (const auto &g : gages) {
        ++gi;
        if (!g.hasData() || !m_visibleGages.contains(g.id)) continue;
        const auto pts = RainfallSeriesModel::buildStepSeries(g, b);
        auto *s = new QLineSeries(m_overlay.chart);
        s->setName(g.id);
        s->setUseOpenGL(true);
        s->replace(pts);
        openswmmvis::plot::applySeriesStyle(
            openswmmvis::plot::defaultStyleForCycle(gi), s);
        m_overlay.chart->addSeries(s);
        s->attachAxis(m_overlay.axisX);
        s->attachAxis(m_overlay.axisY);
        for (const auto &pt : pts) {
            xMin = std::min(xMin, pt.x());
            xMax = std::max(xMax, pt.x());
            yMax = std::max(yMax, pt.y());
        }
    }

    const bool haveData = xMax > xMin;
    if (haveData) {
        const QDateTime lo = QDateTime::fromMSecsSinceEpoch(qint64(xMin), Qt::UTC);
        const QDateTime hi = QDateTime::fromMSecsSinceEpoch(qint64(xMax), Qt::UTC);
        m_overlay.axisX->setRange(lo, hi);
        m_overlay.axisY->setRange(0.0, yMax > 0.0 ? yMax * 1.05 : 1.0);
    }
    m_overlay.axisY->setTitleText(basisAxisTitle_());

    // ── Per-gage panels ─────────────────────────────────────────────────────
    // Rebuild from scratch: gage count is small and this keeps axis wiring
    // simple. Chart objects are parented to their views → deleted with them.
    for (auto &p : m_panels) {
        if (p.view) p.view->deleteLater();
    }
    m_panels.clear();

    // Clear every layout item (widgets were deleteLater'd above) so the
    // panels re-added below share ALL of the viewport height: with a tail
    // stretch the extra space went to the stretch and each chart sat at its
    // 150 px minimum even when one panel had the whole tab to itself.
    auto *hostLayout = qobject_cast<QVBoxLayout *>(m_panelsHost->layout());
    while (QLayoutItem *it = hostLayout->takeAt(0)) delete it;
    gi = -1;
    for (const auto &g : gages) {
        ++gi;
        if (!g.hasData() || !m_visibleGages.contains(g.id)) continue;
        GagePanel p;
        p.chart = new QChart();
        p.chart->legend()->setVisible(false);
        p.chart->setTitle(g.id);
        p.axisX = new QDateTimeAxis(p.chart);
        p.axisX->setFormat(openswmmvis::core::swmmDateTimeDisplayFormat());
        p.axisY = new QValueAxis(p.chart);
        p.axisY->setTitleText(basisAxisTitle_());
        p.chart->addAxis(p.axisX, Qt::AlignBottom);
        p.chart->addAxis(p.axisY, Qt::AlignLeft);

        const auto pts = RainfallSeriesModel::buildStepSeries(g, b);
        auto *s = new QLineSeries(p.chart);
        s->setName(g.id);
        s->setUseOpenGL(true);
        s->replace(pts);
        openswmmvis::plot::applySeriesStyle(
            openswmmvis::plot::defaultStyleForCycle(gi), s);
        p.chart->addSeries(s);
        s->attachAxis(p.axisX);
        s->attachAxis(p.axisY);

        double gYMax = 0.0;
        for (const auto &pt : pts) gYMax = std::max(gYMax, pt.y());
        if (haveData)
            p.axisX->setRange(QDateTime::fromMSecsSinceEpoch(qint64(xMin), Qt::UTC),
                              QDateTime::fromMSecsSinceEpoch(qint64(xMax), Qt::UTC));
        p.axisY->setRange(0.0, gYMax > 0.0 ? gYMax * 1.05 : 1.0);

        p.view = new InteractiveChartView(p.chart, m_panelsHost);
        p.view->setMinimumHeight(150);
        p.fmt  = new ChartAxisFormatController(p.chart, p.view);
        // Equal stretch: N panels split the whole viewport height; the
        // 150 px minimum keeps many-gage layouts scrollable instead of
        // squashed (the scroll area's widgetResizable handles both cases).
        hostLayout->addWidget(p.view, /*stretch=*/1);

        m_panels.push_back(p);
    }

    // Shared time axis: mirror any panel's X range onto every other panel
    // (and the overlay), guarded against re-entry.
    auto wireX = [this](QDateTimeAxis *src) {
        connect(src, &QDateTimeAxis::rangeChanged, this,
                [this, src](const QDateTime &lo, const QDateTime &hi) {
                    if (m_syncingX) return;
                    m_syncingX = true;
                    for (auto &q : m_panels)
                        if (q.axisX && q.axisX != src) q.axisX->setRange(lo, hi);
                    m_syncingX = false;
                });
    };
    for (auto &p : m_panels) wireX(p.axisX);

    applyMode_();
}

void RainfallVisualizationDialog::rebuildStatsTable_()
{
    const auto &gages = m_model.gages();
    const QString u = rainUnitLabel_();
    const QString fmt = openswmmvis::core::swmmDateTimeDisplayFormat();

    // Programmatic check-state writes must not re-enter onStatsItemChanged_.
    const QSignalBlocker blocker(m_statsTable);

    m_statsTable->setRowCount(gages.size());
    int row = 0;
    for (const auto &g : gages) {
        const RainGageStats st = RainfallSeriesModel::computeStats(g);

        auto put = [&](int col, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            m_statsTable->setItem(row, col, item);
            return item;
        };
        auto *gageItem = put(0, g.id);
        gageItem->setFlags(gageItem->flags() | Qt::ItemIsUserCheckable);
        gageItem->setCheckState(m_visibleGages.contains(g.id) ? Qt::Checked
                                                              : Qt::Unchecked);
        gageItem->setToolTip(tr("Show this gage in the charts"));
        put(1, sourceLabel(g));
        put(2, g.hasData() ? tr("%1 %2").arg(st.totalDepth, 0, 'f', 3).arg(u)
                           : QStringLiteral("—"));
        put(3, g.hasData() ? tr("%1 %2/hr").arg(st.peakIntensity, 0, 'f', 3).arg(u)
                           : QStringLiteral("—"));
        put(4, st.peakTime.isValid() ? st.peakTime.toString(fmt)
                                     : QStringLiteral("—"));
        put(5, fmtDuration(qint64(g.intervalSec)));
        put(6, st.first.isValid() ? st.first.toString(fmt) : QStringLiteral("—"));
        put(7, st.last.isValid() ? st.last.toString(fmt) : QStringLiteral("—"));
        put(8, g.hasData() ? QString::number(st.gapCount) : QStringLiteral("—"));
        put(9, fmtDuration(st.longestGapSecs));
        put(10, QString::number(st.sampleCount));

        QString status;
        if (g.fileFailed)       status = tr("file failed to load (0 entries)");
        else if (!g.hasData())  status = tr("no data in window");
        else                    status = tr("OK");
        auto *statusItem = put(11, status);
        if (!g.hasData())
            statusItem->setForeground(Qt::red);
        ++row;
    }
    m_statsTable->resizeColumnsToContents();
}

void RainfallVisualizationDialog::onStatsItemChanged_(QTableWidgetItem *item)
{
    if (!item || item->column() != 0) return;
    const QString id = item->text();
    const bool on = (item->checkState() == Qt::Checked);
    if (on == m_visibleGages.contains(id)) return;   // text-only change
    if (on) m_visibleGages.insert(id);
    else    m_visibleGages.remove(id);
    rebuildCharts_();
}

void RainfallVisualizationDialog::setFocusGage(const QString &gageId)
{
    if (gageId.isEmpty()) return;
    bool known = false;
    for (const auto &g : m_model.gages())
        if (g.id == gageId) { known = true; break; }
    if (!known) return;   // stale ref (renamed/deleted gage) — keep current view
    m_visibleGages = {gageId};
    rebuildCharts_();
    rebuildStatsTable_();
}

} // namespace openswmmvis::ui
