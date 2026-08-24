/*!
 * \file   swmmoutrunlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/swmmoutrunlayer.h"

#include "plot/resultdescriptor.h"

#include "layers/swmmresultslayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <cmath>
#include <vector>

namespace openswmmvis::plot {

SwmmOutRunLayer::SwmmOutRunLayer(SWMMResultsLayer *layer)
    : m_layer(layer)
{}

SWMMResultsLayer *SwmmOutRunLayer::layer() const
{
    return m_layer.data();
}

QString SwmmOutRunLayer::scenarioName() const
{
    return m_layer ? m_layer->scenarioName() : QStringLiteral("(closed)");
}

UnitSystem SwmmOutRunLayer::unitSystem() const
{
    if (!m_layer)
        return UnitSystem::US;
    const int fu = m_layer->flowUnits();
    return (fu >= 0) ? unitSystemFromFlowUnits(fu) : UnitSystem::US;
}

double SwmmOutRunLayer::startDateJulian() const
{
    if (!m_layer || !m_layer->outputHandle())
        return std::nan("");
    double j = std::nan("");
    if (swmm_output_get_start_date(m_layer->outputHandle(), &j) != 0)
        return std::nan("");
    return j;
}

int SwmmOutRunLayer::periodCount() const
{
    if (!m_layer)
        return 0;
    return m_layer->totalTimeSteps();
}

int SwmmOutRunLayer::reportStepSeconds() const
{
    return m_layer ? m_layer->reportStepSeconds() : 0;
}

QString SwmmOutRunLayer::persistenceKey() const
{
    return m_layer ? m_layer->resultsFilePath() : QString();
}

// SwmmOutRunLayer::variableCodeFor lives in swmmoutrunlayer_codes.cpp so it
// can be linked into unit tests without dragging in SWMMResultsLayer and its
// transitive deps.

bool SwmmOutRunLayer::supportsAttribute(PlotAttribute attr) const
{
    // 1D .out files don't carry 2D mesh quantities.
    if (isMesh2DAttribute(attr))
        return false;
    return attr != PlotAttribute::Unknown;
}

void SwmmOutRunLayer::getSeriesAt(const ObjectRef& ref,
                                  PlotAttribute attr,
                                  SeriesData& out) const
{
    out.ok = false;
    out.errorMessage.clear();
    out.timesJulian.clear();
    out.values.clear();

    if (!m_layer || !m_layer->outputHandle()) {
        out.errorMessage = QStringLiteral("Result layer not available");
        return;
    }

    const int varCode = variableCodeFor(attr, ref.kind);
    if (varCode < 0) {
        out.errorMessage = QStringLiteral("Attribute not applicable to object kind");
        return;
    }

    int objIdx = -1;
    switch (ref.kind) {
    case ObjectRef::Kind::Node:     objIdx = m_layer->nodeOutputIndex(ref.name);     break;
    case ObjectRef::Kind::Link:     objIdx = m_layer->linkOutputIndex(ref.name);     break;
    case ObjectRef::Kind::Subcatch: objIdx = m_layer->subcatchOutputIndex(ref.name); break;
    case ObjectRef::Kind::System:   objIdx = 0; break;  // No per-object index for system series
    default:
        out.errorMessage = QStringLiteral("Unsupported object kind for 1D .out");
        return;
    }
    if (objIdx < 0) {
        out.errorMessage = QStringLiteral("Object '%1' not found in .out").arg(ref.name);
        return;
    }

    SWMM_Output handle = m_layer->outputHandle();
    const int n_periods = m_layer->totalTimeSteps();
    if (n_periods <= 0) {
        out.errorMessage = QStringLiteral("Empty .out (no periods)");
        return;
    }

    // Fetch the full series in one bulk call.
    std::vector<float> values(static_cast<std::size_t>(n_periods));
    int rc = -1;
    switch (ref.kind) {
    case ObjectRef::Kind::Node:
        rc = swmm_output_get_node_series(handle, objIdx, varCode,
                                          0, n_periods - 1, values.data());
        break;
    case ObjectRef::Kind::Link:
        rc = swmm_output_get_link_series(handle, objIdx, varCode,
                                          0, n_periods - 1, values.data());
        break;
    case ObjectRef::Kind::Subcatch:
        rc = swmm_output_get_subcatch_series(handle, objIdx, varCode,
                                              0, n_periods - 1, values.data());
        break;
    case ObjectRef::Kind::System:
        rc = swmm_output_get_system_series(handle, varCode,
                                            0, n_periods - 1, values.data());
        break;
    default:
        break;
    }
    if (rc != 0) {
        out.errorMessage = QStringLiteral("Engine returned error %1 from get_*_series").arg(rc);
        return;
    }

    // Build the matching time axis: start_julian + i * (report_step / 86400).
    const double t0 = startDateJulian();
    const int stepSec = reportStepSeconds();
    if (!std::isfinite(t0) || stepSec <= 0) {
        out.errorMessage = QStringLiteral("Missing start date or report step");
        return;
    }
    const double step_days = static_cast<double>(stepSec) / 86400.0;

    out.timesJulian.resize(static_cast<std::size_t>(n_periods));
    out.values.resize(static_cast<std::size_t>(n_periods));
    for (int i = 0; i < n_periods; ++i) {
        // SWMM .out report periods are 1-based in the file but the engine API
        // exposes 0-based indexing for start/end periods (start_period=0,
        // end_period=n-1 returns n values).  Each value at index i corresponds
        // to time t0 + (i+1) * stepSec (the engine writes at the END of each
        // reporting interval — same convention TimeSeriesPlotDialog uses).
        out.timesJulian[i] = t0 + (i + 1) * step_days;
        out.values[i]      = static_cast<double>(values[i]);
    }
    out.ok = true;
}

QVector<ResultDescriptor> SwmmOutRunLayer::resultDescriptorsForKind(
    ObjectRef::Kind kind) const
{
    // Y2b-1 (amendment D-Y4): the fixed set plus THIS run's species by
    // name. A destroyed or quality-free layer degrades to the fixed set —
    // exactly what a legacy .out should serve.
    QStringList species;
    if (m_layer)
        species = m_layer->speciesNames();
    return plot::resultDescriptorsForKind(kind, species);
}

} // namespace openswmmvis::plot
