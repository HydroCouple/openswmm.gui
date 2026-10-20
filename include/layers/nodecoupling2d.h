/*!
 * \file   nodecoupling2d.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * A node's 1D↔2D coupling structure ([2D_NODE_COUPLING]) as the GUI edits
 * it. The engine holds the model (swmm_node_*_2d_coupling_*); every editor —
 * the property browser, the coupling selector, the remap dialog — reads and
 * writes these snapshots through SWMMModelLayer, so they stay in step.
 */
#ifndef OPENSWMMVIS_LAYERS_NODECOUPLING2D_H
#define OPENSWMMVIS_LAYERS_NODECOUPLING2D_H

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace nodecoupling2d {

/*! Coupling structure types, in the engine's SWMM_NodeCoupling2DType order. */
enum Type { None = 0, Manhole = 1, Grate = 2, Curb = 3, Open = 4, Outfall = 5 };

/*! One node's coupling as authored: whether it has a [2D_NODE_COUPLING]
 *  row, its type (effective — the class default when not authored) and the
 *  parameter tokens it authors (key → section token, e.g. "D" → "1.2"). */
struct State
{
    QString node;
    bool    authored = false;
    int     type     = Manhole;
    QMap<QString, QString> params;

    bool operator==(const State &o) const {
        return node == o.node && authored == o.authored && type == o.type
            && params == o.params;
    }
    bool operator!=(const State &o) const { return !(*this == o); }
};

/*! Every parameter key, in [2D_NODE_COUPLING] order. */
const QStringList &paramKeys();

/*! Section token / display name of a type ("MANHOLE" / "Manhole"). */
QString typeToken(int type);
QString typeLabel(int type);

/*! Read \a node's state from the engine; false when the node is unknown. */
bool read(SWMM_Engine engine, const QString &node, State *out);

/*! The node's class default type (what it has without a row). */
int defaultType(SWMM_Engine engine, const QString &node);

/*! The types \a node may take: an outfall NONE / OUTFALL, a storage node
 *  NONE / MANHOLE / GRATE / CURB / OPEN, any other node NONE / MANHOLE /
 *  GRATE / CURB. Empty for an unknown node. */
QList<int> allowedTypes(SWMM_Engine engine, const QString &node);

/*! Make the engine hold exactly \a s: no row when !authored, otherwise the
 *  type and only the listed parameters. On failure \a error names the
 *  rejected type or key; the node may then be partly written. */
bool write(SWMM_Engine engine, const State &s, QString *error = nullptr);

} // namespace nodecoupling2d

#endif // OPENSWMMVIS_LAYERS_NODECOUPLING2D_H
