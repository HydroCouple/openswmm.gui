/*!
 * \file   channelburnselector.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Which conduits get burned, and their geometry
 * (CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.1, §4.8, §16.4).
 */
#include "mesh/channelburnselector.h"

#include "core/queryparser.h"
#include "ui/sectionview/xsectsampler.h"

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QSet>
#include <QVariantMap>

#include <cmath>

using openswmmvis::QueryPredicate;
using openswmmvis::evaluateQuery;
using openswmmvis::parseQuery;
using openswmmvis::sectionview::XsectSampler;

namespace mesh {

namespace {

/*! Depth samples for the analytic ladder. Generous: a section is reconstructed
 *  once per burn, not per frame, and the invert is where accuracy is won. */
constexpr int kSectionSamples = 128;

QString linkId(SWMM_Engine eng, int idx)
{
    const char *id = swmm_link_id(eng, idx);
    return (id && *id) ? QString::fromUtf8(id) : QString();
}

/*! The [TRANSECTS] entry behind an IRREGULAR section, modifiers applied to a
 *  copy exactly as the engine and the GUI spell them. */
SectionGeometry transectSection(SWMM_Engine eng, int transectIdx, QString *reason)
{
    SectionGeometry g;
    const int n = swmm_transect_get_station_count(eng, transectIdx);
    if (n < 2)
    {
        if (reason) *reason = QStringLiteral("transect %1 has fewer than 2 stations")
                                  .arg(transectIdx);
        return g;
    }

    QVector<double> st, el;
    st.reserve(n);
    el.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        double s = 0.0, e = 0.0;
        if (swmm_transect_get_station(eng, transectIdx, i, &s, &e) != SWMM_OK) continue;
        st.append(s);
        el.append(e);
    }
    if (st.size() < 2)
    {
        if (reason) *reason = QStringLiteral("transect %1 stations are unreadable")
                                  .arg(transectIdx);
        return g;
    }

    double xLeft = qQNaN(), xRight = qQNaN();
    swmm_transect_get_bank_stations(eng, transectIdx, &xLeft, &xRight);
    double nL = qQNaN(), nR = qQNaN(), nC = qQNaN();
    swmm_transect_get_roughness(eng, transectIdx, &nL, &nR, &nC);
    double xf = 1.0, yf = 0.0, lf = 1.0;
    swmm_transect_get_modifiers(eng, transectIdx, &xf, &yf, &lf);

    // A transect with both bank stations equal has no overbanks; leave them
    // unset rather than clipping the corridor to nothing.
    if (std::isfinite(xLeft) && std::isfinite(xRight) && xLeft >= xRight)
    { xLeft = qQNaN(); xRight = qQNaN(); }

    return sectionFromTransect(st, el, xLeft, xRight, nL, nC, nR, xf, yf);
}

} // namespace

SectionGeometry sectionForLink(SWMM_Engine eng, int linkIdx, bool si,
                               const BurnOptions &opt, QString *reason)
{
    SectionGeometry empty;
    if (!eng) { if (reason) *reason = QStringLiteral("no engine"); return empty; }

    int shape = -1;
    double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    if (swmm_link_get_xsect(eng, linkIdx, &shape, &g1, &g2, &g3, &g4) != SWMM_OK)
    {
        if (reason) *reason = QStringLiteral("no cross-section");
        return empty;
    }

    // The open-channel gate. fullProps() is the only reachable spelling of
    // swmm_xsect_is_open() here, and the engine calls a STREET closed — which
    // is what makes D-F's opt-in the right shape.
    XsectSampler s = openswmmvis::sectionview::samplerForLink(eng, linkIdx, shape,
                                                              g1, g2, g3, g4, si);
    if (!s.isValid())
    {
        if (reason) *reason = QStringLiteral("cross-section geometry is unresolved "
                                             "(the engine is still BUILDING)");
        return empty;
    }
    const auto props = s.fullProps();
    const bool isStreet = (shape == SWMM_XSECT_STREET);
    if (!props.open && !(isStreet && opt.burnStreets))
    {
        if (reason)
            *reason = isStreet
                          ? QStringLiteral("street section (enable \"burn streets\" to include)")
                          : QStringLiteral("closed section — a culvert is a structure, not terrain");
        return empty;
    }
    if (!(props.yFull > 0.0))
    {
        if (reason) *reason = QStringLiteral("section has no depth");
        return empty;
    }

    // IRREGULAR keeps its authored asymmetry; everything else comes from the
    // engine's own width ladder, cosine-spaced so the invert is resolved.
    if (shape == SWMM_XSECT_IRREGULAR)
        return transectSection(eng, int(g1), reason);

    const QVector<double> depths = cosineDepthLadder(props.yFull, kSectionSamples);
    const QVector<double> widths = s.widthsAtDepths(depths);
    if (widths.size() != depths.size())
    {
        if (reason) *reason = QStringLiteral("the engine returned no width ladder");
        return empty;
    }
    const SectionGeometry g = sectionFromWidths(depths, widths);
    if (g.station.size() < 2 && reason)
        *reason = QStringLiteral("the section reconstructed to nothing");
    return g;
}

QVector<BurnCandidate> resolveBurnSet(SWMM_Engine eng,
                                      const BurnSelector &sel,
                                      const BurnOptions &opt,
                                      const QHash<QString, QVector<QPointF>> &polylines,
                                      bool si,
                                      const QHash<QString, QVariantMap> &rows,
                                      QStringList *warnings)
{
    QVector<BurnCandidate> out;
    if (!eng) return out;

    const QSet<QString> explicitIds(sel.conduitIds.cbegin(), sel.conduitIds.cend());

    QueryPredicate pred;
    bool haveQuery = false;
    if (sel.mode == BurnSelector::Mode::ByQuery)
    {
        pred = parseQuery(sel.query);
        haveQuery = true;
        if (sel.query.trimmed().isEmpty() && warnings)
            warnings->append(QStringLiteral("the burn query is empty; no conduit matches"));
    }

    const int n = swmm_link_count(eng);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        int type = -1;
        if (swmm_link_get_type(eng, i, &type) != SWMM_OK) continue;
        if (type != SWMM_LINK_CONDUIT) continue;

        BurnCandidate c;
        c.linkIndex = i;
        c.conduitId = linkId(eng, i);
        if (c.conduitId.isEmpty()) continue;

        // 1) Selection.
        switch (sel.mode)
        {
        case BurnSelector::Mode::AllOpen:
            break;
        case BurnSelector::Mode::ExplicitList:
            if (!explicitIds.contains(c.conduitId))
            { c.reason = QStringLiteral("not in the selection"); out.append(c); continue; }
            break;
        case BurnSelector::Mode::ByQuery:
            if (!haveQuery || sel.query.trimmed().isEmpty()
                || !evaluateQuery(pred, rows.value(c.conduitId)))
            { c.reason = QStringLiteral("does not match the filter"); out.append(c); continue; }
            break;
        }

        // 2) Section, and with it the open-channel gate.
        QString why;
        const SectionGeometry section = sectionForLink(eng, i, si, opt, &why);
        if (section.station.size() < 2)
        {
            c.reason = why.isEmpty() ? QStringLiteral("no burnable section") : why;
            out.append(c);
            continue;
        }
        const QString bad = validateSection(section);
        if (!bad.isEmpty()) { c.reason = bad; out.append(c); continue; }

        // 3) Centreline, from the GUI's cache. Never guessed at.
        const auto it = polylines.constFind(c.conduitId);
        if (it == polylines.constEnd() || it.value().size() < 2)
        {
            c.reason = QStringLiteral("no centreline geometry");
            out.append(c);
            continue;
        }

        // 4) End inverts: node invert plus the link's own offset, which is
        //    ALWAYS stored as a depth above the node invert whatever
        //    LINK_OFFSETS says — so it is never re-derived here.
        int n1 = -1, n2 = -1;
        if (swmm_link_get_from_node(eng, i, &n1) != SWMM_OK
            || swmm_link_get_to_node(eng, i, &n2) != SWMM_OK || n1 < 0 || n2 < 0)
        {
            c.reason = QStringLiteral("endpoints are unresolved");
            out.append(c);
            continue;
        }
        double z1 = 0.0, z2 = 0.0, o1 = 0.0, o2 = 0.0;
        swmm_node_get_invert_elev(eng, n1, &z1);
        swmm_node_get_invert_elev(eng, n2, &z2);
        swmm_link_get_offset_up(eng, i, &o1);
        swmm_link_get_offset_dn(eng, i, &o2);

        c.input.conduitId  = c.conduitId;
        c.input.centerline = it.value();
        c.input.zUp        = z1 + o1;
        c.input.zDn        = z2 + o2;
        c.input.section    = section;
        c.accepted         = true;
        out.append(c);
    }
    return out;
}

} // namespace mesh
