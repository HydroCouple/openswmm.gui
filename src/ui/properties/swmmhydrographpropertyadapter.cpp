/*!
 * \file   swmmhydrographpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/properties/swmmhydrographpropertyadapter.h"

#include <openswmm/engine/openswmm_inflows.h>

#include <set>
#include <string>

QString SWMMHydrographPropertyAdapter::displayLabelFor(
        const QString &property) const
{
    if (property == QLatin1String("name"))       return tr("Group Name");
    if (property == QLatin1String("gageName"))   return tr("Rain Gage");
    if (property == QLatin1String("rowCount"))   return tr("Row Count");
    if (property == QLatin1String("rowSummary")) return tr("Parameters");
    return {};
}

QString SWMMHydrographPropertyAdapter::gageName() const
{
    if (!m_engine || m_name.isEmpty()) return {};
    const int n = swmm_hydrograph_gage_count(m_engine);
    const QByteArray want = m_name.toUtf8();
    for (int i = 0; i < n; ++i) {
        char uh[256]   = {};
        char gage[256] = {};
        if (swmm_hydrograph_get_gage(m_engine, i,
                                      uh, sizeof(uh),
                                      gage, sizeof(gage)) != SWMM_OK)
            continue;
        if (want == uh) return QString::fromUtf8(gage);
    }
    return {};
}

// Slice BM.0-Browse-Edit (2026-05-25)
DataObjectRef SWMMHydrographPropertyAdapter::gageNameRef() const
{
    DataObjectRef r;
    r.engine      = m_engine;
    r.layer       = m_layer;
    r.kind        = DataObjectRef::RainGage;
    r.currentName = gageName();
    return r;
}

void SWMMHydrographPropertyAdapter::setGageNameRef(const DataObjectRef &ref)
{
    if (!m_engine || m_name.isEmpty()) return;
    // Empty currentName clears the assignment (engine accepts NULL).
    const QByteArray uh   = m_name.toUtf8();
    const QByteArray gage = ref.currentName.toUtf8();
    const char *gagePtr   = ref.currentName.isEmpty() ? nullptr : gage.constData();
    if (swmm_hydrograph_set_gage(m_engine, uh.constData(), gagePtr) == SWMM_OK)
        emit changed();
}

int SWMMHydrographPropertyAdapter::rowCount() const
{
    if (!m_engine || m_name.isEmpty()) return 0;
    const int n = swmm_hydrograph_count(m_engine);
    const QByteArray want = m_name.toUtf8();
    int matching = 0;
    for (int i = 0; i < n; ++i) {
        char buf[256] = {};
        int  month = 0, response = 0;
        double r = 0, t = 0, k = 0, dmax = 0, drecov = 0, dinit = 0;
        if (swmm_hydrograph_get(m_engine, i, buf, sizeof(buf),
                                 &month, &response, &r, &t, &k,
                                 &dmax, &drecov, &dinit) != SWMM_OK)
            continue;
        if (want == buf) ++matching;
    }
    return matching;
}

QString SWMMHydrographPropertyAdapter::rowSummary() const
{
    if (!m_engine || m_name.isEmpty()) return {};
    const int n = swmm_hydrograph_count(m_engine);
    const QByteArray want = m_name.toUtf8();
    std::set<int> months;
    std::set<int> responses;
    for (int i = 0; i < n; ++i) {
        char buf[256] = {};
        int  month = 0, response = 0;
        double r = 0, t = 0, k = 0, dmax = 0, drecov = 0, dinit = 0;
        if (swmm_hydrograph_get(m_engine, i, buf, sizeof(buf),
                                 &month, &response, &r, &t, &k,
                                 &dmax, &drecov, &dinit) != SWMM_OK)
            continue;
        if (want != buf) continue;
        months.insert(month);
        responses.insert(response);
    }
    if (months.empty()) return tr("(no parameter rows)");
    return tr("%1 month%2 × %3 response%4")
            .arg(months.size())
            .arg(months.size() == 1 ? QString() : QStringLiteral("s"))
            .arg(responses.size())
            .arg(responses.size() == 1 ? QString() : QStringLiteral("s"));
}
