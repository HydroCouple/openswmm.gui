/*!
 * \file   scatterplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/scatterplotdialog.h"

#include "core/preferencesmanager.h"
#include "layers/swmmresultslayer.h"
#include "ui/widgets/chartaxisformatcontroller.h"

#include <openswmm/engine/openswmm_output.h>

#include <QAction>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QMenu>
#include <QScatterSeries>
#include <QValueAxis>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace openswmmvis::ui {

namespace {

void varsForKind(int kind, QComboBox *combo)
{
    combo->clear();
    switch (kind) {
    case 0:  // node
        combo->addItem("Depth",          SWMM_OUT_NODE_DEPTH);
        combo->addItem("Head",           SWMM_OUT_NODE_HEAD);
        combo->addItem("Volume",         SWMM_OUT_NODE_VOLUME);
        combo->addItem("Lateral inflow", SWMM_OUT_NODE_LATERAL_INFLOW);
        combo->addItem("Total inflow",   SWMM_OUT_NODE_TOTAL_INFLOW);
        combo->addItem("Overflow",       SWMM_OUT_NODE_OVERFLOW);
        break;
    case 1:  // link
        combo->addItem("Flow",     SWMM_OUT_LINK_FLOW);
        combo->addItem("Depth",    SWMM_OUT_LINK_DEPTH);
        combo->addItem("Velocity", SWMM_OUT_LINK_VELOCITY);
        combo->addItem("Volume",   SWMM_OUT_LINK_VOLUME);
        combo->addItem("Capacity", SWMM_OUT_LINK_CAPACITY);
        break;
    default: // subcatch
        combo->addItem("Rainfall",     SWMM_OUT_SUBCATCH_RAINFALL);
        combo->addItem("Snow depth",   SWMM_OUT_SUBCATCH_SNOW_DEPTH);
        combo->addItem("Evap",         SWMM_OUT_SUBCATCH_EVAP);
        combo->addItem("Infiltration", SWMM_OUT_SUBCATCH_INFIL);
        combo->addItem("Runoff",       SWMM_OUT_SUBCATCH_RUNOFF);
        break;
    }
}

bool fetchSeries(SWMM_Output handle, int kind, int objIdx, int var,
                 int periods, std::vector<float> &out)
{
    out.assign(periods, 0.0f);
    if (objIdx < 0) return false;
    int rc = -1;
    switch (kind) {
    case 0: rc = swmm_output_get_node_series(handle, objIdx, var, 0, periods - 1, out.data()); break;
    case 1: rc = swmm_output_get_link_series(handle, objIdx, var, 0, periods - 1, out.data()); break;
    default:rc = swmm_output_get_subcatch_series(handle, objIdx, var, 0, periods - 1, out.data()); break;
    }
    return rc == 0;
}

} // namespace

ScatterPlotDialog::ScatterPlotDialog(SWMMResultsLayer *layer, QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Variable Correlation Scatter"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("ScatterPlotDialog"));
    resize(820, 640);
    buildUi();
    populateObjectCombos(m_xKind, m_xObj, m_xVar);
    populateObjectCombos(m_yKind, m_yObj, m_yVar);
    replot();
}

ScatterPlotDialog::~ScatterPlotDialog() = default;

void ScatterPlotDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *picksRow = new QHBoxLayout;

    // X picker
    auto *xGrp = new QGroupBox(tr("X variable"), this);
    auto *xForm = new QFormLayout(xGrp);
    m_xKind = new QComboBox(xGrp);
    m_xKind->addItem(tr("Nodes"), 0);
    m_xKind->addItem(tr("Links"), 1);
    m_xKind->addItem(tr("Subcatchments"), 2);
    m_xObj  = new QComboBox(xGrp);
    m_xVar  = new QComboBox(xGrp);
    xForm->addRow(tr("&Kind:"),     m_xKind);
    xForm->addRow(tr("O&bject:"),   m_xObj);
    xForm->addRow(tr("&Variable:"), m_xVar);
    picksRow->addWidget(xGrp);

    // Y picker
    auto *yGrp = new QGroupBox(tr("Y variable"), this);
    auto *yForm = new QFormLayout(yGrp);
    m_yKind = new QComboBox(yGrp);
    m_yKind->addItem(tr("Nodes"), 0);
    m_yKind->addItem(tr("Links"), 1);
    m_yKind->addItem(tr("Subcatchments"), 2);
    m_yObj = new QComboBox(yGrp);
    m_yVar = new QComboBox(yGrp);
    yForm->addRow(tr("K&ind:"),     m_yKind);
    yForm->addRow(tr("Ob&ject:"),   m_yObj);
    yForm->addRow(tr("V&ariable:"), m_yVar);
    picksRow->addWidget(yGrp);

    root->addLayout(picksRow);

    m_chart = new QChartView(this);
    m_chart->setRenderHint(QPainter::Antialiasing);
    m_chart->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_chart, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) {
                QMenu menu(this);
                QAction *props = menu.addAction(tr("Chart Properties…"));
                if (menu.exec(m_chart->mapToGlobal(p)) == props && m_axisFmt)
                    m_axisFmt->openDialog(this);
            });
    root->addWidget(m_chart, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    auto change = [this]() { onAnyChange(); };
    connect(m_xKind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, change);
    connect(m_yKind, QOverload<int>::of(&QComboBox::currentIndexChanged), this, change);
    connect(m_xObj,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, change);
    connect(m_yObj,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, change);
    connect(m_xVar,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, change);
    connect(m_yVar,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, change);
}

void ScatterPlotDialog::populateObjectCombos(QComboBox *kindCombo,
                                              QComboBox *objCombo,
                                              QComboBox *varCombo)
{
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int kind = kindCombo->currentData().toInt();
    objCombo->blockSignals(true);
    objCombo->clear();
    int n = 0;
    auto idOf = [&](int i) -> const char* {
        switch (kind) {
        case 0: return swmm_output_get_node_id(handle, i);
        case 1: return swmm_output_get_link_id(handle, i);
        default: return swmm_output_get_subcatch_id(handle, i);
        }
    };
    switch (kind) {
    case 0:  n = swmm_output_get_node_count(handle); break;
    case 1:  n = swmm_output_get_link_count(handle); break;
    default: n = swmm_output_get_subcatch_count(handle); break;
    }
    for (int i = 0; i < n; ++i)
        objCombo->addItem(QString::fromUtf8(idOf(i)), i);
    objCombo->blockSignals(false);
    varsForKind(kind, varCombo);
}

void ScatterPlotDialog::onAnyChange()
{
    // Repopulate dependent combos.
    populateObjectCombos(m_xKind, m_xObj, m_xVar);
    populateObjectCombos(m_yKind, m_yObj, m_yVar);
    replot();
}

void ScatterPlotDialog::replot()
{
    if (!m_layer) return;
    SWMM_Output handle = m_layer->outputHandle();
    if (!handle) return;
    const int periods = swmm_output_get_period_count(handle);
    if (periods <= 1) return;

    std::vector<float> xs, ys;
    if (!fetchSeries(handle,
                     m_xKind->currentData().toInt(),
                     m_xObj->currentData().toInt(),
                     m_xVar->currentData().toInt(),
                     periods, xs)) return;
    if (!fetchSeries(handle,
                     m_yKind->currentData().toInt(),
                     m_yObj->currentData().toInt(),
                     m_yVar->currentData().toInt(),
                     periods, ys)) return;

    // Linear regression y = a + b*x.
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    int n = std::min(xs.size(), ys.size());
    for (int i = 0; i < n; ++i) {
        const double x = xs[i], y = ys[i];
        sx  += x;       sy  += y;
        sxx += x * x;   syy += y * y;
        sxy += x * y;
    }
    const double meanX = sx / n;
    const double meanY = sy / n;
    const double denom = sxx - n * meanX * meanX;
    const double b = (std::fabs(denom) > 1e-12) ? (sxy - n * meanX * meanY) / denom : 0.0;
    const double a = meanY - b * meanX;
    const double ssTot = syy - n * meanY * meanY;
    double ssRes = 0;
    for (int i = 0; i < n; ++i) {
        const double resid = ys[i] - (a + b * xs[i]);
        ssRes += resid * resid;
    }
    const double r2 = (ssTot > 0) ? (1.0 - ssRes / ssTot) : 0.0;

    // Plot.
    auto *chart = new QChart;
    const QString xLabel = QStringLiteral("%1.%2").arg(m_xObj->currentText(), m_xVar->currentText());
    const QString yLabel = QStringLiteral("%1.%2").arg(m_yObj->currentText(), m_yVar->currentText());
    chart->setTitle(tr("%1 vs %2  (R²=%3, slope=%4)")
                       .arg(yLabel, xLabel)
                       .arg(r2, 0, 'f', 3)
                       .arg(b, 0, 'f', 3));
    chart->legend()->setVisible(false);

    auto *scatter = new QScatterSeries;
    scatter->setMarkerSize(4.5);

    double xMin = std::numeric_limits<double>::infinity();
    double xMax = -std::numeric_limits<double>::infinity();
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        scatter->append(xs[i], ys[i]);
        if (xs[i] < xMin) xMin = xs[i];
        if (xs[i] > xMax) xMax = xs[i];
        if (ys[i] < yMin) yMin = ys[i];
        if (ys[i] > yMax) yMax = ys[i];
    }
    chart->addSeries(scatter);

    auto *prefs = PreferencesManager::instance();
    auto *xAx = new QValueAxis;
    xAx->setTitleText(xLabel);
    xAx->setLabelFormat(prefs->plotXAxisFormat().printfSpec());
    chart->addAxis(xAx, Qt::AlignBottom);
    scatter->attachAxis(xAx);
    if (std::isfinite(xMin) && std::isfinite(xMax) && xMax > xMin) {
        const double pad = 0.05 * (xMax - xMin);
        xAx->setRange(xMin - pad, xMax + pad);
    }

    auto *yAx = new QValueAxis;
    yAx->setTitleText(yLabel);
    yAx->setLabelFormat(prefs->plotYAxisFormat().printfSpec());
    chart->addAxis(yAx, Qt::AlignLeft);
    scatter->attachAxis(yAx);
    if (std::isfinite(yMin) && std::isfinite(yMax) && yMax > yMin) {
        const double pad = 0.05 * (yMax - yMin);
        yAx->setRange(yMin - pad, yMax + pad);
    }

    // Regression line.
    if (std::isfinite(xMin) && std::isfinite(xMax) && xMax > xMin) {
        auto *line = new QLineSeries;
        QPen p(Qt::red); p.setStyle(Qt::DashLine);
        line->setPen(p);
        line->append(xMin, a + b * xMin);
        line->append(xMax, a + b * xMax);
        chart->addSeries(line);
        line->attachAxis(xAx);
        line->attachAxis(yAx);
    }

    m_chart->setChart(chart);

    // Bind (or rebind, since replot creates a fresh chart) the persistent
    // axis-format controller so per-chart precision survives replots.
    if (!m_axisFmt) m_axisFmt = new ChartAxisFormatController(chart, this);
    else            m_axisFmt->setChart(chart);
}

} // namespace openswmmvis::ui
