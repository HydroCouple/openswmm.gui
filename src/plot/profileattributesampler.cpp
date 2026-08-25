/*!
 * \file   profileattributesampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See profileattributesampler.h for the contracts.
 */
#include "plot/profileattributesampler.h"

#include "layers/swmmresultslayer.h"
#include "plot/swmmoutrunlayer.h"   // variableCodeFor — the code table

#include <openswmm/engine/openswmm_output.h>

#include <cmath>
#include <limits>

namespace ProfileAttributeSampler
{

namespace {

using openswmmvis::plot::ObjectRef;
using openswmmvis::plot::PlotAttribute;
using openswmmvis::plot::SwmmOutRunLayer;

// Same shape (and the same INCLUSIVE end_period gotcha) as the helpers in
// profilesourcefetcher.cpp: an unusable handle/index leaves the row EMPTY so
// "no data" never masquerades as a zero-filled series.
void fillSeries(SWMM_Output handle, bool node, int outputIdx, int var,
                int periodCount, QVector<float> &row)
{
    row.clear();
    if (!handle || outputIdx < 0 || var < 0 || periodCount <= 0) return;
    row.resize(periodCount);
    const int rc = node
        ? swmm_output_get_node_series(handle, outputIdx, var,
                                      /*start=*/0,
                                      /*end inclusive=*/periodCount - 1,
                                      row.data())
        : swmm_output_get_link_series(handle, outputIdx, var,
                                      /*start=*/0,
                                      /*end inclusive=*/periodCount - 1,
                                      row.data());
    if (rc != 0)
        row.clear();
}

void envelope(const QVector<float> &row, float &mn, float &mx)
{
    mn = std::numeric_limits<float>::quiet_NaN();
    mx = std::numeric_limits<float>::quiet_NaN();
    for (float v : row) {
        if (!std::isfinite(v)) continue;
        if (std::isnan(mn) || v < mn) mn = v;
        if (std::isnan(mx) || v > mx) mx = v;
    }
}

} // namespace

// isNodeAttribute / isTrackableAttribute are inline in the header — see
// the note there (keeps UI/tests free of this file's link dependencies).

AttributeProfile fetch(SWMMResultsLayer *resultsLayer,
                       const ProfileBuilder::PathStatic &path,
                       PlotAttribute attr)
{
    AttributeProfile out;
    out.attribute       = attr;
    out.isNodeAttribute = isNodeAttribute(attr);
    if (!resultsLayer || !isTrackableAttribute(attr)) return out;

    SWMM_Output handle = resultsLayer->outputHandle();
    if (!handle) return out;

    const int periodCount = resultsLayer->totalTimeSteps();
    out.periodCount = periodCount;

    const bool node = out.isNodeAttribute;
    const int  var  = SwmmOutRunLayer::variableCodeFor(
        attr, node ? ObjectRef::Kind::Node : ObjectRef::Kind::Link);

    const int count = node ? path.nodes.size() : path.links.size();
    out.byPath   .resize(count);
    out.minByPath.resize(count);
    out.maxByPath.resize(count);

    for (int i = 0; i < count; ++i) {
        const QString &name = node ? path.nodes[i].name : path.links[i].name;
        const int idx = node ? resultsLayer->nodeOutputIndex(name)
                             : resultsLayer->linkOutputIndex(name);
        fillSeries(handle, node, idx, var, periodCount, out.byPath[i]);
        envelope(out.byPath[i], out.minByPath[i], out.maxByPath[i]);
    }
    return out;
}

AttributeProfile fetchSpecies(SWMMResultsLayer *resultsLayer,
                              const ProfileBuilder::PathStatic &path,
                              const QString &species,
                              bool nodeScope)
{
    AttributeProfile out;
    out.isNodeAttribute = nodeScope;
    if (!resultsLayer || species.isEmpty()) return out;

    SWMM_Output handle = resultsLayer->outputHandle();
    if (!handle) return out;

    const int periodCount = resultsLayer->totalTimeSteps();
    out.periodCount = periodCount;

    // -1 when the run doesn't carry the species — fillSeries then leaves
    // every row empty, never zero-filled or pointed at a wrong column.
    const int var = SwmmOutRunLayer::speciesVariableCodeFor(
        species, resultsLayer->speciesNames(),
        nodeScope ? ObjectRef::Kind::Node : ObjectRef::Kind::Link);

    const int count = nodeScope ? path.nodes.size() : path.links.size();
    out.byPath   .resize(count);
    out.minByPath.resize(count);
    out.maxByPath.resize(count);

    for (int i = 0; i < count; ++i) {
        const QString &name = nodeScope ? path.nodes[i].name
                                        : path.links[i].name;
        const int idx = nodeScope ? resultsLayer->nodeOutputIndex(name)
                                  : resultsLayer->linkOutputIndex(name);
        fillSeries(handle, nodeScope, idx, var, periodCount, out.byPath[i]);
        envelope(out.byPath[i], out.minByPath[i], out.maxByPath[i]);
    }
    return out;
}

} // namespace ProfileAttributeSampler
