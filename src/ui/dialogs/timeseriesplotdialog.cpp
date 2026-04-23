/*!
 * \file   timeseriesplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "ui/dialogs/timeseriesplotdialog.h"

#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QValueAxis>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_output.h>

// Qt 6 puts charts classes in the global namespace — no QtCharts:: prefix.

namespace {

struct VarOption {
    QString label;
    int     code;     // SWMM_OUT_*_* enum value
};

QList<VarOption> variablesFor(SWMMObjectRef::ObjectType t)
{
    using L = SWMMObjectRef;
    switch (t)
    {
    case L::Node:
        return {
            {QStringLiteral("Depth"),         0 /*SWMM_OUT_NODE_DEPTH*/},
            {QStringLiteral("Head"),          1 /*SWMM_OUT_NODE_HEAD*/},
            {QStringLiteral("Volume"),        2 /*SWMM_OUT_NODE_VOLUME*/},
            {QStringLiteral("Lateral inflow"),3 /*SWMM_OUT_NODE_LATERAL_INFLOW*/},
            {QStringLiteral("Total inflow"),  4 /*SWMM_OUT_NODE_TOTAL_INFLOW*/},
            {QStringLiteral("Overflow"),      5 /*SWMM_OUT_NODE_OVERFLOW*/},
        };
    case L::Link:
        return {
            {QStringLiteral("Flow"),     0 /*SWMM_OUT_LINK_FLOW*/},
            {QStringLiteral("Depth"),    1 /*SWMM_OUT_LINK_DEPTH*/},
            {QStringLiteral("Velocity"), 2 /*SWMM_OUT_LINK_VELOCITY*/},
            {QStringLiteral("Volume"),   3 /*SWMM_OUT_LINK_VOLUME*/},
            {QStringLiteral("Capacity"), 4 /*SWMM_OUT_LINK_CAPACITY (variable index 4)*/},
        };
    case L::Subcatchment:
        return {
            {QStringLiteral("Rainfall"),     0 /*SWMM_OUT_SUBCATCH_RAINFALL*/},
            {QStringLiteral("Snow depth"),   1 /*SWMM_OUT_SUBCATCH_SNOW_DEPTH*/},
            {QStringLiteral("Evaporation"),  2 /*SWMM_OUT_SUBCATCH_EVAP*/},
            {QStringLiteral("Infiltration"), 3 /*SWMM_OUT_SUBCATCH_INFIL*/},
            {QStringLiteral("Runoff"),       4 /*SWMM_OUT_SUBCATCH_RUNOFF*/},
        };
    default:
        return {};
    }
}

} // anonymous

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TimeSeriesPlotDialog::TimeSeriesPlotDialog(const QString &outPath,
                                           const SWMMObjectRef &obj,
                                           QWidget *parent)
    : QDialog(parent),
      m_outPath(outPath),
      m_object(obj)
{
    setWindowTitle(tr("Time Series — %1").arg(obj.name));
    resize(720, 480);
    buildUi();
    populateVariables();
    plotSeries();
}

TimeSeriesPlotDialog::~TimeSeriesPlotDialog() = default;

void TimeSeriesPlotDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    auto *header = new QHBoxLayout;
    m_titleLabel = new QLabel(this);
    m_titleLabel->setText(tr("<b>%1</b>  (from %2)")
                              .arg(m_object.name.toHtmlEscaped(),
                                   m_outPath.toHtmlEscaped()));
    m_titleLabel->setTextFormat(Qt::RichText);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_titleLabel->setWordWrap(true);
    header->addWidget(m_titleLabel, 1);

    m_varCombo = new QComboBox(this);
    header->addWidget(new QLabel(tr("Variable:"), this));
    header->addWidget(m_varCombo);
    root->addLayout(header);

    m_chartView = new QChartView(this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    root->addWidget(m_chartView, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    connect(m_varCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TimeSeriesPlotDialog::onVariableChanged);
}

void TimeSeriesPlotDialog::populateVariables()
{
    m_varCombo->clear();
    for (const auto &v : variablesFor(m_object.objectType))
        m_varCombo->addItem(v.label, v.code);
}

void TimeSeriesPlotDialog::onVariableChanged(int)
{
    plotSeries();
}

void TimeSeriesPlotDialog::plotSeries()
{
    auto *chart = new QChart;
    chart->setTitle(tr("%1 — %2").arg(m_object.name, m_varCombo->currentText()));
    chart->legend()->setVisible(false);

    SWMM_Output out = swmm_output_open(m_outPath.toUtf8().constData());
    if (!out)
    {
        chart->setTitle(tr("Could not open %1").arg(m_outPath));
        m_chartView->setChart(chart);
        return;
    }

    const int periods = swmm_output_get_period_count(out);
    if (periods <= 0)
    {
        chart->setTitle(tr("Output file has no reporting periods"));
        m_chartView->setChart(chart);
        swmm_output_close(out);
        return;
    }

    // Find the object's index by ID match.
    auto findIdx = [&]() -> int {
        const QByteArray want = m_object.name.toUtf8();
        switch (m_object.objectType)
        {
        case SWMMObjectRef::Node:
        {
            const int n = swmm_output_get_node_count(out);
            for (int i = 0; i < n; ++i)
                if (qstrcmp(swmm_output_get_node_id(out, i), want.constData()) == 0)
                    return i;
            break;
        }
        case SWMMObjectRef::Link:
        {
            const int n = swmm_output_get_link_count(out);
            for (int i = 0; i < n; ++i)
                if (qstrcmp(swmm_output_get_link_id(out, i), want.constData()) == 0)
                    return i;
            break;
        }
        case SWMMObjectRef::Subcatchment:
        {
            const int n = swmm_output_get_subcatch_count(out);
            for (int i = 0; i < n; ++i)
                if (qstrcmp(swmm_output_get_subcatch_id(out, i), want.constData()) == 0)
                    return i;
            break;
        }
        default: break;
        }
        return -1;
    };

    const int idx = findIdx();
    if (idx < 0)
    {
        chart->setTitle(tr("Object %1 not found in output").arg(m_object.name));
        m_chartView->setChart(chart);
        swmm_output_close(out);
        return;
    }

    const int var = m_varCombo->currentData().toInt();

    QVector<float> values(periods);
    int rc = -1;
    switch (m_object.objectType)
    {
    case SWMMObjectRef::Node:
        rc = swmm_output_get_node_series(out, idx, var, 0, periods - 1, values.data());
        break;
    case SWMMObjectRef::Link:
        rc = swmm_output_get_link_series(out, idx, var, 0, periods - 1, values.data());
        break;
    case SWMMObjectRef::Subcatchment:
        rc = swmm_output_get_subcatch_series(out, idx, var, 0, periods - 1, values.data());
        break;
    default: break;
    }

    if (rc != 0)
    {
        chart->setTitle(tr("Engine returned error %1 reading series").arg(rc));
        m_chartView->setChart(chart);
        swmm_output_close(out);
        return;
    }

    // X axis: simulation hours since start. The engine returns START_DATE as
    // a Julian-date double (days since SWMM's epoch) — converting to a
    // calendar QDateTime for a QDateTimeAxis is left to a follow-up slice
    // since SWMM's epoch isn't documented uniformly. Hours-since-start is
    // unambiguous and immediately useful.
    const int reportStepSec = swmm_output_get_report_step(out);

    auto *series = new QLineSeries;
    series->setName(m_varCombo->currentText());
    double minV = 0.0, maxV = 0.0;
    double maxX = 0.0;
    for (int p = 0; p < periods; ++p)
    {
        const double tHours = double(p) * double(reportStepSec) / 3600.0;
        const double v = double(values[p]);
        series->append(tHours, v);
        maxX = tHours;
        if (p == 0) { minV = maxV = v; }
        else { if (v < minV) minV = v; if (v > maxV) maxV = v; }
    }

    chart->addSeries(series);

    auto *xAxis = new QValueAxis;
    xAxis->setTitleText(tr("Hours since simulation start"));
    xAxis->setRange(0.0, maxX);
    chart->addAxis(xAxis, Qt::AlignBottom);
    series->attachAxis(xAxis);

    auto *yAxis = new QValueAxis;
    yAxis->setTitleText(m_varCombo->currentText());
    // 5 % padding so the line doesn't kiss the top edge.
    const double pad = 0.05 * (maxV - minV);
    yAxis->setRange(minV - pad, maxV + pad);
    chart->addAxis(yAxis, Qt::AlignLeft);
    series->attachAxis(yAxis);

    m_chartView->setChart(chart);
    swmm_output_close(out);
}
