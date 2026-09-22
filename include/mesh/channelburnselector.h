/*!
 * \file   channelburnselector.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Which conduits get burned, and their geometry — phase P1/P6 bridge of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.1, §4.8, §16.4).
 *
 * WHAT THIS IS.  The one place that reads the MODEL: it walks the conduits,
 * decides which ones participate, and turns each one into a \ref ChannelInput
 * the rest of the burn can work from without an engine handle. Everything
 * downstream — profile, corridor, raster, lattice — is pure.
 *
 * WHERE IT RUNS.  The GUI thread, inside `collectInputs`. The mesh worker may
 * not touch the engine (§16.3), so it receives the resolved profiles, never the
 * predicate.
 *
 * THE OPEN-CHANNEL GATE.  `swmm_xsect_is_open()` takes an `SWMM_XSect` HANDLE,
 * so it cannot be called on `swmm_link_get_xsect` output — the plan's §4.1
 * step 1 does not typecheck. The gate is `XsectSampler::fullProps().open`,
 * which the sampler fills from that very call. A useful side effect: the engine
 * classifies a STREET as CLOSED, so D-F's "streets off by default" comes free,
 * and \ref BurnOptions::burnStreets is what opts back in.
 *
 * SECTION GEOMETRY.  IRREGULAR sections are rebuilt from the raw [TRANSECTS]
 * station/elevation pairs, NOT from `XsectSampler::outline()`, which mirrors
 * half-widths and would throw away exactly the asymmetry a natural channel is
 * burned for (§16.1). Analytic shapes come from the engine's own width ladder.
 */
#ifndef OPENSWMMVIS_MESH_CHANNELBURNSELECTOR_H
#define OPENSWMMVIS_MESH_CHANNELBURNSELECTOR_H

#include "mesh/channelburnprofile.h"

#include <openswmm/engine/openswmm_engine.h>

#include <QHash>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mesh {

/*! \brief One conduit's verdict, and its geometry when accepted. */
struct BurnCandidate
{
    QString      conduitId;
    int          linkIndex = -1;
    bool         accepted  = false;
    QString      reason;    ///< Why it was rejected; empty when accepted.
    ChannelInput input;     ///< Ready for \ref buildBurnProfile. Only when accepted.
};

/*!
 * \brief Resolve the burn set against the model.
 *
 * \param eng        An engine in BUILDING or OPENED state.
 * \param polylines  Conduit id → centreline in the MESH CRS, from the GUI's own
 *                   cache (`SWMMModelLayer::cachedLinkPolyline`). A conduit with
 *                   no entry is rejected rather than guessed at.
 * \param si         True when the model is metric — the sampler needs it.
 * \param rows       Conduit id → attribute row, for `Mode::ByQuery`. Ignored in
 *                   the other modes. Keys must match the attribute table's
 *                   column keys, which is what makes the filter syntax the one
 *                   the user already knows.
 *
 * Every conduit is returned, accepted or not, so the burn report can say why
 * something was skipped rather than leaving the user to guess.
 */
[[nodiscard]] QVector<BurnCandidate> resolveBurnSet(
    SWMM_Engine eng,
    const BurnSelector &sel,
    const BurnOptions &opt,
    const QHash<QString, QVector<QPointF>> &polylines,
    bool si,
    const QHash<QString, QVariantMap> &rows = {},
    QStringList *warnings = nullptr);

/*!
 * \brief The section of one conduit, by the rule in §16.4.
 *
 * \returns An empty geometry when the link has no burnable section; \p reason
 *          then says why.
 */
[[nodiscard]] SectionGeometry sectionForLink(SWMM_Engine eng, int linkIdx, bool si,
                                             const BurnOptions &opt, QString *reason);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNSELECTOR_H
