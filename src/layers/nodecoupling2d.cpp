/**
 * @file      nodecoupling2d.cpp
 * @brief     A node's 1D↔2D coupling structure ([2D_NODE_COUPLING]) as the GUI edits it.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "layers/nodecoupling2d.h"

#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QCoreApplication>

namespace nodecoupling2d
{

const QStringList &paramKeys()
{
    static const QStringList keys = {QStringLiteral("D"),
                                     QStringLiteral("PERIMETER"),
                                     QStringLiteral("AREA"),
                                     QStringLiteral("CHAMBER_AREA"),
                                     QStringLiteral("LID_OPEN_FRACTION"),
                                     QStringLiteral("CREST"),
                                     QStringLiteral("COEFFS"),
                                     QStringLiteral("CW"),
                                     QStringLiteral("CSW"),
                                     QStringLiteral("COI"),
                                     QStringLiteral("COO"),
                                     QStringLiteral("LAW"),
                                     QStringLiteral("SPREAD"),
                                     QStringLiteral("TAILWATER_RELAX")};
    return keys;
}

QString typeToken(int type)
{
    switch (type) {
    case None:
        return QStringLiteral("NONE");
    case Manhole:
        return QStringLiteral("MANHOLE");
    case Grate:
        return QStringLiteral("GRATE");
    case Curb:
        return QStringLiteral("CURB");
    case Open:
        return QStringLiteral("OPEN");
    case Outfall:
        return QStringLiteral("OUTFALL");
    }
    return {};
}

QString typeLabel(int type)
{
    switch (type) {
    case None:
        return QCoreApplication::translate("nodecoupling2d", "Not coupleable");
    case Manhole:
        return QCoreApplication::translate("nodecoupling2d", "Manhole");
    case Grate:
        return QCoreApplication::translate("nodecoupling2d", "Grate inlet");
    case Curb:
        return QCoreApplication::translate("nodecoupling2d", "Curb inlet");
    case Open:
        return QCoreApplication::translate("nodecoupling2d", "Open storage");
    case Outfall:
        return QCoreApplication::translate("nodecoupling2d", "Outfall");
    }
    return {};
}

bool read(SWMM_Engine engine, const QString &node, State *out)
{
    if (!engine || !out)
        return false;
    const int idx = swmm_node_index(engine, node.toUtf8().constData());
    if (idx < 0)
        return false;
    State s;
    s.node = node;
    int type = Manhole, authored = 0;
    if (swmm_node_get_2d_coupling_type(engine, idx, &type, &authored) != 0)
        return false;
    s.type = type;
    s.authored = authored != 0;
    for (const QString &key : paramKeys()) {
        char buf[128] = {};
        if (swmm_node_get_2d_coupling_param(engine, idx, key.toUtf8().constData(), buf,
                                            sizeof(buf)) != SWMM_OK)
            return false;
        if (buf[0] != '\0')
            s.params.insert(key, QString::fromUtf8(buf));
    }
    *out = s;
    return true;
}

int defaultType(SWMM_Engine engine, const QString &node)
{
    const int idx = engine ? swmm_node_index(engine, node.toUtf8().constData()) : -1;
    int type = Manhole;
    if (idx >= 0)
        swmm_node_get_2d_coupling_default_type(engine, idx, &type);
    return type;
}

QList<int> allowedTypes(SWMM_Engine engine, const QString &node)
{
    const int idx = engine ? swmm_node_index(engine, node.toUtf8().constData()) : -1;
    int kind = -1;
    if (idx < 0 || swmm_node_get_type(engine, idx, &kind) != 0)
        return {};
    if (kind == SWMM_NODE_OUTFALL)
        return {None, Outfall};
    if (kind == SWMM_NODE_STORAGE)
        return {None, Manhole, Grate, Curb, Open};
    return {None, Manhole, Grate, Curb};
}

bool write(SWMM_Engine engine, const State &s, QString *error)
{
    if (!engine)
        return false;
    QList<QByteArray> keys, values;
    QList<SWMM_NodeCoupling2DParam> params;
    for (auto it = s.params.cbegin(); it != s.params.cend(); ++it) {
        keys << it.key().toUtf8();
        values << it.value().toUtf8();
    }
    for (int i = 0; i < keys.size(); ++i)
        params << SWMM_NodeCoupling2DParam{keys[i].constData(), values[i].constData()};
    const int node = swmm_node_index(engine, s.node.toUtf8().constData());
    const bool ok =
        node >= 0 && swmm_node_replace_2d_coupling(engine, node, s.authored, s.type,
                                                   params.constData(), params.size()) == SWMM_OK;
    if (!ok && error)
        *error = QCoreApplication::translate("nodecoupling2d",
                                             "Invalid complete coupling record for %1.")
                     .arg(s.node);
    return ok;
}

} // namespace nodecoupling2d
