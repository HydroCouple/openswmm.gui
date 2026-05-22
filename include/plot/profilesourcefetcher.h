/*!
 * \file   profilesourcefetcher.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Helpers that pull a `ProfileBuilder::SourceSeries` from a live
 *         `SWMMResultsLayer` for a given path.
 *
 *         Used by `ProfilePlotDialog` to materialize the per-source data
 *         the plot widget needs.  Kept separate from the dialog so the
 *         engine-output call sequence has one home and can be reused by
 *         Stage 5's Library Dock when re-binding a saved profile to a
 *         different results layer.
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
 *        or links not present in the output file get zero-filled series.
 *
 *        Returns an empty SourceSeries (with `periodCount = 0`) if the
 *        layer has no open output handle.
 */
[[nodiscard]] ProfileBuilder::SourceSeries fetch(
    SWMMResultsLayer *resultsLayer,
    const ProfileBuilder::PathStatic &path,
    const QString &sourceId);

} // namespace ProfileSourceFetcher

#endif // PROFILE_SOURCE_FETCHER_H
