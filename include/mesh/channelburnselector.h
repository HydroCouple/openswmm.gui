/**
 * @file      channelburnselector.h
 * @brief     Which conduits get burned, and their geometry — phase P1/P6 bridge of
 *            workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.1, §4.8, §16.4).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Which conduits get burned, and their geometry — phase P1/P6 bridge of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.1, §4.8, §16.4).
 *
 * WHAT THIS IS.  The one place that reads the MODEL: it walks the conduits,
 * decides which ones participate, and turns each one into a \ref mesh::ChannelInput
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
 * which the sampler fills from that very call.
 *
 * STREET is the exception and is gated on \ref mesh::BurnOptions::burnStreets alone.
 * The engine's isOpen() whitelist has classified a street both ways, and
 * tests/gui/test_xsectsampler.cpp pins that answer precisely because it moves;
 * deriving D-F ("streets opt-in, default off") from it would let a whitelist
 * change silently start burning every kerb line in the model.
 *
 * SECTION GEOMETRY.  IRREGULAR sections are rebuilt from the raw [TRANSECTS]
 * station/elevation pairs, NOT from `XsectSampler::outline()`, which mirrors
 * half-widths and would throw away exactly the asymmetry a natural channel is
 * burned for (§16.1). Analytic shapes come from the engine's own width ladder.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace mesh
{

/*! \brief One conduit's verdict, and its geometry when accepted. */
struct BurnCandidate {
    QString conduitId;
    int linkIndex = -1;
    bool accepted = false;
    QString reason;     ///< Why it was rejected; empty when accepted.
    ChannelInput input; ///< Ready for \ref buildBurnProfile. Only when accepted.
};

/**
 * @brief Snapshot selected readable conduit geometry into owned burn candidates on the
 * model-owning thread.
 * @param[in] eng Borrowed live BUILDING/OPENED engine, read only; null returns empty. Caller
 * excludes concurrent engine mutation/close and calls on the GUI/model-owning thread, not the
 * mesh worker.
 * @param[in] sel Borrowed all-open/explicit-ID/query selector, unchanged; exact IDs/query table
 * keys used.
 * @param[in] opt Borrowed options, unchanged; section gate honors burnStreets, other profile
 * options apply later.
 * @param[in] polylines Borrowed conduit-ID to mesh-CRS centerlines, unchanged; missing/<2 points
 * rejects. Coordinates must already be in the intended model/mesh horizontal frame; no
 * transform/finite audit here.
 * @param[in] si Metric-model flag used for sampler reconstruction; must match engine unit
 * convention. Node invert plus stored link-depth offset forms endpoint elevations in model
 * length units.
 * @param[in] rows Borrowed attribute maps used only for ByQuery, unchanged; missing row becomes
 * empty map.
 * @param[in,out] warnings Optional borrowed list, appended for empty query, not cleared or
 * retained.
 * @return Owned accepted/rejected records in engine link order for conduits with readable
 * type/nonempty ID. Unreadable/nonconduit/nameless entries are skipped, not reported. Accepted
 * means section validated, centerline has >=2 points and endpoint references resolve;
 * invert/offset getter statuses are not checked and zero defaults can survive failure. Does not
 * build a profile, mutate engine, retain handles in returned records or perform file writes;
 * allocates geometry copies/query/result storage.
 *
 * @details Resolve the burn set against the model.
 *
 *
 * Every conduit is returned, accepted or not, so the burn report can say why something was
 * skipped rather than leaving the user to guess.
 */
[[nodiscard]] QVector<BurnCandidate>
resolveBurnSet(SWMM_Engine eng, const BurnSelector &sel, const BurnOptions &opt,
               const QHash<QString, QVector<QPointF>> &polylines, bool si,
               const QHash<QString, QVariantMap> &rows = {}, QStringList *warnings = nullptr);

/**
 * @brief Reconstruct an eligible open conduit section from engine transect data or sampled
 * widths.
 * @param[in] eng Borrowed live stable engine, read only; null fails. Model-owning thread and
 * exclusive engine lifecycle access required; returned value retains no engine handle.
 * @param[in] linkIdx Zero-based link index with readable resolved cross-section; invalid index
 * fails.
 * @param[in] si Model metric flag used by sampler, must match engine convention.
 * @param[in] opt Borrowed options, unchanged; STREET requires burnStreets independently of open
 * whitelist.
 * @param[out] reason Optional borrowed diagnostic, set on recognized failure, not cleared on
 * success.
 * @return Owned section in model length units or empty/unusable geometry on failure. Rejects
 * closed, unresolved/nonpositive-depth sections. IRREGULAR copies asymmetric raw transect
 * stations plus modifiers; analytic sections use a 128-point cosine depth ladder and engine
 * widths. Caller validates final geometry with validateSection(); helper itself does not enforce
 * all station/finite constraints. No source/model mutation, coordinate transform, filesystem
 * access or retained input references.
 *
 * @details The section of one conduit, by the rule in §16.4.
 */
[[nodiscard]] SectionGeometry sectionForLink(SWMM_Engine eng, int linkIdx, bool si,
                                             const BurnOptions &opt, QString *reason);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNSELECTOR_H
