/*!
 * \file   vjsourcesummary.cpp
 * \brief  See vjsourcesummary.h.
 */

#include "layers/vjsourcesummary.h"

#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QCoreApplication>
#include <QStringList>

namespace OpenSWMMVis::VirtualJunction
{

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("VirtualJunctionSourceSummary", text);
}

// Same per-node scans the property adapter's compound rows use
// (SWMMNodePropertyAdapter::inflowsRef / dwfRef / rdiiRef): the engine keeps
// one flat list per section, so a node's rows are found by filtering.
int inflowRowsFor(SWMM_Engine engine, int nodeIdx)
{
    int matched = 0;
    const int total = swmm_ext_inflow_count(engine);
    char consBuf[64], tsBuf[64], typeBuf[16], patBuf[64];
    for (int i = 0; i < total; ++i) {
        int ni = -1;
        double mf = 0.0, sf = 0.0, base = 0.0;
        if (swmm_ext_inflow_get(engine, i, &ni,
                                consBuf, sizeof(consBuf),
                                tsBuf,   sizeof(tsBuf),
                                typeBuf, sizeof(typeBuf),
                                &mf, &sf, &base,
                                patBuf,  sizeof(patBuf)) != SWMM_OK)
            continue;
        if (ni == nodeIdx) ++matched;
    }
    return matched;
}

int dwfRowsFor(SWMM_Engine engine, int nodeIdx)
{
    int matched = 0;
    const int total = swmm_dwf_count(engine);
    char consBuf[64], p1Buf[64], p2Buf[64], p3Buf[64], p4Buf[64];
    for (int i = 0; i < total; ++i) {
        int ni = -1;
        double avg = 0.0;
        if (swmm_dwf_get(engine, i, &ni,
                         consBuf, sizeof(consBuf),
                         &avg,
                         p1Buf, sizeof(p1Buf),
                         p2Buf, sizeof(p2Buf),
                         p3Buf, sizeof(p3Buf),
                         p4Buf, sizeof(p4Buf)) != SWMM_OK)
            continue;
        if (ni == nodeIdx) ++matched;
    }
    return matched;
}

int rdiiRowsFor(SWMM_Engine engine, int nodeIdx)
{
    int matched = 0;
    const int total = swmm_rdii_count(engine);
    char uhBuf[128];
    for (int i = 0; i < total; ++i) {
        int ni = -1;
        double area = 0.0;
        if (swmm_rdii_get(engine, i, &ni, uhBuf, sizeof(uhBuf), &area) != SWMM_OK)
            continue;
        if (ni == nodeIdx) ++matched;
    }
    return matched;
}

QString counted(int n, const char *one, const char *many)
{
    return (n == 1) ? tr(one) : tr(many).arg(n);
}

} // namespace

QString lateralSourceSummary(SWMM_Engine engine, int nodeIdx)
{
    if (!engine || nodeIdx < 0) return {};

    const int nInflows = inflowRowsFor(engine, nodeIdx);
    const int nDwf     = dwfRowsFor(engine, nodeIdx);
    const int nRdii    = rdiiRowsFor(engine, nodeIdx);

    QStringList subcatchments;
    const int ns = swmm_subcatch_count(engine);
    for (int s = 0; s < ns; ++s) {
        int outlet = -1;
        if (swmm_subcatch_get_outlet(engine, s, &outlet) != SWMM_OK
            || outlet != nodeIdx)
            continue;
        if (const char *id = swmm_subcatch_id(engine, s))
            subcatchments << QStringLiteral("\"%1\"").arg(QString::fromUtf8(id));
    }

    QStringList rows;
    if (nInflows > 0) rows << counted(nInflows, "1 inflow entry", "%1 inflow entries");
    if (nDwf > 0)     rows << counted(nDwf,     "1 DWF entry",    "%1 DWF entries");
    if (nRdii > 0)    rows << counted(nRdii,    "1 RDII entry",   "%1 RDII entries");

    QString text;
    if (!rows.isEmpty())
        text = tr("Re-fusing will remove its %1.")
                   .arg(rows.join(QStringLiteral(", ")));
    if (!subcatchments.isEmpty()) {
        if (!text.isEmpty()) text += QLatin1Char(' ');
        text += (subcatchments.size() == 1)
            ? tr("Subcatchment %1 will lose its outlet.").arg(subcatchments.first())
            : tr("Subcatchments %1 will lose their outlet.")
                  .arg(subcatchments.join(QStringLiteral(", ")));
    }
    return text;
}

} // namespace OpenSWMMVis::VirtualJunction
