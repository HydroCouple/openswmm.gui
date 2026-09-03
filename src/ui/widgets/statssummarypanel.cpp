/*!
 * \file   statssummarypanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/statssummarypanel.h"

#include "core/preferencesmanager.h"
#include "core/swmmdatetime.h"
#include "plot/comparisonplotmodel.h"
#include "plot/fitmetrics.h"
#include "plot/irunlayer.h"
#include "plot/seriesstatistics.h"

#include <QAction>
#include <QDateTime>
#include <QHeaderView>
#include <QMenu>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui {

using namespace openswmmvis::plot;

namespace {
const QStringList kBaseColumns = {
    QStringLiteral("Series"),
    QStringLiteral("count"),
    QStringLiteral("mean"),
    QStringLiteral("median"),
    QStringLiteral("stddev"),
    QStringLiteral("min"),
    QStringLiteral("max"),
    QStringLiteral("p05"),
    QStringLiteral("p25"),
    QStringLiteral("p50"),
    QStringLiteral("p75"),
    QStringLiteral("p95"),
    QStringLiteral("sum"),
};
const QStringList kFitColumns = {
    QStringLiteral("NSE"),
    QStringLiteral("R²"),
    QStringLiteral("RMSE"),
    QStringLiteral("PBIAS%"),
};
} // namespace

StatsSummaryPanel::StatsSummaryPanel(QWidget *parent)
    : QWidget(parent)
{
    m_valueFormat = PreferencesManager::instance()->plotYAxisFormat();
    loadNumberFormat();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    root->addWidget(m_tabs);

    loadColumnVisibility();
}

void StatsSummaryPanel::loadNumberFormat()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ComparisonPlotDialog/StatsFormat"));
    const int defaultMode = static_cast<int>(m_valueFormat.mode);
    const int mode = s.value(QStringLiteral("mode"), defaultMode).toInt();
    m_valueFormat.mode = (mode >= static_cast<int>(NumberFormatMode::Decimals)
                          && mode <= static_cast<int>(NumberFormatMode::Thousands))
                             ? static_cast<NumberFormatMode>(mode)
                             : NumberFormatMode::Decimals;
    m_valueFormat.count = s.value(QStringLiteral("precision"), m_valueFormat.count).toInt();
    m_valueFormat.custom = s.value(QStringLiteral("custom"), m_valueFormat.custom).toString();
    s.endGroup();
}

void StatsSummaryPanel::saveNumberFormat() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("ComparisonPlotDialog/StatsFormat"));
    s.setValue(QStringLiteral("mode"), static_cast<int>(m_valueFormat.mode));
    s.setValue(QStringLiteral("precision"), m_valueFormat.count);
    s.setValue(QStringLiteral("custom"), m_valueFormat.custom);
    s.endGroup();
}

void StatsSummaryPanel::setStatisticNumberFormat(const NumberFormat &format)
{
    m_valueFormat = format;
    saveNumberFormat();
    refresh();
}

QString StatsSummaryPanel::formatValue(double value) const
{
    return formatStatisticValue(value, m_valueFormat);
}

void StatsSummaryPanel::loadColumnVisibility()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ComparisonPlotDialog/StatsColumns"));
    // Default = visible for every known column; load any persisted override.
    const auto allCols = kBaseColumns + kFitColumns;
    for (const QString &col : allCols) {
        if (col == QStringLiteral("Series")) continue;
        m_columnVisible[col] = s.value(col, true).toBool();
    }
    s.endGroup();
}

void StatsSummaryPanel::saveColumnVisibility()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ComparisonPlotDialog/StatsColumns"));
    for (auto it = m_columnVisible.constBegin();
         it != m_columnVisible.constEnd(); ++it) {
        s.setValue(it.key(), it.value());
    }
    s.endGroup();
}

void StatsSummaryPanel::applyColumnVisibility(QTableWidget *table)
{
    if (!table) return;
    for (int c = 0; c < table->columnCount(); ++c) {
        const QString name = table->horizontalHeaderItem(c)
            ? table->horizontalHeaderItem(c)->text() : QString();
        // "Series" column is always visible.
        if (name == QStringLiteral("Series")) {
            table->setColumnHidden(c, false);
            continue;
        }
        const bool visible = m_columnVisible.value(name, true);
        table->setColumnHidden(c, !visible);
    }
}

void StatsSummaryPanel::showHeaderContextMenu(QTableWidget *table,
                                               const QPoint &globalPos)
{
    if (!table) return;
    QMenu menu(this);
    menu.addAction(tr("Show / hide columns"))->setEnabled(false);
    menu.addSeparator();
    for (int c = 0; c < table->columnCount(); ++c) {
        const QString name = table->horizontalHeaderItem(c)
            ? table->horizontalHeaderItem(c)->text() : QString();
        if (name.isEmpty() || name == QStringLiteral("Series")) continue;
        auto *a = menu.addAction(name);
        a->setCheckable(true);
        a->setChecked(m_columnVisible.value(name, true));
        connect(a, &QAction::toggled, this, [this, name](bool on) {
            m_columnVisible[name] = on;
            saveColumnVisibility();
            // Apply to every tab so columns sync across attribute rows.
            for (int i = 0; i < m_tabs->count(); ++i) {
                if (auto *t = qobject_cast<QTableWidget*>(m_tabs->widget(i)))
                    applyColumnVisibility(t);
            }
        });
    }
    menu.exec(globalPos);
}

void StatsSummaryPanel::setModel(ComparisonPlotModel *model)
{
    if (m_model == model) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        connect(m_model, &ComparisonPlotModel::seriesAdded,
                this, &StatsSummaryPanel::refresh);
        connect(m_model, &ComparisonPlotModel::seriesRemoved,
                this, &StatsSummaryPanel::refresh);
        connect(m_model, &ComparisonPlotModel::runSourceAdded,
                this, &StatsSummaryPanel::refresh);
        connect(m_model, &ComparisonPlotModel::runSourceRemoved,
                this, &StatsSummaryPanel::refresh);
        connect(m_model, &ComparisonPlotModel::rowsChanged,
                this, &StatsSummaryPanel::refresh);
        connect(m_model, &ComparisonPlotModel::baselineChanged,
                this, &StatsSummaryPanel::refresh);
    }
    refresh();
}

void StatsSummaryPanel::setSelectionRange(QDateTime lo, QDateTime hi)
{
    if (m_selLo == lo && m_selHi == hi) return;
    m_selLo = std::move(lo);
    m_selHi = std::move(hi);
    refresh();
}

void StatsSummaryPanel::refresh()
{
    rebuildTabs();
}

void StatsSummaryPanel::rebuildTabs()
{
    m_tabs->clear();
    if (!m_model) return;
    const auto &rows = m_model->rows();
    for (int r = 0; r < rows.size(); ++r) {
        const auto &row = rows.at(r);
        auto *table = new QTableWidget(m_tabs);
        QStringList columns = kBaseColumns;
        const bool haveBaseline = m_model->baselineRunIndex() >= 0
                                   && m_model->runSourceCount() >= 2;
        if (haveBaseline) columns += kFitColumns;
        table->setColumnCount(columns.size());
        table->setHorizontalHeaderLabels(columns);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);

        // AT.3 — right-click on the horizontal header → column visibility menu.
        table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(table->horizontalHeader(),
                &QHeaderView::customContextMenuRequested,
                this, [this, table](const QPoint &p) {
                    showHeaderContextMenu(
                        table,
                        table->horizontalHeader()->viewport()->mapToGlobal(p));
                });

        populateTab(table, r);
        applyColumnVisibility(table);

        // Y2b-3: species rows label by the descriptor authority — a
        // species row carries attribute == Unknown, and labelFor(Unknown)
        // would caption the tab "Unknown".
        const openswmmvis::plot::ResultDescriptor rowDesc =
            row.species.isEmpty()
                ? openswmmvis::plot::ResultDescriptor::forAttribute(
                      row.attribute)
                : openswmmvis::plot::ResultDescriptor::forSpecies(
                      row.species);
        const QString tabLabel = QStringLiteral("%1 (%2)")
            .arg(rowDesc.label(), row.unitsLabel);
        m_tabs->addTab(table, tabLabel);
    }
}

void StatsSummaryPanel::populateTab(QTableWidget *table, int rowIndex)
{
    if (!m_model) return;
    const auto &row = m_model->rows().at(rowIndex);
    table->setRowCount(row.seriesIndices.size());
    const bool haveBaseline = m_model->baselineRunIndex() >= 0
                               && m_model->runSourceCount() >= 2;
    const int baselineRun = m_model->baselineRunIndex();

    // Resolve baseline samples per object once so we can pair against
    // each comparison series in the same row.
    struct BaselineKey { ObjectRef ref; };
    auto refKey = [](const ObjectRef &r) {
        return QStringLiteral("%1|%2|%3")
            .arg(static_cast<int>(r.kind)).arg(r.name).arg(r.triIdx);
    };

    std::map<QString, std::vector<double>> baselineByObj;
    std::map<QString, std::vector<double>> baselineTimesByObj;
    if (haveBaseline) {
        for (int sIdx : row.seriesIndices) {
            const auto &spec = m_model->spec(sIdx);
            if (spec.runIndex != baselineRun) continue;
            SeriesData data;
            m_model->resolveSeries(sIdx, data);
            if (!data.ok) continue;
            baselineByObj[refKey(spec.objectRef)]      = data.values;
            baselineTimesByObj[refKey(spec.objectRef)] = data.timesJulian;
        }
    }

    int outRow = 0;
    for (int sIdx : row.seriesIndices) {
        const auto &spec = m_model->spec(sIdx);
        SeriesData data;
        m_model->resolveSeries(sIdx, data);
        if (!data.ok) {
            table->setItem(outRow, 0,
                new QTableWidgetItem(QStringLiteral("(unresolved series %1)").arg(sIdx)));
            ++outRow;
            continue;
        }

        // Filter to selection range.
        std::vector<double> filtered;
        filtered.reserve(data.values.size());
        for (std::size_t i = 0; i < data.values.size(); ++i) {
            const QDateTime t = core::swmmDateTimeToQDateTime(data.timesJulian[i]);
            if (m_selLo.isValid() && t < m_selLo) continue;
            if (m_selHi.isValid() && t > m_selHi) continue;
            filtered.push_back(data.values[i]);
        }

        const auto stats = computeStatistics(filtered);

        const QString legendName = !spec.legendOverride.isEmpty()
            ? spec.legendOverride
            : QStringLiteral("%1 — %2")
                .arg(m_model->runSource(spec.runIndex).label,
                     spec.objectRef.kind == ObjectRef::Kind::Mesh2DCell
                         ? QStringLiteral("Cell %1").arg(spec.objectRef.triIdx)
                         : spec.objectRef.name.isEmpty()
                             ? QStringLiteral("(System)")
                             : spec.objectRef.name);

        int col = 0;
        table->setItem(outRow, col++, new QTableWidgetItem(legendName));
        table->setItem(outRow, col++, new QTableWidgetItem(QString::number(stats.count)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.mean)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.median)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.stddev)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.min)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.max)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.p05)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.p25)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.p50)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.p75)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.p95)));
        table->setItem(outRow, col++, new QTableWidgetItem(formatValue(stats.sum)));

        if (haveBaseline) {
            // Pair against baseline samples by timestamp (within ½-step).
            auto it = baselineByObj.find(refKey(spec.objectRef));
            FitMetrics fit;
            bool       haveFit = false;
            if (it != baselineByObj.end() && spec.runIndex != baselineRun) {
                const auto &btimes = baselineTimesByObj[refKey(spec.objectRef)];
                const auto &bvals  = it->second;
                const double halfStep = (btimes.size() >= 2)
                    ? 0.5 * std::fabs(btimes[1] - btimes[0]) : 0.0;
                std::vector<double> xs, ys;
                std::size_t i = 0, j = 0;
                while (i < btimes.size() && j < data.timesJulian.size()) {
                    const double tb = btimes[i];
                    const double tc = data.timesJulian[j];
                    const QDateTime tbDt = core::swmmDateTimeToQDateTime(tb);
                    const bool inWin =
                        (!m_selLo.isValid() || tbDt >= m_selLo) &&
                        (!m_selHi.isValid() || tbDt <= m_selHi);
                    if (std::fabs(tb - tc) <= halfStep) {
                        if (inWin && std::isfinite(bvals[i])
                            && std::isfinite(data.values[j])) {
                            xs.push_back(bvals[i]);
                            ys.push_back(data.values[j]);
                        }
                        ++i; ++j;
                    } else if (tb < tc) ++i;
                    else                ++j;
                }
                if (!xs.empty()) {
                    fit     = FitMetrics::compute(xs, ys);
                    haveFit = true;
                }
            }
            table->setItem(outRow, col++,
                new QTableWidgetItem(haveFit ? formatValue(fit.nse)   : QStringLiteral("—")));
            table->setItem(outRow, col++,
                new QTableWidgetItem(haveFit ? formatValue(fit.r2)    : QStringLiteral("—")));
            table->setItem(outRow, col++,
                new QTableWidgetItem(haveFit ? formatValue(fit.rmse)  : QStringLiteral("—")));
            table->setItem(outRow, col++,
                new QTableWidgetItem(haveFit ? formatValue(fit.pbias) : QStringLiteral("—")));
        }
        ++outRow;
    }
    table->resizeColumnsToContents();
}

} // namespace openswmmvis::ui
