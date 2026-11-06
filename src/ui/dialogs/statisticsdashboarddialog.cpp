/**
 * @file      statisticsdashboarddialog.cpp
 * @brief     Reads cumulative statistics from openswmm_statistics.h: per-node max-depth /
 *            flood-volume / flood-time, per-link max-flow / max-velocity / max-capacity,
 *            per-subcatch peak-runoff / total-runoff.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/statisticsdashboarddialog.h"
#include "ui/theme/themehelpers.h"

#include "core/queryparser.h"
#include "layers/speciesattributes.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "plot/mesh2drunlayer.h"
#include "plot/pollutantload.h"
#include "plot/seriesintegration.h"
#include "plot/swmmoutrunlayer.h"
#include "ui/theme/iconfactory.h"

#include <openswmm/engine/openswmm_output.h>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTextStream>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <vector>

namespace openswmmvis::ui
{

namespace
{

class StatsFilteringProxy final : public QSortFilterProxyModel
{
public:
    /**
     * @param[in] parent Optional QObject owner; proxy does not take ownership of external source
     *   model through binding.
     * @brief Initialize ordinary Qt sort/filter proxy with empty query predicate.
     */
    explicit StatsFilteringProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}

    /**
     * @param[in] predicate Borrowed parsed predicate copied into proxy; invalid predicate disables
     *   additional query filtering.
     * @brief Replace query and invalidate row filter synchronously; source data remains unchanged.
     */
    void setQueryPredicate(const openswmmvis::QueryPredicate &predicate)
    {
        m_predicate = predicate;
        invalidateRowsFilter();
    }

protected:
    /**
     * @param[in] row Zero-based source row tested by Qt.
     * @param[in] parent Borrowed source parent index used for row data lookup.
     * @return False if base Qt filter rejects; otherwise true for invalid predicate/missing source,
     *   or evaluateQuery result over EditRole (DisplayRole fallback) values keyed by translated
     *   headers and Name alias for first column. No conversion beyond QVariant evaluation.
     */
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (!QSortFilterProxyModel::filterAcceptsRow(row, parent))
            return false;
        if (!m_predicate.isValid())
            return true;

        auto *src = sourceModel();
        if (!src)
            return true;

        QVariantMap values;
        const int nCol = src->columnCount();
        for (int c = 0; c < nCol; ++c) {
            const QString key = src->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
            const QModelIndex idx = src->index(row, c, parent);
            QVariant value = src->data(idx, Qt::EditRole);
            if (!value.isValid())
                value = src->data(idx, Qt::DisplayRole);
            values.insert(key, value);
            if (c == 0)
                values.insert(QStringLiteral("Name"), value);
        }
        return openswmmvis::evaluateQuery(m_predicate, values);
    }

private:
    openswmmvis::QueryPredicate m_predicate;
};

using openswmmvis::plot::ObjectRef;
using openswmmvis::plot::PlotAttribute;
using openswmmvis::plot::ResultDescriptor;
using openswmmvis::plot::SeriesData;

/*! V6: what a column reports about one descriptor series. */
enum class Stat { Max, Min, SampleSum, TimeIntegral, TimeMean, Final };

struct StatColumn {
    QString header;
    ResultDescriptor descriptor;
    Stat stat = Stat::Max;
    char format = 'f'; ///< display rounding; the sort/query value is the rounded text
    int precision = 3;
    QString units;
    double scale = 1.0; ///< applied to a time integral (seconds -> volume/depth units)
    QString tip;
};

double statOf(const SeriesData &s, Stat stat, double start, int step)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (!s.ok || s.values.empty())
        return nan;
    std::vector<double> finite;
    for (double v : s.values)
        if (std::isfinite(v))
            finite.push_back(v);
    switch (stat) {
    case Stat::Max:
        return finite.empty() ? nan : *std::max_element(finite.begin(), finite.end());
    case Stat::Min:
        return finite.empty() ? nan : *std::min_element(finite.begin(), finite.end());
    case Stat::SampleSum: // the sum of the stored samples, NOT a time integral
        return finite.empty() ? nan : std::accumulate(finite.begin(), finite.end(), 0.0);
    case Stat::TimeIntegral:
        return openswmmvis::plot::timeIntegral(s.timesJulian, s.values, start, step);
    case Stat::TimeMean:
        return openswmmvis::plot::timeWeightedMean(s.timesJulian, s.values, start, step);
    case Stat::Final:
        return s.values.back();
    }
    return nan;
}

/*! A table cell: rounded like the dashboard always rounded, or an empty
 *  item (no EditRole) for a missing value — never 0. */
QStandardItem *statItem(double v, const StatColumn &c)
{
    if (!std::isfinite(v)) {
        auto *item = new QStandardItem;
        item->setToolTip(QObject::tr("Missing (not zero): the run has no value for %1 here.")
                             .arg(c.header));
        return item;
    }
    auto *item = new QStandardItem(QString::number(v, c.format, c.precision));
    item->setData(item->text().toDouble(), Qt::EditRole); // sortable numeric
    return item;
}

/*! Fill \p model with one row per id: each column a statistic of a
 *  descriptor series read through \p run (each descriptor read once). */
void populateStats(QStandardItemModel *model, const QString &idHeader, ObjectRef::Kind kind,
                   const QStringList &ids, const QVector<StatColumn> &columns,
                   const openswmmvis::plot::IRunLayer &run)
{
    model->clear();
    QStringList headers{idHeader};
    for (const StatColumn &c : columns)
        headers.append(c.header);
    model->setHorizontalHeaderLabels(headers);
    for (int c = 0; c < columns.size(); ++c) {
        QString tip = columns[c].units.isEmpty()
                          ? QString()
                          : QObject::tr("Units: %1").arg(columns[c].units);
        if (!columns[c].tip.isEmpty())
            tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + columns[c].tip;
        model->setHeaderData(c + 1, Qt::Horizontal, tip, Qt::ToolTipRole);
    }
    QVector<ResultDescriptor> unique;
    QVector<int> source(columns.size());
    for (int c = 0; c < columns.size(); ++c) {
        int at = unique.indexOf(columns[c].descriptor);
        if (at < 0) {
            at = int(unique.size());
            unique.append(columns[c].descriptor);
        }
        source[c] = at;
    }
    const double start = run.startDateJulian();
    const int step = run.reportStepSeconds();
    for (const QString &id : ids) {
        QVector<openswmmvis::plot::SeriesRequest> requests;
        const ObjectRef ref = kind == ObjectRef::Kind::System ? ObjectRef::forSystem()
                                                              : ObjectRef(kind, id);
        for (const ResultDescriptor &d : std::as_const(unique))
            requests.append({ref, d, 0});
        QVector<SeriesData> series;
        run.getSeriesBatch(requests, series);
        QList<QStandardItem *> row;
        row << new QStandardItem(id);
        for (int c = 0; c < columns.size(); ++c) {
            double v = statOf(series.value(source[c]), columns[c].stat, start, step);
            if (columns[c].stat == Stat::TimeIntegral)
                v *= columns[c].scale;
            row << statItem(v, columns[c]);
        }
        model->appendRow(row);
    }
}

/*! The run's 1D object ids of \p kind, in `.out` order. */
QStringList objectIds(SWMM_Output handle, ObjectRef::Kind kind)
{
    QStringList ids;
    if (!handle)
        return ids;
    const int n = kind == ObjectRef::Kind::Node   ? swmm_output_get_node_count(handle)
                  : kind == ObjectRef::Kind::Link ? swmm_output_get_link_count(handle)
                                                  : swmm_output_get_subcatch_count(handle);
    if (n <= 0 || swmm_output_get_period_count(handle) <= 0)
        return ids;
    for (int i = 0; i < n; ++i)
        ids.append(QString::fromUtf8(kind == ObjectRef::Kind::Node
                                         ? swmm_output_get_node_id(handle, i)
                                     : kind == ObjectRef::Kind::Link
                                         ? swmm_output_get_link_id(handle, i)
                                         : swmm_output_get_subcatch_id(handle, i)));
    return ids;
}

/*! Species columns: the max of each `.out` species and, on nodes and links,
 *  each pollutant's total load (the time integral of flow x concentration). */
QVector<StatColumn> speciesColumns(const openswmmvis::plot::SwmmOutRunLayer &run,
                                   ObjectRef::Kind kind)
{
    QVector<StatColumn> columns;
    namespace load = openswmmvis::plot::pollutantload;
    for (const ResultDescriptor &d : run.resultDescriptorsForKind(kind)) {
        if (d.isSpecies()) {
            StatColumn c;
            c.header = QObject::tr("Max %1").arg(
                OpenSWMMVis::Species::speciesDisplayLabel(d.species));
            c.descriptor = d;
            c.format = 'g';
            c.precision = 6;
            c.units = d.unitLabel(run.unitSystem());
            columns.append(c);
        } else if (load::isLoadDescriptor(d)) {
            QString species;
            bool cumulative = false;
            if (!load::parseKey(d.variableKey, &species, &cumulative) || !cumulative)
                continue;
            // Not the engine's own total: say so in the header and the tip.
            const QString name = OpenSWMMVis::Species::speciesDisplayLabel(species);
            StatColumn c;
            c.header = kind == ObjectRef::Kind::Link
                           ? QObject::tr("%1 net load (integrated from report-step samples)")
                                 .arg(name)
                           : QObject::tr("%1 load (integrated from report-step samples)").arg(name);
            c.descriptor = d;
            c.stat = Stat::Final;
            c.format = 'g';
            c.precision = 6;
            c.units = d.variableUnits;
            c.tip = d.variableNote;
            columns.append(c);
        }
    }
    return columns;
}

StatColumn fixedColumn(const QString &header, PlotAttribute a, Stat stat,
                       openswmmvis::plot::UnitSystem u, int precision = 3, int flowUnits = -1)
{
    StatColumn c;
    c.header = header;
    c.descriptor = ResultDescriptor::forAttribute(a);
    c.stat = stat;
    c.precision = precision;
    // .out flow columns are in the run's flow units (LPS, MGD, ...).
    const bool flow = a == PlotAttribute::NodeOverflow || a == PlotAttribute::LinkFlow ||
                      a == PlotAttribute::SubcatchRunoff;
    const QString flowName = openswmmvis::plot::pollutantload::flowUnitsName(flowUnits);
    c.units = flow && !flowName.isEmpty() ? flowName : openswmmvis::plot::unitsFor(a, u);
    if (stat == Stat::SampleSum)
        c.tip = QObject::tr("Sum of the stored report samples, not a time integral.");
    return c;
}

/*! Volume unit and per-second factor of a flow-units code's time integral. */
std::pair<QString, double> flowVolume(int flowUnitsCode)
{
    switch (flowUnitsCode) {
    case 0:
        return {QStringLiteral("ft³"), 1.0};
    case 1:
        return {QStringLiteral("gal"), 1.0 / 60.0};
    case 2:
        return {QStringLiteral("Mgal"), 1.0 / 86400.0};
    case 3:
        return {QStringLiteral("m³"), 1.0};
    case 4:
        return {QStringLiteral("L"), 1.0};
    case 5:
        return {QStringLiteral("ML"), 1.0 / 86400.0};
    default:
        return {QString(), std::numeric_limits<double>::quiet_NaN()};
    }
}

} // namespace

StatisticsDashboardDialog::StatisticsDashboardDialog(SWMMResultsLayer *layer, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Statistics Dashboard"));
    resize(960, 640);
    buildUi();
    setResultsLayer(layer);
}

void StatisticsDashboardDialog::setResultsLayer(SWMMResultsLayer *layer)
{
    m_layer = layer;

    if (m_nodeModel)
        m_nodeModel->removeRows(0, m_nodeModel->rowCount());
    if (m_linkModel)
        m_linkModel->removeRows(0, m_linkModel->rowCount());
    if (m_subModel)
        m_subModel->removeRows(0, m_subModel->rowCount());

    populateNodeStats();
    populateLinkStats();
    populateSubcatchStats();
    if (applyQueryToAllTables())
        updateQueryStatus();
}

StatisticsDashboardDialog::~StatisticsDashboardDialog() = default;

void StatisticsDashboardDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    auto *queryRow = new QHBoxLayout;
    queryRow->setContentsMargins(0, 0, 0, 0);
    queryRow->addWidget(new QLabel(tr("Query:"), this));
    m_queryEdit = new QLineEdit(this);
    m_queryEdit->setObjectName(QStringLiteral("statisticsDashboardQueryEdit"));
    m_queryEdit->setClearButtonEnabled(true);
    m_queryEdit->setPlaceholderText(
        tr("e.g. Name LIKE 'J%'; \"Max depth\" > 5; \"Max flow\" >= 10"));
    m_queryEdit->setToolTip(
        tr("Filter rows by the same SQL-like WHERE clause used by the Attribute Table.\n"
           "\n"
           "Quote column names that contain spaces:\n"
           "    \"Max depth\" > 5\n"
           "\n"
           "LIKE is case-insensitive and supports % / _ wildcards:\n"
           "    Name LIKE 'J%'\n"
           "\n"
           "Use = != < <= > >=, IN (...), AND / OR / NOT."));
    queryRow->addWidget(m_queryEdit, 1);
    m_queryApply = new QPushButton(tr("Apply"), this);
    m_queryClear = new QPushButton(tr("Clear"), this);
    m_queryStatus = new QLabel(this);
    m_queryStatus->setMinimumWidth(150);
    m_queryStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    queryRow->addWidget(m_queryApply);
    queryRow->addWidget(m_queryClear);
    queryRow->addWidget(m_queryStatus);
    root->addLayout(queryRow);

    m_tabs = new QTabWidget(this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("StatisticsDashboardDialog"));
    m_tabs->setObjectName(QStringLiteral("tabs"));

    // Node table -------------------------------------------------------------
    m_nodeModel = new QStandardItemModel(this);
    m_nodeModel->setHorizontalHeaderLabels(
        {tr("Node"), tr("Max depth"), tr("Max head"), tr("Max overflow"),
         tr("Volume (sample sum)")});
    m_nodeProxy = new StatsFilteringProxy(this);
    m_nodeProxy->setSourceModel(m_nodeModel);
    m_nodeProxy->setSortRole(Qt::EditRole);
    m_nodeTable = new QTableView(this);
    m_nodeTable->setModel(m_nodeProxy);
    m_nodeTable->setSortingEnabled(true);
    m_nodeTable->setObjectName(QStringLiteral("nodeTable"));
    m_nodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nodeTable->setAlternatingRowColors(true);
    m_nodeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tabs->addTab(m_nodeTable, tr("&Nodes"));

    // Link table -------------------------------------------------------------
    m_linkModel = new QStandardItemModel(this);
    m_linkModel->setHorizontalHeaderLabels(
        {tr("Link"), tr("Max flow"), tr("Max depth"), tr("Max velocity"), tr("Max capacity")});
    m_linkProxy = new StatsFilteringProxy(this);
    m_linkProxy->setSourceModel(m_linkModel);
    m_linkProxy->setSortRole(Qt::EditRole);
    m_linkTable = new QTableView(this);
    m_linkTable->setModel(m_linkProxy);
    m_linkTable->setSortingEnabled(true);
    m_linkTable->setObjectName(QStringLiteral("linkTable"));
    m_linkTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_linkTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_linkTable->setAlternatingRowColors(true);
    m_linkTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tabs->addTab(m_linkTable, tr("&Links"));

    // Subcatchment table -----------------------------------------------------
    m_subModel = new QStandardItemModel(this);
    m_subModel->setHorizontalHeaderLabels({tr("Subcatchment"), tr("Peak runoff"),
                                           tr("Runoff (sample sum)"),
                                           tr("Infiltration (sample sum)"),
                                           tr("Evaporation (sample sum)"), tr("Runoff volume"),
                                           tr("Infiltration depth"), tr("Evaporation depth")});
    m_subProxy = new StatsFilteringProxy(this);
    m_subProxy->setSourceModel(m_subModel);
    m_subProxy->setSortRole(Qt::EditRole);
    m_subTable = new QTableView(this);
    m_subTable->setModel(m_subProxy);
    m_subTable->setSortingEnabled(true);
    m_subTable->setObjectName(QStringLiteral("subTable"));
    m_subTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_subTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_subTable->setAlternatingRowColors(true);
    m_subTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tabs->addTab(m_subTable, tr("S&ubcatchments"));

    // V6: 2D regions — the map-selected cells, the selection, each cell tag
    // and the whole domain for one 2D variable and aggregate.
    m_meshPage = new QWidget(this);
    auto *meshLayout = new QVBoxLayout(m_meshPage);
    meshLayout->setContentsMargins(0, 0, 0, 0);
    auto *meshRow = new QHBoxLayout;
    meshRow->addWidget(new QLabel(tr("2D variable:"), m_meshPage));
    m_meshVariable = new QComboBox(m_meshPage);
    m_meshVariable->setObjectName(QStringLiteral("meshStatsVariable"));
    meshRow->addWidget(m_meshVariable, 1);
    meshRow->addWidget(new QLabel(tr("Aggregate:"), m_meshPage));
    m_meshAggregate = new QComboBox(m_meshPage);
    m_meshAggregate->setObjectName(QStringLiteral("meshStatsAggregate"));
    for (auto a : {openswmmvis::results::MeshAggregate::AreaWeightedMean,
                   openswmmvis::results::MeshAggregate::Mean,
                   openswmmvis::results::MeshAggregate::Sum,
                   openswmmvis::results::MeshAggregate::Mass})
        m_meshAggregate->addItem(openswmmvis::results::aggregateLabel(a), int(a));
    m_meshAggregate->setToolTip(
        tr("How a region's cells combine at each report time. Mass = concentration x water "
           "volume (depth x cell area), for surface species concentrations. Missing cells are "
           "left out, never counted as zero."));
    meshRow->addWidget(m_meshAggregate);
    meshLayout->addLayout(meshRow);
    m_meshModel = new QStandardItemModel(this);
    m_meshProxy = new StatsFilteringProxy(this);
    m_meshProxy->setSourceModel(m_meshModel);
    m_meshProxy->setSortRole(Qt::EditRole);
    m_meshTable = new QTableView(m_meshPage);
    m_meshTable->setModel(m_meshProxy);
    m_meshTable->setSortingEnabled(true);
    m_meshTable->setObjectName(QStringLiteral("meshTable"));
    m_meshTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_meshTable->setAlternatingRowColors(true);
    m_meshTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_meshTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    meshLayout->addWidget(m_meshTable, 1);
    m_tabs->addTab(m_meshPage, tr("2D &regions"));
    m_tabs->setTabVisible(3, false);
    connect(m_meshVariable, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { populateMeshStats(); });
    connect(m_meshAggregate, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { populateMeshStats(); });

    // Histogram view is kept as an off-layout helper so selecting a numeric
    // column can still update the chart object without taking vertical space
    // from the tabular result review.
    m_histView = new QChartView(this);
    m_histView->setRenderHint(QPainter::Antialiasing);
    m_histView->hide();
    root->addWidget(m_tabs, 1);

    // Buttons ---------------------------------------------------------------
    auto *btnRow = new QHBoxLayout;
    auto *exportBtn = new QPushButton(
        openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportCsv")), tr("Export CSV…"), this);
    btnRow->addWidget(exportBtn);
    btnRow->addStretch(1);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
    connect(exportBtn, &QPushButton::clicked, this, &StatisticsDashboardDialog::onExportClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_queryEdit, &QLineEdit::returnPressed, this,
            &StatisticsDashboardDialog::onQueryApplyClicked);
    connect(m_queryApply, &QPushButton::clicked, this,
            &StatisticsDashboardDialog::onQueryApplyClicked);
    connect(m_queryClear, &QPushButton::clicked, this,
            &StatisticsDashboardDialog::onQueryClearClicked);
    connect(m_tabs, &QTabWidget::currentChanged, this,
            &StatisticsDashboardDialog::onCurrentTabChanged);

    // The dashboard reports simulation results; the tables are a read-only
    // view of the .out file, so no cell may be edited.  Selection → histogram,
    // selection → cross-view selection bus, and right-click → zoom to extents.
    for (QTableView *table : {m_nodeTable, m_linkTable, m_subTable}) {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(table, &QWidget::customContextMenuRequested, this,
                &StatisticsDashboardDialog::onTableContextMenuRequested);
        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                &StatisticsDashboardDialog::onTableSelectionChanged);
    }
}

void StatisticsDashboardDialog::setProject(SWMMModelLayer *layer, SelectionManager *selMgr,
                                           MapCanvas *canvas)
{
    if (m_selMgr)
        disconnect(m_selMgr, nullptr, this, nullptr);

    m_modelLayer = layer;
    m_selMgr = selMgr;
    m_canvas = canvas;

    if (m_selMgr) {
        connect(m_selMgr, &SelectionManager::selectionChanged, this,
                &StatisticsDashboardDialog::onSelectionManagerChanged);
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
    }
}

SWMMObjectRef::ObjectType StatisticsDashboardDialog::currentObjectType() const
{
    if (!m_tabs)
        return SWMMObjectRef::Unknown;
    switch (m_tabs->currentIndex()) {
    case 0:
        return SWMMObjectRef::Node;
    case 1:
        return SWMMObjectRef::Link;
    case 2:
        return SWMMObjectRef::Subcatchment;
    default:
        return SWMMObjectRef::Unknown;
    }
}

void StatisticsDashboardDialog::populateNodeStats()
{
    m_nodeModel->removeRows(0, m_nodeModel->rowCount());
    if (!m_layer)
        return;
    using U = openswmmvis::plot::UnitSystem;
    const openswmmvis::plot::SwmmOutRunLayer run(m_layer);
    const U u = run.unitSystem();
    QVector<StatColumn> columns = {
        fixedColumn(tr("Max depth"), PlotAttribute::NodeDepth, Stat::Max, u),
        fixedColumn(tr("Max head"), PlotAttribute::NodeHead, Stat::Max, u),
        fixedColumn(tr("Max overflow"), PlotAttribute::NodeOverflow, Stat::Max, u, 3,
                    m_layer->flowUnits()),
        fixedColumn(tr("Volume (sample sum)"), PlotAttribute::NodeVolume, Stat::SampleSum, u, 1)};
    columns += speciesColumns(run, ObjectRef::Kind::Node);
    populateStats(m_nodeModel, tr("Node"), ObjectRef::Kind::Node,
                  objectIds(m_layer->outputHandle(), ObjectRef::Kind::Node), columns, run);
    QStringList units{QString()};
    for (const StatColumn &c : columns)
        units.append(c.units);
    m_units.insert(m_nodeModel, units);
}

void StatisticsDashboardDialog::populateLinkStats()
{
    m_linkModel->removeRows(0, m_linkModel->rowCount());
    if (!m_layer)
        return;
    const openswmmvis::plot::SwmmOutRunLayer run(m_layer);
    const auto u = run.unitSystem();
    QVector<StatColumn> columns = {
        fixedColumn(tr("Max flow"), PlotAttribute::LinkFlow, Stat::Max, u, 3,
                    m_layer->flowUnits()),
        fixedColumn(tr("Max depth"), PlotAttribute::LinkDepth, Stat::Max, u),
        fixedColumn(tr("Max velocity"), PlotAttribute::LinkVelocity, Stat::Max, u),
        fixedColumn(tr("Max capacity"), PlotAttribute::LinkCapacity, Stat::Max, u)};
    columns += speciesColumns(run, ObjectRef::Kind::Link);
    populateStats(m_linkModel, tr("Link"), ObjectRef::Kind::Link,
                  objectIds(m_layer->outputHandle(), ObjectRef::Kind::Link), columns, run);
    QStringList units{QString()};
    for (const StatColumn &c : columns)
        units.append(c.units);
    m_units.insert(m_linkModel, units);
}

void StatisticsDashboardDialog::populateSubcatchStats()
{
    m_subModel->removeRows(0, m_subModel->rowCount());
    if (!m_layer)
        return;
    using U = openswmmvis::plot::UnitSystem;
    const openswmmvis::plot::SwmmOutRunLayer run(m_layer);
    const U u = run.unitSystem();
    QVector<StatColumn> columns = {
        fixedColumn(tr("Peak runoff"), PlotAttribute::SubcatchRunoff, Stat::Max, u, 3,
                    m_layer->flowUnits()),
        fixedColumn(tr("Runoff (sample sum)"), PlotAttribute::SubcatchRunoff, Stat::SampleSum, u,
                    3, m_layer->flowUnits()),
        fixedColumn(tr("Infiltration (sample sum)"), PlotAttribute::SubcatchInfil,
                    Stat::SampleSum, u),
        fixedColumn(tr("Evaporation (sample sum)"), PlotAttribute::SubcatchEvap,
                    Stat::SampleSum, u)};
    // The time-integral variants of the three rates (V6 / brief item 3).
    const auto [volumeUnits, volumeScale] = flowVolume(m_layer->flowUnits());
    const QString depth = u == U::US ? QStringLiteral("in") : QStringLiteral("mm");
    const QString integralTip =
        tr("Time integral of the report samples (each value held over the interval ending at "
           "it).");
    StatColumn runoff = fixedColumn(tr("Runoff volume"), PlotAttribute::SubcatchRunoff,
                                    Stat::TimeIntegral, u);
    runoff.units = volumeUnits;
    runoff.scale = volumeScale;
    runoff.tip = integralTip;
    StatColumn infil = fixedColumn(tr("Infiltration depth"), PlotAttribute::SubcatchInfil,
                                   Stat::TimeIntegral, u);
    infil.units = depth;
    infil.scale = 1.0 / 3600.0; // in/hr | mm/hr x s
    infil.tip = integralTip;
    StatColumn evap = fixedColumn(tr("Evaporation depth"), PlotAttribute::SubcatchEvap,
                                  Stat::TimeIntegral, u);
    evap.units = depth;
    evap.scale = 1.0 / 86400.0; // in/day | mm/day x s
    evap.tip = integralTip;
    columns << runoff << infil << evap;
    columns += speciesColumns(run, ObjectRef::Kind::Subcatch);
    populateStats(m_subModel, tr("Subcatchment"), ObjectRef::Kind::Subcatch,
                  objectIds(m_layer->outputHandle(), ObjectRef::Kind::Subcatch), columns, run);
    QStringList units{QString()};
    for (const StatColumn &c : columns)
        units.append(c.units);
    m_units.insert(m_subModel, units);
}

void StatisticsDashboardDialog::set2DResults(SWMM2DResultsLayer *mesh, const QStringList &cellTags)
{
    m_mesh = mesh;
    m_cellTags = cellTags;
    if (m_tabs) {
        m_tabs->setTabVisible(3, m_mesh != nullptr);
        if (m_mesh && !m_layer) // a 2D-only run opens on its tab
            m_tabs->setCurrentIndex(3);
    }
    refreshMeshVariables();
}

void StatisticsDashboardDialog::refreshMeshVariables()
{
    if (!m_meshVariable)
        return;
    const QSignalBlocker block(m_meshVariable);
    m_meshVariable->clear();
    if (m_mesh) {
        const openswmmvis::plot::Mesh2DRunLayer run(m_mesh);
        const auto cellDescriptors = run.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell);
        for (const ResultDescriptor &d : cellDescriptors) {
            if (!d.isMeshVariable() && d.attr != PlotAttribute::Mesh2DDepth)
                continue;
            const QString units = d.unitLabel(openswmmvis::plot::UnitSystem::SI);
            const QString text =
                units.isEmpty() ? d.label() : QStringLiteral("%1 (%2)").arg(d.label(), units);
            m_meshVariable->addItem(text, d.isMeshVariable()
                                              ? QVariant(QStringList{d.variableKey,
                                                                     d.variableLabel,
                                                                     d.variableUnits})
                                              : QVariant(int(d.attr)));
        }
    }
    populateMeshStats();
}

void StatisticsDashboardDialog::populateMeshStats()
{
    if (!m_meshModel)
        return;
    m_meshModel->clear();
    if (!m_mesh || !m_mesh->source() || m_meshVariable->currentIndex() < 0)
        return;
    const QVariant data = m_meshVariable->currentData();
    const QStringList keyed = data.toStringList();
    const ResultDescriptor d =
        keyed.size() == 3 ? ResultDescriptor::forMeshVariable(keyed[0], keyed[1], keyed[2])
                          : ResultDescriptor::forAttribute(PlotAttribute(data.toInt()));
    const auto aggregate =
        static_cast<openswmmvis::results::MeshAggregate>(m_meshAggregate->currentData().toInt());

    // Regions: each map-selected cell, the selection, each tag, the domain.
    std::set<int> picked;
    const auto &triCell = m_mesh->triCellMap();
    for (int triangle : m_mesh->highlightedCells())
        if (triangle >= 0 && std::size_t(triangle) < triCell.size() &&
            triCell[std::size_t(triangle)] >= 0)
            picked.insert(triCell[std::size_t(triangle)]);
    const int cells = m_mesh->source()->triangleCount();
    QVector<openswmmvis::results::MeshRegion> regions;
    for (int c : picked)
        regions.append({tr("Cell %1").arg(c), {c}});
    if (!picked.empty())
        regions.append({tr("Selection (%n cells)", nullptr, int(picked.size())),
                        std::vector<int>(picked.begin(), picked.end())});
    if (m_cellTags.size() == cells)
        for (auto r : openswmmvis::results::tagRegions(m_cellTags)) {
            r.label = tr("%1 (%n cells)", nullptr, int(r.cells.size())).arg(r.label);
            regions.append(r);
        }
    auto domain = openswmmvis::results::domainRegion(cells);
    domain.label = tr("%1 (%n cells)", nullptr, cells).arg(domain.label);
    regions.append(domain);

    const openswmmvis::plot::Mesh2DRunLayer run(m_mesh);
    QVector<SeriesData> series;
    run.getRegionSeries(regions, d, aggregate, series);
    const QString units = run.regionUnits(d, aggregate);
    const QString integralUnits = units.isEmpty() ? QString() : units + QStringLiteral("·s");
    QVector<StatColumn> columns;
    const auto column = [&](const QString &header, Stat stat, const QString &u) {
        StatColumn c;
        c.header = header;
        c.descriptor = d;
        c.stat = stat;
        c.format = 'g';
        c.precision = 6;
        c.units = u;
        columns.append(c);
    };
    column(tr("Max"), Stat::Max, units);
    column(tr("Min"), Stat::Min, units);
    column(tr("Mean (time-weighted)"), Stat::TimeMean, units);
    column(tr("Final"), Stat::Final, units);
    column(tr("Time integral"), Stat::TimeIntegral, integralUnits);
    columns.last().tip = tr("Each report value held over the interval ending at it.");

    QStringList headers{tr("Region")};
    QStringList unitRow{QString()};
    for (const StatColumn &c : std::as_const(columns)) {
        headers.append(c.header);
        unitRow.append(c.units);
    }
    m_meshModel->setHorizontalHeaderLabels(headers);
    for (int c = 0; c < columns.size(); ++c)
        if (!columns[c].units.isEmpty())
            m_meshModel->setHeaderData(c + 1, Qt::Horizontal, tr("Units: %1").arg(columns[c].units),
                                       Qt::ToolTipRole);
    m_units.insert(m_meshModel, unitRow);
    const double start = run.startDateJulian();
    const int step = run.reportStepSeconds();
    for (int r = 0; r < regions.size(); ++r) {
        QList<QStandardItem *> row;
        auto *label = new QStandardItem(regions[r].label);
        if (!series.value(r).ok)
            label->setToolTip(series.value(r).errorMessage);
        else if (!series.value(r).displayNote.isEmpty())
            label->setToolTip(series.value(r).displayNote);
        row << label;
        for (const StatColumn &c : std::as_const(columns))
            row << statItem(statOf(series.value(r), c.stat, start, step), c);
        m_meshModel->appendRow(row);
    }
    if (applyQueryToAllTables())
        updateQueryStatus();
}

void StatisticsDashboardDialog::refreshHistogramForSelection()
{
    auto *table = currentTable();
    if (!table || !table->selectionModel())
        return;
    const QModelIndexList sel = table->selectionModel()->selectedIndexes();
    if (sel.isEmpty())
        return;
    const int col = sel.first().column();
    if (col == 0)
        return; // ID column — no histogram
    rebuildHistogramFor(col);
}

void StatisticsDashboardDialog::onTableSelectionChanged()
{
    refreshHistogramForSelection();

    // Push the gesture onto the cross-view selection bus so the map highlights
    // the same features.  Guarded against the echo of our own bus update.
    if (m_applyingFromBus || !m_selMgr)
        return;
    auto *table = currentTable();
    if (!table || !table->selectionModel())
        return;
    if (table->selectionModel() != sender())
        return; // background tab

    const auto type = currentObjectType();
    if (type == SWMMObjectRef::Unknown)
        return;

    QSet<SWMMObjectRef> refs;
    for (const QModelIndex &idx : table->selectionModel()->selectedRows()) {
        const QString name = idx.data(Qt::DisplayRole).toString();
        if (!name.isEmpty())
            refs.insert(SWMMObjectRef(type, name));
    }
    m_selMgr->select(refs, SelectionManager::Replace);
}

void StatisticsDashboardDialog::onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                                          const QSet<SWMMObjectRef> & /*added*/,
                                                          const QSet<SWMMObjectRef> & /*removed*/)
{
    // Mirror the bus into all three tables so switching tabs shows the right
    // rows already highlighted.  The guard stops the resulting
    // selectionChanged signals from bouncing straight back to the bus.
    m_applyingFromBus = true;

    struct {
        QTableView *table;
        SWMMObjectRef::ObjectType type;
    } tabs[] = {
        {m_nodeTable, SWMMObjectRef::Node},
        {m_linkTable, SWMMObjectRef::Link},
        {m_subTable, SWMMObjectRef::Subcatchment},
    };

    for (const auto &t : tabs) {
        if (!t.table || !t.table->model() || !t.table->selectionModel())
            continue;
        auto *model = t.table->model();
        auto *sel = t.table->selectionModel();
        sel->clearSelection();

        QSet<QString> names;
        for (const auto &ref : current) {
            if (ref.objectType == t.type)
                names.insert(ref.name);
        }
        if (names.isEmpty())
            continue;

        // Row order is the proxy's (sorted / filtered), so resolve by name.
        for (int r = 0, n = model->rowCount(); r < n; ++r) {
            const QModelIndex idx = model->index(r, 0);
            if (names.contains(idx.data(Qt::DisplayRole).toString()))
                sel->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    m_applyingFromBus = false;
}

void StatisticsDashboardDialog::onTableContextMenuRequested(const QPoint &pos)
{
    auto *table = qobject_cast<QTableView *>(sender());
    if (!table)
        return;

    QMenu menu(this);
    QAction *zoom = menu.addAction(tr("Zoom to Selected"));
    zoom->setEnabled(m_canvas && m_modelLayer && table->selectionModel() &&
                     !table->selectionModel()->selectedRows().isEmpty());
    connect(zoom, &QAction::triggered, this, &StatisticsDashboardDialog::onZoomToSelected);
    menu.exec(table->viewport()->mapToGlobal(pos));
}

void StatisticsDashboardDialog::onZoomToSelected()
{
    if (!m_canvas || !m_modelLayer)
        return;
    auto *table = currentTable();
    if (!table || !table->selectionModel())
        return;

    // Union the layer-CRS bboxes of the selected rows, skipping names the
    // model layer can't resolve (results may name objects the .inp doesn't).
    MapExtent acc;
    bool any = false;
    for (const QModelIndex &idx : table->selectionModel()->selectedRows()) {
        const QString name = idx.data(Qt::DisplayRole).toString();
        if (name.isEmpty())
            continue;
        const MapExtent e = m_modelLayer->objectExtent(name);
        if (!std::isfinite(e.xMin()) || !std::isfinite(e.xMax()))
            continue;
        if (!any) {
            acc = e;
            any = true;
        } else {
            acc = acc.united(e);
        }
    }
    if (!any)
        return;

    // Same padding heuristic as AttributeTablePanel::onZoomToSelectedClicked():
    // 10 % pad for extents with area, an absolute buffer for point selections.
    MapExtent obj = m_canvas->extentInCanvasCRS(m_modelLayer, acc);
    if (!std::isfinite(obj.xMin()) || !std::isfinite(obj.xMax()))
        return;

    double x0 = obj.xMin(), y0 = obj.yMin();
    double x1 = obj.xMax(), y1 = obj.yMax();
    if (obj.width() == 0.0 && obj.height() == 0.0) {
        double buf = 100.0;
        if (const MapExtent le = m_canvas->layerExtentInCanvasCRS(m_modelLayer); le.isValid()) {
            buf = std::max(25.0, 0.005 * std::max(le.xMax() - le.xMin(), le.yMax() - le.yMin()));
        }
        x0 -= buf;
        y0 -= buf;
        x1 += buf;
        y1 += buf;
    } else {
        const double padX = std::max(1e-6, obj.width() * 0.10);
        const double padY = std::max(1e-6, obj.height() * 0.10);
        x0 -= padX;
        y0 -= padY;
        x1 += padX;
        y1 += padY;
    }
    const MapExtent zoom(x0, y0, x1, y1);
    if (zoom.isValid())
        m_canvas->setExtent(zoom);
}

void StatisticsDashboardDialog::rebuildHistogramFor(int column)
{
    auto *table = currentTable();
    if (!table)
        return;
    auto *model = table->model();
    if (!model)
        return;

    // Collect visible values, build 20-bin histogram.
    std::vector<double> values;
    values.reserve(model->rowCount());
    double minV = std::numeric_limits<double>::infinity();
    double maxV = -std::numeric_limits<double>::infinity();
    for (int r = 0; r < model->rowCount(); ++r) {
        const double v = model->index(r, column).data(Qt::EditRole).toDouble();
        if (!std::isfinite(v))
            continue;
        values.push_back(v);
        if (v < minV)
            minV = v;
        if (v > maxV)
            maxV = v;
    }
    if (values.size() < 2 || maxV <= minV) {
        m_histView->setChart(new QChart);
        return;
    }

    const int kBins = 20;
    std::vector<int> bins(kBins, 0);
    for (double v : values) {
        int b = static_cast<int>(((v - minV) / (maxV - minV)) * (kBins - 1));
        if (b < 0)
            b = 0;
        if (b >= kBins)
            b = kBins - 1;
        ++bins[b];
    }

    auto *chart = new QChart;
    chart->setTitle(tr("Frequency distribution — %1 (%2 samples)")
                        .arg(model->headerData(column, Qt::Horizontal).toString())
                        .arg(static_cast<int>(values.size())));
    auto *barSet = new QBarSet(model->headerData(column, Qt::Horizontal).toString());
    for (int v : bins)
        barSet->append(v);
    auto *barSeries = new QBarSeries;
    barSeries->append(barSet);
    chart->addSeries(barSeries);

    auto *yAx = new QValueAxis;
    yAx->setTitleText(tr("Count"));
    chart->addAxis(yAx, Qt::AlignLeft);
    barSeries->attachAxis(yAx);

    auto *xAx = new QValueAxis;
    xAx->setTitleText(tr("Bin (min=%1, max=%2)").arg(minV, 0, 'f', 3).arg(maxV, 0, 'f', 3));
    chart->addAxis(xAx, Qt::AlignBottom);
    barSeries->attachAxis(xAx);
    xAx->setRange(0, kBins - 1);

    m_histView->setChart(chart);
}

void StatisticsDashboardDialog::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export statistics"), QStringLiteral("statistics.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    exportTo(path);
}

bool StatisticsDashboardDialog::exportTo(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream out(&f);

    // V6: each section is its header, a Units row, then the (filtered) rows
    // with a Status column naming the missing (blank, never 0) values.
    auto dumpModel = [&](const QString &sectionTitle, QSortFilterProxyModel *m) {
        if (!m)
            return;
        const auto *source = qobject_cast<const QStandardItemModel *>(m->sourceModel());
        const QStringList units = m_units.value(source);
        out << "# " << sectionTitle << '\n';
        for (int c = 0; c < m->columnCount(); ++c) {
            if (c)
                out << ',';
            out << m->headerData(c, Qt::Horizontal).toString();
        }
        out << ',' << tr("Status") << '\n';
        out << tr("Units");
        for (int c = 1; c < m->columnCount(); ++c)
            out << ',' << units.value(c);
        out << ",\n";
        for (int r = 0; r < m->rowCount(); ++r) {
            QStringList missing;
            for (int c = 0; c < m->columnCount(); ++c) {
                if (c)
                    out << ',';
                const QString text = m->index(r, c).data(Qt::DisplayRole).toString();
                if (c > 0 && text.isEmpty())
                    missing.append(m->headerData(c, Qt::Horizontal).toString());
                out << text;
            }
            out << ','
                << (missing.isEmpty()
                        ? QString()
                        : tr("missing: %1").arg(missing.join(QStringLiteral("; "))))
                << '\n';
        }
        out << '\n';
    };
    dumpModel(tr("Nodes"), m_nodeProxy);
    dumpModel(tr("Links"), m_linkProxy);
    dumpModel(tr("Subcatchments"), m_subProxy);
    if (m_mesh)
        dumpModel(tr("2D regions: %1, %2")
                      .arg(m_meshVariable->currentText(), m_meshAggregate->currentText()),
                  m_meshProxy);
    out.flush();
    return out.status() == QTextStream::Ok;
}

void StatisticsDashboardDialog::onQueryApplyClicked()
{
    if (applyQueryToAllTables())
        updateQueryStatus();
}

void StatisticsDashboardDialog::onQueryClearClicked()
{
    if (!m_queryEdit)
        return;
    m_queryEdit->clear();
    m_queryEdit->setStyleSheet(QString());
    if (applyQueryToAllTables())
        updateQueryStatus();
}

void StatisticsDashboardDialog::onCurrentTabChanged(int /*index*/)
{
    updateQueryStatus();
    refreshHistogramForSelection();
}

bool StatisticsDashboardDialog::applyQueryToAllTables()
{
    if (!m_queryEdit)
        return false;

    const QString text = m_queryEdit->text().trimmed();
    const auto pred = openswmmvis::parseQuery(text);
    if (!text.isEmpty() && !pred.isValid()) {
        m_queryEdit->setStyleSheet(
            openswmmvis::ui::theme::bannerStyle(openswmmvis::ui::theme::Banner::Error));
        if (m_queryStatus) {
            m_queryStatus->setText(tr("Error col %1: %2").arg(pred.errorPos).arg(pred.error));
        }
        return false;
    }

    m_queryEdit->setStyleSheet(QString());
    static_cast<StatsFilteringProxy *>(m_nodeProxy)->setQueryPredicate(pred);
    static_cast<StatsFilteringProxy *>(m_linkProxy)->setQueryPredicate(pred);
    static_cast<StatsFilteringProxy *>(m_subProxy)->setQueryPredicate(pred);
    if (m_meshProxy)
        static_cast<StatsFilteringProxy *>(m_meshProxy)->setQueryPredicate(pred);
    return true;
}

void StatisticsDashboardDialog::updateQueryStatus()
{
    if (!m_queryStatus)
        return;
    auto *proxy = currentProxy();
    auto *source = proxy ? proxy->sourceModel() : nullptr;
    const int total = source ? source->rowCount() : 0;
    const int matched = proxy ? proxy->rowCount() : 0;
    const QString text = m_queryEdit ? m_queryEdit->text().trimmed() : QString();
    if (text.isEmpty()) {
        m_queryStatus->setText(tr("%1 row%2").arg(total).arg(total == 1 ? "" : "s"));
    } else {
        m_queryStatus->setText(tr("%1 of %2 matched").arg(matched).arg(total));
    }
}

QTableView *StatisticsDashboardDialog::currentTable() const
{
    if (m_tabs && m_tabs->currentWidget() == m_meshPage)
        return m_meshTable;
    return m_tabs ? qobject_cast<QTableView *>(m_tabs->currentWidget()) : nullptr;
}

QSortFilterProxyModel *StatisticsDashboardDialog::currentProxy() const
{
    if (!m_tabs)
        return nullptr;
    switch (m_tabs->currentIndex()) {
    case 0:
        return m_nodeProxy;
    case 1:
        return m_linkProxy;
    case 2:
        return m_subProxy;
    case 3:
        return m_meshProxy;
    default:
        return nullptr;
    }
}

} // namespace openswmmvis::ui
