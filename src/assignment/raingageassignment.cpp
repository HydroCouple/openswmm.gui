/*!
 * \file   raingageassignment.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "assignment/raingageassignment.h"

#include "core/gageassignment.h"
#include "core/swmmdatetime.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "mesh/naturalnbinterpolator.h"
#include "timeseries/timeseriesseriescommands.h"

#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUndoCommand>
#include <QtConcurrent/QtConcurrentMap>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_tables.h>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace openswmmvis::assignment::raingage {

namespace {

QString tr(const char *s)
{
    return QCoreApplication::translate("RainGageAssignment", s);
}
QString trn(const char *s, int n)
{
    return QCoreApplication::translate("RainGageAssignment", s, nullptr, n);
}

// Generated objects are named NNG_### (Natural-Neighbour Gage). The pattern is
// also how a later run RECOGNISES its own output, so it must stay stable.
const QString kGenPrefix = QStringLiteral("NNG_");

QString generatedGageName(int ordinal)
{
    return kGenPrefix + QStringLiteral("%1").arg(ordinal, 3, 10, QLatin1Char('0'));
}

bool isGeneratedName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^NNG_\\d{3,}(_\\d+)?$"));
    return re.match(name).hasMatch();
}

// Same 1e-7 quantisation NaturalNeighbourInterpolator uses to snap-dedupe its
// seeds, so the plan and the interpolator agree on which gages are distinct.
QPair<qint64, qint64> siteKey(const QPointF &p)
{
    return qMakePair(qRound64(p.x() * 1e7), qRound64(p.y() * 1e7));
}

QString str(const char *id)
{
    return id ? QString::fromUtf8(id) : QString();
}

QString rdiiMember(const QString &node) { return QStringLiteral("rdii:") + node; }

/*! Run fn(begin, end) over [0, n) in chunks on the global pool, counting
 *  progress and honouring cancellation. Results must be written by index so
 *  the outcome is independent of scheduling. */
template <class Fn>
void parallelChunks(int n, Progress *progress, Fn &&fn)
{
    constexpr int kChunk = 64;
    QVector<QPair<int, int>> ranges;
    for (int b = 0; b < n; b += kChunk)
        ranges.append({b, std::min(n, b + kChunk)});
    QtConcurrent::blockingMap(ranges, [&](const QPair<int, int> &r) {
        if (progress && progress->cancel.load())
            return;
        fn(r.first, r.second);
        if (progress)
            progress->done.fetch_add(r.second - r.first);
    });
}

bool cancelled(const Progress *progress)
{
    return progress && progress->cancel.load();
}

bool sameRows(UhGroup a, UhGroup b)
{
    if (a.rows.size() != b.rows.size() || a.decay.size() != b.decay.size())
        return false;
    // The engine may store a copy's rows in a different order than its source.
    const auto byKey = [](const UhGroup::Row &x, const UhGroup::Row &y) {
        return std::tie(x.month, x.response) < std::tie(y.month, y.response);
    };
    std::sort(a.rows.begin(), a.rows.end(), byKey);
    std::sort(b.rows.begin(), b.rows.end(), byKey);
    const auto byResp = [](const UhGroup::Decay &x, const UhGroup::Decay &y) {
        return x.response < y.response;
    };
    std::sort(a.decay.begin(), a.decay.end(), byResp);
    std::sort(b.decay.begin(), b.decay.end(), byResp);
    for (int i = 0; i < a.rows.size(); ++i)
    {
        const UhGroup::Row &x = a.rows[i], &y = b.rows[i];
        if (x.month != y.month || x.response != y.response || x.r != y.r || x.t != y.t
            || x.k != y.k || x.dmax != y.dmax || x.drecov != y.drecov || x.dinit != y.dinit)
            return false;
    }
    for (int i = 0; i < a.decay.size(); ++i)
    {
        const UhGroup::Decay &x = a.decay[i], &y = b.decay[i];
        if (x.response != y.response || x.kDep != y.kDep || x.k0 != y.k0 || x.kT != y.kT
            || x.tRef != y.tRef || x.thetaRec != y.thetaRec || x.tFreeze != y.tFreeze
            || x.snowOn != y.snowOn || x.snowT != y.snowT || x.snowDdf != y.snowDdf)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------

bool readSourceGage(SWMM_Engine eng, const GageSite &site, GageBlend::SourceGage *out,
                    QString *error)
{
    // One path for every gage, whatever its data source. The engine resolves the
    // series — rain type, the rain-file units factor, and the gage scale factor
    // are already applied — so nothing here needs to know whether the gage reads
    // a [TIMESERIES] table or an external rain file.
    int count = 0;
    if (swmm_gage_get_rainfall_series_count(eng, site.index, &count) != SWMM_OK
        || count <= 0)
    {
        int source = 0;
        swmm_gage_get_data_source(eng, site.index, &source);
        *error = (source == SWMM_GAGE_FILE)
            ? tr("Rain gage \"%1\" has no readable rainfall data. Its file "
                 "could not be read, or is in a format the engine does not "
                 "load — either way the gage contributes no rainfall to a "
                 "run, not just to this tool.").arg(site.name)
            : tr("Rain gage \"%1\" has no rainfall data.").arg(site.name);
        return false;
    }

    QVector<double> times(count), values(count);
    if (swmm_gage_get_rainfall_series(eng, site.index, times.data(), values.data(),
                                      count) != SWMM_OK)
    {
        *error = tr("Could not read the rainfall series for rain gage \"%1\".")
                     .arg(site.name);
        return false;
    }

    out->name = site.name;
    out->points.reserve(count);
    for (int k = 0; k < count; ++k)
    {
        const QDateTime when = openswmmvis::core::swmmDateTimeToQDateTime(times[k]);
        if (!when.isValid())
            continue;
        out->points.append({when.toSecsSinceEpoch(), values[k]});
    }
    if (out->points.isEmpty())
    {
        *error = tr("Rain gage \"%1\" has no readable rainfall entries.").arg(site.name);
        return false;
    }

    // Values are resolved intensity; only the recording interval is still
    // needed, to give each entry the boxcar width the engine applies.
    double interval = 3600.0;
    swmm_gage_get_rain_interval(eng, site.index, &interval);
    out->rainType    = GageBlend::RainType::Intensity;
    out->intervalSec = static_cast<qint64>(std::llround(interval));
    out->scaleFactor = 1.0;
    return true;
}

QMap<QString, UhGroup> readGroups(SWMM_Engine eng)
{
    QMap<QString, UhGroup> groups;
    char buf[256];
    for (int i = 0, n = swmm_hydrograph_group_count(eng); i < n; ++i)
        if (swmm_hydrograph_group_id(eng, i, buf, sizeof buf) == SWMM_OK)
            groups.insert(QString::fromUtf8(buf), {});

    for (int i = 0, n = swmm_hydrograph_count(eng); i < n; ++i)
    {
        UhGroup::Row r;
        if (swmm_hydrograph_get(eng, i, buf, sizeof buf, &r.month, &r.response, &r.r,
                                &r.t, &r.k, &r.dmax, &r.drecov, &r.dinit) == SWMM_OK)
            groups[QString::fromUtf8(buf)].rows.append(r);
    }
    char gbuf[256];
    for (int i = 0, n = swmm_hydrograph_gage_count(eng); i < n; ++i)
        if (swmm_hydrograph_get_gage(eng, i, buf, sizeof buf, gbuf, sizeof gbuf) == SWMM_OK)
            groups[QString::fromUtf8(buf)].gage = QString::fromUtf8(gbuf);
    for (int i = 0, n = swmm_rdii_decay_count(eng); i < n; ++i)
    {
        UhGroup::Decay d;
        if (swmm_rdii_decay_get(eng, i, buf, sizeof buf, &d.response, &d.kDep, &d.k0,
                                &d.kT, &d.tRef, &d.thetaRec, &d.tFreeze, &d.snowOn,
                                &d.snowT, &d.snowDdf) == SWMM_OK)
            groups[QString::fromUtf8(buf)].decay.append(d);
    }
    return groups;
}

// ---------------------------------------------------------------------------
// RDII apply command
// ---------------------------------------------------------------------------

/*! Unit-hydrograph side of an assignment: create group copies, set group gages,
 *  and move RDII entries onto their new groups. The engine has no "update
 *  entry" call, so a move is remove + re-add (the entry then sits at the end of
 *  [RDII]; order carries no meaning there). */
class AssignRdiiGagesCommand : public QUndoCommand
{
public:
    struct Move { QString node, from, to; double area = 0.0; };

    AssignRdiiGagesCommand(SWMMModelLayer *layer, QVector<NewGroup> created,
                           QMap<QString, UhGroup> groups,
                           QMap<QString, QString> newGages, QVector<Move> moves,
                           QUndoCommand *parent)
        : QUndoCommand(QObject::tr("Assign rain gages to RDII"), parent)
        , m_layer(layer), m_created(std::move(created)), m_groups(std::move(groups))
        , m_newGages(std::move(newGages)), m_moves(std::move(moves))
    {}

    void redo() override
    {
        SWMM_Engine eng = engine();
        if (!eng) return;
        for (const NewGroup &g : std::as_const(m_created))
            createCopy(eng, g);
        for (auto it = m_newGages.constBegin(); it != m_newGages.constEnd(); ++it)
            setGage(eng, it.key(), it.value());
        for (const Move &m : std::as_const(m_moves))
            moveEntry(eng, m.node, m.from, m.to, m.area);
        notify();
    }

    void undo() override
    {
        SWMM_Engine eng = engine();
        if (!eng) return;
        // Entries first: removing a group also deletes the RDII rows that
        // still reference it.
        for (int i = int(m_moves.size()) - 1; i >= 0; --i)
            moveEntry(eng, m_moves[i].node, m_moves[i].to, m_moves[i].from, m_moves[i].area);
        for (auto it = m_newGages.constBegin(); it != m_newGages.constEnd(); ++it)
            setGage(eng, it.key(), m_groups.value(it.key()).gage);
        for (int i = int(m_created.size()) - 1; i >= 0; --i)
            swmm_hydrograph_remove_group(eng, m_created[i].name.toUtf8().constData());
        notify();
    }

private:
    SWMM_Engine engine() const { return m_layer ? m_layer->engine() : nullptr; }

    void notify() { if (m_layer) m_layer->markHydrographsEdited(); }

    static void setGage(SWMM_Engine eng, const QString &group, const QString &gage)
    {
        swmm_hydrograph_set_gage(eng, group.toUtf8().constData(),
                                 gage.toUtf8().constData());
    }

    void createCopy(SWMM_Engine eng, const NewGroup &g) const
    {
        const UhGroup src = m_groups.value(g.source);
        const QByteArray name = g.name.toUtf8();
        for (const UhGroup::Row &r : src.rows)
        {
            swmm_hydrograph_set_rtk(eng, name.constData(), r.month, r.response, r.r, r.t, r.k);
            swmm_hydrograph_set_ia(eng, name.constData(), r.month, r.response, r.dmax,
                                   r.drecov, r.dinit);
        }
        for (const UhGroup::Decay &d : src.decay)
            swmm_rdii_decay_set(eng, name.constData(), d.response, d.kDep, d.k0, d.kT,
                                d.tRef, d.thetaRec, d.tFreeze, d.snowOn, d.snowT,
                                d.snowDdf);
        setGage(eng, g.name, g.gage);
    }

    static void moveEntry(SWMM_Engine eng, const QString &node, const QString &from,
                          const QString &to, double area)
    {
        const int nodeIdx = swmm_node_index(eng, node.toUtf8().constData());
        if (nodeIdx < 0) return;
        char buf[256];
        for (int i = 0, n = swmm_rdii_count(eng); i < n; ++i)
        {
            int ni = -1;
            double a = 0.0;
            if (swmm_rdii_get(eng, i, &ni, buf, sizeof buf, &a) != SWMM_OK) continue;
            if (ni != nodeIdx || QString::fromUtf8(buf) != from || a != area) continue;
            swmm_rdii_remove(eng, i);
            swmm_rdii_add(eng, nodeIdx, to.toUtf8().constData(), area);
            return;
        }
    }

    QPointer<SWMMModelLayer> m_layer;
    QVector<NewGroup>        m_created;
    QMap<QString, UhGroup>   m_groups;     ///< Snapshot: sources + old gages.
    QMap<QString, QString>   m_newGages;   ///< Existing group -> new gage.
    QVector<Move>            m_moves;
};

} // namespace

// ===========================================================================
// Step 1 — inputs
// ===========================================================================

QVector<GageSite> eligibleGages(SWMMModelLayer *layer, QStringList *unlocated,
                                QStringList *coincident)
{
    QVector<GageSite> sites;
    if (!layer || !layer->engine())
        return sites;

    QSet<QPair<qint64, qint64>> seen;
    const int n = layer->cachedGageCount();
    for (int i = 0; i < n; ++i)
    {
        double x = 0.0, y = 0.0;
        if (!layer->cachedGageCoord(i, &x, &y))
            continue;
        const QString name = str(swmm_gage_id(layer->engine(), i));
        if (name.isEmpty())
            continue;

        // (0,0) is the engine's "no [SYMBOLS] row" sentinel. The display cache
        // relocates such gages to the mean of every model vertex, which would
        // plant a phantom site in the middle of the network.
        if (x == 0.0 && y == 0.0)
        {
            if (unlocated) unlocated->append(name);
            continue;
        }

        // Coincident gages: the interpolator snap-dedupes its seeds and keeps
        // no record, so a duplicate would silently never carry weight. Drop it
        // here instead, deterministically by engine index, and say so.
        const auto key = siteKey(QPointF(x, y));
        if (seen.contains(key))
        {
            if (coincident) coincident->append(name);
            continue;
        }
        seen.insert(key);
        sites.append({i, name, QPointF(x, y)});
    }
    return sites;
}

Input gatherInput(SWMMModelLayer *layer, const Options &options)
{
    Input in;
    in.options = options;
    if (!layer || !layer->engine())
    {
        in.error = tr("No model is open.");
        return in;
    }
    SWMM_Engine eng = layer->engine();

    // Rain files are read once, at open. Nothing re-reads them afterwards, so a
    // gage whose path, station, or units were edited this session would still be
    // serving the PREVIOUS file's data. Refresh before planning anything.
    swmm_gage_reload_rain_files(eng);

    in.gages = eligibleGages(layer, &in.unlocated, &in.coincident);
    for (int i = 0, n = swmm_gage_count(eng); i < n; ++i)
        in.gageNames.insert(str(swmm_gage_id(eng, i)));

    // ── Subcatchments ───────────────────────────────────────────────────
    const int nSub = layer->cachedSubcatchCount();
    QVector<QString> subGage(nSub);
    for (int s = 0; s < nSub; ++s)
    {
        int g = -1;
        if (swmm_subcatch_get_gage(eng, s, &g) == SWMM_OK && g >= 0)
            subGage[s] = str(swmm_gage_id(eng, g));
    }
    if (options.subcatchments)
    {
        const QSet<QString> picked(options.selectedSubcatchments.begin(),
                                   options.selectedSubcatchments.end());
        for (int s = 0; s < nSub; ++s)
        {
            // Engine index → name directly: objectNameAt() takes a DISPLAY row,
            // which differs once the user reorders the category.
            const QString name = str(swmm_subcatch_id(eng, s));
            if (name.isEmpty() || (options.selectedOnly && !picked.contains(name)))
                continue;
            in.subcatchments.append({name, subGage[s], layer->cachedSubcatchVertices(s)});
        }
    }

    // ── RDII + unit-hydrograph groups ───────────────────────────────────
    in.groups = readGroups(eng);
    QSet<QString> groupsWithNodes;
    {
        const QSet<QString> picked(options.selectedNodes.begin(), options.selectedNodes.end());
        char buf[256];
        for (int i = 0, n = swmm_rdii_count(eng); i < n; ++i)
        {
            int ni = -1;
            double area = 0.0;
            if (swmm_rdii_get(eng, i, &ni, buf, sizeof buf, &area) != SWMM_OK || ni < 0)
                continue;
            RdiiInput r;
            r.node  = str(swmm_node_id(eng, ni));
            r.group = QString::fromUtf8(buf);
            r.area  = area;
            double x = 0.0, y = 0.0;
            layer->cachedNodeCoord(ni, &x, &y);
            r.pos = QPointF(x, y);
            r.inScope = options.rdii && (!options.selectedOnly || picked.contains(r.node));
            groupsWithNodes.insert(r.group);
            in.rdii.append(r);
        }
    }

    // ── What earlier runs generated, and who uses it ────────────────────
    for (const QString &g : std::as_const(in.gageNames))
        if (isGeneratedName(g))
            in.existingClaims.insert(g, {});
    if (!in.existingClaims.isEmpty())
    {
        for (int s = 0; s < nSub; ++s)
        {
            auto it = in.existingClaims.find(subGage[s]);
            if (it != in.existingClaims.end())
                it->append(str(swmm_subcatch_id(eng, s)));
        }
        for (const RdiiInput &r : std::as_const(in.rdii))
        {
            auto it = in.existingClaims.find(in.groups.value(r.group).gage);
            if (it != in.existingClaims.end())
                it->append(rdiiMember(r.node));
        }
        // A group no node uses still references its gage; never delete it.
        for (auto g = in.groups.constBegin(); g != in.groups.constEnd(); ++g)
        {
            if (groupsWithNodes.contains(g.key())) continue;
            auto it = in.existingClaims.find(g->gage);
            if (it != in.existingClaims.end())
                it->append(QStringLiteral("uh:") + g.key());
        }
    }

    // ── Source series (interpolated only) ───────────────────────────────
    if (options.method == Method::Interpolated && in.unlocated.isEmpty()
        && in.gages.size() >= 2)
    {
        in.sources.reserve(in.gages.size());
        for (int i = 0; i < in.gages.size(); ++i)
        {
            GageBlend::SourceGage s;
            if (!readSourceGage(eng, in.gages[i], &s, &in.error))
                return in;
            in.sources.append(s);
            double scf = 1.0;
            swmm_gage_get_snow_factor(eng, in.gages[i].index, &scf);
            in.scfMin = (i == 0) ? scf : std::min(in.scfMin, scf);
            in.scfMax = (i == 0) ? scf : std::max(in.scfMax, scf);
        }
    }
    return in;
}

// ===========================================================================
// Step 2 — the plan
// ===========================================================================

namespace {

/*! RDII side of a plan, shared by both methods: \p target is the gage each
 *  in-scope entry should get (index-aligned with input.rdii; empty = skip). */
void planRdii(const Input &in, const QVector<QString> &target,
              const QVector<QString> &detail, Plan &plan)
{
    // Demand per group: gage -> sewer area. Out-of-scope entries pin their
    // group's current gage, so a partial run never moves a node it was not
    // asked to touch.
    struct Demand { QMap<QString, double> area; bool pinned = false; };
    QMap<QString, Demand> demand;
    for (int i = 0; i < in.rdii.size(); ++i)
    {
        const RdiiInput &r = in.rdii[i];
        Demand &d = demand[r.group];
        if (!r.inScope) { d.pinned = true; continue; }
        if (!target[i].isEmpty())
            d.area[target[i]] += r.area;
    }

    QSet<QString> groupNames(in.groups.keyBegin(), in.groups.keyEnd());
    QMap<QPair<QString, QString>, QString> copyOf;   // (group, gage) -> group serving it
    for (auto it = demand.constBegin(); it != demand.constEnd(); ++it)
    {
        const QString &group = it.key();
        const UhGroup &g = in.groups.value(group);
        const Demand &d = it.value();
        if (d.area.isEmpty()) continue;

        // The group itself keeps the gage carrying the most sewer area (ties
        // toward the name that sorts first — QMap order); pinned groups keep
        // the gage they have.
        QString keep = g.gage;
        if (!d.pinned)
        {
            double best = -1.0;
            for (auto a = d.area.constBegin(); a != d.area.constEnd(); ++a)
                if (a.value() > best) { best = a.value(); keep = a.key(); }
            if (keep != g.gage)
                plan.groupGages.insert(group, keep);
        }
        copyOf.insert({group, keep}, group);

        bool split = false;
        for (auto a = d.area.constBegin(); a != d.area.constEnd(); ++a)
        {
            if (a.key() == keep) continue;
            split = true;
            // Reuse a copy that already serves this gage with identical rows;
            // otherwise the first free "<group>_<gage>[_n]".
            const QString base = group + QLatin1Char('_') + a.key();
            QString name = base;
            for (int n = 2;; ++n)
            {
                if (!groupNames.contains(name)) break;
                const UhGroup &other = in.groups.value(name);
                if (other.gage == a.key() && sameRows(other, g)) break;
                name = base + QStringLiteral("_%1").arg(n);
            }
            if (!in.groups.contains(name))
            {
                plan.newGroups.append({name, group, a.key()});
                groupNames.insert(name);
            }
            copyOf.insert({group, a.key()}, name);
        }
        if (split) ++plan.splitGroups;
    }

    for (int i = 0; i < in.rdii.size(); ++i)
    {
        const RdiiInput &r = in.rdii[i];
        if (!r.inScope || target[i].isEmpty()) continue;
        RowPlan row;
        row.kind     = RowPlan::Kind::Rdii;
        row.object   = r.node;
        row.oldGroup = r.group;
        row.oldGage  = in.groups.value(r.group).gage;
        row.newGage  = target[i];
        row.newGroup = copyOf.value({r.group, target[i]}, r.group);
        row.detail   = detail[i];
        if (row.newGroup != row.oldGroup)
            row.detail += (row.detail.isEmpty() ? QString() : QStringLiteral("; "))
                          + tr("%1 → %2").arg(row.oldGroup, row.newGroup);
        row.changed = row.newGage != row.oldGage || row.newGroup != row.oldGroup;
        if (row.changed) ++plan.changed;
        ++plan.scanned;
        plan.rows.append(row);
    }
}

QRectF extentOf(const Input &in)
{
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    bool any = false;
    const auto add = [&](const QPointF &p) {
        if (!any) { x0 = x1 = p.x(); y0 = y1 = p.y(); any = true; return; }
        x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x());
        y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y());
    };
    for (const SubcatchInput &s : in.subcatchments)
        for (const QPointF &p : s.ring) add(p);
    for (const RdiiInput &r : in.rdii) add(r.pos);
    return any ? QRectF(QPointF(x0, y0), QPointF(x1, y1)) : QRectF();
}

Plan nearestPlan(const Input &in, Progress *progress)
{
    Plan plan;
    QVector<QPointF> sites;
    for (const GageSite &g : in.gages) sites.append(g.pos);
    GageAssignment::ThiessenIndex index;
    index.build(sites, extentOf(in));

    struct Res { int winner = -1; double fraction = 0.0; };
    QVector<Res> res(in.subcatchments.size());
    parallelChunks(int(in.subcatchments.size()), progress, [&](int b, int e) {
        for (int i = b; i < e; ++i)
        {
            const QVector<QPointF> &ring = in.subcatchments[i].ring;
            if (ring.size() < 3) continue;   // no [Polygons] row
            res[i].winner = GageAssignment::areaMajorityGage(index.areaShares(ring),
                                                              &res[i].fraction);
        }
    });
    if (cancelled(progress)) { plan.error = tr("Cancelled."); return plan; }

    for (int i = 0; i < in.subcatchments.size(); ++i)
    {
        const SubcatchInput &s = in.subcatchments[i];
        ++plan.scanned;
        if (res[i].winner < 0) { ++plan.skipped; continue; }
        RowPlan row;
        row.object  = s.name;
        row.oldGage = s.oldGage;
        row.newGage = in.gages[res[i].winner].name;
        row.detail  = tr("%1 % of area").arg(res[i].fraction * 100.0, 0, 'f', 1);
        row.changed = row.newGage != row.oldGage;
        if (row.changed) ++plan.changed;
        plan.rows.append(row);
    }

    QVector<QString> target(in.rdii.size()), detail(in.rdii.size());
    for (int i = 0; i < in.rdii.size(); ++i)
    {
        if (!in.rdii[i].inScope) continue;
        const int w = index.nearestSite(in.rdii[i].pos);
        if (w >= 0) { target[i] = in.gages[w].name; detail[i] = tr("nearest gage"); }
    }
    planRdii(in, target, detail, plan);
    return plan;
}

Plan interpolatedPlan(const Input &in, Progress *progress)
{
    Plan plan;

    // An un-located gage is fatal here, not merely excluded: interpolation is
    // a statement about the whole network, and silently dropping a gage would
    // redistribute its rainfall onto its neighbours without the user knowing.
    if (!in.unlocated.isEmpty())
    {
        plan.error = tr("These rain gages have no map location: %1.\n\n"
                        "Interpolation needs every gage placed. Give them "
                        "coordinates, or delete them, and try again.")
                         .arg(in.unlocated.join(QStringLiteral(", ")));
        return plan;
    }
    if (in.gages.size() < 2)
    {
        plan.error = tr("Interpolation needs at least two located rain gages. "
                        "With one, use the nearest-gage method instead.");
        return plan;
    }

    // Snow-catch factors cannot be folded into a series: SCF applies only on
    // the snowfall branch and is conditioned on temperature.
    if (in.scfMax - in.scfMin > 1e-12)
        plan.warnings.append(
            tr("Snow catch factors differ across the gages (%1 to %2); the "
               "generated gages use the average. SCF cannot be blended into a "
               "series because it applies only to snowfall.")
                .arg(in.scfMin, 0, 'g', 4).arg(in.scfMax, 0, 'g', 4));
    const double blendedScf = 0.5 * (in.scfMin + in.scfMax);

    const GageBlend::PreparedSources prepared = GageBlend::prepare(in.sources);
    if (!prepared.error.isEmpty())
    {
        plan.error = prepared.error;
        return plan;
    }

    QVector<QPointF> sites;
    for (const GageSite &g : in.gages) sites.append(g.pos);
    mesh::NaturalNeighbourInterpolator interp;
    interp.setVariant(in.options.laplace
                          ? mesh::NaturalNeighbourInterpolator::Variant::Laplace
                          : mesh::NaturalNeighbourInterpolator::Variant::Sibson);
    QString buildErr;
    const bool haveNN = interp.build(sites, &buildErr);
    if (!haveNN)
        plan.warnings.append(
            tr("Natural-neighbour weighting is unavailable (%1); "
               "inverse-distance weighting was used throughout.").arg(buildErr));

    const double tol = in.options.tolerance;
    const int nGages = static_cast<int>(in.gages.size());

    // ── Weights per subcatchment (area average) and per RDII node (point) ──
    struct Res { GageAssignment::ClusterKey key; qint64 samples = 0, idw = 0; };
    const int nSub = int(in.subcatchments.size());
    const int nAll = nSub + int(in.rdii.size());
    QVector<Res> res(nAll);
    parallelChunks(nAll, progress, [&](int b, int e) {
        // The interpolator caches its walk start, so each chunk queries its
        // own copy.
        const mesh::NaturalNeighbourInterpolator local = interp;
        QVector<QPair<int, double>> w;
        for (int i = b; i < e; ++i)
        {
            QVector<QPointF> samples;
            if (i < nSub)
            {
                const QVector<QPointF> &ring = in.subcatchments[i].ring;
                if (ring.size() < 3) continue;
                // Equal-area samples, so a plain mean of the per-sample weight
                // vectors already IS the area average.
                samples = GageAssignment::samplePolygon(ring);
            }
            else
            {
                const RdiiInput &r = in.rdii[i - nSub];
                if (!r.inScope) continue;
                samples = {r.pos};
            }
            if (samples.isEmpty()) continue;
            QVector<double> dense(nGages, 0.0);
            for (const QPointF &p : std::as_const(samples))
            {
                ++res[i].samples;
                if (!haveNN || !local.weightsAt(p.x(), p.y(), w))
                {
                    ++res[i].idw;
                    w = GageAssignment::idwWeights(p, sites);
                }
                for (const QPair<int, double> &t : std::as_const(w))
                    if (t.first >= 0 && t.first < nGages)
                        dense[t.first] += t.second;
            }
            for (double &v : dense)
                v /= static_cast<double>(samples.size());
            res[i].key = GageAssignment::quantizeWeights(dense, tol);
        }
    });
    if (cancelled(progress)) { plan.error = tr("Cancelled."); return plan; }

    // ── Cluster (QMap: order follows the key, not scheduling) ───────────
    QMap<QString, QStringList>     members;
    QMap<QString, QVector<double>> clusterWeights;
    qint64 idwSamples = 0, totalSamples = 0;
    const auto addToCluster = [&](const GageAssignment::ClusterKey &key, const QString &m) {
        members[key.serialized].append(m);
        if (!clusterWeights.contains(key.serialized))
            clusterWeights[key.serialized] =
                GageAssignment::dequantizeWeights(key, tol, nGages);
    };
    // Detail lists the dominant contributors, so the preview stays readable
    // with a large gage network.
    const auto describe = [&](const QString &key) {
        QVector<QPair<double, QString>> byWeight;
        const QVector<double> &cw = clusterWeights[key];
        for (int i = 0; i < nGages; ++i)
            if (cw[i] > 0.0) byWeight.append({cw[i], in.gages[i].name});
        std::sort(byWeight.begin(), byWeight.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
        QStringList parts;
        for (int i = 0; i < byWeight.size() && i < 4; ++i)
            parts << QStringLiteral("%1 %2%").arg(byWeight[i].second)
                         .arg(byWeight[i].first * 100.0, 0, 'f', 0);
        if (byWeight.size() > 4) parts << QStringLiteral("…");
        return parts.join(QStringLiteral(", "));
    };

    QVector<RowPlan> rows;
    for (int i = 0; i < nSub; ++i)
    {
        const SubcatchInput &s = in.subcatchments[i];
        ++plan.scanned;
        idwSamples += res[i].idw;
        totalSamples += res[i].samples;
        if (res[i].key.isEmpty()) { ++plan.skipped; continue; }
        addToCluster(res[i].key, s.name);
        RowPlan row;
        row.object  = s.name;
        row.oldGage = s.oldGage;
        row.detail  = describe(res[i].key.serialized);
        row.newGage = res[i].key.serialized;   // placeholder; resolved below
        rows.append(row);
    }
    QVector<QString> rdiiKey(in.rdii.size()), rdiiDetail(in.rdii.size());
    for (int i = 0; i < in.rdii.size(); ++i)
    {
        const Res &r = res[nSub + i];
        if (!in.rdii[i].inScope || r.key.isEmpty()) continue;
        idwSamples += r.idw;
        totalSamples += r.samples;
        addToCluster(r.key, rdiiMember(in.rdii[i].node));
        rdiiKey[i] = r.key.serialized;
        rdiiDetail[i] = describe(r.key.serialized);
    }

    if (members.isEmpty())
    {
        plan.error = tr("Nothing in scope produced usable interpolation weights.");
        return plan;
    }

    // ── Idempotency: match clusters to gages a previous run created ──────
    // Matching is by MEMBER SET (subcatchments + RDII nodes), not by stored
    // metadata: TimeseriesProvider descriptions are never persisted, so a
    // marker written into one would not survive save/reload.
    QHash<QString, QString> claimOwner;   // sorted member list -> gage
    for (auto cit = in.existingClaims.constBegin(); cit != in.existingClaims.constEnd(); ++cit)
    {
        QStringList claim = cit.value();
        std::sort(claim.begin(), claim.end());
        claim.removeDuplicates();
        claimOwner.insert(claim.join(QChar(0x1f)), cit.key());
    }

    QSet<QString> reusedGages;
    QMap<QString, QString> keyToGageName;
    int nextOrdinal = 1;
    const auto freshName = [&]() {
        QString candidate;
        do {
            candidate = generatedGageName(nextOrdinal++);
        } while (in.gageNames.contains(candidate) || reusedGages.contains(candidate));
        return candidate;
    };

    // Blend every cluster from its key's dequantised weights, so a series is a
    // pure function of the key rather than of whichever member came first.
    const QStringList keys = members.keys();
    QVector<GageBlend::BlendResult> blends(keys.size());
    parallelChunks(int(keys.size()), nullptr, [&](int b, int e) {
        for (int i = b; i < e; ++i)
            blends[i] = GageBlend::blendPrepared(prepared, clusterWeights[keys[i]]);
    });

    for (int c = 0; c < keys.size(); ++c)
    {
        const QString &key = keys[c];
        QStringList group = members[key];
        std::sort(group.begin(), group.end());

        GeneratedGage gen;
        gen.key        = key;
        gen.members    = group;
        gen.snowFactor = blendedScf;

        const GageBlend::BlendResult &blended = blends[c];
        if (!blended.error.isEmpty())
        {
            plan.error = blended.error;
            return plan;
        }
        if (!blended.volumeOk())
        {
            // Abort everything: nothing has been mutated yet, and a silent
            // change in total rainfall is exactly what this gate exists for.
            plan.error = tr("Volume check failed for a generated gage: the "
                            "blended total is %1 against an expected %2 "
                            "(relative error %3). Nothing was changed.")
                             .arg(blended.blendedDepth, 0, 'g', 8)
                             .arg(blended.referenceDepth, 0, 'g', 8)
                             .arg(blended.relativeError, 0, 'g', 3);
            return plan;
        }
        gen.points      = blended.points;
        gen.intervalSec = blended.intervalSec;
        gen.relError    = blended.relativeError;

        // Reuse the gage a previous run gave this exact member set.
        const QString matched = claimOwner.value(group.join(QChar(0x1f)));
        if (!matched.isEmpty())
        {
            gen.gageName = matched;
            gen.isUpdate = true;
            reusedGages.insert(matched);
        }
        else
        {
            gen.gageName = freshName();
            gen.isNew    = true;
        }
        gen.seriesName = gen.gageName + QStringLiteral("_TS");
        keyToGageName.insert(key, gen.gageName);
        plan.generated.append(gen);
    }

    // Generated gages this run no longer needs, and which nothing else uses.
    QSet<QString> inRun;
    for (const RowPlan &r : std::as_const(rows)) inRun.insert(r.object);
    for (int i = 0; i < in.rdii.size(); ++i)
        if (!rdiiKey[i].isEmpty()) inRun.insert(rdiiMember(in.rdii[i].node));
    for (auto cit = in.existingClaims.constBegin(); cit != in.existingClaims.constEnd(); ++cit)
    {
        if (reusedGages.contains(cit.key())) continue;
        bool stillClaimed = false;
        for (const QString &m : cit.value())
            if (!inRun.contains(m)) { stillClaimed = true; break; }
        if (!stillClaimed)
            plan.staleGages.append(cit.key());
        else
            plan.warnings.append(
                tr("Generated gage %1 was kept — it is still assigned to "
                   "objects outside this run.").arg(cit.key()));
    }

    // Resolve the placeholder keys to real gage names.
    for (RowPlan &r : rows)
    {
        r.newGage = keyToGageName.value(r.newGage);
        r.changed = (r.newGage != r.oldGage);
        if (r.changed) ++plan.changed;
    }
    plan.rows = rows;

    QVector<QString> target(in.rdii.size());
    for (int i = 0; i < in.rdii.size(); ++i)
        if (!rdiiKey[i].isEmpty()) target[i] = keyToGageName.value(rdiiKey[i]);
    planRdii(in, target, rdiiDetail, plan);

    if (idwSamples > 0 && totalSamples > 0)
        plan.warnings.append(
            tr("%1 % of sample points fell outside the gage network and used "
               "inverse-distance weighting.")
                .arg(100.0 * double(idwSamples) / double(totalSamples), 0, 'f', 1));
    return plan;
}

} // namespace

Plan computePlan(const Input &in, Progress *progress)
{
    Plan plan;
    if (!in.error.isEmpty())
    {
        plan.error = in.error;
        return plan;
    }
    int rdiiInScope = 0;
    for (const RdiiInput &r : in.rdii) rdiiInScope += r.inScope ? 1 : 0;
    if (in.subcatchments.isEmpty() && rdiiInScope == 0)
    {
        plan.error = tr("Nothing is in scope: no subcatchments or RDII inflows "
                        "were selected.");
        return plan;
    }
    if (in.gages.isEmpty())
    {
        plan.error = tr("No rain gage has a map location. Place the gages on the "
                        "map (or give them [SYMBOLS] coordinates) first.");
        return plan;
    }
    if (progress)
    {
        progress->done = 0;
        progress->total = int(in.subcatchments.size())
                          + (in.options.method == Method::Interpolated ? rdiiInScope : 0);
    }

    plan = in.options.method == Method::Interpolated ? interpolatedPlan(in, progress)
                                                     : nearestPlan(in, progress);

    QStringList pre;
    if (!in.unlocated.isEmpty())
        pre << trn("%n gage(s) have no map location and were excluded.",
                   int(in.unlocated.size()));
    if (!in.coincident.isEmpty())
        pre << trn("%n gage(s) share a location with another and were excluded: %1.",
                   int(in.coincident.size()))
                   .arg(in.coincident.join(QStringLiteral(", ")));
    plan.warnings = pre + plan.warnings;
    if (plan.skipped > 0)
        plan.warnings.append(trn("%n subcatchment(s) were skipped for having no "
                                 "polygon.", plan.skipped));
    if (plan.splitGroups > 0)
        plan.warnings.append(
            trn("%n unit hydrograph group(s) serve nodes under different gages "
                "and were split.", plan.splitGroups));

    // A generated gage is a bookkeeping object, not a physical instrument: it
    // has no real location. Park them at the centre of the gage network so
    // they render somewhere sensible instead of at the origin.
    double cx = 0.0, cy = 0.0;
    for (const GageSite &s : in.gages) { cx += s.pos.x(); cy += s.pos.y(); }
    if (!in.gages.isEmpty())
        plan.parkAt = QPointF(cx / in.gages.size(), cy / in.gages.size());
    return plan;
}

// ===========================================================================
// Step 3 — apply
// ===========================================================================

QUndoCommand *makeApplyCommand(SWMMModelLayer *layer, MapCanvas *canvas,
                               const Plan &plan, const QString &text)
{
    if (!layer || !layer->engine() || !plan.error.isEmpty())
        return nullptr;

    bool any = !plan.generated.isEmpty() || !plan.staleGages.isEmpty()
               || !plan.groupGages.isEmpty() || !plan.newGroups.isEmpty();
    for (const RowPlan &r : plan.rows) any = any || r.changed;
    if (!any)
        return nullptr;

    // One BulkEditCommand parents every child, so redo AND undo run inside a
    // single SWMMModelLayer::BulkEdit scope: one geometryChanged at the end
    // instead of a full view refresh per generated gage and per object.
    // Children run in creation order on redo and in reverse on undo, and the
    // order is load-bearing: series exist before any gage points at one;
    // generated gages are tail-appended so rollbackTailGageAdd can pop them
    // LIFO; assignments move before stale gages are configured and removed, so
    // no cascade nullification ever fires.
    auto *macro = new BulkEditCommand(layer, text);
    void *layerHandle = layer;
    using namespace openswmmvis::timeseries;

    for (const GeneratedGage &g : plan.generated)
    {
        QVector<TimeseriesPoint> pts;
        pts.reserve(g.points.size());
        for (const GageBlend::SeriesPoint &p : g.points)
            // UTC on both sides: the sources were read as UTC calendar fields
            // (swmmDateTimeToQDateTime) and qDateTimeToSwmmDateTime encodes the
            // calendar fields it is given. Local time here shifted every
            // generated entry by the machine's UTC offset.
            pts.append({QDateTime::fromSecsSinceEpoch(p.t, QTimeZone::utc()), p.value});

        const QString note = tr("Generated by Assign Rain Gages — weights %1").arg(g.key);

        // Choose by whether the series exists RIGHT NOW, not by whether the gage
        // was reused: a reused gage whose series was deleted out from under it
        // still needs a create, and SetTimeseriesPoints would silently no-op.
        const bool seriesExists =
            swmm_table_index(layer->engine(), g.seriesName.toUtf8().constData()) >= 0;
        if (seriesExists)
            new SetTimeseriesPointsCommand(layerHandle, g.seriesName, pts, note, macro);
        else
            new AddTimeseriesCommand(layerHandle, g.seriesName, pts, QString(), note, macro);
    }

    for (const GeneratedGage &g : plan.generated)
        if (g.isNew)
            new AddGageCommand(layer, g.gageName, plan.parkAt.x(), plan.parkAt.y(),
                               canvas, macro);

    for (const GeneratedGage &g : plan.generated)
    {
        ConfigureGageCommand::Config cfg;
        cfg.dataSource  = SWMM_GAGE_TIMESERIES;
        cfg.timeseries  = g.seriesName;
        cfg.rainType    = SWMM_RAIN_INTENSITY;   // interval-independent by design
        cfg.intervalSec = static_cast<double>(g.intervalSec);
        cfg.scaleFactor = 1.0;                   // source factors already folded in
        cfg.snowFactor  = g.snowFactor;
        bool ok = false;
        const ConfigureGageCommand::Config old =
            ConfigureGageCommand::capture(layer, g.gageName, &ok);
        new ConfigureGageCommand(layer, g.gageName, cfg, ok ? old : cfg, canvas, macro);
    }

    {
        QStringList names, newGages, oldGages;
        for (const RowPlan &r : plan.rows)
        {
            if (r.kind != RowPlan::Kind::Subcatchment || !r.changed) continue;
            names << r.object;
            newGages << r.newGage;
            oldGages << r.oldGage;
        }
        if (!names.isEmpty())
            new AssignSubcatchGagesCommand(
                layer, names, newGages, oldGages,
                QObject::tr("Assign rain gage to %n subcatchment(s)", nullptr,
                            int(names.size())),
                canvas, macro);
    }

    {
        QVector<AssignRdiiGagesCommand::Move> moves;
        for (const RowPlan &r : plan.rows)
            if (r.kind == RowPlan::Kind::Rdii && r.newGroup != r.oldGroup)
                moves.append({r.object, r.oldGroup, r.newGroup, 0.0});
        if (!moves.isEmpty() || !plan.groupGages.isEmpty() || !plan.newGroups.isEmpty())
        {
            // Sewer areas and source rows come from the engine now, on the GUI
            // thread, rather than from the (possibly stale) plan input.
            SWMM_Engine eng = layer->engine();
            QHash<QPair<QString, QString>, QVector<double>> areas;   // (node, group)
            char buf[256];
            for (int i = 0, n = swmm_rdii_count(eng); i < n; ++i)
            {
                int ni = -1;
                double a = 0.0;
                if (swmm_rdii_get(eng, i, &ni, buf, sizeof buf, &a) == SWMM_OK && ni >= 0)
                    areas[{str(swmm_node_id(eng, ni)), QString::fromUtf8(buf)}].append(a);
            }
            QVector<AssignRdiiGagesCommand::Move> resolved;
            for (AssignRdiiGagesCommand::Move m : moves)
            {
                QVector<double> &pool = areas[{m.node, m.from}];
                if (pool.isEmpty()) continue;
                m.area = pool.takeFirst();
                resolved.append(m);
            }
            new AssignRdiiGagesCommand(layer, plan.newGroups, readGroups(eng),
                                       plan.groupGages, resolved, macro);
        }
    }

    for (const QString &stale : plan.staleGages)
    {
        // Snapshot the configuration first: DeleteObjectCommand restores only
        // a gage's name and coordinates, so undo would otherwise bring it back
        // stripped of its series, rain type, and interval.
        bool ok = false;
        const ConfigureGageCommand::Config old =
            ConfigureGageCommand::capture(layer, stale, &ok);
        if (ok)
            new ConfigureGageCommand(layer, stale, old, old, canvas, macro);
        new DeleteObjectCommand(layer, stale, DeleteObjectCommand::DeleteGage, canvas, macro);
        new DeleteTimeseriesCommand(layerHandle, stale + QStringLiteral("_TS"), macro);
    }
    return macro;
}

} // namespace openswmmvis::assignment::raingage
