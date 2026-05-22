/*!
 * \file   comparisonplotmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/comparisonplotmodel.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::plot {

ComparisonPlotModel::ComparisonPlotModel(QObject *parent)
    : QObject(parent)
{
}

ComparisonPlotModel::~ComparisonPlotModel() = default;

int ComparisonPlotModel::addRunSource(RunSource src)
{
    if (src.label.isEmpty() && src.layer)
        src.label = src.layer->scenarioName();

    m_runs.push_back(std::move(src));
    const int idx = m_runs.size() - 1;

    // First run becomes the default baseline.
    if (m_baselineIdx < 0)
        m_baselineIdx = idx;

    emit runSourceAdded(idx);
    return idx;
}

void ComparisonPlotModel::removeRunSource(int runIndex)
{
    if (runIndex < 0 || runIndex >= m_runs.size())
        return;

    // Drop any series referencing this run; shift run indices on the rest.
    for (int i = m_specs.size() - 1; i >= 0; --i) {
        if (m_specs[i].runIndex == runIndex) {
            m_specs.removeAt(i);
            emit seriesRemoved(i);
        } else if (m_specs[i].runIndex > runIndex) {
            --m_specs[i].runIndex;
        }
    }

    m_runs.removeAt(runIndex);
    if (m_baselineIdx == runIndex)
        m_baselineIdx = m_runs.isEmpty() ? -1 : 0;
    else if (m_baselineIdx > runIndex)
        --m_baselineIdx;

    deriveRows_();
    emit runSourceRemoved(runIndex);
    if (m_baselineIdx >= 0)
        emit baselineChanged(m_baselineIdx);
    emit rowsChanged();
}

int ComparisonPlotModel::findRunByLayer(const IRunLayer* layer) const
{
    if (!layer)
        return -1;
    for (int i = 0; i < m_runs.size(); ++i) {
        if (m_runs[i].layer.get() == layer)
            return i;
    }
    return -1;
}

void ComparisonPlotModel::setBaseline(int runIndex)
{
    if (runIndex < -1 || runIndex >= m_runs.size())
        return;
    if (runIndex == m_baselineIdx)
        return;
    m_baselineIdx = runIndex;
    emit baselineChanged(m_baselineIdx);
}

int ComparisonPlotModel::addSeries(SeriesSpec spec)
{
    // Auto-style cycle: if the spec carries the default style, assign one
    // from the run's cycle position.
    if (spec.style.color == SeriesStyle{}.color && spec.runIndex >= 0 &&
        spec.runIndex < m_runs.size())
    {
        RunSource &run = m_runs[spec.runIndex];
        spec.style = defaultStyleForCycle(run.cycleSeed++);
    }

    m_specs.push_back(std::move(spec));
    const int idx = m_specs.size() - 1;
    deriveRows_();
    emit seriesAdded(idx);
    emit rowsChanged();
    return idx;
}

void ComparisonPlotModel::removeSeries(int seriesIndex)
{
    if (seriesIndex < 0 || seriesIndex >= m_specs.size())
        return;
    m_specs.removeAt(seriesIndex);
    deriveRows_();
    emit seriesRemoved(seriesIndex);
    emit rowsChanged();
}

void ComparisonPlotModel::updateStyle(int seriesIndex, const SeriesStyle& style)
{
    if (seriesIndex < 0 || seriesIndex >= m_specs.size())
        return;
    m_specs[seriesIndex].style = style;
    emit styleChanged(seriesIndex);
}

void ComparisonPlotModel::rebuildRows()
{
    deriveRows_();
    emit rowsChanged();
}

void ComparisonPlotModel::deriveRows_()
{
    // Preserve insertion order: rows are listed in the order each attribute
    // first appears in m_specs.
    QVector<AttributeRow> rows;
    QHash<PlotAttribute, int> rowIndexByAttr;

    for (int s = 0; s < m_specs.size(); ++s) {
        const SeriesSpec &spec = m_specs[s];
        if (!spec.isValid())
            continue;

        int r;
        if (rowIndexByAttr.contains(spec.attribute)) {
            r = rowIndexByAttr.value(spec.attribute);
        } else {
            AttributeRow row;
            row.attribute = spec.attribute;
            row.unitSystem = (spec.runIndex >= 0 && spec.runIndex < m_runs.size() &&
                              m_runs[spec.runIndex].layer)
                              ? m_runs[spec.runIndex].layer->unitSystem()
                              : UnitSystem::US;
            row.unitsLabel = unitsFor(spec.attribute, row.unitSystem);
            row.ymin = std::numeric_limits<double>::infinity();
            row.ymax = -std::numeric_limits<double>::infinity();
            rows.push_back(std::move(row));
            r = rows.size() - 1;
            rowIndexByAttr.insert(spec.attribute, r);
        }
        rows[r].seriesIndices.push_back(s);
    }

    // Sanitize empty axis ranges → [0, 1] so views don't see infinities.
    for (AttributeRow &row : rows) {
        if (!std::isfinite(row.ymin) || !std::isfinite(row.ymax) || row.ymin > row.ymax) {
            row.ymin = 0.0;
            row.ymax = 1.0;
        }
    }

    m_rows = std::move(rows);
}

void ComparisonPlotModel::resolveSeries(int seriesIndex, SeriesData& out) const
{
    out.ok = false;
    out.errorMessage.clear();
    out.timesJulian.clear();
    out.values.clear();

    if (seriesIndex < 0 || seriesIndex >= m_specs.size()) {
        out.errorMessage = QStringLiteral("Series index out of range");
        return;
    }
    const SeriesSpec &spec = m_specs.at(seriesIndex);
    if (!spec.isValid()) {
        out.errorMessage = QStringLiteral("Invalid series spec");
        return;
    }
    if (spec.runIndex < 0 || spec.runIndex >= m_runs.size()) {
        out.errorMessage = QStringLiteral("Run index out of range");
        return;
    }
    const RunSource &run = m_runs.at(spec.runIndex);
    if (!run.layer) {
        out.errorMessage = QStringLiteral("Run has no source");
        return;
    }
    run.layer->getSeriesAt(spec.objectRef, spec.attribute, out);
}

void ComparisonPlotModel::setAnimationTime(QDateTime t)
{
    if (m_animTime == t)
        return;
    m_animTime = std::move(t);
    emit animationTimeChanged(m_animTime);
}

} // namespace openswmmvis::plot
