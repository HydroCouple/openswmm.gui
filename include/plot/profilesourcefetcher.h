/**
 * @file      profilesourcefetcher.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Helpers that pull a `ProfileBuilder::SourceSeries` from a live
 *         `SWMMResultsLayer` for a given path.
 *
 *         Used by `ProfilePlotDialog` to materialize the per-source data
 *         the plot widget needs.  Kept separate from the dialog so the
 *         engine-output call sequence has one home and can be reused by
 *         Stage 5's Library Dock when re-binding a saved profile to a
 *         different results layer.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_SOURCE_FETCHER_H
#define PROFILE_SOURCE_FETCHER_H

#include "plot/profilebuilder.h"

#include <QString>

class SWMMResultsLayer;

namespace ProfileSourceFetcher
{

/*!
 * \brief Pulls `NodeHead` / `NodeDepth` (per path node) and `LinkVelocity`
 *        (per path link) from \p resultsLayer 's open `.out` file for the
 *        given \p path.  The returned series is sized exactly to the path
 *        — `nodeHead[i]` corresponds to `path.nodes[i]` and
 *        `linkVelocity[i]` to `path.links[i]`.
 *
 *        Resolves engine indices via name (`path.nodes[i].name` /
 *        `path.links[i].name`) into the layer's output-index maps.  Nodes
 *        or links not present in the output file get empty series.
 *        Uses a private output reader so worker reads cannot interfere
 *        with the map's file cursor during playback.
 *
 *        Returns an empty SourceSeries (with `periodCount = 0`) if the
 *        layer has no open output handle or its file cannot be reopened.
 * @param[in] resultsLayer Optional borrowed results layer; metadata/path-index lookups must remain valid/stable throughout the call.
 * @param[in] path Ordered node/link names; output rows use these path indices.
 * @param[in] sourceId Identity copied verbatim into the returned value.
 * @return Owned head/depth/velocity rows in raw output units plus copied report count/step/start. Private-reader open failure yields periodCount zero; individual bulk-read failures leave that row empty without failing the whole result.
 * @note Opens/closes its own live output reader, avoiding shared file-cursor interference. This protects the reader, not concurrent destruction/mutation of the supplied layer's metadata.
 */
[[nodiscard]] ProfileBuilder::SourceSeries fetch(SWMMResultsLayer *resultsLayer,
                                                 const ProfileBuilder::PathStatic &path,
                                                 const QString &sourceId);

/*!
 * \brief Live results: extends a series previously returned by \ref fetch
 *        for the same \p path with the periods \p resultsLayer has gained
 *        since (`[series.periodCount, totalTimeSteps())`), reading only that
 *        tail from the `.out`. Rows the initial fetch left empty (object
 *        absent from the output) stay empty. Updates `series.periodCount`.
 * \returns false when the layer has no handle, shrank, or \p series was not
 *          fetched for this path; true otherwise (including nothing new).
 * @param[in] resultsLayer Optional borrowed layer, with stable metadata and a readable results path.
 * @param[in] path Same node/link names and ordering as the initial fetch; only vector sizes are verified, not path identity.
 * @param[in,out] series Existing full rows and nonnegative periodCount. Each row of exactly the prior periodCount is extended on successful read; empty/foreign-sized rows are retained.
 * @note Uses a fresh private live reader. Individual read failures are silently skipped and the global periodCount still advances; true therefore does not guarantee every row gained data. Start/report-step/source identity metadata is retained. A false result from initial checks leaves series untouched.
 */
bool appendTail(SWMMResultsLayer *resultsLayer, const ProfileBuilder::PathStatic &path,
                ProfileBuilder::SourceSeries &series);

} // namespace ProfileSourceFetcher

#endif // PROFILE_SOURCE_FETCHER_H
