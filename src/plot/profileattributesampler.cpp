/**
 * @file      profileattributesampler.cpp
 * @brief     one generic result attribute (node depth / head / …, link flow / velocity / …) from
 *            an open .out file.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * See profileattributesampler.h for the contracts.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/profileattributesampler.h"

#include "layers/swmmresultslayer.h"
#include "plot/swmmoutrunlayer.h" // variableCodeFor — the code table

#include <openswmm/engine/openswmm_output.h>

#include <cmath>
#include <memory>
#include <limits>

namespace ProfileAttributeSampler
{

namespace
{

using openswmmvis::plot::ObjectRef;
using openswmmvis::plot::PlotAttribute;
using openswmmvis::plot::SwmmOutRunLayer;

// Same shape (and the same INCLUSIVE end_period gotcha) as the helpers in
// profilesourcefetcher.cpp: an unusable handle/index leaves the row EMPTY so
// "no data" never masquerades as a zero-filled series.
/**
 * @brief Fetch a full node/link variable row, using an inclusive final period.
 * @param[in] handle Borrowed output reader, exclusively accessed for this read.
 * @param[in] node True for node getter, false for link getter.
 * @param[in] outputIdx Nonnegative file-local object index.
 * @param[in] var Compatible engine variable code; negative is treated unavailable.
 * @param[in] periodCount Desired full period count; nonpositive leaves no row.
 * @param[out] row Cleared first, filled with raw floats on success, or left empty for unusable arguments/read failure.
 */
void fillSeries(SWMM_Output handle, bool node, int outputIdx, int var, int periodCount,
                QVector<float> &row)
{
    row.clear();
    if (!handle || outputIdx < 0 || var < 0 || periodCount <= 0)
        return;
    row.resize(periodCount);
    const int rc = node
                       ? swmm_output_get_node_series(handle, outputIdx, var,
                                                     /*start=*/0,
                                                     /*end inclusive=*/periodCount - 1, row.data())
                       : swmm_output_get_link_series(handle, outputIdx, var,
                                                     /*start=*/0,
                                                     /*end inclusive=*/periodCount - 1, row.data());
    if (rc != 0)
        row.clear();
}

/**
 * @brief Reduce a row to finite-only extrema without altering samples.
 * @param[in] row Raw samples; NaN/infinity are skipped.
 * @param[out] mn Minimum finite sample or NaN if none.
 * @param[out] mx Maximum finite sample or NaN if none.
 */
void envelope(const QVector<float> &row, float &mn, float &mx)
{
    mn = std::numeric_limits<float>::quiet_NaN();
    mx = std::numeric_limits<float>::quiet_NaN();
    for (float v : row) {
        if (!std::isfinite(v))
            continue;
        if (std::isnan(mn) || v < mn)
            mn = v;
        if (std::isnan(mx) || v > mx)
            mx = v;
    }
}

} // namespace

// isNodeAttribute / isTrackableAttribute are inline in the header — see
// the note there (keeps UI/tests free of this file's link dependencies).

namespace
{
/** @brief Fill per-path rows for one variable through a private reader on plan.resultsPath. */
AttributeProfile readRows(const ProfileSourceFetcher::FetchPlan &plan,
                          const ProfileBuilder::PathStatic &path, bool node, int var,
                          AttributeProfile out)
{
    const QVector<int> &outputIdx = node ? plan.nodeOutputIdx : plan.linkOutputIdx;
    const int count = node ? path.nodes.size() : path.links.size();
    if (!plan.open || outputIdx.size() != count)
        return out;
    // A private reader: the layer's own reader is used (and can be closed or
    // reopened) by the GUI thread while this runs on a worker.
    const QByteArray file = plan.resultsPath.toUtf8();
    const std::unique_ptr<void, decltype(&swmm_output_close)> reader(
        swmm_output_open_live(file.constData()), &swmm_output_close);
    SWMM_Output handle = reader.get();
    if (!handle)
        return out;

    out.periodCount = plan.periodCount;
    out.byPath.resize(count);
    out.minByPath.resize(count);
    out.maxByPath.resize(count);
    for (int i = 0; i < count; ++i) {
        fillSeries(handle, node, outputIdx[i], var, plan.periodCount, out.byPath[i]);
        envelope(out.byPath[i], out.minByPath[i], out.maxByPath[i]);
    }
    return out;
}
} // namespace

AttributeProfile fetch(const ProfileSourceFetcher::FetchPlan &plan,
                       const ProfileBuilder::PathStatic &path, PlotAttribute attr)
{
    AttributeProfile out;
    out.attribute = attr;
    out.isNodeAttribute = isNodeAttribute(attr);
    if (!isTrackableAttribute(attr))
        return out;
    const bool node = out.isNodeAttribute;
    const int var = SwmmOutRunLayer::variableCodeFor(attr, node ? ObjectRef::Kind::Node
                                                                : ObjectRef::Kind::Link);
    return readRows(plan, path, node, var, out);
}

AttributeProfile fetchSpecies(const ProfileSourceFetcher::FetchPlan &plan,
                              const ProfileBuilder::PathStatic &path, const QString &species,
                              bool nodeScope)
{
    AttributeProfile out;
    out.isNodeAttribute = nodeScope;
    if (species.isEmpty())
        return out;
    // -1 when the run doesn't carry the species — fillSeries then leaves
    // every row empty, never zero-filled or pointed at a wrong column.
    const int var = SwmmOutRunLayer::speciesVariableCodeFor(
        species, plan.speciesNames, nodeScope ? ObjectRef::Kind::Node : ObjectRef::Kind::Link);
    return readRows(plan, path, nodeScope, var, out);
}

/**
 * @brief Read a fixed node/link attribute along path-indexed object names. GUI thread only (reads
 *   the layer); a worker uses the FetchPlan overload.
 * @param[in] resultsLayer Optional borrowed layer.
 * @param[in] path Ordered node/link names, not modified.
 * @param[in] attr One of the eleven trackable quantities; an unsupported value yields no rows.
 * @return Owned raw-output-unit rows and finite-only extrema; null/unopened sources return no rows and individual missing/read-failed objects retain empty rows with NaN extrema.
 */
AttributeProfile fetch(SWMMResultsLayer *resultsLayer, const ProfileBuilder::PathStatic &path,
                       PlotAttribute attr)
{
    return fetch(ProfileSourceFetcher::plan(resultsLayer, path), path, attr);
}

AttributeProfile fetchSpecies(SWMMResultsLayer *resultsLayer,
                              const ProfileBuilder::PathStatic &path, const QString &species,
                              bool nodeScope)
{
    return fetchSpecies(ProfileSourceFetcher::plan(resultsLayer, path), path, species, nodeScope);
}

} // namespace ProfileAttributeSampler
