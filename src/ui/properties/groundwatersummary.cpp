/*!
 * \file   groundwatersummary.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/properties/groundwatersummary.h"

#include <QCoreApplication>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace {

bool hasGwfExpression(SWMM_Engine e, int subIdx, int type)
{
    char buf[512] = {};
    return swmm_subcatch_get_gwf_expression(e, subIdx, type, buf, sizeof(buf)) == SWMM_OK
           && buf[0] != '\0';
}

} // namespace

QString groundwaterSummary(SWMM_Engine engine, int subIdx)
{
    const QString none = QCoreApplication::translate("GroundwaterSummary", "(none)");
    if (!engine || subIdx < 0) return none;

    int aq = -1;
    if (swmm_subcatch_get_aquifer(engine, subIdx, &aq) != SWMM_OK || aq < 0)
        return none;
    const char *aqId = swmm_aquifer_id(engine, aq);
    if (!aqId || !*aqId) return none;

    QString node;
    int nd = -1;
    if (swmm_subcatch_get_gw_node(engine, subIdx, &nd) == SWMM_OK && nd >= 0)
        if (const char *id = swmm_node_id(engine, nd))
            node = QString::fromUtf8(id);
    if (node.isEmpty())
        node = QCoreApplication::translate("GroundwaterSummary", "(no node)");

    QString text = QStringLiteral("%1 → %2").arg(QString::fromUtf8(aqId), node);
    if (hasGwfExpression(engine, subIdx, SWMM_GWF_LATERAL)
        || hasGwfExpression(engine, subIdx, SWMM_GWF_DEEP))
        text += QCoreApplication::translate("GroundwaterSummary", " (custom)");
    return text;
}
