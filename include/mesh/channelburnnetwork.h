/**
 * @file      channelburnnetwork.h
 * @brief     1D network surgery for the channel burn-in — phase P4 of
 *            workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§6, D-A, D-B, D-J).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * 1D network surgery for the channel burn-in — phase P4 of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§6, D-A, D-B, D-J).
 *
 * WHAT THIS IS.  Given the network as plain data and the set of burned
 * conduits, decide what happens to every node they touch.  Deciding is a model
 * operation and is pure; APPLYING the decision needs `SWMMModelLayer` and
 * `MapUndoStack` and belongs on the GUI thread (§16.3) — the mesh worker may
 * not touch either.
 *
 * THE PREMISE (D-A).  A burned conduit's conveyance now lives in the 2D mesh,
 * so it leaves the 1D network; otherwise the reach is routed twice.  Every rule
 * below follows from that.
 *
 * AN OUTFALL MUST BE TERMINAL.  SWMM allows one link on an outfall — the reason
 * `InsertNodeSplitCommand` refuses OUTFALL outright.  So a node that keeps two
 * or more surviving links cannot become one (D-J): it stays a junction and is
 * coupled as a junction, which is a coupling the engine already provides.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_CHANNELBURNNETWORK_H
#define OPENSWMMVIS_MESH_CHANNELBURNNETWORK_H

#include <QSet>
#include <QString>
#include <QVector>

namespace mesh
{

/*! \brief What the burn does to one node. */
enum class BurnNodeRole {
    Untouched,      ///< Touches no burned conduit. Left alone.
    Removed,        ///< Everything it touched was burned, and nothing else feeds it.
    Outfall,        ///< Becomes a coupled outfall: exactly one link survives on it.
    CoupledJunction ///< Two or more links survive — outfall is illegal, so it stays a
                    ///<     junction and couples as one (D-J).
};

/*! \brief The 1D network as plain data: no engine handle, no layer. */
struct BurnNetwork {
    struct Node {
        QString id;
        /*! Something still delivers water here with the burned links gone — a
         *  subcatchment outlet, an `[INFLOWS]` series, a dry-weather pattern.
         *  It is what separates a headwater worth keeping from an orphan. */
        bool hasExternalInflow = false;
        /*! False for storage / divider / outfall. Converting one of those to an
         *  outfall clears its type-specific data, so the plan says so out loud
         *  in the report rather than discovering it later. */
        bool isJunction = true;
        bool preserve = false; ///< Storage, initial water, controls or other non-link references.
    };
    struct Link {
        QString id;
        int from = -1; ///< Index into \ref nodes.
        int to = -1;
        QString replacementError; ///< A reference which cannot safely follow a split/removal.
    };

    QVector<Node> nodes;
    QVector<Link> links;

    /**
     * @brief Find the first network node with an exact case-sensitive identifier match.
     * @param[in] id Borrowed identifier, unchanged; empty IDs may match empty stored IDs.
     * @return Zero-based node position or -1 if absent. Linear scan, duplicate IDs resolve to
     * first node; no network/engine mutation, allocation, reference validation or retained
     * input.
     */
    [[nodiscard]] int indexOfNode(const QString &id) const;
};

/*! \brief One node's fate, and why — the burn report's node half. */
struct BurnNodePlan {
    QString nodeId;
    BurnNodeRole role = BurnNodeRole::Untouched;
    int burnedLinks = 0;
    int survivingLinks = 0;
    QString note;
};

/**
 * @brief Plan roles for nodes incident to burned links without applying any model changes.
 * @param[in] net Borrowed plain-data network, unchanged; endpoint indices outside nodes are
 * ignored. Self-loops count each endpoint incidence twice; link IDs are matched literally and
 * need not be unique.
 * @param[in] burnedLinkIds Borrowed exact case-sensitive IDs, unchanged; unknown IDs have no
 * effect.
 * @return Owned plans in node order, omitting nodes with zero burned incidences. Non-junction or
 * preserve nodes always receive CoupledJunction role to retain original type/behavior. Other
 * touched nodes with >=2 surviving incidences couple as junctions; one survivor or external
 * inflow becomes Outfall; otherwise Removed. Notes describe the decision, not successful edits.
 * Does not inspect replacementError, validate references, convert node data or touch engine/undo
 * state. Allocates count/plan arrays.
 *
 * @details Classify every node against \p burnedLinkIds (§6, as refined below).
 *
 * | surviving | external inflow | role |
 * |---|---|---|
 * | >=2 | any | CoupledJunction: outfall cannot carry multiple surviving links. |
 * | 1 | any | Outfall: interface to the burned reach. |
 * | 0 | yes | Outfall: inflow-fed headwater delivers onto the mesh. |
 * | 0 | no | Removed: no surviving link or external inflow. |
 *
 * The last two rows refine the plan's "Terminal ⇒ OUTFALL". Under D-A a terminal node loses its
 * only link, so an outfall there is an orphan **unless** something external still feeds it — and
 * then it is exactly right, because its runoff reaches the channel through the coupling.
 * Recorded as D-L in §16.
 *
 * Non-junction/preserved nodes retain their type through CoupledJunction before the table rules.
 * Nodes are returned in the order they appear in \p net; untouched nodes are omitted.
 */
[[nodiscard]] QVector<BurnNodePlan> classifyBurnNodes(const BurnNetwork &net,
                                                      const QSet<QString> &burnedLinkIds);

/**
 * @brief List network link IDs selected for removal, retaining network order.
 * @param[in] net Borrowed plain network, unchanged; no endpoint or replacementError validation.
 * @param[in] burnedLinkIds Borrowed exact case-sensitive selection, unchanged; absent IDs
 * ignored.
 * @return Owned names of every matching link, including duplicates in net. No actual link/node
 * removal, engine access or retained references; allocates result strings/list.
 *
 * @details The links that leave the 1D network — \p burnedLinkIds that exist in
 * \p net, in network order.
 */
[[nodiscard]] QStringList burnedLinksToRemove(const BurnNetwork &net,
                                              const QSet<QString> &burnedLinkIds);

/**
 * @brief Map a planned node role to the report's human-readable English label.
 * @param[in] role Enumeration value to label; unknown cast values are tolerated.
 * @return Owned role text, or empty for unsupported value. Does not validate/apply a plan or
 * alter state.
 *
 * @details Human-readable role name for the report CSV.
 */
[[nodiscard]] QString burnNodeRoleName(BurnNodeRole role);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNNETWORK_H
