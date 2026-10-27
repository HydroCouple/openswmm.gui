/**
 * @file comparisonplot_timeseries.cpp
 * @brief Native observation-layer registration in the existing Comparison plot tab.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#include "ui/dialogs/comparisonplotdialog.h"
#include "layers/timeserieslayer.h"
#include "plot/timeseriesrunlayer.h"
#include "plot/utctimeaxis.h"
#include "ui/widgets/statssummarypanel.h"
#include <QSignalBlocker>
namespace openswmmvis::ui
{
using namespace openswmmvis::plot;
int ComparisonPlotDialog::ensureRunSourceForTimeSeriesLayer(::TimeSeriesLayer *layer)
{
    if (!layer || !layer->dataset()) return -1;
    const auto key = QStringLiteral("timeseries://") + layer->dataset()->id;
    for (int i = 0; i < m_model->runSourceCount(); ++i)
        if (m_model->runSource(i).layer && m_model->runSource(i).layer->persistenceKey() == key) return i;
    auto adapter = std::make_shared<TimeSeriesRunLayer>(layer->dataset());
    RunSource source; source.layer = adapter;
    const int index = m_model->addRunSource(std::move(source));
    bool hasModelSource = false;
    for (int i=0; i<index; ++i)
        if (!std::dynamic_pointer_cast<TimeSeriesRunLayer>(m_model->runSource(i).layer)) hasModelSource = true;
    const QPointer<ComparisonPlotDialog> owner(this);
    if (hasModelSource && !confirmObservationClockMappings()) {
        if (owner) m_model->removeRunSource(index);
        return -1;
    }
    observeSourceLifetime(layer, adapter);
    connect(layer, &TimeSeriesLayer::datasetChanged, this, [this, weak = std::weak_ptr<IRunLayer>(adapter)] {
        const auto old = weak.lock();
        if (!old) return;
        // Removing the last series emits intermediate signals before its row list changes.
        // Publish only the completed removal, as the source-destruction path does.
        {
            const QSignalBlocker blocked(m_model.get());
            for (int i = m_model->runSourceCount()-1; i >= 0; --i)
                if (m_model->runSource(i).layer == old) m_model->removeRunSource(i);
        }
        onRunSourceRemoved(-1);
    });
    connect(adapter.get(), &TimeSeriesRunLayer::dataReady, this, [this] {
        QDateTime lo, hi;
        if (m_observationRangeChosen && !m_rowWidgets.isEmpty() && m_rowWidgets.first().xAxis) {
            lo = m_rowWidgets.first().xAxis->min(); hi = m_rowWidgets.first().xAxis->max();
        }
        m_restoringObservationRange = true;
        rebuildCharts();
        if (lo.isValid() && hi > lo) for (auto &row : m_rowWidgets) if (row.xAxis) row.xAxis->setRange(lo,hi);
        m_restoringObservationRange = false;
        if (m_statsPanel) m_statsPanel->refresh();
    });
    return index;
}
int ComparisonPlotDialog::addTimeSeriesLayerSeries(::TimeSeriesLayer *layer, const QStringList &ids)
{
    const int run = ensureRunSourceForTimeSeriesLayer(layer);
    if (run < 0) return 0;
    const auto adapter = std::dynamic_pointer_cast<TimeSeriesRunLayer>(m_model->runSource(run).layer);
    QVector<SeriesSpec> specs;
    QStringList pending;
    for (const auto &id : ids) {
        if (!adapter->series(id) || pending.contains(id)) continue;
        bool exists = false;
        for (int i=0; i<m_model->seriesCount(); ++i)
            if (m_model->spec(i).runIndex == run && m_model->spec(i).objectRef == ObjectRef::forObserved(id)) exists = true;
        if (exists) continue;
        SeriesSpec spec; spec.runIndex = run; spec.objectRef = ObjectRef::forObserved(id);
        spec.setDescriptor(adapter->descriptorFor(id));
        spec.legendOverride = layer->seriesCaption(id);
        specs.append(std::move(spec)); pending.append(id);
    }
    adapter->requestWindow(pending);
    return m_model->addSeriesBatch(std::move(specs));
}

}
