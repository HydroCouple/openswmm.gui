/*!
 * \file   tabularresultsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/tabularresultsdialog.h"

#include "layers/swmmresultslayer.h"
#include "ui/theme/iconfactory.h"

#include <openswmm/engine/openswmm_output.h>

#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>

#include <vector>

namespace openswmmvis::ui {

TabularResultsDialog::TabularResultsDialog(SWMMResultsLayer *layer, QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Tabular Results"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("TabularResultsDialog"));
    resize(1080, 640);
    buildUi();
    onModeChanged();
}

TabularResultsDialog::~TabularResultsDialog() = default;

void TabularResultsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Mode + selectors row.
    auto *modeGrp = new QGroupBox(tr("View"), this);
    auto *modeRow = new QHBoxLayout(modeGrp);
    m_byObjectRadio   = new QRadioButton(tr("By object"),   modeGrp);
    m_byVariableRadio = new QRadioButton(tr("By variable"), modeGrp);
    m_byObjectRadio->setChecked(true);

    m_kindCombo = new QComboBox(modeGrp);
    m_kindCombo->addItem(tr("Nodes"),         0);
    m_kindCombo->addItem(tr("Links"),         1);
    m_kindCombo->addItem(tr("Subcatchments"), 2);

    m_varOrObjCombo = new QComboBox(modeGrp);
    m_varOrObjCombo->setMinimumWidth(220);

    modeRow->addWidget(m_byObjectRadio);
    modeRow->addWidget(m_byVariableRadio);
    modeRow->addSpacing(20);
    modeRow->addWidget(new QLabel(tr("Kind:"), modeGrp));
    modeRow->addWidget(m_kindCombo);
    modeRow->addWidget(new QLabel(tr("Variable / Object:"), modeGrp));
    modeRow->addWidget(m_varOrObjCombo, 1);
    root->addWidget(modeGrp);

    // Table.
    m_model = new QStandardItemModel(this);
    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    // Buttons.
    auto *btnRow = new QHBoxLayout;
    auto *csvBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportCsv")),
                                   tr("Export CSV…"), this);
    auto *tsvBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportCsv")),
                                   tr("Export TSV…"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(csvBtn);
    btnRow->addWidget(tsvBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(m_byObjectRadio,   &QRadioButton::toggled, this, &TabularResultsDialog::onModeChanged);
    connect(m_byVariableRadio, &QRadioButton::toggled, this, &TabularResultsDialog::onModeChanged);
    connect(m_kindCombo,       QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TabularResultsDialog::onSelectionChanged);
    connect(m_varOrObjCombo,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TabularResultsDialog::onSelectionChanged);
    connect(csvBtn,   &QPushButton::clicked, this, &TabularResultsDialog::onExportCsvClicked);
    connect(tsvBtn,   &QPushButton::clicked, this, &TabularResultsDialog::onExportTsvClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void TabularResultsDialog::onModeChanged()
{
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const bool byObject = m_byObjectRadio->isChecked();

    m_varOrObjCombo->blockSignals(true);
    m_varOrObjCombo->clear();

    if (byObject) {
        // List variables for the current kind.
        const int kind = m_kindCombo->currentData().toInt();
        switch (kind) {
        case 0:  // node
            m_varOrObjCombo->addItem(tr("Depth"),         SWMM_OUT_NODE_DEPTH);
            m_varOrObjCombo->addItem(tr("Head"),          SWMM_OUT_NODE_HEAD);
            m_varOrObjCombo->addItem(tr("Volume"),        SWMM_OUT_NODE_VOLUME);
            m_varOrObjCombo->addItem(tr("Lateral inflow"),SWMM_OUT_NODE_LATERAL_INFLOW);
            m_varOrObjCombo->addItem(tr("Total inflow"),  SWMM_OUT_NODE_TOTAL_INFLOW);
            m_varOrObjCombo->addItem(tr("Overflow"),      SWMM_OUT_NODE_OVERFLOW);
            break;
        case 1:  // link
            m_varOrObjCombo->addItem(tr("Flow"),     SWMM_OUT_LINK_FLOW);
            m_varOrObjCombo->addItem(tr("Depth"),    SWMM_OUT_LINK_DEPTH);
            m_varOrObjCombo->addItem(tr("Velocity"), SWMM_OUT_LINK_VELOCITY);
            m_varOrObjCombo->addItem(tr("Volume"),   SWMM_OUT_LINK_VOLUME);
            m_varOrObjCombo->addItem(tr("Capacity"), SWMM_OUT_LINK_CAPACITY);
            break;
        default: // subcatch
            m_varOrObjCombo->addItem(tr("Rainfall"),    SWMM_OUT_SUBCATCH_RAINFALL);
            m_varOrObjCombo->addItem(tr("Snow depth"),  SWMM_OUT_SUBCATCH_SNOW_DEPTH);
            m_varOrObjCombo->addItem(tr("Evap"),        SWMM_OUT_SUBCATCH_EVAP);
            m_varOrObjCombo->addItem(tr("Infiltration"),SWMM_OUT_SUBCATCH_INFIL);
            m_varOrObjCombo->addItem(tr("Runoff"),      SWMM_OUT_SUBCATCH_RUNOFF);
            break;
        }
        rebuildByObject();
    } else {
        // List objects of the current kind.
        const int kind = m_kindCombo->currentData().toInt();
        int n = 0;
        auto idOf = [&](int idx) -> const char* {
            switch (kind) {
            case 0: return swmm_output_get_node_id(handle, idx);
            case 1: return swmm_output_get_link_id(handle, idx);
            default: return swmm_output_get_subcatch_id(handle, idx);
            }
        };
        switch (kind) {
        case 0: n = swmm_output_get_node_count(handle);     break;
        case 1: n = swmm_output_get_link_count(handle);     break;
        default:n = swmm_output_get_subcatch_count(handle); break;
        }
        for (int i = 0; i < n; ++i) {
            m_varOrObjCombo->addItem(QString::fromUtf8(idOf(i)), i);
        }
        rebuildByVariable();
    }
    m_varOrObjCombo->blockSignals(false);
}

void TabularResultsDialog::onSelectionChanged()
{
    if (m_byObjectRadio->isChecked()) rebuildByObject();
    else                                rebuildByVariable();
}

void TabularResultsDialog::rebuildByObject()
{
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int periods = swmm_output_get_period_count(handle);
    const int kind = m_kindCombo->currentData().toInt();
    const int var  = m_varOrObjCombo->currentData().toInt();

    auto countOf = [&]() -> int {
        switch (kind) {
        case 0:  return swmm_output_get_node_count(handle);
        case 1:  return swmm_output_get_link_count(handle);
        default: return swmm_output_get_subcatch_count(handle);
        }
    };
    auto idOf = [&](int idx) -> const char* {
        switch (kind) {
        case 0:  return swmm_output_get_node_id(handle, idx);
        case 1:  return swmm_output_get_link_id(handle, idx);
        default: return swmm_output_get_subcatch_id(handle, idx);
        }
    };
    auto fetchSeries = [&](int idx, std::vector<float>& v) -> bool {
        switch (kind) {
        case 0: return swmm_output_get_node_series(handle, idx, var, 0, periods - 1, v.data()) == 0;
        case 1: return swmm_output_get_link_series(handle, idx, var, 0, periods - 1, v.data()) == 0;
        default:return swmm_output_get_subcatch_series(handle, idx, var, 0, periods - 1, v.data()) == 0;
        }
    };

    const int n = countOf();
    m_model->clear();
    QStringList headers;
    headers << tr("Object");
    for (int p = 0; p < periods; ++p)
        headers << tr("t=%1").arg(p);
    m_model->setHorizontalHeaderLabels(headers);

    std::vector<float> ser(periods);
    for (int j = 0; j < n; ++j) {
        QList<QStandardItem*> row;
        row << new QStandardItem(QString::fromUtf8(idOf(j)));
        if (fetchSeries(j, ser)) {
            for (int p = 0; p < periods; ++p) {
                auto *it = new QStandardItem(QString::number(double(ser[p]), 'f', 3));
                it->setData(double(ser[p]), Qt::EditRole);
                row << it;
            }
        }
        m_model->appendRow(row);
    }
}

void TabularResultsDialog::rebuildByVariable()
{
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int periods = swmm_output_get_period_count(handle);
    const int kind = m_kindCombo->currentData().toInt();
    const int objIdx = m_varOrObjCombo->currentData().toInt();
    if (periods <= 0 || objIdx < 0) return;

    QVector<QPair<QString,int>> vars;
    switch (kind) {
    case 0:
        vars = {{tr("Depth"), SWMM_OUT_NODE_DEPTH}, {tr("Head"), SWMM_OUT_NODE_HEAD},
                {tr("Volume"), SWMM_OUT_NODE_VOLUME},
                {tr("Lat inflow"), SWMM_OUT_NODE_LATERAL_INFLOW},
                {tr("Tot inflow"), SWMM_OUT_NODE_TOTAL_INFLOW},
                {tr("Overflow"), SWMM_OUT_NODE_OVERFLOW}};
        break;
    case 1:
        vars = {{tr("Flow"), SWMM_OUT_LINK_FLOW}, {tr("Depth"), SWMM_OUT_LINK_DEPTH},
                {tr("Velocity"), SWMM_OUT_LINK_VELOCITY},
                {tr("Volume"), SWMM_OUT_LINK_VOLUME}, {tr("Capacity"), SWMM_OUT_LINK_CAPACITY}};
        break;
    default:
        vars = {{tr("Rainfall"), SWMM_OUT_SUBCATCH_RAINFALL},
                {tr("Snow"), SWMM_OUT_SUBCATCH_SNOW_DEPTH},
                {tr("Evap"), SWMM_OUT_SUBCATCH_EVAP},
                {tr("Infil"), SWMM_OUT_SUBCATCH_INFIL},
                {tr("Runoff"), SWMM_OUT_SUBCATCH_RUNOFF}};
        break;
    }

    m_model->clear();
    QStringList headers;
    headers << tr("Period");
    for (const auto &p : vars) headers << p.first;
    m_model->setHorizontalHeaderLabels(headers);

    QVector<std::vector<float>> cols(vars.size());
    for (int c = 0; c < vars.size(); ++c) {
        cols[c].resize(periods);
        const int v = vars[c].second;
        int rc = -1;
        if (kind == 0) rc = swmm_output_get_node_series(handle, objIdx, v, 0, periods - 1, cols[c].data());
        else if (kind == 1) rc = swmm_output_get_link_series(handle, objIdx, v, 0, periods - 1, cols[c].data());
        else  rc = swmm_output_get_subcatch_series(handle, objIdx, v, 0, periods - 1, cols[c].data());
        if (rc != 0) cols[c].assign(periods, 0.0f);
    }

    for (int p = 0; p < periods; ++p) {
        QList<QStandardItem*> row;
        row << new QStandardItem(QString::number(p));
        row.first()->setData(p, Qt::EditRole);
        for (int c = 0; c < vars.size(); ++c) {
            auto *it = new QStandardItem(QString::number(double(cols[c][p]), 'f', 3));
            it->setData(double(cols[c][p]), Qt::EditRole);
            row << it;
        }
        m_model->appendRow(row);
    }
}

void TabularResultsDialog::exportDelimited(const QString &path, QChar delim)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    const int rows = m_model->rowCount();
    const int cols = m_model->columnCount();
    for (int c = 0; c < cols; ++c) {
        if (c) out << delim;
        out << m_model->headerData(c, Qt::Horizontal).toString();
    }
    out << '\n';
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (c) out << delim;
            out << m_model->index(r, c).data(Qt::DisplayRole).toString();
        }
        out << '\n';
    }
}

void TabularResultsDialog::onExportCsvClicked()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"),
        QStringLiteral("results.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    exportDelimited(path, QLatin1Char(','));
}

void TabularResultsDialog::onExportTsvClicked()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Export TSV"),
        QStringLiteral("results.tsv"), tr("TSV (*.tsv)"));
    if (path.isEmpty()) return;
    exportDelimited(path, QLatin1Char('\t'));
}

} // namespace openswmmvis::ui
