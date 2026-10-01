// SPDX-License-Identifier: GPL-3.0-or-later
#include "output/tracedata.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <limits>

namespace openswmmvis::trace
{
QJsonObject Snapshot::toJson() const
{
    QJsonArray ns, ls;
    for (const auto &n : nodes)
        ns.append(QJsonObject{{"id", n.id},
                              {"type", n.type},
                              {"flags", n.flags},
                              {"geometry", n.hasGeometry},
                              {"x", n.point.x()},
                              {"y", n.point.y()}});
    for (const auto &l : links)
    {
        QJsonArray points;
        for (auto p : l.points)
            points.append(QJsonArray{p.x(), p.y()});
        ls.append(QJsonObject{{"id", l.id},
                              {"from", l.from},
                              {"to", l.to},
                              {"type", l.type},
                              {"length", l.length},
                              {"points", points}});
    }
    return {{"nodes", ns}, {"links", ls}, {"wkt", wkt}, {"modelFingerprint", modelFingerprint}};
}
Snapshot Snapshot::fromJson(const QJsonObject &j)
{
    Snapshot s;
    s.wkt = j["wkt"].toString();
    s.modelFingerprint = j["modelFingerprint"].toString();
    for (auto v : j["nodes"].toArray())
    {
        auto n = v.toObject();
        s.nodes.append({n["id"].toString(),
                        n["type"].toInt(),
                        n["flags"].toInt(),
                        {n["x"].toDouble(), n["y"].toDouble()},
                        n["geometry"].toBool()});
    }
    for (auto v : j["links"].toArray())
    {
        auto n = v.toObject();
        Link l{n["id"].toString(), n["from"].toInt(-1),    n["to"].toInt(-1),
               n["type"].toInt(),  n["length"].toDouble(), {}};
        for (auto p : n["points"].toArray())
        {
            auto a = p.toArray();
            if (a.size() == 2)
                l.points.append({a[0].toDouble(), a[1].toDouble()});
        }
        s.links.append(l);
    }
    return s;
}
bool Snapshot::valid(QString *error) const
{
    auto fail = [&](const QString &m)
    {
        if (error)
            *error = m;
        return false;
    };
    if (nodes.isEmpty())
        return fail(QStringLiteral("No nodes in the model snapshot."));
    QSet<QString> ids;
    for (const auto &n : nodes)
    {
        if (n.id.isEmpty() || ids.contains(n.id) || n.type < 0 || n.type > 3)
            return fail(QStringLiteral("Invalid or duplicate node ID/type."));
        ids.insert(n.id);
        if (n.hasGeometry && (!std::isfinite(n.point.x()) || !std::isfinite(n.point.y())))
            return fail(QStringLiteral("Invalid node coordinates."));
    }
    ids.clear();
    for (const auto &l : links)
    {
        if (l.id.isEmpty() || ids.contains(l.id) || l.from < 0 || l.to < 0 ||
            l.from >= nodes.size() || l.to >= nodes.size() || !std::isfinite(l.length) ||
            l.length < 0)
            return fail(QStringLiteral("Invalid link topology or length."));
        ids.insert(l.id);
        for (auto p : l.points)
            if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
                return fail(QStringLiteral("Invalid link coordinates."));
    }
    return true;
}
QString Result::title() const
{
    return dataset ? QStringLiteral("%1 — %2 %3")
                         .arg(dataset->label,
                              direction ? QStringLiteral("Upstream to")
                                        : QStringLiteral("Downstream from"),
                              dataset->snapshot.nodes.value(seed).id)
                   : QString();
}
QString statusText(int f)
{
    QStringList s;
    if (f & SWMM_TRACE_UNREACHABLE)
        s << QStringLiteral("Unreachable");
    if (f & SWMM_TRACE_NO_DIRECTION)
        s << QStringLiteral("Negligible net flow");
    if (f & SWMM_TRACE_REVERSAL)
        s << QStringLiteral("Flow reversals");
    if (f & SWMM_TRACE_TRAPPED)
        s << QStringLiteral("Trapped circulation");
    if (f & SWMM_TRACE_UNKNOWN_TIME)
        s << QStringLiteral("Unknown time");
    else if (f & SWMM_TRACE_PARTIAL_TIME)
        s << QStringLiteral("Partial time coverage");
    if (f & SWMM_TRACE_APPROXIMATE)
        s << QStringLiteral("Unresolved exchange/loss");
    return s.isEmpty() ? QStringLiteral("OK") : s.join(QStringLiteral("; "));
}
QJsonObject infoJson(const SWMM_TraceInfo &i)
{
    return {{"schema", i.schema_version},   {"algorithm", i.algorithm_version},
            {"nodes", i.node_count},        {"links", i.link_count},
            {"periods", i.periods},         {"units", i.source_flow_units},
            {"first", i.first_report_date}, {"last", i.last_report_date},
            {"duration", i.duration_s}};
}
SWMM_TraceInfo infoFromJson(const QJsonObject &j)
{
    return {j["schema"].toInt(),
            j["algorithm"].toInt(),
            j["nodes"].toInt(),
            j["links"].toInt(),
            j["periods"].toInt(),
            j["units"].toInt(),
            0,
            j["first"].toDouble(),
            j["last"].toDouble(),
            j["duration"].toDouble()};
}
QJsonObject optionsJson(const SWMM_TraceOptions &o)
{
    return {{"flowEpsilon", o.flow_epsilon_m3s},
            {"velocityEpsilon", o.velocity_epsilon_mps},
            {"dominance", o.reversal_dominance},
            {"tolerance", o.solver_tolerance},
            {"iterations", o.max_iterations}};
}
SWMM_TraceOptions optionsFromJson(const QJsonObject &j)
{
    SWMM_TraceOptions o;
    swmm_trace_default_options(&o);
    o.flow_epsilon_m3s = j["flowEpsilon"].toDouble(o.flow_epsilon_m3s);
    o.velocity_epsilon_mps = j["velocityEpsilon"].toDouble(o.velocity_epsilon_mps);
    o.reversal_dominance = j["dominance"].toDouble(o.reversal_dominance);
    o.solver_tolerance = j["tolerance"].toDouble(o.solver_tolerance);
    o.max_iterations = j["iterations"].toInt(o.max_iterations);
    return o;
}
QJsonObject summaryJson(const SWMM_TraceSummary &s)
{
    QJsonArray t;
    for (double x : s.terminal)
        t.append(x);
    return {{"terminal", t},
            {"accountingError", s.accounting_error},
            {"solverResidual", s.solver_residual},
            {"boundaryIn", s.boundary_in_m3},
            {"boundaryOut", s.boundary_out_m3},
            {"lateralIn", s.lateral_in_m3},
            {"withdrawal", s.withdrawal_m3},
            {"knownLoss", s.known_loss_m3},
            {"storageChange", s.storage_change_m3},
            {"partialResidual", std::isfinite(s.partial_balance_residual_m3)
                                    ? QJsonValue(s.partial_balance_residual_m3)
                                    : QJsonValue()},
            {"cyclic", s.cyclic},
            {"nodes", s.reached_nodes},
            {"links", s.reached_links}};
}
SWMM_TraceSummary summaryFromJson(const QJsonObject &j)
{
    SWMM_TraceSummary s{};
    auto t = j["terminal"].toArray();
    for (int i = 0; i < std::min(t.size(), qsizetype(SWMM_TRACE_TERMINAL_COUNT)); ++i)
        s.terminal[i] = t[i].toDouble();
    s.accounting_error = j["accountingError"].toDouble();
    s.solver_residual = j["solverResidual"].toDouble();
    s.boundary_in_m3 = j["boundaryIn"].toDouble();
    s.boundary_out_m3 = j["boundaryOut"].toDouble();
    s.lateral_in_m3 = j["lateralIn"].toDouble();
    s.withdrawal_m3 = j["withdrawal"].toDouble();
    s.known_loss_m3 = j["knownLoss"].toDouble();
    s.storage_change_m3 = j["storageChange"].toDouble();
    s.partial_balance_residual_m3 = j["partialResidual"].isDouble()
                                        ? j["partialResidual"].toDouble()
                                        : std::numeric_limits<double>::quiet_NaN();
    s.cyclic = j["cyclic"].toInt();
    s.reached_nodes = j["nodes"].toInt();
    s.reached_links = j["links"].toInt();
    return s;
}
SWMM_Trace createHandle(const Snapshot &s, const SWMM_TraceOptions &o, QString *error)
{
    if (!s.valid(error))
        return nullptr;
    QVector<QByteArray> ids;
    ids.reserve(s.nodes.size() + s.links.size());
    for (const auto &n : s.nodes)
        ids.append(n.id.toUtf8());
    for (const auto &l : s.links)
        ids.append(l.id.toUtf8());
    QVector<SWMM_TraceNodeInput> ns;
    QVector<SWMM_TraceLinkInput> ls;
    for (int i = 0; i < s.nodes.size(); ++i)
        ns.append({ids[i].constData(), s.nodes[i].type, s.nodes[i].flags});
    for (int i = 0; i < s.links.size(); ++i)
    {
        auto &l = s.links[i];
        ls.append({ids[s.nodes.size() + i].constData(), l.from, l.to, l.type, l.length});
    }
    SWMM_Trace h = nullptr;
    if (swmm_trace_create(ns.data(), ns.size(), ls.data(), ls.size(), &o, &h) != 0 && error)
        *error = QStringLiteral("Cannot initialize trace topology/options: %1")
                     .arg(QString::fromUtf8(swmm_trace_error(nullptr)));
    return h;
}
} // namespace openswmmvis::trace
