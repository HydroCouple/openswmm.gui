/*!
 * \file   statisticsdashboarddialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/statisticsdashboarddialog.h"

#include "layers/swmmresultslayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
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

StatisticsDashboardDialog::StatisticsDashboardDialog(SWMMResultsLayer *layer,
                                                      QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Statistics Dashboard"));
    resize(960, 640);
    buildUi();
    populateNodeStats();
    populateLinkStats();
    populateSubcatchStats();
}

StatisticsDashboardDialog::~StatisticsDashboardDialog() = default;

void StatisticsDashboardDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);

    // Node table -------------------------------------------------------------
    m_nodeModel = new QStandardItemModel(this);
    m_nodeModel->setHorizontalHeaderLabels(
        {tr("Node"), tr("Max depth"), tr("Max head"), tr("Max overflow"), tr("Volume")});
    m_nodeTable = new QTableView(this);
    m_nodeTable->setModel(m_nodeModel);
    m_nodeTable->setSortingEnabled(true);
    m_nodeTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tabs->addTab(m_nodeTable, tr("Nodes"));

    // Link table -------------------------------------------------------------
    m_linkModel = new QStandardItemModel(this);
    m_linkModel->setHorizontalHeaderLabels(
        {tr("Link"), tr("Max flow"), tr("Max depth"), tr("Max velocity"), tr("Max capacity")});
    m_linkTable = new QTableView(this);
    m_linkTable->setModel(m_linkModel);
    m_linkTable->setSortingEnabled(true);
    m_linkTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tabs->addTab(m_linkTable, tr("Links"));

    // Subcatchment table -----------------------------------------------------
    m_subModel = new QStandardItemModel(this);
    m_subModel->setHorizontalHeaderLabels(
        {tr("Subcatchment"), tr("Peak runoff"), tr("Total runoff"),
         tr("Total infil"), tr("Total evap")});
    m_subTable = new QTableView(this);
    m_subTable->setModel(m_subModel);
    m_subTable->setSortingEnabled(true);
    m_subTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tabs->addTab(m_subTable, tr("Subcatchments"));

    root->addWidget(m_tabs, 2);

    // Histogram view ---------------------------------------------------------
    m_histView = new QChartView(this);
    m_histView->setRenderHint(QPainter::Antialiasing);
    m_histView->setMinimumHeight(220);
    root->addWidget(m_histView, 1);

    // Buttons ---------------------------------------------------------------
    auto *btnRow = new QHBoxLayout;
    auto *exportBtn = new QPushButton(tr("Export CSV…"), this);
    btnRow->addWidget(exportBtn);
    btnRow->addStretch(1);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
    connect(exportBtn, &QPushButton::clicked, this, &StatisticsDashboardDialog::onExportClicked);
    connect(closeBtn,  &QPushButton::clicked, this, &QDialog::accept);

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
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int n = swmm_output_get_node_count(handle);
    const int periods = swmm_output_get_period_count(handle);
    if (n <= 0 || periods <= 0) return;

    m_nodeModel->removeRows(0, m_nodeModel->rowCount());

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
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int n = swmm_output_get_link_count(handle);
    const int periods = swmm_output_get_period_count(handle);
    if (n <= 0 || periods <= 0) return;

    m_linkModel->removeRows(0, m_linkModel->rowCount());
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
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int n = swmm_output_get_subcatch_count(handle);
    const int periods = swmm_output_get_period_count(handle);
    if (n <= 0 || periods <= 0) return;

    m_subModel->removeRows(0, m_subModel->rowCount());
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
    auto *table = qobject_cast<QTableView*>(m_tabs->currentWidget());
    if (!table || !table->selectionModel()) return;
    const QModelIndexList sel = table->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;
    const int col = sel.first().column();
    if (col == 0) return;   // ID column — no histogram
    rebuildHistogramFor(col);
}

void StatisticsDashboardDialog::rebuildHistogramFor(int column)
{
    auto *table = qobject_cast<QTableView*>(m_tabs->currentWidget());
    if (!table) return;
    auto *model = qobject_cast<QStandardItemModel*>(table->model());
    if (!model) return;

    // Collect values, build 20-bin histogram.
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

    auto dumpModel = [&](const QString &sectionTitle, QStandardItemModel *m) {
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
    dumpModel(tr("Nodes"),         m_nodeModel);
    dumpModel(tr("Links"),         m_linkModel);
    dumpModel(tr("Subcatchments"), m_subModel);
}

} // namespace openswmmvis::ui
