/*!
 * \file   customreportdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/customreportdialog.h"

#include "layers/swmmresultslayer.h"
#include "ui/theme/iconfactory.h"

#include <openswmm/engine/openswmm_output.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>
#include <vector>

namespace openswmmvis::ui {

namespace {

enum class Aggregate { Max, Min, Mean, Sum, PeakTime, TimeAboveThreshold };

double aggregateOver(Aggregate agg, const std::vector<float>& s,
                     int reportStepSec, double threshold)
{
    if (s.empty()) return 0.0;
    switch (agg) {
    case Aggregate::Max:  return *std::max_element(s.begin(), s.end());
    case Aggregate::Min:  return *std::min_element(s.begin(), s.end());
    case Aggregate::Mean: return std::accumulate(s.begin(), s.end(), 0.0) / s.size();
    case Aggregate::Sum:  return std::accumulate(s.begin(), s.end(), 0.0);
    case Aggregate::PeakTime: {
        auto it = std::max_element(s.begin(), s.end());
        return (it - s.begin()) * double(reportStepSec) / 3600.0;
    }
    case Aggregate::TimeAboveThreshold: {
        int count = 0;
        for (float v : s) if (v > threshold) ++count;
        return count * double(reportStepSec) / 3600.0;
    }
    }
    return 0.0;
}

const char* aggregateName(Aggregate a)
{
    switch (a) {
    case Aggregate::Max:                  return "max";
    case Aggregate::Min:                  return "min";
    case Aggregate::Mean:                 return "mean";
    case Aggregate::Sum:                  return "sum";
    case Aggregate::PeakTime:             return "peak-time (h)";
    case Aggregate::TimeAboveThreshold:   return "time-above-thr (h)";
    }
    return "?";
}

} // namespace

CustomReportDialog::CustomReportDialog(SWMMResultsLayer *layer, QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Custom Report Builder"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("CustomReportDialog"));
    resize(1000, 720);
    buildUi();
}

CustomReportDialog::~CustomReportDialog() = default;

void CustomReportDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *split = new QSplitter(Qt::Vertical, this);
    split->setObjectName(QStringLiteral("main"));

    // ----- Clause table -----------------------------------------------------
    auto *top = new QWidget(split);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);

    m_clauseTable = new QTableWidget(top);
    m_clauseTable->setColumnCount(6);
    m_clauseTable->setHorizontalHeaderLabels({
        tr("Label"), tr("Kind"), tr("Object filter"),
        tr("Variable"), tr("Aggregate"), tr("Threshold")
    });
    m_clauseTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    topLayout->addWidget(m_clauseTable);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Add clause"), top);
    auto *rmBtn  = new QPushButton(tr("Remove"),     top);
    auto *runBtn = new QPushButton(tr("Run"),        top);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(rmBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(runBtn);
    topLayout->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, this, &CustomReportDialog::onAddClauseClicked);
    connect(rmBtn,  &QPushButton::clicked, this, &CustomReportDialog::onRemoveClauseClicked);
    connect(runBtn, &QPushButton::clicked, this, &CustomReportDialog::onRunClicked);

    split->addWidget(top);

    // ----- Results table ----------------------------------------------------
    auto *bot = new QWidget(split);
    auto *botLayout = new QVBoxLayout(bot);
    botLayout->setContentsMargins(0, 0, 0, 0);

    m_resultModel = new QStandardItemModel(this);
    m_resultTable = new QTableView(bot);
    m_resultTable->setModel(m_resultModel);
    m_resultTable->setSortingEnabled(true);
    m_resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    botLayout->addWidget(m_resultTable);

    auto *botBtns = new QHBoxLayout;
    auto *exportBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportCsv")),
                                      tr("Export CSV…"), bot);
    auto *closeBtn  = new QPushButton(tr("Close"),       bot);
    botBtns->addWidget(exportBtn);
    botBtns->addStretch(1);
    botBtns->addWidget(closeBtn);
    botLayout->addLayout(botBtns);
    connect(exportBtn, &QPushButton::clicked, this, &CustomReportDialog::onExportClicked);
    connect(closeBtn,  &QPushButton::clicked, this, &QDialog::accept);

    split->addWidget(bot);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    onAddClauseClicked();   // seed with one empty clause
}

void CustomReportDialog::onAddClauseClicked()
{
    const int r = m_clauseTable->rowCount();
    m_clauseTable->insertRow(r);

    auto setItem = [&](int c, const QString &v) {
        m_clauseTable->setItem(r, c, new QTableWidgetItem(v));
    };
    setItem(0, tr("clause%1").arg(r + 1));

    auto *kind = new QComboBox(m_clauseTable);
    kind->addItem(tr("Nodes"), 0);
    kind->addItem(tr("Links"), 1);
    kind->addItem(tr("Subcatchments"), 2);
    m_clauseTable->setCellWidget(r, 1, kind);

    setItem(2, QStringLiteral("*"));

    auto *var = new QComboBox(m_clauseTable);
    var->addItem(tr("Depth"),    0);
    var->addItem(tr("Head"),     1);
    var->addItem(tr("Flow"),     2);
    var->addItem(tr("Velocity"), 3);
    var->addItem(tr("Runoff"),   4);
    m_clauseTable->setCellWidget(r, 3, var);

    auto *agg = new QComboBox(m_clauseTable);
    agg->addItem(tr("max"),       int(Aggregate::Max));
    agg->addItem(tr("min"),       int(Aggregate::Min));
    agg->addItem(tr("mean"),      int(Aggregate::Mean));
    agg->addItem(tr("sum"),       int(Aggregate::Sum));
    agg->addItem(tr("peak-time"), int(Aggregate::PeakTime));
    agg->addItem(tr("time-above-thr"), int(Aggregate::TimeAboveThreshold));
    m_clauseTable->setCellWidget(r, 4, agg);

    setItem(5, QStringLiteral("0.0"));
}

void CustomReportDialog::onRemoveClauseClicked()
{
    const int r = m_clauseTable->currentRow();
    if (r >= 0) m_clauseTable->removeRow(r);
}

void CustomReportDialog::onRunClicked()
{
    evaluate();
}

void CustomReportDialog::evaluate()
{
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int periods = swmm_output_get_period_count(handle);
    const int reportStep = swmm_output_get_report_step(handle);

    m_resultModel->clear();
    m_resultModel->setHorizontalHeaderLabels(
        {tr("Clause"), tr("Object"), tr("Aggregate"), tr("Value")});

    // Map variable picker indices to engine var codes per kind.
    auto resolveVarCode = [](int kind, int comboIdx) -> int {
        switch (kind) {
        case 0:  // node
            switch (comboIdx) {
            case 0: return SWMM_OUT_NODE_DEPTH;
            case 1: return SWMM_OUT_NODE_HEAD;
            case 2: return SWMM_OUT_NODE_TOTAL_INFLOW;
            case 3: return SWMM_OUT_NODE_OVERFLOW;
            case 4: return SWMM_OUT_NODE_LATERAL_INFLOW;
            } return SWMM_OUT_NODE_DEPTH;
        case 1:  // link
            switch (comboIdx) {
            case 0: return SWMM_OUT_LINK_DEPTH;
            case 1: return SWMM_OUT_LINK_DEPTH;
            case 2: return SWMM_OUT_LINK_FLOW;
            case 3: return SWMM_OUT_LINK_VELOCITY;
            case 4: return SWMM_OUT_LINK_CAPACITY;
            } return SWMM_OUT_LINK_FLOW;
        default: // subcatch
            switch (comboIdx) {
            case 4: return SWMM_OUT_SUBCATCH_RUNOFF;
            default: return SWMM_OUT_SUBCATCH_RUNOFF;
            }
        }
    };

    std::vector<float> series(periods);
    const int rows = m_clauseTable->rowCount();
    for (int r = 0; r < rows; ++r) {
        const QString label = m_clauseTable->item(r, 0)
                                ? m_clauseTable->item(r, 0)->text()
                                : tr("clause%1").arg(r + 1);
        auto *kindCombo = qobject_cast<QComboBox*>(m_clauseTable->cellWidget(r, 1));
        const QString filter = m_clauseTable->item(r, 2)
                                 ? m_clauseTable->item(r, 2)->text()
                                 : QString();
        auto *varCombo  = qobject_cast<QComboBox*>(m_clauseTable->cellWidget(r, 3));
        auto *aggCombo  = qobject_cast<QComboBox*>(m_clauseTable->cellWidget(r, 4));
        const double thr = m_clauseTable->item(r, 5)
                             ? m_clauseTable->item(r, 5)->text().toDouble()
                             : 0.0;
        if (!kindCombo || !varCombo || !aggCombo) continue;

        const int kind = kindCombo->currentData().toInt();
        const int varCode = resolveVarCode(kind, varCombo->currentIndex());
        const Aggregate agg = static_cast<Aggregate>(aggCombo->currentData().toInt());

        int n = 0;
        auto idOf = [&](int idx) -> const char* {
            switch (kind) {
            case 0: return swmm_output_get_node_id(handle, idx);
            case 1: return swmm_output_get_link_id(handle, idx);
            default: return swmm_output_get_subcatch_id(handle, idx);
            }
        };
        switch (kind) {
        case 0:  n = swmm_output_get_node_count(handle); break;
        case 1:  n = swmm_output_get_link_count(handle); break;
        default: n = swmm_output_get_subcatch_count(handle); break;
        }
        // Filter: simple "*" = all, otherwise comma-separated exact names.
        const QStringList filterParts =
            filter.trimmed() == QStringLiteral("*")
              ? QStringList{}
              : filter.split(QChar(','), Qt::SkipEmptyParts);

        for (int j = 0; j < n; ++j) {
            const QString objName = QString::fromUtf8(idOf(j));
            if (!filterParts.isEmpty() && !filterParts.contains(objName, Qt::CaseInsensitive))
                continue;

            int rc = -1;
            switch (kind) {
            case 0: rc = swmm_output_get_node_series(handle, j, varCode, 0, periods - 1, series.data()); break;
            case 1: rc = swmm_output_get_link_series(handle, j, varCode, 0, periods - 1, series.data()); break;
            default:rc = swmm_output_get_subcatch_series(handle, j, varCode, 0, periods - 1, series.data()); break;
            }
            if (rc != 0) continue;

            const double val = aggregateOver(agg, series, reportStep, thr);

            QList<QStandardItem*> row;
            row << new QStandardItem(label)
                << new QStandardItem(objName)
                << new QStandardItem(QString::fromUtf8(aggregateName(agg)))
                << new QStandardItem(QString::number(val, 'f', 4));
            row.last()->setData(val, Qt::EditRole);
            m_resultModel->appendRow(row);
        }
    }
}

void CustomReportDialog::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"),
        QStringLiteral("custom_report.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    const int cols = m_resultModel->columnCount();
    for (int c = 0; c < cols; ++c) {
        if (c) out << ',';
        out << m_resultModel->headerData(c, Qt::Horizontal).toString();
    }
    out << '\n';
    for (int r = 0; r < m_resultModel->rowCount(); ++r) {
        for (int c = 0; c < cols; ++c) {
            if (c) out << ',';
            out << m_resultModel->index(r, c).data().toString();
        }
        out << '\n';
    }
}

} // namespace openswmmvis::ui
