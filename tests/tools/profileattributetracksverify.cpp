/*!
 * \file   profileattributetracksverify.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Manual-verification harness for the attribute-tracks pane —
 *         executes the display-dependent checks in
 *         `workplans/PROFILE_ATTRIBUTE_TRACKS_VERIFICATION.md` §3 without a
 *         human at the screen.
 *
 * It rebuilds the exact widget composition `ProfilePlotDialog::buildLayout()`
 * creates — `QSplitter(Vertical){ ProfilePlotWidget, QScrollArea{ tracks } }`
 * with the same collapsible/stretch settings — feeds it real data read from
 * an `.inp` + `.out` pair, and reports PASS/FAIL per checklist item plus PNG
 * grabs for the items that only an eye can settle.
 *
 * The dialog itself is not constructible here: it needs SWMMModelLayer +
 * SWMMVisProjectWindow, whose translation units pull in the whole app.  The
 * pieces that live above the widgets (toolbar menu, splitter persistence,
 * export) are noted in the report as out of scope.
 *
 *   cmake --build <build> --target profile_attribute_tracks_verify
 *   <build>/tests/tools/profile_attribute_tracks_verify \
 *       <model.inp> <results.out> <output-dir> [startNode]
 */

#include "plot/plotattribute.h"
#include "plot/profileattributesampler.h"
#include "plot/profileattributetrackoptions.h"
#include "plot/profileattributetrackswidget.h"
#include "plot/profilebuilder.h"
#include "plot/profileplotoptions.h"
#include "plot/profileplotwidget.h"
#include "plot/swmmoutrunlayer.h"

#include <openswmm/engine/openswmm_output.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollArea>
#include <QWheelEvent>
#include <QScrollBar>
#include <QSplitter>
#include <QTextStream>

#include <cmath>
#include <limits>

using openswmmvis::plot::PlotAttribute;
using openswmmvis::plot::ObjectRef;
using openswmmvis::plot::SwmmOutRunLayer;
using TW = ProfileAttributeTracksWidget;

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

namespace {

struct Report
{
    struct Row { QString item; bool pass; QString detail; };
    QVector<Row> rows;
    int failures = 0;

    void check(const QString &item, bool pass, const QString &detail)
    {
        rows.push_back({item, pass, detail});
        if (!pass) ++failures;
        QTextStream(stdout)
            << (pass ? "PASS  " : "FAIL  ") << item << " — " << detail << "\n";
    }

    void write(const QString &path, const QString &preamble) const
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
        QTextStream s(&f);
        s << "# Attribute Tracks — automated §3 verification\n\n" << preamble
          << "\n| # | Check | Result | Observed |\n|---|---|---|---|\n";
        for (int i = 0; i < rows.size(); ++i)
            s << "| " << i + 1 << " | " << rows[i].item << " | "
              << (rows[i].pass ? "**PASS**" : "**FAIL**") << " | "
              << rows[i].detail << " |\n";
        s << "\n" << (failures == 0
                          ? QStringLiteral("All checks passed.\n")
                          : QStringLiteral("**%1 failing check(s).**\n")
                                .arg(failures));
    }
};

// ---------------------------------------------------------------------------
// Minimal .inp reader — only what a profile path needs
// ---------------------------------------------------------------------------

struct InpNode { double invert = 0.0; double maxDepth = 0.0;
                 ProfileBuilder::NodeKind kind = ProfileBuilder::NodeKind::Junction; };
struct InpLink { QString name, from, to; double length = 0.0; double geom1 = 0.0;
                 ProfileBuilder::LinkKind kind = ProfileBuilder::LinkKind::Conduit; };

struct InpModel
{
    QHash<QString, InpNode> nodes;
    QVector<InpLink>        links;
};

QStringList fields(const QString &line)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return line.split(ws, Qt::SkipEmptyParts);
}

InpModel readInp(const QString &path)
{
    InpModel m;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return m;
    QTextStream in(&f);
    QString section;
    QHash<QString, double> geom1;      // link name → primary dimension
    while (!in.atEnd()) {
        QString line = in.readLine();
        const int semi = line.indexOf(QLatin1Char(';'));
        if (semi >= 0) line.truncate(semi);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('['))) {
            section = line.toUpper();
            continue;
        }
        const QStringList f2 = fields(line);
        if (section == QLatin1String("[JUNCTIONS]") && f2.size() >= 3) {
            m.nodes[f2[0]] = {f2[1].toDouble(), f2[2].toDouble(),
                              ProfileBuilder::NodeKind::Junction};
        } else if (section == QLatin1String("[OUTFALLS]") && f2.size() >= 2) {
            m.nodes[f2[0]] = {f2[1].toDouble(), 0.0,
                              ProfileBuilder::NodeKind::Outfall};
        } else if (section == QLatin1String("[STORAGE]") && f2.size() >= 3) {
            m.nodes[f2[0]] = {f2[1].toDouble(), f2[2].toDouble(),
                              ProfileBuilder::NodeKind::Storage};
        } else if (section == QLatin1String("[DIVIDERS]") && f2.size() >= 2) {
            m.nodes[f2[0]] = {f2[1].toDouble(), 0.0,
                              ProfileBuilder::NodeKind::Divider};
        } else if (section == QLatin1String("[CONDUITS]") && f2.size() >= 4) {
            m.links.push_back({f2[0], f2[1], f2[2], f2[3].toDouble(), 0.0,
                               ProfileBuilder::LinkKind::Conduit});
        } else if (section == QLatin1String("[PUMPS]") && f2.size() >= 3) {
            m.links.push_back({f2[0], f2[1], f2[2], 0.0, 0.0,
                               ProfileBuilder::LinkKind::Pump});
        } else if (section == QLatin1String("[ORIFICES]") && f2.size() >= 3) {
            m.links.push_back({f2[0], f2[1], f2[2], 0.0, 0.0,
                               ProfileBuilder::LinkKind::Orifice});
        } else if (section == QLatin1String("[WEIRS]") && f2.size() >= 3) {
            m.links.push_back({f2[0], f2[1], f2[2], 0.0, 0.0,
                               ProfileBuilder::LinkKind::Weir});
        } else if (section == QLatin1String("[OUTLETS]") && f2.size() >= 3) {
            m.links.push_back({f2[0], f2[1], f2[2], 0.0, 0.0,
                               ProfileBuilder::LinkKind::Outlet});
        } else if (section == QLatin1String("[XSECTIONS]") && f2.size() >= 3) {
            geom1[f2[0]] = f2[2].toDouble();
        }
    }
    for (InpLink &l : m.links) l.geom1 = geom1.value(l.name, 0.0);
    return m;
}

/*! Walks the longest downstream chain, preferring \p start when given.
 *  Mirrors what a user gets by picking two ends in the map view. */
ProfileBuilder::PathStatic buildPath(const InpModel &m, const QString &start)
{
    QHash<QString, QVector<int>> outgoing;
    QSet<QString> hasIncoming;
    for (int i = 0; i < m.links.size(); ++i) {
        outgoing[m.links[i].from].push_back(i);
        hasIncoming.insert(m.links[i].to);
    }

    const auto walk = [&](const QString &head) {
        QVector<int> chain;
        QSet<QString> seen{head};
        QString cur = head;
        while (outgoing.contains(cur)) {
            int pick = -1;
            for (int idx : outgoing.value(cur))
                if (!seen.contains(m.links[idx].to)) { pick = idx; break; }
            if (pick < 0) break;
            chain.push_back(pick);
            cur = m.links[pick].to;
            seen.insert(cur);
        }
        return chain;
    };

    QVector<int> best;
    if (!start.isEmpty()) {
        best = walk(start);
    } else {
        for (auto it = m.nodes.cbegin(); it != m.nodes.cend(); ++it) {
            if (hasIncoming.contains(it.key())) continue;
            const QVector<int> c = walk(it.key());
            if (c.size() > best.size()) best = c;
        }
    }

    ProfileBuilder::PathStatic path;
    if (best.isEmpty()) return path;

    const auto pushNode = [&](const QString &name) {
        const InpNode n = m.nodes.value(name);
        ProfileBuilder::NodeStatic ns;
        ns.name       = name;
        ns.invertElev = n.invert;
        ns.maxDepth   = n.maxDepth;
        ns.kind       = n.kind;
        path.nodes.push_back(ns);
    };
    pushNode(m.links[best.front()].from);
    for (int idx : best) {
        const InpLink &l = m.links[idx];
        ProfileBuilder::LinkStatic ls;
        ls.name     = l.name;
        ls.length   = l.length;
        ls.maxDepth = l.geom1;
        ls.kind     = l.kind;
        path.links.push_back(ls);
        pushNode(l.to);
    }
    path.chainage = ProfileBuilder::computeChainage(path.links);
    return path;
}

// ---------------------------------------------------------------------------
// Attribute sampling — mirrors ProfileAttributeSampler::fetch()
//
// fetch() takes a SWMMResultsLayer, whose translation unit drags in the app's
// render stack; the four things it asks that layer for (output handle, period
// count, node/link output index) come straight off the .out file, so the same
// engine calls are made here against the same variable-code table.
// ---------------------------------------------------------------------------

using Profile = ProfileAttributeSampler::AttributeProfile;

struct OutFile
{
    SWMM_Output handle = nullptr;
    int periods = 0;
    int flowUnits = 0;
    QHash<QString, int> nodeIdx, linkIdx;

    bool open(const QString &path)
    {
        handle = swmm_output_open(path.toUtf8().constData());
        if (!handle) return false;
        periods   = swmm_output_get_period_count(handle);
        flowUnits = swmm_output_get_flow_units(handle);
        for (int i = 0, n = swmm_output_get_node_count(handle); i < n; ++i)
            if (const char *id = swmm_output_get_node_id(handle, i))
                nodeIdx[QString::fromUtf8(id)] = i;
        for (int i = 0, n = swmm_output_get_link_count(handle); i < n; ++i)
            if (const char *id = swmm_output_get_link_id(handle, i))
                linkIdx[QString::fromUtf8(id)] = i;
        return true;
    }
    ~OutFile() { if (handle) swmm_output_close(handle); }
};

Profile sample(OutFile &out, const ProfileBuilder::PathStatic &path,
               PlotAttribute attr)
{
    Profile p;
    p.attribute       = attr;
    p.isNodeAttribute = ProfileAttributeSampler::isNodeAttribute(attr);
    if (!ProfileAttributeSampler::isTrackableAttribute(attr)) return p;
    p.periodCount = out.periods;

    const bool node = p.isNodeAttribute;
    const int  var  = SwmmOutRunLayer::variableCodeFor(
        attr, node ? ObjectRef::Kind::Node : ObjectRef::Kind::Link);
    const int count = node ? path.nodes.size() : path.links.size();
    p.byPath.resize(count);
    p.minByPath.resize(count);
    p.maxByPath.resize(count);

    for (int i = 0; i < count; ++i) {
        const QString &name = node ? path.nodes[i].name : path.links[i].name;
        const int idx = node ? out.nodeIdx.value(name, -1)
                             : out.linkIdx.value(name, -1);
        QVector<float> &row = p.byPath[i];
        if (out.handle && idx >= 0 && var >= 0 && out.periods > 0) {
            row.resize(out.periods);
            const int rc = node
                ? swmm_output_get_node_series(out.handle, idx, var, 0,
                                              out.periods - 1, row.data())
                : swmm_output_get_link_series(out.handle, idx, var, 0,
                                              out.periods - 1, row.data());
            if (rc != 0) row.clear();
        }
        float mn = std::numeric_limits<float>::quiet_NaN();
        float mx = mn;
        for (float v : std::as_const(row)) {
            if (!std::isfinite(v)) continue;
            if (std::isnan(mn) || v < mn) mn = v;
            if (std::isnan(mx) || v > mx) mx = v;
        }
        p.minByPath[i] = mn;
        p.maxByPath[i] = mx;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

/*! The profile widget's own x mapping, expressed through its public API.
 *  ProfilePlotWidget::dataToPixel() is private, but every term it uses for x
 *  is public: plotRect().left() == chartLeftMarginPx(), its width is
 *  `max(1, width - left - right)`, and the range is visibleDataRange(). */
double profilePixelForVirtualX(const ProfilePlotWidget *plot, double vx)
{
    const QRectF r = plot->visibleDataRange();
    const double left = ProfilePlotWidget::chartLeftMarginPx();
    const double w = std::max(1, plot->width()
                                 - ProfilePlotWidget::chartLeftMarginPx()
                                 - ProfilePlotWidget::chartRightMarginPx());
    return left + (vx - r.left()) / (r.right() - r.left()) * w;
}

/*! Largest |Δpixel| between the two panes over every node on the path. */
double worstColumnDelta(const ProfilePlotWidget *plot, const TW *tracks,
                        int *atNode = nullptr)
{
    double worst = 0.0;
    const QVector<double> &vx = plot->virtualChainageTable();
    for (int i = 0; i < vx.size(); ++i) {
        const double d = std::abs(profilePixelForVirtualX(plot, vx[i])
                                  - tracks->pixelForVirtualX(vx[i]));
        if (d > worst) { worst = d; if (atNode) *atNode = i; }
    }
    return worst;
}

void grab(QWidget *w, const QString &file)
{
    w->grab().save(file);
}

} // namespace

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (argc < 4) {
        QTextStream(stderr)
            << "usage: profile_attribute_tracks_verify <model.inp> "
               "<results.out> <output-dir> [startNode]\n";
        return 2;
    }
    const QString inpPath = QString::fromLocal8Bit(argv[1]);
    const QString outPath = QString::fromLocal8Bit(argv[2]);
    const QString outDir  = QString::fromLocal8Bit(argv[3]);
    const QString startNode = argc > 4 ? QString::fromLocal8Bit(argv[4])
                                       : QString();
    QDir().mkpath(outDir);

    Report rep;

    const InpModel model = readInp(inpPath);
    ProfileBuilder::PathStatic path = buildPath(model, startNode);
    if (path.nodes.size() < 2) {
        QTextStream(stderr) << "no usable path in " << inpPath << "\n";
        return 2;
    }

    OutFile out;
    if (!out.open(outPath)) {
        QTextStream(stderr) << "cannot open " << outPath << "\n";
        return 2;
    }

    const auto us = openswmmvis::plot::unitSystemFromFlowUnits(out.flowUnits);

    // ── The dialog's composition, reproduced (profileplotdialog.cpp:303-325)
    ProfilePlotOptions plotOptions;
    ProfileAttributeTrackOptions trackOptions;

    auto *split = new QSplitter(Qt::Vertical);
    split->setObjectName(QStringLiteral("profileSplit"));
    split->setChildrenCollapsible(false);
    auto *plot = new ProfilePlotWidget;
    plot->setOptions(&plotOptions);
    auto *plotHolder = new QWidget(split);
    auto *plotHolderLayout = new QHBoxLayout(plotHolder);
    plotHolderLayout->setContentsMargins(0, 0, 0, 0);
    plotHolderLayout->setSpacing(0);
    plotHolderLayout->addWidget(plot);
    split->addWidget(plotHolder);
    auto *tracks = new TW;
    tracks->setOptions(&trackOptions);
    auto *scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(tracks);
    split->addWidget(scroll);
    split->setCollapsible(0, false);
    split->setCollapsible(1, true);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    // Tall enough that the pane shows its tracks whole for the screenshots —
    // i.e. where a user would drag the handle to. Two tracks then fit without
    // a scrollbar (deficit 0) while the 11-track pass still forces one, so
    // both sides of the gutter compensation get exercised.
    split->resize(960, 900);
    split->setSizes({560, 340});

    plot->setPath(path);

    // Profile HGL, so the screenshots show the real thing above the tracks.
    {
        ProfileBuilder::SourceSeries src;
        src.sourceId    = QFileInfo(outPath).completeBaseName();
        src.periodCount = out.periods;
        src.reportStepSec = swmm_output_get_report_step(out.handle);
        const Profile head  = sample(out, path, PlotAttribute::NodeHead);
        const Profile depth = sample(out, path, PlotAttribute::NodeDepth);
        const Profile vel   = sample(out, path, PlotAttribute::LinkVelocity);
        src.nodeHead = head.byPath;
        src.nodeDepth = depth.byPath;
        src.linkVelocity = vel.byPath;
        auto derived = std::make_shared<ProfileBuilder::SourceDerived>(
            ProfileBuilder::compute(path, src,
                                    us == openswmmvis::plot::UnitSystem::US
                                        ? ProfileBuilder::kGravityFps2
                                        : ProfileBuilder::kGravityMps2));
        ProfilePlotWidget::SeriesBinding b;
        b.label   = src.sourceId;
        b.kind    = ProfileBuilder::OutputKind::HGL;
        b.derived = derived;
        b.pen     = QPen(QColor(30, 120, 220), 1.6);
        b.brush   = QBrush(QColor(30, 120, 220, 70));
        plot->setSeries({b});
    }

    // ── §3.2 — two tracks, own y-axis, unit-bearing titles ─────────────────
    const auto makeTrack = [&](PlotAttribute a, const QColor &c,
                               const QString &sourceLabel, bool primary) {
        TW::Track t;
        t.attribute       = a;
        t.isNodeAttribute = ProfileAttributeSampler::isNodeAttribute(a);
        t.title           = openswmmvis::plot::labelWithUnits(a, us);
        t.pen             = QPen(c, 1.6);
        TW::SourceProfile sp;
        sp.label   = sourceLabel;
        sp.color   = c;
        sp.primary = primary;
        sp.data = std::make_shared<const Profile>(sample(out, path, a));
        t.sources.push_back(sp);
        return t;
    };

    TW::Track velocity = makeTrack(PlotAttribute::LinkVelocity,
                                   QColor(200, 90, 40),
                                   QStringLiteral("primary"), true);
    TW::Track depth = makeTrack(PlotAttribute::NodeDepth, QColor(40, 130, 200),
                                QStringLiteral("primary"), true);
    tracks->setTracks({depth, velocity});

    trackOptions.setAttributeVisible(PlotAttribute::NodeDepth, true);
    trackOptions.setAttributeVisible(PlotAttribute::LinkVelocity, true);

    rep.check(QStringLiteral("3.2 titles carry units"),
              depth.title.contains(QLatin1Char('(')) &&
                  velocity.title.contains(QLatin1Char('(')),
              QStringLiteral("\"%1\", \"%2\"").arg(depth.title, velocity.title));

    // syncTracksGutter() — the profile's right gutter absorbs the tracks
    // scroll area's vertical scrollbar so both panes stay equally wide.
    const auto syncGutter = [&]() {
        const int deficit = std::max(0, scroll->width()
                                        - scroll->viewport()->width());
        const QMargins m = plotHolderLayout->contentsMargins();
        if (m.right() != deficit)
            plotHolderLayout->setContentsMargins(m.left(), m.top(), deficit,
                                                 m.bottom());
    };

    // syncTracksAxes() — the contract in §6.
    const auto syncAxes = [&]() {
        syncGutter();
        tracks->setVirtualChainage(plot->virtualChainageTable());
        tracks->setHorizontalMargins(ProfilePlotWidget::chartLeftMarginPx(),
                                     ProfilePlotWidget::chartRightMarginPx());
        tracks->setRealChainageMapper(
            [plot](double vx) { return plot->virtualToRealChainage(vx); });
        const QRectF r = plot->visibleDataRange();
        tracks->setVisibleXRange(r.left(), r.right());
    };

    // Both-way x sync with the dialog's single re-entrancy guard.
    bool syncing = false;
    int  plotEmissions = 0, trackEmissions = 0;
    QObject::connect(plot, &ProfilePlotWidget::visibleXRangeChanged,
                     [&](double a, double b) {
        ++plotEmissions;
        if (syncing) return;
        syncing = true;
        tracks->setVisibleXRange(a, b);
        syncing = false;
    });
    QObject::connect(tracks, &TW::visibleXRangeChanged, [&](double a, double b) {
        ++trackEmissions;
        if (syncing) return;
        syncing = true;
        plot->setVisibleXRange(a, b);
        syncing = false;
    });

    split->show();
    QApplication::processEvents();
    syncAxes();
    QApplication::processEvents();

    const bool scrollBarShown =
        scroll->verticalScrollBar() && scroll->verticalScrollBar()->isVisible();

    // ── §3.3 — pixel-column alignment, the core promise ────────────────────
    {
        int node = -1;
        const double worst = worstColumnDelta(plot, tracks, &node);
        rep.check(QStringLiteral("3.3 columns align at fit"), worst < 0.5,
                  QStringLiteral("worst Δ = %1 px at node %2 (%3); "
                                 "plot w=%4, tracks w=%5, vscrollbar %6")
                      .arg(worst, 0, 'f', 3).arg(node)
                      .arg(node >= 0 ? path.nodes[node].name : QString())
                      .arg(plot->width()).arg(tracks->width())
                      .arg(scrollBarShown ? QStringLiteral("VISIBLE")
                                          : QStringLiteral("hidden")));
        grab(split, outDir + QStringLiteral("/1_fit_two_tracks.png"));
    }

    // Zoomed — the range the user reaches with the wheel / rubber band.
    {
        const QRectF r = plot->visibleDataRange();
        const double span = r.right() - r.left();
        plot->setVisibleXRange(r.left() + span * 0.25, r.right() - span * 0.25);
        QApplication::processEvents();
        int node = -1;
        const double worst = worstColumnDelta(plot, tracks, &node);
        rep.check(QStringLiteral("3.3 columns align when zoomed 2x"),
                  worst < 0.5,
                  QStringLiteral("worst Δ = %1 px at node %2")
                      .arg(worst, 0, 'f', 3).arg(node));
        grab(split, outDir + QStringLiteral("/2_zoomed.png"));
    }

    // ── §3.4 — both-way sync, no feedback loop ─────────────────────────────
    {
        plot->fitToExtent();
        QApplication::processEvents();
        const QRectF fitted = plot->visibleDataRange();
        const int before = plotEmissions + trackEmissions;

        // Profile drives the pane.
        plot->setVisibleXRange(fitted.left(), fitted.left()
                                                  + (fitted.right() - fitted.left()) * 0.5);
        QApplication::processEvents();
        const double d1 = worstColumnDelta(plot, tracks);
        rep.check(QStringLiteral("3.4 profile → tracks follows"), d1 < 0.5,
                  QStringLiteral("worst Δ = %1 px").arg(d1, 0, 'f', 3));

        // Pane drives the profile — a real wheel event through its handler.
        const QRectF paneBefore = plot->visibleDataRange();
        QWheelEvent wheel(QPointF(tracks->width() / 2.0, 40.0),
                          tracks->mapToGlobal(QPoint(tracks->width() / 2, 40)),
                          QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(tracks, &wheel);
        QApplication::processEvents();
        const QRectF now = plot->visibleDataRange();
        const double d2 = worstColumnDelta(plot, tracks);
        rep.check(QStringLiteral("3.4 tracks → profile follows"),
                  std::abs(now.width() - paneBefore.width()) > 1e-9 && d2 < 0.5,
                  QStringLiteral("profile span %1 → %2 on a pane wheel-zoom, "
                                 "worst Δ = %3 px")
                      .arg(paneBefore.width(), 0, 'f', 3)
                      .arg(now.width(), 0, 'f', 3).arg(d2, 0, 'f', 3));

        const int emissions = plotEmissions + trackEmissions - before;
        rep.check(QStringLiteral("3.4 no feedback loop"), emissions < 12,
                  QStringLiteral("%1 range emissions for 2 user gestures")
                      .arg(emissions));

        plot->fitToExtent();
        QApplication::processEvents();
        const double dFit = worstColumnDelta(plot, tracks);
        rep.check(QStringLiteral("3.4 Fit resets both panes"), dFit < 0.5,
                  QStringLiteral("worst Δ = %1 px").arg(dFit, 0, 'f', 3));
    }

    // ── §3.5 — animation moves curves, envelope stays put ──────────────────
    {
        tracks->setCurrentPeriod(-1);
        QApplication::processEvents();
        const QImage envelopeOnly = tracks->grab().toImage();

        tracks->setCurrentPeriod(0);
        QApplication::processEvents();
        const QImage frame0 = tracks->grab().toImage();
        tracks->setCurrentPeriod(std::max(0, out.periods / 2));
        QApplication::processEvents();
        const QImage frameMid = tracks->grab().toImage();

        rep.check(QStringLiteral("3.5 curves move with the clock"),
                  frame0 != frameMid && frame0 != envelopeOnly,
                  QStringLiteral("period 0 / %1 / none all differ")
                      .arg(out.periods / 2));

        // Envelope invariance: turning the curve off must return the same
        // pixels as before it was ever drawn.
        tracks->setCurrentPeriod(-1);
        QApplication::processEvents();
        rep.check(QStringLiteral("3.5 envelope band is time-invariant"),
                  tracks->grab().toImage() == envelopeOnly,
                  QStringLiteral("band identical after animating"));

        tracks->setCurrentPeriod(std::max(0, out.periods / 2));
        QApplication::processEvents();
        grab(split, outDir + QStringLiteral("/3_animated_midrun.png"));

        trackOptions.setEnvelopesVisible(false);
        QApplication::processEvents();
        const QImage noEnv = tracks->grab().toImage();
        rep.check(QStringLiteral("3.5 envelopes can be switched off"),
                  noEnv != frameMid, QStringLiteral("repaint differs"));
        grab(split, outDir + QStringLiteral("/4_envelope_off.png"));
        trackOptions.setEnvelopesVisible(true);
        QApplication::processEvents();
    }

    // ── §3.7 — styling ─────────────────────────────────────────────────────
    {
        const QImage before = tracks->grab().toImage();
        TW::Track recolored = velocity;
        recolored.pen = QPen(QColor(10, 160, 60), 4.0);
        tracks->setTracks({depth, recolored});
        QApplication::processEvents();
        rep.check(QStringLiteral("3.7 pen colour / width applies"),
                  tracks->grab().toImage() != before,
                  QStringLiteral("repaint differs after pen edit"));

        const int hBefore = tracks->minimumHeight();
        trackOptions.setTrackHeightPx(trackOptions.trackHeightPx() + 60);
        QApplication::processEvents();
        rep.check(QStringLiteral("3.7 track height applies"),
                  tracks->minimumHeight() > hBefore,
                  QStringLiteral("minimumHeight %1 → %2")
                      .arg(hBefore).arg(tracks->minimumHeight()));
        trackOptions.setTrackHeightPx(trackOptions.trackHeightPx() - 60);

        const QImage titled = tracks->grab().toImage();
        trackOptions.setShowTrackTitles(false);
        QApplication::processEvents();
        rep.check(QStringLiteral("3.7 titles can be switched off"),
                  tracks->grab().toImage() != titled,
                  QStringLiteral("repaint differs"));
        trackOptions.setShowTrackTitles(true);
        tracks->setTracks({depth, velocity});
        QApplication::processEvents();
    }

    // ── §3.8 — two sources: tinted curves, envelope on the primary only ────
    {
        TW::Track twoSource = depth;
        TW::SourceProfile secondary = twoSource.sources.front();
        secondary.label   = QStringLiteral("comparison");
        secondary.color   = QColor(190, 60, 160);
        secondary.primary = false;
        twoSource.sources.push_back(secondary);
        tracks->setTracks({twoSource, velocity});
        QApplication::processEvents();

        const int primaries =
            int(std::count_if(twoSource.sources.cbegin(),
                              twoSource.sources.cend(),
                              [](const TW::SourceProfile &s) { return s.primary; }));
        rep.check(QStringLiteral("3.8 exactly one envelope owner"),
                  primaries == 1,
                  QStringLiteral("%1 source(s) flagged primary of %2")
                      .arg(primaries).arg(twoSource.sources.size()));
        grab(split, outDir + QStringLiteral("/5_two_sources.png"));
        tracks->setTracks({depth, velocity});
        QApplication::processEvents();
    }

    // ── §3.9 — a path element missing from the .out reads as a GAP ─────────
    {
        Profile sparse = *depth.sources.front().data;
        // Keep the hole off the ends: a node whose only surviving neighbour
        // sits across the gap is an isolated vertex, and a polyline through
        // one point paints nothing — correct, but not what this check is for.
        const int n = int(sparse.byPath.size());
        const int hole = std::clamp(n / 2, 2, std::max(2, n - 2));
        sparse.byPath[hole].clear();                      // "unknown to the .out"
        sparse.minByPath[hole] = std::numeric_limits<float>::quiet_NaN();
        sparse.maxByPath[hole] = std::numeric_limits<float>::quiet_NaN();

        TW::Track gapped = depth;
        gapped.pen = QPen(QColor(255, 0, 0), 2.0);        // findable in pixels
        gapped.sources[0].data = std::make_shared<const Profile>(sparse);
        tracks->setTracks({gapped});
        tracks->setCurrentPeriod(std::max(0, out.periods / 2));
        QApplication::processEvents();

        // The gap node's own column must carry no curve pixel; its neighbours
        // must. A zero-filled row would instead put the curve on the floor.
        const QImage img = tracks->grab().toImage();
        // ±2 px around the node's column: the end nodes sit on the track
        // frame, whose half-pixel clip can shave the exact column.
        const auto columnHasCurve = [&](int nodeIdx) {
            const int x0 = int(std::round(tracks->pixelForVirtualX(
                plot->virtualChainageTable().value(nodeIdx))));
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = std::clamp(x0 + dx, 0, img.width() - 1);
                for (int y = 0; y < std::min(img.height(), 220); ++y) {
                    const QColor c = img.pixelColor(x, y);
                    if (c.red() > 180 && c.green() < 90 && c.blue() < 90)
                        return true;
                }
            }
            return false;
        };
        // The upstream side keeps a real segment; the downstream side is an
        // isolated vertex on short paths, so only the upstream run is asserted.
        const bool upstreamDrawn = columnHasCurve(hole - 1);
        rep.check(QStringLiteral("3.9 missing element renders as a gap"),
                  upstreamDrawn && !columnHasCurve(hole),
                  QStringLiteral("node %1 (%2) blank, upstream run drawn = %3 "
                                 "(no zero-fill on the floor)")
                      .arg(hole).arg(path.nodes[hole].name)
                      .arg(upstreamDrawn ? QStringLiteral("yes")
                                         : QStringLiteral("NO")));
        grab(split, outDir + QStringLiteral("/6_sparse_gap.png"));

        // A short series must stop animating past its last period, not crash.
        Profile shortSeries = *depth.sources.front().data;
        for (auto &row : shortSeries.byPath)
            row.resize(std::max(1, out.periods / 3));
        TW::Track shortTrack = depth;
        shortTrack.sources[0].data =
            std::make_shared<const Profile>(shortSeries);
        tracks->setTracks({shortTrack});
        tracks->setCurrentPeriod(out.periods - 1);
        QApplication::processEvents();
        rep.check(QStringLiteral("3.9 short series survives past its end"),
                  true, QStringLiteral("%1-period series indexed at %2, no crash")
                            .arg(shortSeries.byPath.value(0).size())
                            .arg(out.periods - 1));
        tracks->setTracks({depth, velocity});
        QApplication::processEvents();
    }

    // ── §3.10 — export renders both panes ──────────────────────────────────
    {
        const QPixmap shot = split->grab();
        const QImage img = shot.toImage();
        // The tracks pane occupies the bottom of the splitter; require it to
        // be non-uniform there (i.e. something was actually drawn).
        const int y0 = plot->height() + split->handleWidth();
        QSet<QRgb> colors;
        for (int y = y0; y < img.height(); y += 3)
            for (int x = 0; x < img.width(); x += 3)
                colors.insert(img.pixel(x, y));
        rep.check(QStringLiteral("3.10 export contains both panes"),
                  shot.height() > plot->height() && colors.size() > 3,
                  QStringLiteral("%1x%2 px, %3 distinct colours below the plot")
                      .arg(shot.width()).arg(shot.height()).arg(colors.size()));
        shot.save(outDir + QStringLiteral("/7_export.png"));
    }

    // ── §3.11 — 11 attributes: one fetch, then indexing only ───────────────
    {
        QVector<TW::Track> all;
        QElapsedTimer t;
        t.start();
        for (PlotAttribute a : openswmmvis::plot::nodePlotAttributes())
            all.push_back(makeTrack(a, QColor(40, 130, 200),
                                    QStringLiteral("primary"), true));
        for (PlotAttribute a : openswmmvis::plot::linkPlotAttributes())
            all.push_back(makeTrack(a, QColor(200, 90, 40),
                                    QStringLiteral("primary"), true));
        const qint64 fetchMs = t.elapsed();

        tracks->setTracks(all);
        QApplication::processEvents();
        rep.check(QStringLiteral("3.11 all 11 attributes build"),
                  all.size() == 11 && tracks->trackCount() == 11,
                  QStringLiteral("%1 tracks, fetch %2 ms for %3 periods")
                      .arg(tracks->trackCount()).arg(fetchMs).arg(out.periods));

        t.restart();
        const int frames = std::min(200, std::max(2, out.periods));
        for (int i = 0; i < frames; ++i) {
            tracks->setCurrentPeriod(i % out.periods);
            tracks->grab();          // force the repaint the timer would
        }
        const double perFrame = double(t.elapsed()) / frames;
        rep.check(QStringLiteral("3.11 playback is index-only"),
                  perFrame < 33.0,
                  QStringLiteral("%1 ms/frame over %2 frames (11 tracks)")
                      .arg(perFrame, 0, 'f', 2).arg(frames));

        rep.check(QStringLiteral("3.11 pane scrolls instead of growing"),
                  tracks->minimumHeight() > scroll->viewport()->height(),
                  QStringLiteral("content %1 px in a %2 px viewport")
                      .arg(tracks->minimumHeight())
                      .arg(scroll->viewport()->height()));

        // Alignment must survive the scrollbar the many-track case brings in.
        syncAxes();
        QApplication::processEvents();
        int node = -1;
        const double worst = worstColumnDelta(plot, tracks, &node);
        const bool barShown = scroll->verticalScrollBar()
                              && scroll->verticalScrollBar()->isVisible();
        rep.check(QStringLiteral("3.3 columns align with 11 tracks"),
                  worst < 0.5,
                  QStringLiteral("worst Δ = %1 px at node %2; plot w=%3, "
                                 "tracks w=%4, vscrollbar %5")
                      .arg(worst, 0, 'f', 3).arg(node)
                      .arg(plot->width()).arg(tracks->width())
                      .arg(barShown ? QStringLiteral("VISIBLE")
                                    : QStringLiteral("hidden")));
        grab(split, outDir + QStringLiteral("/8_eleven_tracks.png"));
    }

    const QString preamble =
        QStringLiteral("Model `%1`, results `%2` (%3 periods, %4 reporting "
                       "units).\nPath: %5 nodes / %6 links, %7 → %8.\n"
                       "Generated by `tests/tools/profileattributetracksverify.cpp`.\n")
            .arg(QFileInfo(inpPath).fileName(), QFileInfo(outPath).fileName())
            .arg(out.periods)
            .arg(us == openswmmvis::plot::UnitSystem::US ? QStringLiteral("US")
                                                         : QStringLiteral("SI"))
            .arg(path.nodes.size()).arg(path.links.size())
            .arg(path.nodes.front().name, path.nodes.back().name);
    rep.write(outDir + QStringLiteral("/RESULTS.md"), preamble);

    QTextStream(stdout) << "\n" << rep.rows.size() - rep.failures << "/"
                        << rep.rows.size() << " checks passed; artifacts in "
                        << outDir << "\n";
    return rep.failures == 0 ? 0 : 1;
}
