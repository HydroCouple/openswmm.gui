/*!
 * \file   statisticsdashboarddialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/statisticsdashboarddialog.h"

#include "core/queryparser.h"
#include "layers/swmmresultslayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTextStream>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <vector>

namespace openswmmvis::ui {

namespace {

class StatsFilteringProxy final : public QSortFilterProxyModel
{
public:
    explicit StatsFilteringProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
    }

    void setQueryPredicate(const openswmmvis::QueryPredicate &predicate)
    {
        beginFilterChange();
        m_predicate = predicate;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

protected:
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
            const QString key =
                src->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
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

} // namespace

StatisticsDashboardDialog::StatisticsDashboardDialog(SWMMResultsLayer *layer,
                                                      QWidget *parent)
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
    m_queryEdit->setToolTip(tr(
        "Filter rows by the same SQL-like WHERE clause used by the Attribute Table.\n"
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

    // Node table -------------------------------------------------------------
    m_nodeModel = new QStandardItemModel(this);
    m_nodeModel->setHorizontalHeaderLabels(
        {tr("Node"), tr("Max depth"), tr("Max head"), tr("Max overflow"), tr("Volume")});
    m_nodeProxy = new StatsFilteringProxy(this);
    m_nodeProxy->setSourceModel(m_nodeModel);
    m_nodeProxy->setSortRole(Qt::EditRole);
    m_nodeTable = new QTableView(this);
    m_nodeTable->setModel(m_nodeProxy);
    m_nodeTable->setSortingEnabled(true);
    m_nodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_nodeTable->setAlternatingRowColors(true);
    m_nodeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tabs->addTab(m_nodeTable, tr("Nodes"));

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
    m_linkTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_linkTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_linkTable->setAlternatingRowColors(true);
    m_linkTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tabs->addTab(m_linkTable, tr("Links"));

    // Subcatchment table -----------------------------------------------------
    m_subModel = new QStandardItemModel(this);
    m_subModel->setHorizontalHeaderLabels(
        {tr("Subcatchment"), tr("Peak runoff"), tr("Total runoff"),
         tr("Total infil"), tr("Total evap")});
    m_subProxy = new StatsFilteringProxy(this);
    m_subProxy->setSourceModel(m_subModel);
    m_subProxy->setSortRole(Qt::EditRole);
    m_subTable = new QTableView(this);
    m_subTable->setModel(m_subProxy);
    m_subTable->setSortingEnabled(true);
    m_subTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_subTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_subTable->setAlternatingRowColors(true);
    m_subTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tabs->addTab(m_subTable, tr("Subcatchments"));

    // Histogram view is kept as an off-layout helper so selecting a numeric
    // column can still update the chart object without taking vertical space
    // from the tabular result review.
    m_histView = new QChartView(this);
    m_histView->setRenderHint(QPainter::Antialiasing);
    m_histView->hide();
    root->addWidget(m_tabs, 1);

    // Buttons ---------------------------------------------------------------
    auto *btnRow = new QHBoxLayout;
    auto *exportBtn = new QPushButton(tr("Export CSV…"), this);
    btnRow->addWidget(exportBtn);
    btnRow->addStretch(1);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
    connect(exportBtn, &QPushButton::clicked, this, &StatisticsDashboardDialog::onExportClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_queryEdit, &QLineEdit::returnPressed,
            this, &StatisticsDashboardDialog::onQueryApplyClicked);
    connect(m_queryApply, &QPushButton::clicked,
            this, &StatisticsDashboardDialog::onQueryApplyClicked);
    connect(m_queryClear, &QPushButton::clicked,
            this, &StatisticsDashboardDialog::onQueryClearClicked);
    connect(m_tabs, &QTabWidget::currentChanged,
            this, &StatisticsDashboardDialog::onCurrentTabChanged);

    // Selection → histogram
    connect(m_nodeTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &StatisticsDashboardDialog::onTableSelectionChanged);
    connect(m_linkTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &StatisticsDashboardDialog::onTableSelectionChanged);
    connect(m_subTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &StatisticsDashboardDialog::onTableSelectionChanged);
}

void StatisticsDashboardDialog::populateNodeStats()
{
    m_nodeModel->removeRows(0, m_nodeModel->rowCount());
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int n = swmm_output_get_node_count(handle);
    const int periods = swmm_output_get_period_count(handle);
    if (n <= 0 || periods <= 0) return;

    std::vector<float> series(periods);
    for (int j = 0; j < n; ++j) {
        const QString name = QString::fromUtf8(swmm_output_get_node_id(handle, j));
        double maxDepth = -1e30, maxHead = -1e30, maxOver = -1e30, vol = 0.0;
        if (swmm_output_get_node_series(handle, j, SWMM_OUT_NODE_DEPTH,
                                         0, periods - 1, series.data()) == 0) {
            maxDepth = *std::max_element(series.begin(), series.end());
        }
        if (swmm_output_get_node_series(handle, j, SWMM_OUT_NODE_HEAD,
                                         0, periods - 1, series.data()) == 0) {
            maxHead = *std::max_element(series.begin(), series.end());
        }
        if (swmm_output_get_node_series(handle, j, SWMM_OUT_NODE_OVERFLOW,
                                         0, periods - 1, series.data()) == 0) {
            maxOver = *std::max_element(series.begin(), series.end());
        }
        if (swmm_output_get_node_series(handle, j, SWMM_OUT_NODE_VOLUME,
                                         0, periods - 1, series.data()) == 0) {
            vol = std::accumulate(series.begin(), series.end(), 0.0);
        }
        QList<QStandardItem*> row;
        row << new QStandardItem(name)
            << new QStandardItem(QString::number(maxDepth, 'f', 3))
            << new QStandardItem(QString::number(maxHead, 'f', 3))
            << new QStandardItem(QString::number(maxOver, 'f', 3))
            << new QStandardItem(QString::number(vol,     'f', 1));
        for (int c = 1; c < row.size(); ++c)
            row[c]->setData(row[c]->text().toDouble(), Qt::EditRole);   // sortable numeric
        m_nodeModel->appendRow(row);
    }
}

void StatisticsDashboardDialog::populateLinkStats()
{
    m_linkModel->removeRows(0, m_linkModel->rowCount());
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int n = swmm_output_get_link_count(handle);
    const int periods = swmm_output_get_period_count(handle);
    if (n <= 0 || periods <= 0) return;

    std::vector<float> series(periods);
    for (int j = 0; j < n; ++j) {
        const QString name = QString::fromUtf8(swmm_output_get_link_id(handle, j));
        double maxFlow = -1e30, maxDepth = -1e30, maxVel = -1e30, maxCap = -1e30;
        if (swmm_output_get_link_series(handle, j, SWMM_OUT_LINK_FLOW, 0, periods - 1, series.data()) == 0)
            maxFlow = *std::max_element(series.begin(), series.end());
        if (swmm_output_get_link_series(handle, j, SWMM_OUT_LINK_DEPTH, 0, periods - 1, series.data()) == 0)
            maxDepth = *std::max_element(series.begin(), series.end());
        if (swmm_output_get_link_series(handle, j, SWMM_OUT_LINK_VELOCITY, 0, periods - 1, series.data()) == 0)
            maxVel = *std::max_element(series.begin(), series.end());
        if (swmm_output_get_link_series(handle, j, SWMM_OUT_LINK_CAPACITY, 0, periods - 1, series.data()) == 0)
            maxCap = *std::max_element(series.begin(), series.end());

        QList<QStandardItem*> row;
        row << new QStandardItem(name)
            << new QStandardItem(QString::number(maxFlow,  'f', 3))
            << new QStandardItem(QString::number(maxDepth, 'f', 3))
            << new QStandardItem(QString::number(maxVel,   'f', 3))
            << new QStandardItem(QString::number(maxCap,   'f', 3));
        for (int c = 1; c < row.size(); ++c)
            row[c]->setData(row[c]->text().toDouble(), Qt::EditRole);
        m_linkModel->appendRow(row);
    }
}

void StatisticsDashboardDialog::populateSubcatchStats()
{
    m_subModel->removeRows(0, m_subModel->rowCount());
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int n = swmm_output_get_subcatch_count(handle);
    const int periods = swmm_output_get_period_count(handle);
    if (n <= 0 || periods <= 0) return;

    std::vector<float> series(periods);
    for (int j = 0; j < n; ++j) {
        const QString name = QString::fromUtf8(swmm_output_get_subcatch_id(handle, j));
        double peakRunoff = -1e30, totRunoff = 0.0, totInfil = 0.0, totEvap = 0.0;
        if (swmm_output_get_subcatch_series(handle, j, SWMM_OUT_SUBCATCH_RUNOFF, 0, periods - 1, series.data()) == 0) {
            peakRunoff = *std::max_element(series.begin(), series.end());
            totRunoff  = std::accumulate(series.begin(), series.end(), 0.0);
        }
        if (swmm_output_get_subcatch_series(handle, j, SWMM_OUT_SUBCATCH_INFIL, 0, periods - 1, series.data()) == 0)
            totInfil = std::accumulate(series.begin(), series.end(), 0.0);
        if (swmm_output_get_subcatch_series(handle, j, SWMM_OUT_SUBCATCH_EVAP, 0, periods - 1, series.data()) == 0)
            totEvap = std::accumulate(series.begin(), series.end(), 0.0);

        QList<QStandardItem*> row;
        row << new QStandardItem(name)
            << new QStandardItem(QString::number(peakRunoff, 'f', 3))
            << new QStandardItem(QString::number(totRunoff,  'f', 3))
            << new QStandardItem(QString::number(totInfil,   'f', 3))
            << new QStandardItem(QString::number(totEvap,    'f', 3));
        for (int c = 1; c < row.size(); ++c)
            row[c]->setData(row[c]->text().toDouble(), Qt::EditRole);
        m_subModel->appendRow(row);
    }
}

void StatisticsDashboardDialog::onTableSelectionChanged()
{
    auto *table = currentTable();
    if (!table || !table->selectionModel()) return;
    const QModelIndexList sel = table->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;
    const int col = sel.first().column();
    if (col == 0) return;   // ID column — no histogram
    rebuildHistogramFor(col);
}

void StatisticsDashboardDialog::rebuildHistogramFor(int column)
{
    auto *table = currentTable();
    if (!table) return;
    auto *model = table->model();
    if (!model) return;

    // Collect visible values, build 20-bin histogram.
    std::vector<double> values;
    values.reserve(model->rowCount());
    double minV = std::numeric_limits<double>::infinity();
    double maxV = -std::numeric_limits<double>::infinity();
    for (int r = 0; r < model->rowCount(); ++r) {
        const double v = model->index(r, column).data(Qt::EditRole).toDouble();
        if (!std::isfinite(v)) continue;
        values.push_back(v);
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }
    if (values.size() < 2 || maxV <= minV) {
        m_histView->setChart(new QChart);
        return;
    }

    const int kBins = 20;
    std::vector<int> bins(kBins, 0);
    for (double v : values) {
        int b = static_cast<int>(((v - minV) / (maxV - minV)) * (kBins - 1));
        if (b < 0) b = 0;
        if (b >= kBins) b = kBins - 1;
        ++bins[b];
    }

    auto *chart = new QChart;
    chart->setTitle(tr("Frequency distribution — %1 (%2 samples)")
                       .arg(model->headerData(column, Qt::Horizontal).toString())
                       .arg(static_cast<int>(values.size())));
    auto *barSet = new QBarSet(model->headerData(column, Qt::Horizontal).toString());
    for (int v : bins) barSet->append(v);
    auto *barSeries = new QBarSeries;
    barSeries->append(barSet);
    chart->addSeries(barSeries);

    auto *yAx = new QValueAxis;
    yAx->setTitleText(tr("Count"));
    chart->addAxis(yAx, Qt::AlignLeft);
    barSeries->attachAxis(yAx);

    auto *xAx = new QValueAxis;
    xAx->setTitleText(tr("Bin (min=%1, max=%2)")
                         .arg(minV, 0, 'f', 3).arg(maxV, 0, 'f', 3));
    chart->addAxis(xAx, Qt::AlignBottom);
    barSeries->attachAxis(xAx);
    xAx->setRange(0, kBins - 1);

    m_histView->setChart(chart);
}

void StatisticsDashboardDialog::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Export statistics"), QStringLiteral("statistics.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);

    auto dumpModel = [&](const QString &sectionTitle, QAbstractItemModel *m) {
        if (!m) return;
        out << "# " << sectionTitle << '\n';
        for (int c = 0; c < m->columnCount(); ++c) {
            if (c) out << ',';
            out << m->headerData(c, Qt::Horizontal).toString();
        }
        out << '\n';
        for (int r = 0; r < m->rowCount(); ++r) {
            for (int c = 0; c < m->columnCount(); ++c) {
                if (c) out << ',';
                out << m->index(r, c).data(Qt::DisplayRole).toString();
            }
            out << '\n';
        }
        out << '\n';
    };
    dumpModel(tr("Nodes"),         m_nodeProxy);
    dumpModel(tr("Links"),         m_linkProxy);
    dumpModel(tr("Subcatchments"), m_subProxy);
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
    onTableSelectionChanged();
}

bool StatisticsDashboardDialog::applyQueryToAllTables()
{
    if (!m_queryEdit)
        return false;

    const QString text = m_queryEdit->text().trimmed();
    const auto pred = openswmmvis::parseQuery(text);
    if (!text.isEmpty() && !pred.isValid()) {
        m_queryEdit->setStyleSheet(QStringLiteral("background-color: #FFD6D6;"));
        if (m_queryStatus) {
            m_queryStatus->setText(
                tr("Error col %1: %2").arg(pred.errorPos).arg(pred.error));
        }
        return false;
    }

    m_queryEdit->setStyleSheet(QString());
    static_cast<StatsFilteringProxy *>(m_nodeProxy)->setQueryPredicate(pred);
    static_cast<StatsFilteringProxy *>(m_linkProxy)->setQueryPredicate(pred);
    static_cast<StatsFilteringProxy *>(m_subProxy)->setQueryPredicate(pred);
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
        m_queryStatus->setText(tr("%1 row%2")
                                   .arg(total).arg(total == 1 ? "" : "s"));
    } else {
        m_queryStatus->setText(tr("%1 of %2 matched").arg(matched).arg(total));
    }
}

QTableView *StatisticsDashboardDialog::currentTable() const
{
    return m_tabs ? qobject_cast<QTableView *>(m_tabs->currentWidget()) : nullptr;
}

QSortFilterProxyModel *StatisticsDashboardDialog::currentProxy() const
{
    if (!m_tabs)
        return nullptr;
    switch (m_tabs->currentIndex()) {
    case 0: return m_nodeProxy;
    case 1: return m_linkProxy;
    case 2: return m_subProxy;
    default: return nullptr;
    }
}

} // namespace openswmmvis::ui
