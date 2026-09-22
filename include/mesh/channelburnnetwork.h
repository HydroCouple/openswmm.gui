/*!
 * \file   channelburnnetwork.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#ifndef OPENSWMMVIS_MESH_CHANNELBURNNETWORK_H
#define OPENSWMMVIS_MESH_CHANNELBURNNETWORK_H

#include <QSet>
#include <QString>
#include <QVector>

namespace mesh {

/*! \brief What the burn does to one node. */
enum class BurnNodeRole
{
    Untouched,        ///< Touches no burned conduit. Left alone.
    Removed,          ///< Everything it touched was burned, and nothing else feeds it.
    Outfall,          ///< Becomes a coupled outfall: exactly one link survives on it.
    CoupledJunction   ///< Two or more links survive — outfall is illegal, so it stays a
                      ///<     junction and couples as one (D-J).
};

/*! \brief The 1D network as plain data: no engine handle, no layer. */
struct BurnNetwork
{
    struct Node
    {
        QString id;
        /*! Something still delivers water here with the burned links gone — a
         *  subcatchment outlet, an `[INFLOWS]` series, a dry-weather pattern.
         *  It is what separates a headwater worth keeping from an orphan. */
        bool    hasExternalInflow = false;
        /*! False for storage / divider / outfall. Converting one of those to an
         *  outfall clears its type-specific data, so the plan says so out loud
         *  in the report rather than discovering it later. */
        bool    isJunction = true;
    };
    struct Link
    {
        QString id;
        int     from = -1;   ///< Index into \ref nodes.
        int     to   = -1;
    };

    QVector<Node> nodes;
    QVector<Link> links;

    [[nodiscard]] int indexOfNode(const QString &id) const;
};

/*! \brief One node's fate, and why — the burn report's node half. */
struct BurnNodePlan
{
    QString      nodeId;
    BurnNodeRole role = BurnNodeRole::Untouched;
    int          burnedLinks    = 0;
    int          survivingLinks = 0;
    QString      note;
};

/*!
 * \brief Classify every node against \p burnedLinkIds (§6, as refined below).
 *
 * | surviving | external inflow | role |
 * |---|---|---|
 * | ≥ 2 | — | `CoupledJunction` — an outfall may not carry two links (D-J) |
 * | 1 | — | `Outfall` — the interface between the 1D network and the burned reach |
 * | 0 | yes | `Outfall` — a headwater that still receives runoff or an inflow, now
 *              delivering it onto the mesh |
 * | 0 | no | `Removed` — nothing on either side; converting it would leave an
 *              orphan outfall, which is the objection §6 already raises against
 *              converting interior nodes |
 *
 * The last two rows refine the plan's "Terminal ⇒ OUTFALL". Under D-A a terminal
 * node loses its only link, so an outfall there is an orphan **unless** something
 * external still feeds it — and then it is exactly right, because its runoff
 * reaches the channel through the coupling. Recorded as D-L in §16.
 *
 * Nodes are returned in the order they appear in \p net; untouched nodes are
 * omitted.
 */
[[nodiscard]] QVector<BurnNodePlan> classifyBurnNodes(const BurnNetwork &net,
                                                      const QSet<QString> &burnedLinkIds);

/*! \brief The links that leave the 1D network — \p burnedLinkIds that exist in
 *         \p net, in network order. */
[[nodiscard]] QStringList burnedLinksToRemove(const BurnNetwork &net,
                                              const QSet<QString> &burnedLinkIds);

/*! \brief Human-readable role name for the report CSV. */
[[nodiscard]] QString burnNodeRoleName(BurnNodeRole role);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNNETWORK_H
