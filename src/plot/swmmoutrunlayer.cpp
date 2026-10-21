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
#include <algorithm>
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
    const int code=variableCodeFor(attr,ObjectRef::Kind::Subcatch);
    if(code>=SWMM_OUT_SUBCATCH_SNOW_SWE)
        return m_layer && m_layer->outputHandle() && swmm_output_has_subcatch_variable(m_layer->outputHandle(),code)==1;
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
    fetchSeriesByCode_(ref, varCode, out);
}

/// The shared tail of both getSeriesAt overloads: object resolution +
/// bulk fetch + time axis, for an ALREADY-RESOLVED engine var code
/// (fixed attribute or species column — Y2b-2). Preconditions checked by
/// the callers: m_layer and its output handle are non-null.
void SwmmOutRunLayer::fetchSeriesByCode_(const ObjectRef& ref, int varCode,
                                         SeriesData& out) const
{
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

    // Fetch [firstPeriod, n) in one bulk call — the whole series on a fresh
    // chart, only the new tail on a live tick (see SeriesData::firstPeriod).
    out.periodCount = n_periods;
    int from = out.firstPeriod;
    if (from < 0) from = 0;
    if (from > n_periods) from = n_periods;
    const int count = n_periods - from;
    if (count == 0) { out.ok = true; return; }   // nothing new yet

    std::vector<float> values(static_cast<std::size_t>(count));
    int rc = -1;
    switch (ref.kind) {
    case ObjectRef::Kind::Node:
        rc = swmm_output_get_node_series(handle, objIdx, varCode,
                                          from, n_periods - 1, values.data());
        break;
    case ObjectRef::Kind::Link:
        rc = swmm_output_get_link_series(handle, objIdx, varCode,
                                          from, n_periods - 1, values.data());
        break;
    case ObjectRef::Kind::Subcatch:
        rc = swmm_output_get_subcatch_series(handle, objIdx, varCode,
                                              from, n_periods - 1, values.data());
        break;
    case ObjectRef::Kind::System:
        rc = swmm_output_get_system_series(handle, varCode,
                                            from, n_periods - 1, values.data());
        break;
    default:
        break;
    }
    if (rc != 0) {
        out.errorMessage = QStringLiteral("Engine returned error %1 from get_*_series").arg(rc);
        return;
    }

    // The cursor uses the recorded report clock. Use that same clock for
    // series points, including restarted or delayed-report output files.
    const double t0 = startDateJulian();
    if (!std::isfinite(t0)) {
        out.errorMessage = QStringLiteral("Missing start date");
        return;
    }

    std::vector<float> snowTimes;
    if(ref.kind==ObjectRef::Kind::Subcatch && varCode>=SWMM_OUT_SUBCATCH_SNOW_SWE) {
        snowTimes.resize(count);
        if(swmm_output_get_subcatch_series(handle,objIdx,SWMM_OUT_SUBCATCH_SNOW_TIME,from,n_periods-1,snowTimes.data())!=0) {
            out.errorMessage=QStringLiteral("Snow history has no accepted process clock");return;
        }
    }
    out.timesJulian.resize(static_cast<std::size_t>(count));
    out.values.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int p = from + i;
        double time = std::nan("");
        if (!snowTimes.empty()) time = t0 + snowTimes[i] / 86400.;
        else if (swmm_output_get_period_time(handle, p, &time) != 0 || !std::isfinite(time)) {
            out.timesJulian.clear();
            out.values.clear();
            out.errorMessage = QStringLiteral("Missing report time for period %1").arg(p);
            return;
        }
        out.timesJulian[i] = time;
        out.values[i]      = static_cast<double>(values[i]);
    }
    out.ok = true;
}

void SwmmOutRunLayer::getSeriesAt(const ObjectRef& ref,
                                  const ResultDescriptor& descriptor,
                                  SeriesData& out) const
{
    // Fixed attributes take the enum path unchanged.
    if (!descriptor.isSpecies()) {
        getSeriesAt(ref, descriptor.attr, out);
        return;
    }

    out.ok = false;
    out.errorMessage.clear();
    out.timesJulian.clear();
    out.values.clear();

    if (!m_layer || !m_layer->outputHandle()) {
        out.errorMessage = QStringLiteral("Result layer not available");
        return;
    }
    // NAME → index → POLLUT_BASE + index against the run's LIVE species
    // list (D-G1): a series saved against a reordered model resolves to
    // the right column or to a precise miss — never to a wrong column.
    const int varCode = speciesVariableCodeFor(
        descriptor.species, m_layer->speciesNames(), ref.kind);
    if (varCode < 0) {
        out.errorMessage =
            QStringLiteral("Run does not carry species '%1' for this "
                           "object kind")
                .arg(descriptor.species);
        return;
    }
    fetchSeriesByCode_(ref, varCode, out);
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
    auto result=plot::resultDescriptorsForKind(kind, species);
    result.erase(std::remove_if(result.begin(),result.end(),[&](const auto& d){
        return !d.isSpecies() && !supportsAttribute(d.attr);
    }),result.end());return result;
}

} // namespace openswmmvis::plot
