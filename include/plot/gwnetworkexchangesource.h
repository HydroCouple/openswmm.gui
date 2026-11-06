/**
 * @file      gwnetworkexchangesource.h
 * @brief     V3 — per-node aquifer ↔ 1D network exchange series from a saved
 *            2D results file.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * §2 (`GwNetworkExchangeSource`), D-V5 and V3. The engine saves
 * `/groundwater_node_exchange_cum` [time, bed]: the CUMULATIVE volume each
 * `[2D_AQUIFER_NODE]` bed has exchanged, positive out of the aquifer into the
 * node, with the beds named by the `node_names` attribute. This provider
 * serves two node descriptors for the comparison plot (through
 * `Mesh2DRunLayer`, so the series sit under the 2D run):
 *   - `gw_node_exchange_cum` — the cumulative volume as stored (m3);
 *   - `gw_node_exchange`     — the rate over each report interval,
 *     Δcumulative / Δt (m3/s), sampled at the END of the interval; the first
 *     report has no interval and no sample.
 * The sign is the dataset's (+ out of the aquifer) and the labels say so.
 * Per D-V5 the per-cell held `Mesh2_face_gw_node_exchange` is NOT used: it is
 * the last firing, not an interval mean, so the two differ by design.
 *
 * A node that is not in `node_names` has no bed: its series is refused as
 * missing, never returned as zeros.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_GWNETWORKEXCHANGESOURCE_H
#define OPENSWMMVIS_PLOT_GWNETWORKEXCHANGESOURCE_H

#include "plot/irunlayer.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <map>
#include <vector>

namespace openswmmvis::plot
{

class GwNetworkExchangeSource
{
public:
    /*! Descriptor key of the cumulative exchange series. */
    static constexpr const char *kCumulativeKey = "gw_node_exchange_cum";
    /*! Descriptor key of the report-interval rate series. */
    static constexpr const char *kRateKey = "gw_node_exchange";

    /*!
     * \brief Open \p h5Path and read the bed names and report times.
     * \returns false (error() set) when the file or its
     *          `groundwater_node_exchange_cum` names cannot be read.
     */
    bool open(const QString &h5Path);
    /** @return True after a successful open(). */
    bool isAvailable() const noexcept { return m_available; }
    /** @return Why the exchange is unavailable; empty when available. */
    const QString &error() const noexcept { return m_error; }
    /** @return Node names, one per stored bed column. */
    const QStringList &nodeNames() const noexcept { return m_nodes; }
    /** @return True when \p node has a bed column (exact ID match). */
    bool hasNode(const QString &node) const { return m_columns.contains(node); }

    /*! \brief The rate and cumulative node descriptors, labelled with the
     *  sign convention, in the file's volume unit (rate = unit/s). */
    QVector<ResultDescriptor> nodeDescriptors() const;
    /** @return True when \p d is one of the two exchange descriptors. */
    static bool isExchangeDescriptor(const ResultDescriptor &d);

    /*!
     * \brief Fill \p out with \p node's exchange series for descriptor \p d
     *        from `out.firstPeriod` on (times in SWMM DateTime days).
     *        A non-finite cumulative value is a NaN gap, and so is any rate
     *        that uses it.
     * \note out.ok is false with the reason for an unavailable file, an
     *       unknown descriptor, or a node without a bed (missing, not zero).
     */
    void series(const QString &node, const ResultDescriptor &d, SeriesData &out) const;

private:
    bool m_available = false;
    QString m_error;
    QString m_units;
    QStringList m_nodes;
    QHash<QString, int> m_columns;
    std::vector<double> m_times; ///< `/time`, SWMM DateTime days
    int m_frames = 0;            ///< stored exchange rows
    /*! The file is reopened per column read and never held open, so a re-run
     *  can truncate it (the app closes every handle on the old file first). */
    QString m_path;
    mutable std::map<int, std::vector<double>> m_cumulative; ///< per-column cache
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_GWNETWORKEXCHANGESOURCE_H
