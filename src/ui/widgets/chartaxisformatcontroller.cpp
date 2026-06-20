/*!
 * \file   chartaxisformatcontroller.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/chartaxisformatcontroller.h"

#include "core/preferencesmanager.h"
#include "plot/chartproperties.h"
#include "ui/dialogs/chartpropertiesdialog.h"

#include <QChart>
#include <QValueAxis>

namespace openswmmvis::ui {

using openswmmvis::plot::ChartProperties;
using openswmmvis::plot::NumberFormat;
using openswmmvis::plot::NumberFormatMode;

ChartAxisFormatController::ChartAxisFormatController(QChart *chart, QObject *parent)
    : QObject(parent), m_chart(chart)
{
    auto *prefs = PreferencesManager::instance();
    m_x = prefs->plotXAxisFormat();
    m_y = prefs->plotYAxisFormat();
    apply();
}

void ChartAxisFormatController::setChart(QChart *chart)
{
    m_chart = chart;
    apply();
}

void ChartAxisFormatController::apply()
{
    if (!m_chart) return;
    const QString xSpec = m_x.printfSpec();
    const QString ySpec = m_y.printfSpec();
    for (auto *ax : m_chart->axes(Qt::Horizontal))
        if (auto *va = qobject_cast<QValueAxis *>(ax)) va->setLabelFormat(xSpec);
    for (auto *ax : m_chart->axes(Qt::Vertical))
        if (auto *va = qobject_cast<QValueAxis *>(ax)) va->setLabelFormat(ySpec);
}

void ChartAxisFormatController::openDialog(QWidget *parent)
{
    if (!m_chart) return;

    // ChartProperties seeds non-format attributes (title/range/colours/theme)
    // from the live chart; override its label-format fields with our persistent
    // state so the dialog opens showing the current per-chart choice.
    auto *cp = new ChartProperties(m_chart);
    cp->setXLabelFormatMode(static_cast<ChartProperties::LabelFormatMode>(m_x.mode));
    cp->setXLabelPrecision(m_x.count);
    cp->setXLabelFormat(m_x.custom);
    cp->setYLabelFormatMode(static_cast<ChartProperties::LabelFormatMode>(m_y.mode));
    cp->setYLabelPrecision(m_y.count);
    cp->setYLabelFormat(m_y.custom);

    // Write edits back so they persist across the editor's replot cycles. The
    // connections auto-disconnect when the dialog deletes `cp` on close.
    connect(cp, &ChartProperties::xLabelFormatModeChanged, this,
            [this](ChartProperties::LabelFormatMode m) { m_x.mode = static_cast<NumberFormatMode>(m); });
    connect(cp, &ChartProperties::xLabelPrecisionChanged, this,
            [this](int c) { m_x.count = c; });
    connect(cp, &ChartProperties::xLabelFormatChanged, this,
            [this](const QString &s) { m_x.custom = s; });
    connect(cp, &ChartProperties::yLabelFormatModeChanged, this,
            [this](ChartProperties::LabelFormatMode m) { m_y.mode = static_cast<NumberFormatMode>(m); });
    connect(cp, &ChartProperties::yLabelPrecisionChanged, this,
            [this](int c) { m_y.count = c; });
    connect(cp, &ChartProperties::yLabelFormatChanged, this,
            [this](const QString &s) { m_y.custom = s; });

    auto *dlg = new ChartPropertiesDialog(cp, parent);   // takes ownership of cp
    dlg->show();
}

} // namespace openswmmvis::ui
