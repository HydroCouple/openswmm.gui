/**
 * @file      isublayerhost.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Mixin interface for layers that own a list of ISublayers.
 *
 *         Plan reference: RENDERING_OUTPUT_SUBLAYERS_PLAN.md §2 Decision 3
 *         (animation dispatch) and §3 (every results layer owns an ordered
 *         sublayer list).
 *
 *         Any layer that wants to participate in the sublayer architecture
 *         multi-inherits ISublayerHost alongside its existing base
 *         (OpenSWMMVisLayer). The host is the contract the rest of the
 *         system relies on:
 *
 *           1. The layer tree (Slice S3) calls sublayers() to render the
 *              third tier of the tree.
 *           2. The AnimationController (Slice S2 wiring, follow-up) calls
 *              dispatchAnimationTick(period) on every host whenever
 *              currentPeriodChanged fires. The host iterates its sublayer
 *              list and asks each sublayer whose isDynamic() returns true
 *              to invalidate — static sublayers' cached QSG geometry stays
 *              untouched (the perf-relevant cut).
 *           3. The legend (§J.5) collects per-sublayer LegendSymbolItems
 *              by walking sublayers() and concatenating their
 *              legendSymbolItems().
 *
 *         The interface is non-QObject (no signals, no Q_OBJECT) so it can
 *         be multiply inherited alongside an existing QObject-derived
 *         layer base without diamond problems. All signal traffic flows
 *         through the individual ISublayer instances.
 *
 *         Sublayer ownership is the host's responsibility — the interface
 *         does not prescribe a smart-pointer flavour; concrete hosts pick
 *         what fits their existing lifetime story (typically QObject
 *         parent-child for layers that already lean on Qt ownership).
 *
 *         Cross-slice: Slice S2 (sublayer host). Adopted by SWMMResultsLayer
 *         and SWMM2DResultsLayer in later sub-slices; the AnimationController
 *         dispatch wires through it in S2 final.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_ISUBLAYERHOST_H
#define OPENSWMM_RENDER_ISUBLAYERHOST_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \class ISublayerHost
 * \brief Mixin interface for layers that expose a list of ISublayers.
 */
class ISublayerHost
{
public:
    /** @brief Destroy the interface base; concrete hosts manage their sublayer ownership. */
    virtual ~ISublayerHost() = default;

    /*!
     * \brief The ordered list of sublayers owned by this layer.
     *
     *        Paint order is list order (bottom-up). Callers must NOT
     *        cache the returned list across structural changes — the
     *        host may add / remove / reorder sublayers in response to
     *        user actions, and the references become stale.
     *
     *        Returned references are non-owning; the host retains
     *        ownership of every sublayer.
     * @return Owned list of borrowed pointers in bottom-to-top paint order.
     */
    [[nodiscard]] virtual QList<ISublayer *> sublayers() const = 0;

    /*!
     * \brief Move the sublayer at \p from to position \p to in paint order.
     *
     *        Returns true on success.  Hosts that don't support user-driven
     *        reordering may leave the default (returns false / no-op).
     *        Successful implementations are expected to emit their parent
     *        layer's repaint signal so the scene refreshes.
     *
     *        Paint order = sublayers() index order (0 = bottom, painted
     *        first; size()-1 = top).
     * @param from Current zero-based index in sublayers().
     * @param to Requested zero-based destination index after the move.
     * @return True when supported and successful; the base implementation always
     * returns false and leaves the list unchanged, including for invalid indices.
     */
    virtual bool moveSublayer(int from, int to)
    {
        Q_UNUSED(from);
        Q_UNUSED(to);
        return false;
    }

    /*!
     * \brief Hook invoked by AnimationController on every time-step change.
     *
     *        The default implementation walks sublayers() and emits
     *        invalidated() on each entry whose isDynamic() returns true.
     *        Concrete hosts may override to batch the invalidation (e.g.
     *        coalesce multiple sublayer re-uploads into one QSG update
     *        cycle), but the contract is the same: static sublayers
     *        MUST NOT be invalidated by an animation tick.
     *
     *        \p period is the new animation period index, suitable for
     *        forwarding into the per-frame SublayerContext built by the
     *        QSG renderer on its next updatePaintNode() call. The default
     *        implementation does not stash it — the renderer reads the
     *        authoritative value from AnimationController when building
     *        the context — but overrides may use it for per-host caching.
     * @param period New animation period index; the default implementation ignores
     * its value and invalidates every non-null dynamic sublayer.
     */
    virtual void dispatchAnimationTick(int period)
    {
        Q_UNUSED(period);
        for (ISublayer *s : sublayers()) {
            if (s && s->isDynamic())
                s->invalidate();
        }
    }

    /** @brief Prepare a structurally validated snapshot before ID-based restoration.
     * @param j Full sublayer snapshot passed to loadSublayersFromJson().
     * Dynamic hosts can create/remove entries here before styles and ordering
     * are restored. The base implementation ignores the payload. */
    // Dynamic hosts create/remove entries before the ordinary ID-based restore.
    // A complete snapshot can therefore roll back newly added result sublayers.
    virtual void prepareSublayersJsonLoad(const QJsonObject &j) { Q_UNUSED(j); }

    /** @brief Migrate cross-sublayer data after styles and paint order are restored.
     * @param sublayersJson Original validated snapshot, including unknown IDs.
     * Invoked for both project load and style import; the base implementation is a no-op. */
    virtual void onSublayersJsonLoaded(const QJsonObject &sublayersJson)
    {
        Q_UNUSED(sublayersJson);
    }

    // ── Slice S6.1 — JSON persistence helpers ──────────────────────────
    //
    // Layers call these at their `.oswp` save/load points to round-trip
    // every sublayer's visibility / opacity / style bag. Schema:
    //
    //   {
    //     "sublayers": [
    //       { "id": "...", "visible": true, "opacity": 1.0,
    //         "style": { ... per-sublayer style JSON ... } },
    //       ...
    //     ]
    //   }
    //
    // Free static functions because the host interface is non-QObject
    // and we want them callable from layer save/load without
    // instantiating anything. Forward-compatible: unknown sublayer ids
    // in the JSON are silently skipped (the layer ships with whatever
    // sublayers it constructed in its ctor).

    /** @brief Snapshot sublayer identity, visibility, opacity, style and paint order.
     * @param host Host to inspect synchronously; its sublayers must remain alive during traversal.
     * @return Owned object containing a sublayers array in paint order. Null entries
     * are skipped and style is omitted when style() returns nullptr. */
    [[nodiscard]] static QJsonObject saveSublayersToJson(const ISublayerHost &host)
    {
        QJsonArray arr;
        for (const ISublayer *s : host.sublayers()) {
            if (!s)
                continue;
            QJsonObject row;
            row.insert(QStringLiteral("id"), s->id());
            row.insert(QStringLiteral("visible"), s->isVisible());
            row.insert(QStringLiteral("opacity"), s->opacity());
            if (auto *style = const_cast<ISublayer *>(s)->style())
                row.insert(QStringLiteral("style"), style->toJson());
            arr.append(row);
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("sublayers"), arr);
        return obj;
    }

    /*!
     * \brief Aggregates every visible sublayer's `legendSymbolItems()`
     *        into a single flat list (Slice S4 / §J.5 — legend-from-
     *        renderer rule, sublayer extension).
     *
     *        The existing layer-level `IFeatureRenderer::legendSymbolItems()`
     *        is unchanged; the layer tree / legend dock concatenates
     *        whatever the renderer reports with the output of this helper.
     *        Each returned item carries `sublayerId` (LegendSymbolItem
     *        field from S1.4) so right-click → "Edit Sublayer Style…"
     *        can route back to the originating sublayer.
     *
     *        Hidden sublayers contribute zero rows (they are not painted
     *        on screen, so they should not appear in the legend either).
     * @param host Host whose current sublayer list is traversed synchronously.
     * @return Concatenated rows from visible, non-null sublayers in paint order.
     * Rows are copied verbatim; this helper does not insert IDs or adjust opacity.
     */
    [[nodiscard]] static QList<LegendSymbolItem>
    aggregatedLegendSymbolItems(const ISublayerHost &host)
    {
        QList<LegendSymbolItem> out;
        for (const ISublayer *s : host.sublayers()) {
            if (!s || !s->isVisible())
                continue;
            out.append(s->legendSymbolItems());
        }
        return out;
    }

    /*!
     * \brief O(N) lookup of a sublayer by its stable id.
     *        Returns nullptr when not found. Used by the legend dock's
     *        right-click handler in S4 to find the sublayer whose style
     *        dialog should open when the user clicks a sublayer-tagged
     *        swatch.
     * @param host Host to inspect without transferring ownership.
     * @param id Exact, case-sensitive stable ID to find; no trimming occurs.
     * @return Borrowed first matching non-null pointer, or nullptr when absent.
     */
    [[nodiscard]] static ISublayer *findSublayer(const ISublayerHost &host, const QString &id)
    {
        for (ISublayer *s : host.sublayers())
            if (s && s->id() == id)
                return s;
        return nullptr;
    }

    /** @brief Restore a structurally valid sublayer snapshot by stable ID.
     * @param[in,out] host Host receiving prepare/finish hooks, style/visibility/opacity
     * updates, and attempted reordering. Concrete setters can emit repaint signals.
     * @param j Object with a sublayers array. Every row must be an object with a
     * unique nonempty string ID and, when present, an object-valued style.
     * Malformed structure returns before invoking any hook or setter. Valid input
     * invokes preparation, restores known IDs, attempts saved ordering, then invokes
     * the finish hook. Unknown IDs are skipped. Missing properties retain current
     * values; present visibility/opacity use Qt JSON conversions and concrete setters.
     * This operation has no status result or rollback if a concrete hook/setter fails. */
    static void loadSublayersFromJson(ISublayerHost &host, const QJsonObject &j)
    {
        if (!j.value(QStringLiteral("sublayers")).isArray())
            return;
        const QJsonArray arr = j.value(QStringLiteral("sublayers")).toArray();
        // Validate the complete structural snapshot before dynamic hosts remove
        // anything. Malformed/ambiguous rows must not become an empty inventory.
        QSet<QString> identities;
        for (const auto &value : arr) {
            if (!value.isObject())
                return;
            const auto row = value.toObject();
            const auto id = row.value(QStringLiteral("id"));
            if (!id.isString() || id.toString().isEmpty() || identities.contains(id.toString()))
                return;
            if (row.contains(QStringLiteral("style")) &&
                !row.value(QStringLiteral("style")).isObject())
                return;
            identities.insert(id.toString());
        }
        host.prepareSublayersJsonLoad(j);
        // Index host's current sublayers by id so we can apply rows out of order
        // and tolerate added/removed sublayers between schema versions.
        QHash<QString, ISublayer *> byId;
        for (ISublayer *s : host.sublayers())
            if (s)
                byId.insert(s->id(), s);

        for (const QJsonValue &val : arr) {
            const QJsonObject row = val.toObject();
            const QString id = row.value(QStringLiteral("id")).toString();
            auto it = byId.constFind(id);
            if (it == byId.constEnd())
                continue; // unknown sublayer id — skip

            ISublayer *s = it.value();
            if (row.contains(QStringLiteral("visible")))
                s->setVisible(row.value(QStringLiteral("visible")).toBool());
            if (row.contains(QStringLiteral("opacity")))
                s->setOpacity(row.value(QStringLiteral("opacity")).toDouble());
            if (row.contains(QStringLiteral("style")))
                if (auto *style = s->style())
                    style->fromJson(row.value(QStringLiteral("style")).toObject());
        }

        // Replay the saved paint order — JSON array index 0 = bottom of
        // the paint stack.  For each known id, ensure it ends up at the
        // intended target index by calling moveSublayer().  Hosts that
        // don't implement reorder return false and the order silently
        // stays as constructed.
        int target = 0;
        for (const QJsonValue &val : arr) {
            const QString id = val.toObject().value(QStringLiteral("id")).toString();
            const auto cur = host.sublayers();
            int found = -1;
            for (int i = 0; i < cur.size(); ++i)
                if (cur[i] && cur[i]->id() == id) {
                    found = i;
                    break;
                }
            if (found < 0)
                continue; // not in this host — skip silently
            if (found != target)
                host.moveSublayer(found, target);
            ++target;
        }

        // Give the host a chance to migrate style data that moved between
        // sublayers across schema versions (see onSublayersJsonLoaded).
        host.onSublayersJsonLoaded(j);
    }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_ISUBLAYERHOST_H
