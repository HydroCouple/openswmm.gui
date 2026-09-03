/*!
 * \file   profilesourcefetcher.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "plot/profilesourcefetcher.h"

#include "layers/swmmresultslayer.h"

#include <openswmm/engine/openswmm_output.h>

namespace ProfileSourceFetcher
{

namespace {

// Pulls a full-range time-series for one (node, var) into the given row.
// Leaves the row empty when the output handle / index / period count is
// unusable so downstream consumers can detect "no data" via an empty row
// rather than mistake a zero-filled series for genuine zeros.
//
// Note: `end_period` in the engine API is INCLUSIVE — passing
// `periodCount` instead of `periodCount - 1` asks the engine for one more
// value than the buffer can hold and trips an out-of-range error that
// leaves the buffer uninitialised → bogus values rendered as the HGL.
void fillNodeSeries(SWMM_Output handle, int outputIdx, int var,
                    int periodCount, QVector<float> &row)
{
    row.clear();
    if (!handle || outputIdx < 0 || periodCount <= 0) return;
    row.resize(periodCount);
    if (swmm_output_get_node_series(handle, outputIdx, var,
                                    /*start=*/0,
                                    /*end inclusive=*/periodCount - 1,
                                    row.data()) != 0) {
        row.clear();
    }
}

void fillLinkSeries(SWMM_Output handle, int outputIdx, int var,
                    int periodCount, QVector<float> &row)
{
    row.clear();
    if (!handle || outputIdx < 0 || periodCount <= 0) return;
    row.resize(periodCount);
    if (swmm_output_get_link_series(handle, outputIdx, var,
                                    /*start=*/0,
                                    /*end inclusive=*/periodCount - 1,
                                    row.data()) != 0) {
        row.clear();
    }
}

} // namespace

ProfileBuilder::SourceSeries fetch(SWMMResultsLayer *resultsLayer,
                                   const ProfileBuilder::PathStatic &path,
                                   const QString &sourceId)
{
    ProfileBuilder::SourceSeries out;
    out.sourceId = sourceId;
    if (!resultsLayer) return out;

    SWMM_Output handle = resultsLayer->outputHandle();
    if (!handle) return out;

    const int periodCount = resultsLayer->totalTimeSteps();
    out.periodCount   = periodCount;
    out.reportStepSec = resultsLayer->reportStepSeconds();
    out.startTime     = resultsLayer->startDateTime();

    out.nodeHead    .resize(path.nodes.size());
    out.nodeDepth   .resize(path.nodes.size());
    out.linkVelocity.resize(path.links.size());

    for (int i = 0; i < path.nodes.size(); ++i) {
        const int idx = resultsLayer->nodeOutputIndex(path.nodes[i].name);
        fillNodeSeries(handle, idx, SWMM_OUT_NODE_HEAD,
                       periodCount, out.nodeHead[i]);
        fillNodeSeries(handle, idx, SWMM_OUT_NODE_DEPTH,
                       periodCount, out.nodeDepth[i]);
    }
    for (int i = 0; i < path.links.size(); ++i) {
        const int idx = resultsLayer->linkOutputIndex(path.links[i].name);
        fillLinkSeries(handle, idx, SWMM_OUT_LINK_VELOCITY,
                       periodCount, out.linkVelocity[i]);
    }
    return out;
}

namespace {

// Appends periods [start, periodCount) of one (object, var) to `row`. A row
// left empty by the initial fetch means the object is absent from the
// output — keep it empty rather than start a series mid-run. `getter` is
// swmm_output_get_node_series / _link_series.
template <typename Getter>
void appendRange(SWMM_Output handle, int outputIdx, int var,
                 int start, int periodCount, QVector<float> &row, Getter getter)
{
    if (!handle || outputIdx < 0 || periodCount <= start) return;
    if (start > 0 && row.size() != start) return;     // absent (empty) or foreign
    const int n = periodCount - start;
    QVector<float> tail(n);
    if (getter(handle, outputIdx, var, start, periodCount - 1, tail.data()) != 0)
        return;
    row.append(tail);
}

} // namespace

bool appendTail(SWMMResultsLayer *resultsLayer,
                const ProfileBuilder::PathStatic &path,
                ProfileBuilder::SourceSeries &series)
{
    if (!resultsLayer) return false;
    SWMM_Output handle = resultsLayer->outputHandle();
    if (!handle) return false;

    const int periodCount = resultsLayer->totalTimeSteps();
    const int start       = series.periodCount;
    if (periodCount < start) return false;              // not a growth
    if (series.nodeHead.size()     != path.nodes.size() ||
        series.nodeDepth.size()    != path.nodes.size() ||
        series.linkVelocity.size() != path.links.size())
        return false;                                   // not fetched for this path
    if (periodCount == start) return true;

    for (int i = 0; i < path.nodes.size(); ++i) {
        const int idx = resultsLayer->nodeOutputIndex(path.nodes[i].name);
        appendRange(handle, idx, SWMM_OUT_NODE_HEAD,  start, periodCount,
                    series.nodeHead[i],  swmm_output_get_node_series);
        appendRange(handle, idx, SWMM_OUT_NODE_DEPTH, start, periodCount,
                    series.nodeDepth[i], swmm_output_get_node_series);
    }
    for (int i = 0; i < path.links.size(); ++i) {
        const int idx = resultsLayer->linkOutputIndex(path.links[i].name);
        appendRange(handle, idx, SWMM_OUT_LINK_VELOCITY, start, periodCount,
                    series.linkVelocity[i], swmm_output_get_link_series);
    }
    series.periodCount = periodCount;
    return true;
}

} // namespace ProfileSourceFetcher
