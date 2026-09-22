/*!
 * \file   channelburnnetwork.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * 1D network surgery for the channel burn-in (CHANNEL_BURN_IN_PLAN_2026-09-21.md
 * §6, phase P4). Pure decision-making; applying it is the GUI thread's job.
 */
#include "mesh/channelburnnetwork.h"

#include <QHash>
#include <QStringList>

namespace mesh {

int BurnNetwork::indexOfNode(const QString &id) const
{
    for (int i = 0; i < nodes.size(); ++i)
        if (nodes[i].id == id) return i;
    return -1;
}

QVector<BurnNodePlan> classifyBurnNodes(const BurnNetwork &net,
                                        const QSet<QString> &burnedLinkIds)
{
    QVector<int> burned(net.nodes.size(), 0);
    QVector<int> surviving(net.nodes.size(), 0);

    for (const BurnNetwork::Link &l : net.links)
    {
        const bool isBurned = burnedLinkIds.contains(l.id);
        for (const int n : {l.from, l.to})
        {
            if (n < 0 || n >= net.nodes.size()) continue;
            if (isBurned) ++burned[n];
            else          ++surviving[n];
        }
    }

    QVector<BurnNodePlan> out;
    for (int i = 0; i < net.nodes.size(); ++i)
    {
        if (burned[i] == 0) continue;

        BurnNodePlan p;
        p.nodeId         = net.nodes[i].id;
        p.burnedLinks    = burned[i];
        p.survivingLinks = surviving[i];

        if (surviving[i] >= 2)
        {
            p.role = BurnNodeRole::CoupledJunction;
            p.note = QStringLiteral("%1 links survive here; an outfall may carry only one, "
                                    "so it stays a junction and couples as one")
                         .arg(surviving[i]);
        }
        else if (surviving[i] == 1)
        {
            p.role = BurnNodeRole::Outfall;
            p.note = QStringLiteral("interface between the 1D network and the burned reach");
        }
        else if (net.nodes[i].hasExternalInflow)
        {
            p.role = BurnNodeRole::Outfall;
            p.note = QStringLiteral("headwater: no link survives, but runoff or an inflow "
                                    "still arrives and now reaches the mesh here");
        }
        else
        {
            p.role = BurnNodeRole::Removed;
            p.note = QStringLiteral("every link on it was burned and nothing else feeds it");
        }

        if (p.role == BurnNodeRole::Outfall && !net.nodes[i].isJunction)
            p.note += QStringLiteral("; converting a non-junction clears its type-specific data");

        out.append(p);
    }
    return out;
}

QStringList burnedLinksToRemove(const BurnNetwork &net, const QSet<QString> &burnedLinkIds)
{
    QStringList out;
    out.reserve(burnedLinkIds.size());
    for (const BurnNetwork::Link &l : net.links)
        if (burnedLinkIds.contains(l.id)) out.append(l.id);
    return out;
}

QString burnNodeRoleName(BurnNodeRole role)
{
    switch (role)
    {
    case BurnNodeRole::Untouched:       return QStringLiteral("untouched");
    case BurnNodeRole::Removed:         return QStringLiteral("removed");
    case BurnNodeRole::Outfall:         return QStringLiteral("outfall");
    case BurnNodeRole::CoupledJunction: return QStringLiteral("coupled junction");
    }
    return QString();
}

} // namespace mesh
