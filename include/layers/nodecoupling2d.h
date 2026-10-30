/**
 * @file      nodecoupling2d.h
 * @brief     A node's 1D↔2D coupling structure ([2D_NODE_COUPLING]) as the GUI edits it.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * A node's 1D↔2D coupling structure ([2D_NODE_COUPLING]) as the GUI edits
 * it. The engine holds the model (swmm_node_*_2d_coupling_*); every editor —
 * the property browser, the coupling selector, the remap dialog — reads and
 * writes these snapshots through SWMMModelLayer, so they stay in step.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LAYERS_NODECOUPLING2D_H
#define OPENSWMMVIS_LAYERS_NODECOUPLING2D_H

#include <openswmm/engine/openswmm_callbacks.h> // SWMM_Engine typedef

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace nodecoupling2d
{

/*! Coupling structure types, in the engine's SWMM_NodeCoupling2DType order. */
enum Type { None = 0, Manhole = 1, Grate = 2, Curb = 3, Open = 4, Outfall = 5 };

/*! One node's coupling as authored: whether it has a [2D_NODE_COUPLING]
 *  row, its type (effective — the class default when not authored) and the
 *  parameter tokens it authors (key → section token, e.g. "D" → "1.2"). */
struct State {
    QString node;
    bool authored = false;
    int type = Manhole;
    QMap<QString, QString> params;

    /**
     * @brief Compare authored coupling snapshots without accessing an engine.
     * @param[in] o Other snapshot; parameter token text is compared exactly, not numerically normalized.
     * @return True only when node text, authored flag, effective type and entire parameter map agree.
     */
    bool operator==(const State &o) const
    {
        return node == o.node && authored == o.authored && type == o.type && params == o.params;
    }
    /**
     * @param[in] o Snapshot compared by value.
     * @return Negation of operator==(), including exact textual parameter differences.
     */
    bool operator!=(const State &o) const { return !(*this == o); }
};

/*! Every parameter key, in [2D_NODE_COUPLING] order.
 * @return Borrowed process-lifetime canonical parameter-key list in section order, shared by read()/write(); callers must not modify it.
 */
const QStringList &paramKeys();

/*! Section token / display name of a type ("MANHOLE" / "Manhole").
 * @param[in] type Type ordinal from None through Outfall.
 * @return Uppercase section keyword or empty for an unknown ordinal; no engine access.
 */
QString typeToken(int type);
/**
 * @param[in] type Type ordinal from None through Outfall.
 * @return Translated GUI label, including Not coupleable for None, or empty for an unknown ordinal.
 */
QString typeLabel(int type);

/*! Read \a node's state from the engine; false when the node is unknown.
 * @param[in] engine Borrowed stable engine; null fails. Serialize access with engine edits/simulation.
 * @param[in] node Exact node name resolved by the engine.
 * @param[out] out Required destination assigned only after node/type lookup succeeds; null or lookup/type failure leaves it untouched.
 * @return True with node identity, effective type/authorship and each successfully retrieved nonempty canonical parameter token. Individual parameter-query failures are silently omitted.
 * @note Tokens retain engine section representation/units with no GUI conversion. A fixed 128-byte buffer is used per key. No source writes or Qt signals occur.
 */
bool read(SWMM_Engine engine, const QString &node, State *out);

/*! The node's class default type (what it has without a row).
 * @param[in] engine Optional borrowed stable engine, accessed without ownership/locking.
 * @param[in] node Exact node name.
 * @return Engine class default when obtainable, otherwise Manhole, including null engine or unknown node. Does not report failures separately.
 */
int defaultType(SWMM_Engine engine, const QString &node);

/*! The types \a node may take: an outfall NONE / OUTFALL, a storage node
 *  NONE / MANHOLE / GRATE / CURB / OPEN, any other node NONE / MANHOLE /
 *  GRATE / CURB. Empty for an unknown node.
 * @param[in] engine Optional borrowed stable engine; null fails lookup.
 * @param[in] node Exact node name.
 * @return Owned allowed ordinal list based only on node class, or empty for failed name/type lookup. Does not inspect authored parameters or runtime editability.
 */
QList<int> allowedTypes(SWMM_Engine engine, const QString &node);

/*! Make the engine hold exactly \a s: no row when !authored, otherwise the
 *  type and only the listed parameters. On failure \a error names the
 *  rejected type or key; the node may then be partly written.
 * @param[in] engine Borrowed editable stable engine; null fails without assigning error. Serialize with other engine operations.
 * @param[in] s Snapshot whose node selects the target. Unauthored resets the row; authored sets type then every canonical key, clearing omitted/empty tokens. Unknown map keys are ignored. Tokens pass verbatim to engine validation in section units; no conversion occurs.
 * @param[out] error Optional message set for unknown node, rejected type or rejected nonempty parameter. Reset failure/null engine and failed empty-token clearing have no explanatory assignment; prior text can remain.
 * @return True for successful reset or all nonempty parameter writes accepted. False can leave the type/earlier parameters partly written; failures of empty-token clears are ignored. No rollback or Qt/model-change notification is performed.
 */
bool write(SWMM_Engine engine, const State &s, QString *error = nullptr);

} // namespace nodecoupling2d

#endif // OPENSWMMVIS_LAYERS_NODECOUPLING2D_H
