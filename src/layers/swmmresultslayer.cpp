/*!
 * \file   swmmresultslayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "layers/swmmresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "map/graphicsitems.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"
#include "layers/gisrasterlayer.h"
#include "render/categoricalpalette.h"
#include "render/fillsymbollayer.h"   // VS.2b — fill primitive for polygon brush
#include "render/ifeaturerenderer.h"
#include "render/intervalbinner.h"   // Slice OUT.1 — default kind binner
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/categorizedrenderer.h"
// Slice B.5 — Rule Model mirror over per-kind renderers.
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/sublayers/feature/featuresublayer.h"
// Slice SS.5 — Rule → FeatureSublayerStyle back-propagation.
#include "render/sublayers/feature/featuresublayerstyle.h"
#include "render/linesymbollayer.h"
#include "render/markersymbollayer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"
#include "ui/dialogs/ilayerstylesubject.h"

#include <QAtomicInteger>
#include <QCoreApplication>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>
#include <QVariant>

#include <limits>

#include <openswmm/engine/openswmm_output.h>

#include <QDateTime>
#include <QTimeZone>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPolygonF>

#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPen>
#include <QBrush>
#include <cmath>
#include <algorithm>
#include <limits>

// GDAL
#include <ogr_spatialref.h>

namespace {

// Scene-space Y-flip — matches SWMMLayerItem convention.
inline QPointF toScene(double mx, double my) { return QPointF(mx, -my); }

// Julian-day (fractional days since 30 Dec 1899) to QDateTime.
// SWMM uses Excel-epoch Julian dates.
QDateTime julianToDateTime(double julian)
{
    // Excel epoch: day 1 = 1 Jan 1900 (with Lotus 1-2-3 leap-year bug, day 0 = 30 Dec 1899)
    // QDateTime epoch: 1 Jan 1970 00:00:00 UTC
    // Days from 30 Dec 1899 to 1 Jan 1970 = 25569
    const double unixDays = julian - 25569.0;
    const qint64 unixMs   = static_cast<qint64>(unixDays * 86400.0 * 1000.0);
    return QDateTime::fromMSecsSinceEpoch(unixMs, QTimeZone::utc());
}

// Map SWMMResultVariable to the SWMM_OutNodeVar / LinkVar / SubcatchVar enum.
int nodeVar(SWMMResultVariable v)
{
    switch (v) {
    case SWMMResultVariable::NodeDepth:        return SWMM_OUT_NODE_DEPTH;
    case SWMMResultVariable::NodeHead:         return SWMM_OUT_NODE_HEAD;
    case SWMMResultVariable::NodeVolume:       return SWMM_OUT_NODE_VOLUME;
    case SWMMResultVariable::NodeInflow:       return SWMM_OUT_NODE_TOTAL_INFLOW;
    case SWMMResultVariable::NodeOverflow:     return SWMM_OUT_NODE_OVERFLOW;
    case SWMMResultVariable::NodeLateralInflow:return SWMM_OUT_NODE_LATERAL_INFLOW;
    default:                                   return -1;
    }
}

int linkVar(SWMMResultVariable v)
{
    switch (v) {
    case SWMMResultVariable::LinkFlow:     return SWMM_OUT_LINK_FLOW;
    case SWMMResultVariable::LinkDepth:    return SWMM_OUT_LINK_DEPTH;
    case SWMMResultVariable::LinkVelocity: return SWMM_OUT_LINK_VELOCITY;
    case SWMMResultVariable::LinkCapacity: return SWMM_OUT_LINK_CAPACITY;
    default:                               return -1;
    }
}

int subcatchVar(SWMMResultVariable v)
{
    switch (v) {
    case SWMMResultVariable::SubcatchRunoff:       return SWMM_OUT_SUBCATCH_RUNOFF;
    case SWMMResultVariable::SubcatchInfiltration: return SWMM_OUT_SUBCATCH_INFIL;
    case SWMMResultVariable::SubcatchEvaporation:  return SWMM_OUT_SUBCATCH_EVAP;
    case SWMMResultVariable::SubcatchSnowDepth:    return SWMM_OUT_SUBCATCH_SNOW_DEPTH;
    default:                                       return -1;
    }
}

bool isNodeVar(SWMMResultVariable v)
{
    return (v >= SWMMResultVariable::NodeDepth && v <= SWMMResultVariable::NodeLateralInflow);
}

bool isLinkVar(SWMMResultVariable v)
{
    return (v >= SWMMResultVariable::LinkFlow && v <= SWMMResultVariable::LinkCapacity);
}

bool isSubcatchVar(SWMMResultVariable v)
{
    return (v >= SWMMResultVariable::SubcatchRunoff);
}

// ---------------------------------------------------------------------------
// Phase 2 (2026-05-25) — attribute string → SWMM_OUT_* code mappers used by
// the per-sublayer concurrent paint path. The strings are the values each
// sublayer's `style.attribute` Q_PROPERTY carries (e.g. "depth", "flow").
// Returns -1 for unknown attributes so callers can skip cleanly.
// ---------------------------------------------------------------------------

// Accept either the sublayer's short attribute form ("depth", "flow",
// "runoff") or the renderer's stringified SWMMResultVariable enum form
// ("NodeDepth", "LinkFlow", "SubcatchRunoff"). Both naming conventions
// flow into rebuildKindFeatureOverrides → it pulls classifyAttribute()
// from the per-kind renderer (CamelCase) while the legacy paint path
// pulls style.attribute() from FeatureSublayerStyle (lower-case). One
// table, both spellings, no broken lookups.
int nodeOutCodeForAttribute(const QString &attr)
{
    static const QHash<QString, int> kMap = {
        { QStringLiteral("depth"),             SWMM_OUT_NODE_DEPTH },
        { QStringLiteral("head"),              SWMM_OUT_NODE_HEAD },
        { QStringLiteral("volume"),            SWMM_OUT_NODE_VOLUME },
        { QStringLiteral("inflow"),            SWMM_OUT_NODE_TOTAL_INFLOW },
        { QStringLiteral("overflow"),          SWMM_OUT_NODE_OVERFLOW },
        { QStringLiteral("lateralInflow"),     SWMM_OUT_NODE_LATERAL_INFLOW },
        { QStringLiteral("NodeDepth"),         SWMM_OUT_NODE_DEPTH },
        { QStringLiteral("NodeHead"),          SWMM_OUT_NODE_HEAD },
        { QStringLiteral("NodeVolume"),        SWMM_OUT_NODE_VOLUME },
        { QStringLiteral("NodeInflow"),        SWMM_OUT_NODE_TOTAL_INFLOW },
        { QStringLiteral("NodeOverflow"),      SWMM_OUT_NODE_OVERFLOW },
        { QStringLiteral("NodeLateralInflow"), SWMM_OUT_NODE_LATERAL_INFLOW },
    };
    return kMap.value(attr, -1);
}

int linkOutCodeForAttribute(const QString &attr)
{
    static const QHash<QString, int> kMap = {
        { QStringLiteral("flow"),         SWMM_OUT_LINK_FLOW },
        { QStringLiteral("depth"),        SWMM_OUT_LINK_DEPTH },
        { QStringLiteral("velocity"),     SWMM_OUT_LINK_VELOCITY },
        { QStringLiteral("capacity"),     SWMM_OUT_LINK_CAPACITY },
        { QStringLiteral("LinkFlow"),     SWMM_OUT_LINK_FLOW },
        { QStringLiteral("LinkDepth"),    SWMM_OUT_LINK_DEPTH },
        { QStringLiteral("LinkVelocity"), SWMM_OUT_LINK_VELOCITY },
        { QStringLiteral("LinkCapacity"), SWMM_OUT_LINK_CAPACITY },
    };
    return kMap.value(attr, -1);
}

int subcatchOutCodeForAttribute(const QString &attr)
{
    static const QHash<QString, int> kMap = {
        { QStringLiteral("runoff"),               SWMM_OUT_SUBCATCH_RUNOFF },
        { QStringLiteral("infiltration"),         SWMM_OUT_SUBCATCH_INFIL },
        { QStringLiteral("evaporation"),          SWMM_OUT_SUBCATCH_EVAP },
        { QStringLiteral("snowDepth"),            SWMM_OUT_SUBCATCH_SNOW_DEPTH },
        { QStringLiteral("SubcatchRunoff"),       SWMM_OUT_SUBCATCH_RUNOFF },
        { QStringLiteral("SubcatchInfiltration"), SWMM_OUT_SUBCATCH_INFIL },
        { QStringLiteral("SubcatchEvaporation"),  SWMM_OUT_SUBCATCH_EVAP },
        { QStringLiteral("SubcatchSnowDepth"),    SWMM_OUT_SUBCATCH_SNOW_DEPTH },
    };
    return kMap.value(attr, -1);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SWMMResultsLayer::SWMMResultsLayer(const QString &resultsFilePath,
                                   class SWMMModelLayer *modelLayer,
                                   OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent),
      m_resultsFilePath(resultsFilePath),
      m_modelLayer(modelLayer)
{
    setLayerType(OpenSWMMVisLayer::SWMMResultsLayer);
    setName(QStringLiteral("SWMM Results"));

    // Outputs take on the coordinate system of their input file: copy the
    // model layer's CRS so the Properties window reports a meaningful CRS
    // for results layers (instead of "(none)") and so any future
    // CRS-aware operation on the results layer reprojects correctly.
    // Deep-copy via SpatialReferenceSystem's copy ctor so the results
    // layer owns its CRS independently of the model layer's lifetime.
    if (modelLayer && modelLayer->srs())
        setSRS(new SpatialReferenceSystem(*modelLayer->srs(), this),
               /*ownsSRS=*/true);

    m_colorRamp = RasterColorRamp::viridis(0.0, 1.0);

    // Slice BI Phase 8.13.6.3 — initialise the renderer eagerly so renderer()
    // never returns nullptr. The default is a GraduatedRenderer; the paint
    // loop (sub-phase 8.13.6.4) will later read from it instead of m_colorRamp.
    m_renderer = std::make_unique<OpenSWMM::Render::GraduatedRenderer>();

    // Auto-assign a profile-plot line color from the categorical palette,
    // cycled per instantiation so simultaneously-open result layers land
    // on visually-distinct colors without user intervention.
    static QAtomicInteger<int> s_counter{0};
    m_profileLineColor = CategoricalPalette::at(s_counter.fetchAndAddRelaxed(1));

    // Derive default per-source profile-plot pens & brushes from the
    // categorical colour so two simultaneously-open layers render with
    // visually-distinct HGL/EGL passes without any user customisation.
    // The dash patterns mirror the widget's hardcoded defaults
    // (see profileplotwidget.cpp themeEglPen / themeMaxHglPen):
    //   - HGL    : solid
    //   - EGL    : long-dash (12/6)
    //   - Max HGL: short-dash, thinner
    //   - Max EGL: short-dash, thinner
    const QColor &c = m_profileLineColor;
    {
        QPen p(c, 2.0, Qt::SolidLine);
        p.setCapStyle(Qt::FlatCap);
        m_profileHglLinePen = p;
    }
    m_profileHglFillBrush = QBrush(QColor(c.red(), c.green(), c.blue(), 110));
    {
        QPen p(c, 2.0, Qt::CustomDashLine);
        p.setDashPattern({12.0, 6.0});
        p.setCapStyle(Qt::FlatCap);
        m_profileEglLinePen = p;
    }
    {
        QPen p(c, 1.4, Qt::DashLine);
        p.setCapStyle(Qt::FlatCap);
        m_profileMaxHglLinePen = p;
    }
    m_profileMaxHglFillBrush = QBrush(QColor(c.red(), c.green(), c.blue(), 60));
    {
        QPen p(c, 1.4, Qt::DashLine);
        p.setCapStyle(Qt::FlatCap);
        m_profileMaxEglLinePen = p;
    }

    // Slice U-0 — granular per-Category sublayer mix. One FeatureSublayer
    // instance per SWMMModelLayer::Category (11 total). Each carries its
    // own archetype-appropriate style bag so every visual object type can
    // be styled independently. QObject parent-child ownership keeps each
    // sublayer alive for the layer's lifetime.
    struct KindSpec {
        SWMMModelLayer::Category cat;
        const char              *idSuffix;
        const char              *displayName;
        const char              *defaultAttribute;
    };
    static const KindSpec kKindSpecs[] = {
        // Point archetypes (node kinds) — default attribute = depth.
        { SWMMModelLayer::CatJunctions,     "junctions",     QT_TR_NOOP("Junctions"),     "depth"  },
        { SWMMModelLayer::CatOutfalls,      "outfalls",      QT_TR_NOOP("Outfalls"),      "depth"  },
        { SWMMModelLayer::CatStorage,       "storage",       QT_TR_NOOP("Storage units"), "depth"  },
        { SWMMModelLayer::CatDividers,      "dividers",      QT_TR_NOOP("Dividers"),      "depth"  },
        // Line archetypes (link kinds) — default attribute = flow.
        { SWMMModelLayer::CatConduits,      "conduits",      QT_TR_NOOP("Conduits"),      "flow"   },
        { SWMMModelLayer::CatPumps,         "pumps",         QT_TR_NOOP("Pumps"),         "flow"   },
        { SWMMModelLayer::CatOrifices,      "orifices",      QT_TR_NOOP("Orifices"),      "flow"   },
        { SWMMModelLayer::CatWeirs,         "weirs",         QT_TR_NOOP("Weirs"),         "flow"   },
        { SWMMModelLayer::CatOutlets,       "outlets",       QT_TR_NOOP("Outlets"),       "flow"   },
        // Polygon archetype — default attribute = runoff.
        { SWMMModelLayer::CatSubcatchments, "subcatchments", QT_TR_NOOP("Subcatchments"), "runoff" },
        // Marker-only (no result feed) — empty attribute, static.
        { SWMMModelLayer::CatRainGages,     "raingages",     QT_TR_NOOP("Rain gages"),    ""       },
    };
    for (const KindSpec &spec : kKindSpecs) {
        auto *s = new OpenSWMM::Render::FeatureSublayer(
            spec.cat,
            QStringLiteral("results.") + QString::fromLatin1(spec.idSuffix),
            tr(spec.displayName),
            this);
        if (s->featureStyle() && spec.defaultAttribute[0])
            s->featureStyle()->setAttribute(QString::fromLatin1(spec.defaultAttribute));
        m_featureSublayers[static_cast<int>(spec.cat)] = s;
    }

    // 2026-05-25 — sublayer invalidated → re-fetch results for the
    // current period and emit a layer repaint so the canvas re-runs
    // populateScene with the updated per-var cache. This is what makes
    // the layer-tree checkbox + sublayer style edits actually show/hide
    // overlays AND swap colored attributes live.
    auto wireRefresh = [this](OpenSWMM::Render::ISublayer *s) {
        if (!s) return;
        connect(s, &OpenSWMM::Render::ISublayer::invalidated,
                this, [this]() {
                    if (m_handle && m_totalSteps > 0)
                        fetchResultsForStep(m_currentStep);
                    // Slice §Y.2 — sublayer invalidation covers visibility
                    // toggles + style + attribute changes; any of those can
                    // alter the set of items the next paint produces, so
                    // the cached scene is no longer authoritative.
                    escalateSceneDirty(SceneDirty::Structural);
                    emit repaintRequested();
                });
    };
    for (auto *s : m_featureSublayers)
        wireRefresh(s);
}

QList<OpenSWMM::Render::ISublayer *> SWMMResultsLayer::sublayers() const
{
    // Paint order (bottom-up): polygons → lines → point markers. RainGages
    // paint on top of everything else since they're informational glyphs.
    // After Slice GUI-2026-05-30 §2 the user can reorder via the layer tree,
    // so the order is cached in m_sublayerOrder and seeded once with the
    // archetype default below.
    if (m_sublayerOrder.isEmpty()) {
        static const SWMMModelLayer::Category kOrder[] = {
            SWMMModelLayer::CatSubcatchments,
            SWMMModelLayer::CatConduits,
            SWMMModelLayer::CatPumps,
            SWMMModelLayer::CatOrifices,
            SWMMModelLayer::CatWeirs,
            SWMMModelLayer::CatOutlets,
            SWMMModelLayer::CatJunctions,
            SWMMModelLayer::CatOutfalls,
            SWMMModelLayer::CatStorage,
            SWMMModelLayer::CatDividers,
            SWMMModelLayer::CatRainGages,
        };
        for (auto c : kOrder)
            if (auto *s = m_featureSublayers[static_cast<int>(c)])
                m_sublayerOrder.append(s);
    }
    return m_sublayerOrder;
}

bool SWMMResultsLayer::moveSublayer(int from, int to)
{
    (void) sublayers();      // force lazy seed
    if (from < 0 || from >= m_sublayerOrder.size()
        || to   < 0 || to   >= m_sublayerOrder.size()
        || from == to)
        return false;
    m_sublayerOrder.move(from, to);
    emit repaintRequested();
    return true;
}

OpenSWMM::Render::FeatureSublayer *SWMMResultsLayer::featureSublayer(
    SWMMModelLayer::Category c) const
{
    const int idx = static_cast<int>(c);
    if (idx < 0 || idx >= SWMMModelLayer::NumCategories) return nullptr;
    return m_featureSublayers[idx];
}

std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
SWMMResultsLayer::styleSubjects()
{
    std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> out;
    // One subject per Category — grouped into archetype sections so the
    // dialog can present them as nested tabs ("Nodes" → Junctions/Outfalls/
    // Storage/Dividers, "Links" → Conduits/Pumps/Orifices/Weirs/Outlets,
    // "Areas" → Subcatchments, "Other" → RainGages).
    auto archetypeSection = [](SWMMModelLayer::Category c) -> QString {
        switch (c) {
            case SWMMModelLayer::CatJunctions:
            case SWMMModelLayer::CatOutfalls:
            case SWMMModelLayer::CatStorage:
            case SWMMModelLayer::CatDividers:
                return QStringLiteral("Nodes");
            case SWMMModelLayer::CatConduits:
            case SWMMModelLayer::CatPumps:
            case SWMMModelLayer::CatOrifices:
            case SWMMModelLayer::CatWeirs:
            case SWMMModelLayer::CatOutlets:
                return QStringLiteral("Links");
            case SWMMModelLayer::CatSubcatchments:
                return QStringLiteral("Areas");
            case SWMMModelLayer::CatRainGages:
                return QStringLiteral("Other");
            default:
                return QStringLiteral("Other");
        }
    };

    for (int i = 0; i < SWMMModelLayer::NumCategories; ++i) {
        auto *sub = m_featureSublayers[i];
        if (!sub || !sub->style()) continue;
        out.push_back(std::make_unique<openswmmvis::ui::LayerStyleSubject>(
            sub->displayName(),
            sub->style(),
            sub->id(),
            archetypeSection(static_cast<SWMMModelLayer::Category>(i))));
    }
    return out;
}

SWMMResultsLayer::~SWMMResultsLayer()
{
    closeResults();

    if (m_transform)
    {
        OCTDestroyCoordinateTransformation(
            reinterpret_cast<OGRCoordinateTransformationH>(m_transform));
        m_transform = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Results file
// ---------------------------------------------------------------------------

QString SWMMResultsLayer::resultsFilePath() const
{
    // Absolute path so the Properties window shows the full on-disk
    // location of the .out file.
    return m_resultsFilePath.isEmpty()
               ? m_resultsFilePath
               : QFileInfo(m_resultsFilePath).absoluteFilePath();
}

QString SWMMResultsLayer::scenarioName() const
{
    return m_scenarioName;
}

void SWMMResultsLayer::setScenarioName(const QString &name)
{
    if (m_scenarioName == name) return;
    m_scenarioName = name;
    emit scenarioNameChanged(name);
}

QColor SWMMResultsLayer::profileLineColor() const
{
    return m_profileLineColor;
}

void SWMMResultsLayer::setProfileLineColor(const QColor &color)
{
    if (m_profileLineColor == color) return;
    m_profileLineColor = color;
    emit profileLineColorChanged(color);
}

// ---------------------------------------------------------------------------
// Per-source profile-plot style accessors.
//
// The constructor derives sensible defaults from m_profileLineColor.  These
// setters do NOT auto-update on profileLineColor changes — once the user
// customises a pen, it stays put.  Callers that want the "follow the
// categorical colour" behaviour should call setProfileLineColor and then
// also reset each pen explicitly.
// ---------------------------------------------------------------------------

QPen   SWMMResultsLayer::profileHglLinePen()      const { return m_profileHglLinePen; }
QBrush SWMMResultsLayer::profileHglFillBrush()    const { return m_profileHglFillBrush; }
QPen   SWMMResultsLayer::profileEglLinePen()      const { return m_profileEglLinePen; }
QPen   SWMMResultsLayer::profileMaxHglLinePen()   const { return m_profileMaxHglLinePen; }
QBrush SWMMResultsLayer::profileMaxHglFillBrush() const { return m_profileMaxHglFillBrush; }
QPen   SWMMResultsLayer::profileMaxEglLinePen()   const { return m_profileMaxEglLinePen; }

void SWMMResultsLayer::setProfileHglLinePen(const QPen &pen)
{
    if (m_profileHglLinePen == pen) return;
    m_profileHglLinePen = pen;
    emit profileStyleChanged();
}
void SWMMResultsLayer::setProfileHglFillBrush(const QBrush &brush)
{
    if (m_profileHglFillBrush == brush) return;
    m_profileHglFillBrush = brush;
    emit profileStyleChanged();
}
void SWMMResultsLayer::setProfileEglLinePen(const QPen &pen)
{
    if (m_profileEglLinePen == pen) return;
    m_profileEglLinePen = pen;
    emit profileStyleChanged();
}
void SWMMResultsLayer::setProfileMaxHglLinePen(const QPen &pen)
{
    if (m_profileMaxHglLinePen == pen) return;
    m_profileMaxHglLinePen = pen;
    emit profileStyleChanged();
}
void SWMMResultsLayer::setProfileMaxHglFillBrush(const QBrush &brush)
{
    if (m_profileMaxHglFillBrush == brush) return;
    m_profileMaxHglFillBrush = brush;
    emit profileStyleChanged();
}
void SWMMResultsLayer::setProfileMaxEglLinePen(const QPen &pen)
{
    if (m_profileMaxEglLinePen == pen) return;
    m_profileMaxEglLinePen = pen;
    emit profileStyleChanged();
}

// ---------------------------------------------------------------------------
// QSettings persistence helpers
//
// Stored as QVariants of QPen / QBrush — Qt's metatype machinery wires
// the QDataStream operators automatically, so colour, width, dash style,
// dash pattern and brush colour all round-trip.
// ---------------------------------------------------------------------------

void SWMMResultsLayer::writeProfileStyle(QSettings &settings) const
{
    settings.setValue(QStringLiteral("profileHglLinePen"),
                      QVariant::fromValue(m_profileHglLinePen));
    settings.setValue(QStringLiteral("profileHglFillBrush"),
                      QVariant::fromValue(m_profileHglFillBrush));
    settings.setValue(QStringLiteral("profileEglLinePen"),
                      QVariant::fromValue(m_profileEglLinePen));
    settings.setValue(QStringLiteral("profileMaxHglLinePen"),
                      QVariant::fromValue(m_profileMaxHglLinePen));
    settings.setValue(QStringLiteral("profileMaxHglFillBrush"),
                      QVariant::fromValue(m_profileMaxHglFillBrush));
    settings.setValue(QStringLiteral("profileMaxEglLinePen"),
                      QVariant::fromValue(m_profileMaxEglLinePen));
    // profileEglFillBrush / profileMaxEglFillBrush removed (no physical
    // meaning) — legacy keys, if any, are silently ignored by readProfileStyle.
}

void SWMMResultsLayer::readProfileStyle(QSettings &settings)
{
    auto applyPen = [&](const QString &key, void (SWMMResultsLayer::*setter)(const QPen &)) {
        if (!settings.contains(key)) return;
        const QVariant v = settings.value(key);
        if (!v.canConvert<QPen>()) return;
        (this->*setter)(v.value<QPen>());
    };
    auto applyBrush = [&](const QString &key, void (SWMMResultsLayer::*setter)(const QBrush &)) {
        if (!settings.contains(key)) return;
        const QVariant v = settings.value(key);
        if (!v.canConvert<QBrush>()) return;
        (this->*setter)(v.value<QBrush>());
    };
    applyPen  (QStringLiteral("profileHglLinePen"),      &SWMMResultsLayer::setProfileHglLinePen);
    applyBrush(QStringLiteral("profileHglFillBrush"),    &SWMMResultsLayer::setProfileHglFillBrush);
    applyPen  (QStringLiteral("profileEglLinePen"),      &SWMMResultsLayer::setProfileEglLinePen);
    applyPen  (QStringLiteral("profileMaxHglLinePen"),   &SWMMResultsLayer::setProfileMaxHglLinePen);
    applyBrush(QStringLiteral("profileMaxHglFillBrush"), &SWMMResultsLayer::setProfileMaxHglFillBrush);
    applyPen  (QStringLiteral("profileMaxEglLinePen"),   &SWMMResultsLayer::setProfileMaxEglLinePen);
}

int SWMMResultsLayer::periodIndexForDateTime(const QDateTime &dt) const
{
    // Snap the requested time to this layer's report-step grid.  Clamped to
    // [0, totalSteps - 1] so the result is always a valid index even when
    // `dt` lies outside the simulated range — secondary layers fed a
    // primary-layer time will then render their nearest-available period.
    if (m_totalSteps <= 0 || m_reportStepSec <= 0 || !m_startDateTime.isValid())
        return 0;
    const qint64 offsetMs = m_startDateTime.msecsTo(dt);
    const double periodF  = static_cast<double>(offsetMs)
                              / (static_cast<double>(m_reportStepSec) * 1000.0);
    int period = static_cast<int>(std::lround(periodF));
    if (period < 0)              period = 0;
    if (period >= m_totalSteps)  period = m_totalSteps - 1;
    return period;
}

SWMM_Output SWMMResultsLayer::outputHandle() const
{
    return m_handle;
}

int SWMMResultsLayer::nodeOutputIndex(const QString &name) const
{
    return m_nodeOutputIdx.value(name, -1);
}

int SWMMResultsLayer::linkOutputIndex(const QString &name) const
{
    return m_linkOutputIdx.value(name, -1);
}

int SWMMResultsLayer::subcatchOutputIndex(const QString &name) const
{
    return m_subcatchOutputIdx.value(name, -1);
}

int SWMMResultsLayer::flowUnits() const
{
    return m_handle ? swmm_output_get_flow_units(m_handle) : -1;
}

bool SWMMResultsLayer::openResults(QList<QString> &warnings, QList<QString> &errors)
{
    Q_UNUSED(warnings)

    closeResults();

    if (!QFile::exists(m_resultsFilePath))
    {
        errors.append(QStringLiteral("Results file not found: ") + m_resultsFilePath);
        emit resultsError(errors.last());
        return false;
    }

    m_handle = swmm_output_open(m_resultsFilePath.toUtf8().constData());
    if (!m_handle)
    {
        errors.append(QStringLiteral("Failed to open results file: ") + m_resultsFilePath);
        emit resultsError(errors.last());
        return false;
    }

    m_totalSteps    = swmm_output_get_period_count(m_handle);
    m_reportStepSec = swmm_output_get_report_step(m_handle);

    if (m_totalSteps <= 0)
    {
        errors.append(QStringLiteral("Results file contains no output periods."));
        emit resultsError(errors.last());
        closeResults();
        return false;
    }

    // Determine simulation start datetime from period 0.
    double startJulian = 0.0;
    swmm_output_get_start_date(m_handle, &startJulian);
    m_startDateTime = julianToDateTime(startJulian);

    // End datetime from last period time.
    double endJulian = 0.0;
    swmm_output_get_period_time(m_handle, m_totalSteps - 1, &endJulian);
    m_endDateTime = julianToDateTime(endJulian);

    // Build name → output-index maps for fast lookup during rendering.
    buildOutputIdMaps();

    // Pre-fetch results for period 0.
    m_currentStep = 0;
    fetchResultsForStep(0);

    // Slice §Y.2 — new results file: every cached item from any previous
    // file is stale. Force structural rebuild on the next refreshScene.
    escalateSceneDirty(SceneDirty::Structural);

    emit totalTimeStepsChanged(m_totalSteps);
    emit currentTimeStepChanged(m_currentStep);
    emit currentDateTimeChanged(currentDateTime());
    emit resultsOpened();

    return true;
}

void SWMMResultsLayer::closeResults()
{
    if (m_handle)
    {
        swmm_output_close(m_handle);
        m_handle = nullptr;
    }

    m_totalSteps    = 0;
    m_currentStep   = 0;
    m_reportStepSec = 0;
    m_nodeResults.clear();
    m_linkResults.clear();
    m_subcatchResults.clear();
    m_nodeOutputIdx.clear();
    m_linkOutputIdx.clear();
    m_subcatchOutputIdx.clear();

    // Phase 2 + Phase 7 — drop per-var caches and sampled-range caches.
    m_nodeResultsByVar.clear();
    m_linkResultsByVar.clear();
    m_subcatchResultsByVar.clear();
    m_nodeAttributeRange.clear();
    m_linkAttributeRange.clear();
    m_subcatchAttributeRange.clear();
}

// ---------------------------------------------------------------------------
// Slice QA.3 — per-output node summary statistics
// ---------------------------------------------------------------------------
//
// Engine gap QA-01 landed alongside these accessors: the four
// `swmm_output_get_node_stat_*` C functions in openswmm_output.h
// aggregate the four flooding statistics on-demand from the .out file's
// per-period node results (the binary format has no dedicated stats
// block, so the work is one O(n_periods) walk per call). Semantics
// match the editing-engine accessors (`swmm_node_get_stat_*`) except
// values are sourced from this specific output file — which is exactly
// what the Stats-source combo (Slice QA.2) needs for multi-output
// comparison.
//
// nodeOutputIndex(name) returns -1 when the layer is closed OR the
// name doesn't appear in this output's node list; either case yields
// 0.0 here so the caller path stays exception-free and identical to
// the legacy "unknown index → 0" semantics. Engine call failures
// (which shouldn't happen mid-open but the API is defensively coded)
// fall through to 0.0 the same way.

double SWMMResultsLayer::nodeStatMaxDepth(const QString &nodeName) const
{
    const int idx = nodeOutputIndex(nodeName);
    if (idx < 0 || !m_handle) return 0.0;
    double v = 0.0;
    if (swmm_output_get_node_stat_max_depth(m_handle, idx, &v) != 0) return 0.0;
    return v;
}

double SWMMResultsLayer::nodeStatMaxOverflow(const QString &nodeName) const
{
    const int idx = nodeOutputIndex(nodeName);
    if (idx < 0 || !m_handle) return 0.0;
    double v = 0.0;
    if (swmm_output_get_node_stat_max_overflow(m_handle, idx, &v) != 0) return 0.0;
    return v;
}

double SWMMResultsLayer::nodeStatVolFlooded(const QString &nodeName) const
{
    const int idx = nodeOutputIndex(nodeName);
    if (idx < 0 || !m_handle) return 0.0;
    double v = 0.0;
    if (swmm_output_get_node_stat_vol_flooded(m_handle, idx, &v) != 0) return 0.0;
    return v;
}

double SWMMResultsLayer::nodeStatTimeFlooded(const QString &nodeName) const
{
    const int idx = nodeOutputIndex(nodeName);
    if (idx < 0 || !m_handle) return 0.0;
    double v = 0.0;
    if (swmm_output_get_node_stat_time_flooded(m_handle, idx, &v) != 0) return 0.0;
    return v;
}

void SWMMResultsLayer::buildOutputIdMaps()
{
    if (!m_handle)
        return;

    const int nNodes = swmm_output_get_node_count(m_handle);
    m_nodeOutputIdx.reserve(nNodes);
    for (int i = 0; i < nNodes; ++i)
    {
        const char *id = swmm_output_get_node_id(m_handle, i);
        if (id)
            m_nodeOutputIdx.insert(QString::fromUtf8(id), i);
    }

    const int nLinks = swmm_output_get_link_count(m_handle);
    m_linkOutputIdx.reserve(nLinks);
    for (int i = 0; i < nLinks; ++i)
    {
        const char *id = swmm_output_get_link_id(m_handle, i);
        if (id)
            m_linkOutputIdx.insert(QString::fromUtf8(id), i);
    }

    const int nSubs = swmm_output_get_subcatch_count(m_handle);
    m_subcatchOutputIdx.reserve(nSubs);
    for (int i = 0; i < nSubs; ++i)
    {
        const char *id = swmm_output_get_subcatch_id(m_handle, i);
        if (id)
            m_subcatchOutputIdx.insert(QString::fromUtf8(id), i);
    }
}

void SWMMResultsLayer::fetchResultsForStep(int step)
{
    if (!m_handle || step < 0 || step >= m_totalSteps)
        return;

    // Phase 2 (2026-05-25) — collect the set of (kind, varCode) pairs the
    // visible sublayers need so concurrent painting works (e.g. node markers
    // showing depth WHILE conduit lines show flow). The active m_variable
    // is always included so the legacy m_*Results vectors stay populated
    // for any older callers (Slice OUT.2 override caches, comparison-plot
    // dialog, etc.).
    QSet<int> neededNodeVars;
    QSet<int> neededLinkVars;
    QSet<int> neededSubcatchVars;

    auto collect = [](QSet<int> &set, int code) {
        if (code >= 0) set.insert(code);
    };

    // Active variable — legacy path.
    if (isNodeVar(m_variable))         collect(neededNodeVars,     nodeVar(m_variable));
    else if (isLinkVar(m_variable))    collect(neededLinkVars,     linkVar(m_variable));
    else if (isSubcatchVar(m_variable))collect(neededSubcatchVars, subcatchVar(m_variable));

    // Slice U-0 — walk the granular per-Category sublayer set. Each
    // visible sublayer contributes its attribute's output-code to the
    // matching needed-vars set. Line sublayers with showFlowArrows on
    // also need LinkFlow.
    using OpenSWMM::Render::FeatureSublayer;
    for (auto *base : sublayers()) {
        auto *sub = qobject_cast<FeatureSublayer *>(base);
        if (!sub || !sub->isVisible() || !sub->featureStyle()) continue;
        const QString attr = sub->featureStyle()->attribute();
        if (attr.isEmpty()) continue;
        switch (sub->archetype()) {
            case FeatureSublayer::Archetype::Point:
                collect(neededNodeVars,     nodeOutCodeForAttribute(attr));
                break;
            case FeatureSublayer::Archetype::Line:
                collect(neededLinkVars,     linkOutCodeForAttribute(attr));
                if (auto *ls = sub->lineStyle(); ls && ls->showFlowArrows())
                    collect(neededLinkVars, SWMM_OUT_LINK_FLOW);
                break;
            case FeatureSublayer::Archetype::Polygon:
                collect(neededSubcatchVars, subcatchOutCodeForAttribute(attr));
                break;
        }
    }

    // Drop cache entries for vars no longer needed (sublayer toggled off /
    // attribute changed) so memory tracks visible-sublayer set. QHash
    // iterators don't support arithmetic, so explicit if/else around
    // increment vs erase.
    for (auto it = m_nodeResultsByVar.begin(); it != m_nodeResultsByVar.end(); ) {
        if (neededNodeVars.contains(it.key())) ++it;
        else it = m_nodeResultsByVar.erase(it);
    }
    for (auto it = m_linkResultsByVar.begin(); it != m_linkResultsByVar.end(); ) {
        if (neededLinkVars.contains(it.key())) ++it;
        else it = m_linkResultsByVar.erase(it);
    }
    for (auto it = m_subcatchResultsByVar.begin(); it != m_subcatchResultsByVar.end(); ) {
        if (neededSubcatchVars.contains(it.key())) ++it;
        else it = m_subcatchResultsByVar.erase(it);
    }

    // Fetch each needed (kind, var) once for this period.
    const int nodeCount     = swmm_output_get_node_count(m_handle);
    const int linkCount     = swmm_output_get_link_count(m_handle);
    const int subcatchCount = swmm_output_get_subcatch_count(m_handle);

    for (int v : neededNodeVars) {
        QVector<float> &buf = m_nodeResultsByVar[v];
        buf.resize(nodeCount);
        swmm_output_get_node_result(m_handle, step, v, buf.data());
    }
    for (int v : neededLinkVars) {
        QVector<float> &buf = m_linkResultsByVar[v];
        buf.resize(linkCount);
        swmm_output_get_link_result(m_handle, step, v, buf.data());
    }
    for (int v : neededSubcatchVars) {
        QVector<float> &buf = m_subcatchResultsByVar[v];
        buf.resize(subcatchCount);
        swmm_output_get_subcatch_result(m_handle, step, v, buf.data());
    }

    // Legacy single-vector mirrors. Kept for back-compat with code paths
    // (Slice OUT.2 override caches, identify dialog, comparison plot) that
    // still read these directly. They alias the active variable's entry.
    if (isNodeVar(m_variable))
        m_nodeResults = m_nodeResultsByVar.value(nodeVar(m_variable));
    else if (isLinkVar(m_variable))
        m_linkResults = m_linkResultsByVar.value(linkVar(m_variable));
    else if (isSubcatchVar(m_variable))
        m_subcatchResults = m_subcatchResultsByVar.value(subcatchVar(m_variable));

    // Slice OUT.2 — refresh per-feature override caches for every kind
    // in the active variable's scope so animation frames pick up the new
    // values. Each rebuild is O(kindFeatures) and cheap relative to the
    // result-fetch above.
    rebuildAllActiveKindFeatureOverrides();
}

// ---------------------------------------------------------------------------
// Phase 7 (2026-05-25) — per-attribute observed range, sampled across all
// periods. Replaces the hardcoded [0, 1] ramp range so values aren't
// clamped at one end. Sampling is O(periods × features) per variable;
// each variable is sampled at most once per opened results file.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Lazy attribute-range scans
//
// Each ensure*AttributeRange returns immediately:
//   - cached range if already computed,
//   - {0.0, 1.0} placeholder otherwise,
// and kicks off a background QtConcurrent scan if one isn't already
// running for this outCode. When the scan finishes, it posts back to
// the layer thread, fills the cache, drops the pending marker, and
// emits repaintRequested so the canvas re-styles with the real range.
//
// Previously these blocked the main thread on first call — for West
// Whiteland's 427 MB .out that meant several seconds of UI freeze the
// first time a results-styled sublayer was painted.
// ---------------------------------------------------------------------------

namespace {

enum class RangeKind { Node, Link, Subcatch };

// Worker: opens its own swmm_output handle to the same file (so it
// doesn't race the main thread's m_handle), scans every period for the
// given variable, returns the (min, max). Returns {0.0, 1.0} on any
// failure / degenerate case so callers don't have to special-case.
QPair<double, double> scanRangeBlocking(const QString &path,
                                        RangeKind     kind,
                                        int           outCode,
                                        int           totalSteps)
{
    auto fallback = QPair<double, double>(0.0, 1.0);
    if (outCode < 0 || totalSteps <= 0 || path.isEmpty()) return fallback;

    auto *handle = swmm_output_open(path.toUtf8().constData());
    if (!handle) return fallback;

    int n = 0;
    switch (kind) {
        case RangeKind::Node:     n = swmm_output_get_node_count(handle);     break;
        case RangeKind::Link:     n = swmm_output_get_link_count(handle);     break;
        case RangeKind::Subcatch: n = swmm_output_get_subcatch_count(handle); break;
    }
    if (n <= 0) { swmm_output_close(handle); return fallback; }

    QVector<float> buf(n);
    double mn =  std::numeric_limits<double>::infinity();
    double mx = -std::numeric_limits<double>::infinity();
    for (int t = 0; t < totalSteps; ++t) {
        int rc = 0;
        switch (kind) {
            case RangeKind::Node:
                rc = swmm_output_get_node_result(handle, t, outCode, buf.data()); break;
            case RangeKind::Link:
                rc = swmm_output_get_link_result(handle, t, outCode, buf.data()); break;
            case RangeKind::Subcatch:
                rc = swmm_output_get_subcatch_result(handle, t, outCode, buf.data()); break;
        }
        if (rc != 0) continue;
        for (float v : buf) {
            if (!std::isfinite(v)) continue;
            if (double(v) < mn) mn = double(v);
            if (double(v) > mx) mx = double(v);
        }
    }
    swmm_output_close(handle);

    if (!std::isfinite(mn) || !std::isfinite(mx) || mn >= mx) {
        mn = 0.0;
        mx = (std::isfinite(mx) && mx > 0.0) ? mx : 1.0;
    }
    return {mn, mx};
}

} // namespace

QPair<double, double> SWMMResultsLayer::ensureNodeAttributeRange(int outCode)
{
    if (outCode < 0 || !m_handle || m_totalSteps <= 0)
        return {0.0, 1.0};
    auto it = m_nodeAttributeRange.constFind(outCode);
    if (it != m_nodeAttributeRange.constEnd())
        return it.value();
    if (m_nodeRangePending.contains(outCode))
        return {0.0, 1.0};
    m_nodeRangePending.insert(outCode);

    const QString path        = m_resultsFilePath;
    const int     totalSteps  = m_totalSteps;
    QPointer<SWMMResultsLayer> self(this);
    QtConcurrent::run([self, path, totalSteps, outCode]() {
        const auto range = scanRangeBlocking(path, RangeKind::Node, outCode, totalSteps);
        QMetaObject::invokeMethod(qApp, [self, outCode, range]() {
            if (!self) return;
            self->m_nodeAttributeRange.insert(outCode, range);
            self->m_nodeRangePending.remove(outCode);
            emit self->repaintRequested();
        }, Qt::QueuedConnection);
    });
    return {0.0, 1.0};
}

QPair<double, double> SWMMResultsLayer::ensureLinkAttributeRange(int outCode)
{
    if (outCode < 0 || !m_handle || m_totalSteps <= 0)
        return {0.0, 1.0};
    auto it = m_linkAttributeRange.constFind(outCode);
    if (it != m_linkAttributeRange.constEnd())
        return it.value();
    if (m_linkRangePending.contains(outCode))
        return {0.0, 1.0};
    m_linkRangePending.insert(outCode);

    const QString path        = m_resultsFilePath;
    const int     totalSteps  = m_totalSteps;
    QPointer<SWMMResultsLayer> self(this);
    QtConcurrent::run([self, path, totalSteps, outCode]() {
        const auto range = scanRangeBlocking(path, RangeKind::Link, outCode, totalSteps);
        QMetaObject::invokeMethod(qApp, [self, outCode, range]() {
            if (!self) return;
            self->m_linkAttributeRange.insert(outCode, range);
            self->m_linkRangePending.remove(outCode);
            emit self->repaintRequested();
        }, Qt::QueuedConnection);
    });
    return {0.0, 1.0};
}

QPair<double, double> SWMMResultsLayer::ensureSubcatchAttributeRange(int outCode)
{
    if (outCode < 0 || !m_handle || m_totalSteps <= 0)
        return {0.0, 1.0};
    auto it = m_subcatchAttributeRange.constFind(outCode);
    if (it != m_subcatchAttributeRange.constEnd())
        return it.value();
    if (m_subcatchRangePending.contains(outCode))
        return {0.0, 1.0};
    m_subcatchRangePending.insert(outCode);

    const QString path        = m_resultsFilePath;
    const int     totalSteps  = m_totalSteps;
    QPointer<SWMMResultsLayer> self(this);
    QtConcurrent::run([self, path, totalSteps, outCode]() {
        const auto range = scanRangeBlocking(path, RangeKind::Subcatch, outCode, totalSteps);
        QMetaObject::invokeMethod(qApp, [self, outCode, range]() {
            if (!self) return;
            self->m_subcatchAttributeRange.insert(outCode, range);
            self->m_subcatchRangePending.remove(outCode);
            emit self->repaintRequested();
        }, Qt::QueuedConnection);
    });
    return {0.0, 1.0};
}

// ---------------------------------------------------------------------------
// Phase 8 (2026-05-25) — ramp-aware sublayer legend rows. Returns N
// graduated swatches per visible result-driven sublayer so the legend dock
// reflects what populateScene actually paints (a ramp), not the sublayer's
// fallback single colour. Bin count matches the BB legacy 5-interval
// convention; values labelled at bin centres.
// ---------------------------------------------------------------------------

QList<OpenSWMM::Render::LegendSymbolItem>
SWMMResultsLayer::sublayerLegendItems()
{
    using OpenSWMM::Render::LegendSymbolItem;
    using OpenSWMM::Render::SymbolLayer;
    using OpenSWMM::Render::SymbolLayerKind;

    QList<LegendSymbolItem> out;
    constexpr int kBinCount = 5;

    // Helper — build kBinCount graduated rows for one sublayer/attribute
    // pair. `kind` selects the swatch geometry (SimpleMarker / SimpleLine /
    // SimpleFill / MarkerLine) to match what populateScene draws.
    auto appendRampRows =
        [&](const QString &sublayerId,
            const QString &attribute,
            QPair<double, double> range,
            SymbolLayerKind swatchKind,
            double sizeOrWidthPx)
    {
        // Degenerate range → still emit ONE row at the midpoint colour so
        // the legend isn't empty when sampling failed.
        if (!(range.second > range.first)) {
            LegendSymbolItem item;
            item.label      = QStringLiteral("%1 = %2").arg(attribute)
                                .arg(range.first, 0, 'g', 3);
            item.sublayerId = sublayerId;
            item.range      = range;
            SymbolLayer sl;
            sl.kind = swatchKind;
            sl.props.insert(QStringLiteral("color"),
                            m_colorRamp.colorForValue(range.first).name(QColor::HexArgb));
            if (swatchKind == SymbolLayerKind::SimpleMarker)
                sl.props.insert(QStringLiteral("size"), sizeOrWidthPx);
            if (swatchKind == SymbolLayerKind::SimpleLine)
                sl.props.insert(QStringLiteral("width"), sizeOrWidthPx);
            item.symbol.layers.append(sl);
            out.append(item);
            return;
        }

        // Build a local ramp with the attribute's sampled range so
        // colorForValue interpolates correctly across the bins.
        RasterColorRamp localRamp = m_colorRamp;
        localRamp.minValue = range.first;
        localRamp.maxValue = range.second;

        const double span = range.second - range.first;
        for (int i = 0; i < kBinCount; ++i) {
            // Bin centre value (uniform spacing).
            const double t = (kBinCount == 1)
                ? 0.5
                : (double(i) + 0.5) / double(kBinCount);
            const double value = range.first + t * span;

            LegendSymbolItem item;
            // Label format mirrors the BB convention "<low> – <high>" so
            // users see the bin extents, not just centres.
            const double binLo = range.first + double(i)     / kBinCount * span;
            const double binHi = range.first + double(i + 1) / kBinCount * span;
            item.label = QStringLiteral("%1 – %2")
                .arg(binLo, 0, 'g', 3).arg(binHi, 0, 'g', 3);
            item.sublayerId = sublayerId;
            item.range      = { binLo, binHi };
            item.classKey   = QString::number(i); // matches BB legend editor's class indexing

            QColor col = localRamp.colorForValue(value);
            SymbolLayer sl;
            sl.kind = swatchKind;
            sl.props.insert(QStringLiteral("color"), col.name(QColor::HexArgb));
            if (swatchKind == SymbolLayerKind::SimpleMarker)
                sl.props.insert(QStringLiteral("size"), sizeOrWidthPx);
            if (swatchKind == SymbolLayerKind::SimpleLine)
                sl.props.insert(QStringLiteral("width"), sizeOrWidthPx);
            item.symbol.layers.append(sl);
            out.append(item);
        }
    };

    // Slice U-0 — walk the granular per-Category sublayer set. Each
    // visible sublayer with an attribute and result data emits a header
    // row + N graduated swatches. Static / no-attribute sublayers (e.g.
    // RainGages, or any sublayer with useColorRamp=false) emit a single
    // swatch row drawn from the sublayer's single-symbol colour.
    using OpenSWMM::Render::FeatureSublayer;
    using OpenSWMM::Render::PointFeatureSublayerStyle;
    using OpenSWMM::Render::LineFeatureSublayerStyle;
    using OpenSWMM::Render::PolygonFeatureSublayerStyle;

    for (auto *base : sublayers()) {
        auto *sub = qobject_cast<FeatureSublayer *>(base);
        if (!sub || !sub->isVisible() || !sub->featureStyle()) continue;

        const QString sublayerId = sub->id();
        const QString attribute  = sub->featureStyle()->attribute();
        const bool    useRamp    = sub->featureStyle()->useColorRamp();
        const QColor  singleCol  = sub->featureStyle()->color();

        // Pick the swatch geometry + per-archetype size/width to feed
        // appendRampRows. Records also which range sampler to call.
        SymbolLayerKind swatchKind = SymbolLayerKind::SimpleMarker;
        double          swatchSize = 6.0;
        QPair<double, double> range{0.0, 1.0};
        bool haveData = false;

        switch (sub->archetype()) {
            case FeatureSublayer::Archetype::Point: {
                auto *st = sub->pointStyle();
                swatchKind = SymbolLayerKind::SimpleMarker;
                swatchSize = std::max(2.0, st->markerSizePx());
                if (!attribute.isEmpty()) {
                    const int code = nodeOutCodeForAttribute(attribute);
                    if (code >= 0) {
                        range = ensureNodeAttributeRange(code);
                        haveData = true;
                    }
                }
                break;
            }
            case FeatureSublayer::Archetype::Line: {
                auto *st = sub->lineStyle();
                swatchKind = SymbolLayerKind::SimpleLine;
                swatchSize = std::max(1.0, st->lineWidthPx());
                if (!attribute.isEmpty()) {
                    const int code = linkOutCodeForAttribute(attribute);
                    if (code >= 0) {
                        range = ensureLinkAttributeRange(code);
                        haveData = true;
                    }
                }
                break;
            }
            case FeatureSublayer::Archetype::Polygon: {
                swatchKind = SymbolLayerKind::SimpleFill;
                swatchSize = 0.0;
                if (!attribute.isEmpty()) {
                    const int code = subcatchOutCodeForAttribute(attribute);
                    if (code >= 0) {
                        range = ensureSubcatchAttributeRange(code);
                        haveData = true;
                    }
                }
                break;
            }
        }

        // Header row — labelled with the kind + attribute (or kind alone
        // when the sublayer paints a single symbol).
        LegendSymbolItem header;
        header.label = useRamp && haveData
                         ? QStringLiteral("%1 — %2").arg(sub->displayName(), attribute)
                         : sub->displayName();
        header.sublayerId = sublayerId;
        out.append(header);

        if (useRamp && haveData) {
            appendRampRows(sublayerId, attribute, range, swatchKind, swatchSize);
        } else {
            // Single-symbol row for static / no-attribute sublayers.
            LegendSymbolItem item;
            item.label      = singleCol.isValid()
                                ? singleCol.name(QColor::HexArgb)
                                : QStringLiteral("single symbol");
            item.sublayerId = sublayerId;
            SymbolLayer sl;
            sl.kind = swatchKind;
            sl.props.insert(QStringLiteral("color"),
                            singleCol.isValid() ? singleCol.name(QColor::HexArgb)
                                                : QStringLiteral("#606060"));
            if (swatchKind == SymbolLayerKind::SimpleMarker)
                sl.props.insert(QStringLiteral("size"), swatchSize);
            if (swatchKind == SymbolLayerKind::SimpleLine)
                sl.props.insert(QStringLiteral("width"), swatchSize);
            item.symbol.layers.append(sl);
            out.append(item);
        }

        // Optional: emit a Flow-arrows row when the line sublayer has
        // arrows enabled. Single swatch (single colour, single size).
        if (sub->archetype() == FeatureSublayer::Archetype::Line) {
            auto *st = sub->lineStyle();
            if (st && st->showFlowArrows()) {
                const auto flowRange = ensureLinkAttributeRange(SWMM_OUT_LINK_FLOW);
                const double magMax = std::max(std::abs(flowRange.first),
                                                std::abs(flowRange.second));
                LegendSymbolItem item;
                item.label = tr("%1 flow arrows (|Q|max = %2)")
                               .arg(sub->displayName())
                               .arg(magMax, 0, 'g', 3);
                item.sublayerId = sublayerId;
                SymbolLayer sl;
                sl.kind = SymbolLayerKind::SimpleMarker;
                sl.props.insert(QStringLiteral("shape"), QStringLiteral("arrow"));
                sl.props.insert(QStringLiteral("color"),
                                st->arrowColor().name(QColor::HexArgb));
                sl.props.insert(QStringLiteral("size"), st->arrowLengthPx());
                item.symbol.layers.append(sl);
                out.append(item);
            }
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

int SWMMResultsLayer::currentTimeStep() const { return m_currentStep; }

QDateTime SWMMResultsLayer::currentDateTime() const
{
    if (!m_handle || m_totalSteps <= 0)
        return m_startDateTime;

    double t = 0.0;
    swmm_output_get_period_time(m_handle, m_currentStep, &t);
    return julianToDateTime(t);
}

int  SWMMResultsLayer::totalTimeSteps()   const { return m_totalSteps;   }
QDateTime SWMMResultsLayer::startDateTime() const { return m_startDateTime; }
QDateTime SWMMResultsLayer::endDateTime()   const { return m_endDateTime;   }
int  SWMMResultsLayer::reportStepSeconds()  const { return m_reportStepSec; }

void SWMMResultsLayer::setCurrentTimeStep(int step)
{
    if (m_totalSteps <= 0)
        return;

    step = std::clamp(step, 0, m_totalSteps - 1);
    if (step == m_currentStep && !m_nodeResults.isEmpty())
        return;

    m_currentStep = step;
    fetchResultsForStep(m_currentStep);

    // Slice §Y.2 — timestep-only advance: scene geometry is unchanged,
    // only per-feature colours and override values need to be re-applied.
    // restyleScene picks this up via the cached items in m_itemByFeature.
    escalateSceneDirty(SceneDirty::Values);

    emit currentTimeStepChanged(m_currentStep);
    emit currentDateTimeChanged(currentDateTime());
    emit repaintRequested();
}

void SWMMResultsLayer::escalateSceneDirty(SceneDirty next)
{
    if (static_cast<int>(next) > static_cast<int>(m_sceneDirty))
        m_sceneDirty = next;
}

void SWMMResultsLayer::stepForward(bool loop)
{
    if (m_totalSteps <= 0)
        return;

    int next = m_currentStep + 1;
    if (next >= m_totalSteps)
        next = loop ? 0 : m_totalSteps - 1;

    setCurrentTimeStep(next);
}

void SWMMResultsLayer::stepBackward(bool loop)
{
    if (m_totalSteps <= 0)
        return;

    int prev = m_currentStep - 1;
    if (prev < 0)
        prev = loop ? m_totalSteps - 1 : 0;

    setCurrentTimeStep(prev);
}

// ---------------------------------------------------------------------------
// Variable & colour mapping
// ---------------------------------------------------------------------------

SWMMResultVariable SWMMResultsLayer::variable() const { return m_variable; }

void SWMMResultsLayer::setVariable(SWMMResultVariable var)
{
    if (m_variable == var)
        return;

    m_variable = var;
    // Re-fetch results for the current step with the new variable.
    m_nodeResults.clear();
    m_linkResults.clear();
    m_subcatchResults.clear();
    fetchResultsForStep(m_currentStep);

    // Slice §Y.2 — variable swap changes which results-vector each sublayer
    // reads + may flip rows in/out of finite-value range. Force structural.
    escalateSceneDirty(SceneDirty::Structural);

    emit variableChanged(m_variable);
    emit repaintRequested();
}

RasterColorRamp SWMMResultsLayer::colorRamp() const { return m_colorRamp; }

void SWMMResultsLayer::setColorRamp(const RasterColorRamp &ramp)
{
    m_colorRamp = ramp;
    // Slice §Y.2 — ramp swap only changes colours, not geometry.
    escalateSceneDirty(SceneDirty::Values);
    emit repaintRequested();
}

// ── Slice BI Phase 8.13.6.3 — renderer plumbing ─────────────────────────────
//
// API additions only. The paint loop still reads m_colorRamp / m_variable;
// sub-phase 8.13.6.4 will swap those reads over to m_renderer. Until then the
// renderer is essentially write-only state — callers can configure it but
// nothing here consumes it.

OpenSWMM::Render::IFeatureRenderer *SWMMResultsLayer::renderer() const
{
    return m_renderer.get();
}

void SWMMResultsLayer::setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    if (!r)               // contract: renderer() never returns nullptr
        return;
    if (r.get() == m_renderer.get())
        return;           // no-op if the same pointer is reassigned
    m_renderer = std::move(r);
    // Slice §Y.2 — renderer swap can change filter / override semantics
    // for every kind; the cached items may no longer represent the right
    // set of visible features. Force structural.
    escalateSceneDirty(SceneDirty::Structural);
    emit rendererChanged();
}

// Slice OUT.1 — per-kind renderer slots. Mirrors SWMMModelLayer's
// kindRenderer / setKindRenderer / resetKindRendererToDefaults API.
// Slot construction is lazy: slots default to nullptr until the user
// explicitly customises a kind. Slice OUT.2 will refactor the paint
// loop to consult these slots (with the layer-level renderer as
// fallback).

namespace {

QString kindDefaultResultVariable(SWMMModelLayer::Category c)
{
    switch (c) {
    case SWMMModelLayer::CatJunctions:
    case SWMMModelLayer::CatOutfalls:
    case SWMMModelLayer::CatStorage:
    case SWMMModelLayer::CatDividers:
        return QStringLiteral("NodeDepth");
    case SWMMModelLayer::CatConduits:
    case SWMMModelLayer::CatPumps:
    case SWMMModelLayer::CatOrifices:
    case SWMMModelLayer::CatWeirs:
    case SWMMModelLayer::CatOutlets:
        return QStringLiteral("LinkFlow");
    case SWMMModelLayer::CatSubcatchments:
        return QStringLiteral("SubcatchRunoff");
    case SWMMModelLayer::CatRainGages:
    case SWMMModelLayer::NumCategories:
        return QString();
    }
    return QString();
}

std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>
makeDefaultKindRenderer(SWMMModelLayer::Category c)
{
    auto g = std::make_unique<OpenSWMM::Render::GraduatedRenderer>();
    g->setClassifyAttribute(kindDefaultResultVariable(c));
    g->setRamp(RasterColorRamp::viridis(0.0, 1.0));
    OpenSWMM::Render::IntervalBinner b;
    b.setMethod(OpenSWMM::Render::BinMethod::EqualInterval);
    b.setBinCount(5);
    g->setBinner(b);
    return g;
}

} // namespace

OpenSWMM::Render::IFeatureRenderer *SWMMResultsLayer::kindRenderer(
    SWMMModelLayer::Category c) const
{
    const int i = static_cast<int>(c);
    if (i < 0 || i >= static_cast<int>(m_kindRenderers.size()))
        return nullptr;
    return m_kindRenderers[i].get();
}

void SWMMResultsLayer::setKindRenderer(
    SWMMModelLayer::Category c,
    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> r)
{
    const int i = static_cast<int>(c);
    if (i < 0 || i >= static_cast<int>(SWMMModelLayer::NumCategories))
        return;
    if (m_kindRenderers.size() < static_cast<size_t>(SWMMModelLayer::NumCategories))
        m_kindRenderers.resize(SWMMModelLayer::NumCategories);
    if (m_kindRenderers[i].get() == r.get())
        return;
    m_kindRenderers[i] = std::move(r);
    // Slice OUT.2 — installing a new kind renderer invalidates the
    // override cache for that kind; rebuild immediately so the next
    // paint hits the new colors without an extra animation tick.
    rebuildKindFeatureOverrides(c);
    // Slice §Y.2 — renderer swap can flip size / shape / width / dash
    // overrides too (X.15 / X.20), which changes item geometry. Force
    // structural rebuild rather than restyle.
    escalateSceneDirty(SceneDirty::Structural);
    emit rendererChanged();
    emit repaintRequested();
}

// ─── Slice B.5 — Rule Model mirror over per-kind renderers ─────────────

OpenSWMM::Render::RuleList *SWMMResultsLayer::ruleList()
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

const OpenSWMM::Render::RuleList *SWMMResultsLayer::ruleList() const
{
    if (!m_ruleList)
        buildRuleListLazy();
    return m_ruleList.get();
}

void SWMMResultsLayer::buildRuleListLazy() const
{
    auto *self = const_cast<SWMMResultsLayer *>(this);
    m_ruleList = std::make_unique<OpenSWMM::Render::RuleList>(self);

    // Walk every Category. Each Rule's name is the matching kindKey
    // ("Junctions", "Conduits", ...) — same vocabulary as B.4 so the
    // Active Rule combo reads identically on model and results layers.
    for (int i = 0; i < SWMMModelLayer::NumCategories; ++i) {
        const SWMMModelLayer::Category c =
            static_cast<SWMMModelLayer::Category>(i);
        OpenSWMM::Render::IFeatureRenderer *src =
            (i < static_cast<int>(m_kindRenderers.size()))
                ? m_kindRenderers[static_cast<size_t>(i)].get()
                : nullptr;
        std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> cloned =
            src ? src->clone()
                : std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>();
        auto *rule = m_ruleList->append(std::make_unique<OpenSWMM::Render::Rule>(
            SWMMModelLayer::kindKey(c), std::move(cloned)));

        // Rule-side renderer swaps propagate back through the existing
        // setKindRenderer setter — which rebuilds override caches +
        // escalates structural dirtiness so the next animation tick
        // picks up the new symbol geometry.
        //
        // Slice SS.5 — also back-propagate single-symbol attribute
        // edits onto the matching FeatureSublayerStyle so the painter
        // (which reads line/arrow/marker knobs from the style, not
        // from per-feature override caches) renders the change. No-op
        // when the renderer isn't SingleSymbol — Graduated /
        // Categorized modes feed the painter via the per-feature
        // override caches (see plan §4.5).
        QObject::connect(rule, &OpenSWMM::Render::Rule::rendererReplaced,
                         self, [self, c, rule]() {
            if (auto *r = rule->renderer())
                self->setKindRenderer(c, r->clone());

            // Slice SS.5 — back-propagate to the per-category
            // FeatureSublayerStyle. Marker / Line / Polygon archetypes
            // each pick the matching prop subset.
            using namespace OpenSWMM::Render;
            auto *ssr = dynamic_cast<const SingleSymbolRenderer *>(
                rule->renderer());
            if (!ssr) return;
            const SymbolStyle &style = ssr->symbol();
            if (style.layers.isEmpty()) return;
            const SymbolLayer &layer = style.layers.first();

            auto *sub = self->featureSublayer(c);
            if (!sub || !sub->featureStyle()) return;

            // Common: base-style fallback color (used when neither
            // override cache nor ramp resolves a feature colour).
            // For point/polygon this is `fillColor`; for line it's
            // the line `color`. Both flow into FeatureSublayerStyle::color.
            switch (sub->archetype()) {
            case FeatureSublayer::Archetype::Point: {
                auto *st = sub->pointStyle();
                if (!st) break;
                const auto spec = MarkerSymbolLayerSpec::fromSymbolLayer(layer);
                st->setColor(spec.fillColor);
                st->setMarkerSizePx(spec.sizePx);
                // PointFeatureSublayerStyle::MarkerShape only models 4
                // shapes (Circle / Square / Triangle / Diamond /
                // Star — int values 0..4). Richer Z.4 shapes round-
                // trip through the SymbolLayer prop bag but fall back
                // to Circle on the legacy sublayer; same convention as
                // Slice Z.6a step 3 in swmm2dmeshlayer.cpp.
                int shapeIdx = static_cast<int>(spec.shape);
                if (shapeIdx < 0 || shapeIdx > 4) shapeIdx = 0;
                st->setShape(static_cast<PointFeatureSublayerStyle::MarkerShape>(
                    shapeIdx));
                break;
            }
            case FeatureSublayer::Archetype::Line: {
                auto *st = sub->lineStyle();
                if (!st) break;
                const auto spec = LineSymbolLayerSpec::fromSymbolLayer(layer);
                st->setColor(spec.color);
                st->setLineWidthPx(spec.width);
                st->setDashPattern(spec.penStyle);
                st->setShowFlowArrows(spec.drawArrows);
                st->setArrowLengthPx(spec.arrows.lengthPx);
                st->setArrowWidthPx(spec.arrows.widthPx);
                st->setArrowColor(spec.arrows.color);
                break;
            }
            case FeatureSublayer::Archetype::Polygon: {
                auto *st = sub->polygonStyle();
                if (!st) break;
                // Polygon reuses marker keys for fill / outline. The
                // dedicated fillOpacity knob comes from a direct prop
                // read since MarkerSymbolLayerSpec doesn't carry it.
                const auto spec = MarkerSymbolLayerSpec::fromSymbolLayer(layer);
                st->setColor(spec.fillColor);
                st->setOutlineColor(spec.outlineColor);
                st->setOutlineWidthPx(spec.outlineWidth);
                if (layer.props.contains(QStringLiteral("fillOpacity"))) {
                    st->setFillOpacity(
                        layer.props.value(QStringLiteral("fillOpacity"))
                            .toDouble());
                }
                break;
            }
            }
        });
    }

    // Slice Z.7a — per-frame rebinning. On each animation tick, walk
    // the rules; for any rule with rebinPerFrame=true, re-sample the
    // bound attribute from the current step's data and re-classify
    // its graduated renderer. The Rule-aware setKindRenderer chain
    // already refreshes per-feature override caches + emits repaint.
    QObject::connect(self, &SWMMResultsLayer::currentTimeStepChanged,
                     self, [self](int) { self->rebinDynamicRulesIfNeeded(); });
}

// ---------------------------------------------------------------------------
// Slice DM.1 — IAttributeProvider
// ---------------------------------------------------------------------------
//
// Returns the engine output codes the painter already consumes (see the
// nodeOutCodeForAttribute / linkOutCodeForAttribute /
// subcatchOutCodeForAttribute mappers earlier in this file). Canonical
// keys use the short lower-case form — those are the values the
// Graduated / Categorized renderer's `classifyAttribute()` and the
// FeatureSublayerStyle's `attribute` Q_PROPERTY both round-trip through
// .oswp today. Display strings carry units so the picker reads
// naturally. All entries are isDynamic=true (per animation frame); Z.7's
// "Recompute breaks per frame" keys off this flag.
//
// See RENDERING_DIALOG_DEMO_PLAN.md §2.

QVector<OpenSWMM::Render::AttributeField>
SWMMResultsLayer::availableAttributes(OpenSWMMVis::SwmmCategory cat) const
{
    using OpenSWMM::Render::AttributeField;
    // Switch is over the `cat` parameter (OpenSWMMVis::SwmmCategory); alias L
    // to that enum so the case labels match the switched type. (The matching
    // SWMMModelLayer::Category ordinals are identical, but SWMMModelLayer is
    // only forward-declared in this scope, hence the incomplete-type errors.)
    using L = OpenSWMMVis::SwmmCategory;

    auto make = [](const char *name, const char *display,
                   const char *unit) -> AttributeField {
        AttributeField f;
        f.name        = QString::fromLatin1(name);
        f.displayName = QString::fromLatin1(display);
        f.type        = QMetaType::Double;
        f.isDynamic   = true;
        f.unit        = QString::fromLatin1(unit);
        return f;
    };

    QVector<AttributeField> out;
    switch (cat) {
    case L::CatJunctions:
    case L::CatOutfalls:
    case L::CatStorage:
    case L::CatDividers:
        out.append(make("depth",         "depth (m)",             "m"));
        out.append(make("head",          "head (m)",              "m"));
        out.append(make("volume",        "volume (m³)",      "m³"));
        out.append(make("inflow",        "inflow (m³/s)",    "m³/s"));
        out.append(make("overflow",      "overflow (m³/s)",  "m³/s"));
        out.append(make("lateralInflow", "lateral inflow (m³/s)", "m³/s"));
        break;
    case L::CatConduits:
    case L::CatPumps:
    case L::CatOrifices:
    case L::CatWeirs:
    case L::CatOutlets:
        out.append(make("flow",     "flow (m³/s)",  "m³/s"));
        out.append(make("depth",    "depth (m)",         "m"));
        out.append(make("velocity", "velocity (m/s)",    "m/s"));
        out.append(make("capacity", "capacity",          ""));
        break;
    case L::CatSubcatchments:
        out.append(make("runoff",       "runoff (m³/s)",   "m³/s"));
        out.append(make("infiltration", "infiltration (m/s)",   "m/s"));
        out.append(make("evaporation",  "evaporation (m/s)",    "m/s"));
        out.append(make("snowDepth",    "snow depth (m)",       "m"));
        break;
    case L::CatRainGages:
        // Rain gages don't carry per-feature engine output today.
        break;
    default:
        break;
    }
    return out;
}

namespace {
bool catIsNodeScope(SWMMModelLayer::Category c);
bool catIsLinkScope(SWMMModelLayer::Category c);
}

void SWMMResultsLayer::rebinDynamicRulesIfNeeded()
{
    using namespace OpenSWMM::Render;

    // O1-3 — drive off the canonical per-kind renderers (m_kindRenderers),
    // not the m_ruleList mirror. setKindRenderer() never invalidates the
    // lazily-built rule list, so the mirror goes stale after any kind-tree
    // edit; the per-kind renderers are the single source of truth post-MVC
    // switch. For each kind whose Graduated renderer requests
    // PerFrameAutoStretch, re-classify against this frame's samples and
    // refresh the override cache so the next repaint shows the new breaks.
    for (int i = 0; i < static_cast<int>(SWMMModelLayer::NumCategories); ++i) {
        if (i >= static_cast<int>(m_kindRenderers.size()))
            break;

        auto *g = dynamic_cast<GraduatedRenderer *>(
            m_kindRenderers[static_cast<size_t>(i)].get());
        if (!g)
            continue;   // only Graduated rebins; Single/Categorized N/A

        if (g->rangeMode() != RangeMode::PerFrameAutoStretch)
            continue;

        const SWMMModelLayer::Category cat =
            static_cast<SWMMModelLayer::Category>(i);

        // Collect the current step's samples for the renderer's bound
        // attribute via the same lookup rebuildKindFeatureOverrides
        // uses. When no samples are available (variable not loaded,
        // empty file, etc.) skip silently — leave existing breaks.
        const QString attr = g->classifyAttribute();
        const QVector<float> *valuesPtr = nullptr;
        if (catIsNodeScope(cat)) {
            const int outCode = nodeOutCodeForAttribute(attr);
            const auto it = m_nodeResultsByVar.constFind(outCode);
            if (it != m_nodeResultsByVar.constEnd()) valuesPtr = &it.value();
        } else if (catIsLinkScope(cat)) {
            const int outCode = linkOutCodeForAttribute(attr);
            const auto it = m_linkResultsByVar.constFind(outCode);
            if (it != m_linkResultsByVar.constEnd()) valuesPtr = &it.value();
        } else if (cat == SWMMModelLayer::CatSubcatchments) {
            const int outCode = subcatchOutCodeForAttribute(attr);
            const auto it = m_subcatchResultsByVar.constFind(outCode);
            if (it != m_subcatchResultsByVar.constEnd()) valuesPtr = &it.value();
        }
        if (!valuesPtr || valuesPtr->isEmpty())
            continue;

        // autoClassify takes doubles; convert from float storage.
        QVector<double> samples;
        samples.reserve(valuesPtr->size());
        for (float v : *valuesPtr)
            samples.append(static_cast<double>(v));
        g->autoClassify(samples);

        // Refresh per-feature override cache so paint uses the new
        // breaks immediately on the next repaint.
        rebuildKindFeatureOverrides(cat);
    }
}

void SWMMResultsLayer::resetKindRendererToDefaults(SWMMModelLayer::Category c)
{
    setKindRenderer(c, makeDefaultKindRenderer(c));
}

// ---------------------------------------------------------------------------
// Slice OUT.2 — per-feature override cache
// ---------------------------------------------------------------------------

namespace {

// Stringified SWMMResultVariable enumerator (matches what CTX.1 puts in
// the Graduated tab's Attribute combo, so renderer.classifyAttribute()
// can compare cleanly).
QString variableEnumName(SWMMResultVariable v)
{
    switch (v) {
    case SWMMResultVariable::NodeDepth:            return QStringLiteral("NodeDepth");
    case SWMMResultVariable::NodeHead:             return QStringLiteral("NodeHead");
    case SWMMResultVariable::NodeVolume:           return QStringLiteral("NodeVolume");
    case SWMMResultVariable::NodeInflow:           return QStringLiteral("NodeInflow");
    case SWMMResultVariable::NodeOverflow:         return QStringLiteral("NodeOverflow");
    case SWMMResultVariable::NodeLateralInflow:    return QStringLiteral("NodeLateralInflow");
    case SWMMResultVariable::LinkFlow:             return QStringLiteral("LinkFlow");
    case SWMMResultVariable::LinkDepth:            return QStringLiteral("LinkDepth");
    case SWMMResultVariable::LinkVelocity:         return QStringLiteral("LinkVelocity");
    case SWMMResultVariable::LinkCapacity:         return QStringLiteral("LinkCapacity");
    case SWMMResultVariable::SubcatchRunoff:       return QStringLiteral("SubcatchRunoff");
    case SWMMResultVariable::SubcatchInfiltration: return QStringLiteral("SubcatchInfiltration");
    case SWMMResultVariable::SubcatchEvaporation:  return QStringLiteral("SubcatchEvaporation");
    case SWMMResultVariable::SubcatchSnowDepth:    return QStringLiteral("SubcatchSnowDepth");
    }
    return QString();
}

bool catIsNodeScope(SWMMModelLayer::Category c)
{
    return c == SWMMModelLayer::CatJunctions || c == SWMMModelLayer::CatOutfalls
        || c == SWMMModelLayer::CatStorage   || c == SWMMModelLayer::CatDividers;
}

bool catIsLinkScope(SWMMModelLayer::Category c)
{
    return c == SWMMModelLayer::CatConduits || c == SWMMModelLayer::CatPumps
        || c == SWMMModelLayer::CatOrifices || c == SWMMModelLayer::CatWeirs
        || c == SWMMModelLayer::CatOutlets;
}

bool catIsSubcatchScope(SWMMModelLayer::Category c)
{
    return c == SWMMModelLayer::CatSubcatchments;
}

bool catMatchesVariable(SWMMModelLayer::Category c, SWMMResultVariable v)
{
    if (isNodeVar(v))     return catIsNodeScope(c);
    if (isLinkVar(v))     return catIsLinkScope(c);
    if (isSubcatchVar(v)) return catIsSubcatchScope(c);
    return false;
}

// Extract a QColor from the first SymbolLayer's "color" prop. Tolerant
// of QColor variants (Qt registers QColor metatype) and string hex
// shortcuts ("#rrggbb" / named).
QColor extractStyleColor(const OpenSWMM::Render::SymbolStyle &s, QColor fallback)
{
    if (s.layers.isEmpty()) return fallback;
    const QVariant v = s.layers.first().props.value(QStringLiteral("color"));
    if (!v.isValid()) return fallback;
    if (v.canConvert<QColor>()) {
        const QColor c = v.value<QColor>();
        if (c.isValid()) return c;
    }
    const QColor c(v.toString());
    return c.isValid() ? c : fallback;
}

double extractStyleSize(const OpenSWMM::Render::SymbolStyle &s)
{
    if (s.layers.isEmpty()) return -1.0;
    const QVariant v = s.layers.first().props.value(QStringLiteral("size"));
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? d : -1.0;
}

double extractStyleWidth(const OpenSWMM::Render::SymbolStyle &s)
{
    if (s.layers.isEmpty()) return -1.0;
    const QVariant v = s.layers.first().props.value(QStringLiteral("width"));
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? d : -1.0;
}

int extractStyleShape(const OpenSWMM::Render::SymbolStyle &s)
{
    if (s.layers.isEmpty()) return -1;
    const QVariant v = s.layers.first().props.value(QStringLiteral("shape"));
    bool ok = false;
    const int i = v.toInt(&ok);
    return ok ? i : -1;
}

int extractStyleDash(const OpenSWMM::Render::SymbolStyle &s)
{
    if (s.layers.isEmpty()) return -1;
    const QVariant v = s.layers.first().props.value(QStringLiteral("dash"));
    bool ok = false;
    const int i = v.toInt(&ok);
    return ok ? i : -1;
}

} // namespace

void SWMMResultsLayer::rebuildKindFeatureOverrides(SWMMModelLayer::Category c)
{
    const int idx = static_cast<int>(c);
    if (idx < 0 || idx >= static_cast<int>(SWMMModelLayer::NumCategories))
        return;

    m_kindFeatureColors[idx].clear();
    m_kindFeatureSizes[idx].clear();
    m_kindFeatureWidths[idx].clear();
    m_kindFeatureShapes[idx].clear();
    m_kindFeatureDashes[idx].clear();
    m_kindUsesOverrides[idx] = false;

    if (!m_modelLayer || !m_handle)
        return;

    OpenSWMM::Render::IFeatureRenderer *kr = kindRenderer(c);
    if (!kr)
        return;                       // no override; paint falls back to ramp

    // Slice S3 — drive the cache off the renderer's own classify attribute
    // instead of the (legacy) layer-wide m_variable. Each kind renderer
    // can target its own attribute (LinkFlow vs LinkDepth vs LinkCapacity)
    // independently of what the layer's primary variable is, so the
    // graduated-Conduits-while-NodeDepth-active case works.
    QString classifyAttr;
    if (auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(kr))
        classifyAttr = g->classifyAttribute();
    else if (auto *cz = dynamic_cast<OpenSWMM::Render::CategorizedRenderer *>(kr))
        classifyAttr = cz->classifyAttribute();
    // SingleSymbolRenderer / RuleBasedRenderer (no classify attr) fall
    // through with an empty classifyAttr; that's fine — symbolFor returns
    // the static symbol and the per-feature cache still gets populated.

    const int count = m_modelLayer->categoryCount(c);
    if (count <= 0)
        return;

    m_kindFeatureColors[idx].resize(count);
    m_kindFeatureSizes [idx].resize(count);
    m_kindFeatureWidths[idx].resize(count); m_kindFeatureWidths[idx].fill(-1.0);
    m_kindFeatureShapes[idx].resize(count); m_kindFeatureShapes[idx].fill(-1);
    m_kindFeatureDashes[idx].resize(count); m_kindFeatureDashes[idx].fill(-1);

    // Choose the right (outputIdxMap, perVarResults) pair for the scope.
    // For each row we look up the feature's output index then pull the
    // value from the per-var cache keyed by the renderer's attribute.
    const QHash<QString, int> *outIdxMap = nullptr;
    const QVector<float>      *valuesPtr = nullptr;
    if (catIsNodeScope(c)) {
        outIdxMap = &m_nodeOutputIdx;
        const int outCode = nodeOutCodeForAttribute(classifyAttr);
        const auto it = m_nodeResultsByVar.constFind(outCode);
        if (it != m_nodeResultsByVar.constEnd())
            valuesPtr = &it.value();
    } else if (catIsLinkScope(c)) {
        outIdxMap = &m_linkOutputIdx;
        const int outCode = linkOutCodeForAttribute(classifyAttr);
        const auto it = m_linkResultsByVar.constFind(outCode);
        if (it != m_linkResultsByVar.constEnd())
            valuesPtr = &it.value();
    } else if (catIsSubcatchScope(c)) {
        outIdxMap = &m_subcatchOutputIdx;
        const int outCode = subcatchOutCodeForAttribute(classifyAttr);
        const auto it = m_subcatchResultsByVar.constFind(outCode);
        if (it != m_subcatchResultsByVar.constEnd())
            valuesPtr = &it.value();
    } else {
        return;
    }

    // Categorized renderers classify on a *string* attribute pulled from
    // the model's identifyByName map ("tag", "Link type", "shape", …),
    // not from the numeric per-var results cache.  Detect this case so
    // the symbolFor call below can hand the renderer the right value
    // type — without this branch the categorized leg always falls back
    // to fallbackSymbol() and every feature paints the same colour.
    const bool useStringAttrLookup =
        (dynamic_cast<OpenSWMM::Render::CategorizedRenderer *>(kr) != nullptr)
        && !classifyAttr.isEmpty()
        && valuesPtr == nullptr;

    // Slice X.21 — populate the per-(category, attr) cache once, then
    // every feature row reads from a flat QVector<QString> instead of
    // hitting identifyByName (a linear scan over the cached SoA every
    // call).  Topology is invariant to time, so the cache is safe to
    // keep across animation ticks until the model layer reports a
    // schema change.
    const QString cacheKey = QString::number(idx) + QLatin1Char('/') + classifyAttr;
    const QVector<QString> *stringAttrs = nullptr;
    if (useStringAttrLookup) {
        auto it = m_categoricalAttrCache.find(cacheKey);
        if (it == m_categoricalAttrCache.end() || it.value().size() != count) {
            QVector<QString> values; values.reserve(count);
            for (int row = 0; row < count; ++row) {
                const QString name = m_modelLayer->objectNameAt(c, row);
                const QVariantMap byName = m_modelLayer->identifyByName(name);
                values.push_back(byName.value(classifyAttr).toString());
            }
            it = m_categoricalAttrCache.insert(cacheKey, std::move(values));
        }
        stringAttrs = &it.value();
    }

    // Slice S3 — gather samples for autoClassify (graduated renderer
    // needs real data to compute meaningful breaks; the freshly-installed
    // renderer has empty m_lastBreaks and a default [0,1] ramp range, so
    // without this the binner collapses every feature into the same bin).
    QVector<double> values; values.reserve(count);
    for (int row = 0; row < count; ++row) {
        const QString name = m_modelLayer->objectNameAt(c, row);
        if (outIdxMap && valuesPtr) {
            const auto it = outIdxMap->constFind(name);
            if (it != outIdxMap->constEnd()) {
                const int outIdx = it.value();
                if (outIdx >= 0 && outIdx < valuesPtr->size()) {
                    const double v = static_cast<double>(valuesPtr->at(outIdx));
                    if (std::isfinite(v)) values.push_back(v);
                }
            }
        }
    }
    // F4 — shared classify gate (same isEmpty + non-empty-samples condition as
    // the model layer) so the two rebuild paths cannot drift.
    if (auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(kr))
        OpenSWMM::Render::GraduatedRenderer::classifyIfNeeded(g, values);

    for (int row = 0; row < count; ++row) {
        const QString name = m_modelLayer->objectNameAt(c, row);
        double value = std::numeric_limits<double>::quiet_NaN();
        if (outIdxMap && valuesPtr) {
            const auto it = outIdxMap->constFind(name);
            if (it != outIdxMap->constEnd()) {
                const int outIdx = it.value();
                if (outIdx >= 0 && outIdx < valuesPtr->size())
                    value = static_cast<double>(valuesPtr->at(outIdx));
            }
        }

        QVariantMap attrs;
        if (!classifyAttr.isEmpty() && std::isfinite(value))
            attrs.insert(classifyAttr, value);
        if (useStringAttrLookup && stringAttrs) {
            // Slice X.21 — cached lookup, populated once per (category,
            // classifyAttr) at the top of the function.
            attrs.insert(classifyAttr, stringAttrs->value(row));
        }

        OpenSWMM::Render::FeatureRef ref;
        ref.featureIndex = row;
        ref.categoryHint = SWMMModelLayer::kindKey(c);

        const auto style = kr->symbolFor(ref, attrs);
        // Fallback color when the renderer didn't (or couldn't) classify:
        // sample the layer-level ramp so the canvas still has a sensible
        // colour instead of an invalid hole.
        const QColor rampCol = std::isfinite(value)
            ? m_colorRamp.colorForValue(value)
            : QColor();
        m_kindFeatureColors[idx][row] = extractStyleColor(style, rampCol);
        m_kindFeatureSizes [idx][row] = extractStyleSize(style);
        m_kindFeatureWidths[idx][row] = extractStyleWidth(style);
        m_kindFeatureShapes[idx][row] = extractStyleShape(style);
        m_kindFeatureDashes[idx][row] = extractStyleDash(style);
    }

    m_kindUsesOverrides[idx] = true;
}

void SWMMResultsLayer::rebuildAllActiveKindFeatureOverrides()
{
    // Slice S3 — drop the scope-match gating. rebuildKindFeatureOverrides
    // is now variable-agnostic (drives off the renderer's classifyAttribute)
    // so we can rebuild every category unconditionally; the function self-
    // exits when no renderer is installed for the kind.
    for (int i = 0; i < static_cast<int>(SWMMModelLayer::NumCategories); ++i)
        rebuildKindFeatureOverrides(static_cast<SWMMModelLayer::Category>(i));
}

void SWMMResultsLayer::autoStretchColorRamp()
{
    // Lazy: dispatch the full-period scan to a background thread so
    // callers (project open / post-sim auto-load / manual add) never
    // block the UI. On large outputs (West Whiteland's 427 MB .out)
    // the synchronous version cost several seconds of frozen UI.
    if (!m_handle || m_totalSteps <= 0) return;

    RangeKind kind;
    int outCode = -1;
    if      (isNodeVar(m_variable))     { kind = RangeKind::Node;     outCode = nodeVar(m_variable); }
    else if (isLinkVar(m_variable))     { kind = RangeKind::Link;     outCode = linkVar(m_variable); }
    else if (isSubcatchVar(m_variable)) { kind = RangeKind::Subcatch; outCode = subcatchVar(m_variable); }
    else return;
    if (outCode < 0) return;

    const QString path       = m_resultsFilePath;
    const int     totalSteps = m_totalSteps;
    QPointer<SWMMResultsLayer> self(this);
    QtConcurrent::run([self, path, kind, outCode, totalSteps]() {
        const auto range = scanRangeBlocking(path, kind, outCode, totalSteps);
        QMetaObject::invokeMethod(qApp, [self, range]() {
            if (!self) return;
            self->m_colorRamp.minValue = range.first;
            self->m_colorRamp.maxValue = range.second;
            emit self->repaintRequested();
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Legend
// ---------------------------------------------------------------------------

bool SWMMResultsLayer::showLegend() const { return m_showLegend; }

void SWMMResultsLayer::setShowLegend(bool show)
{
    if (m_showLegend == show)
        return;
    m_showLegend = show;
    emit showLegendChanged(m_showLegend);
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// OpenSWMMVisLayer interface
// ---------------------------------------------------------------------------

void SWMMResultsLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    if (m_transform)
    {
        OCTDestroyCoordinateTransformation(
            reinterpret_cast<OGRCoordinateTransformationH>(m_transform));
        m_transform = nullptr;
    }

    if (!srs() || !newCanvasSRS)
        return;

    OGRSpatialReference *srcRef = srs()->ogrSpatialReference();
    OGRSpatialReference *dstRef = newCanvasSRS->ogrSpatialReference();

    if (!srcRef || !dstRef || srcRef->IsSame(dstRef))
        return;

    m_transform = OGRCreateCoordinateTransformation(srcRef, dstRef);

    // Slice §Y.2 — canvas CRS change invalidates every cached scene
    // coordinate; force a structural rebuild on the next refreshScene.
    escalateSceneDirty(SceneDirty::Structural);
}

void SWMMResultsLayer::populateScene(QGraphicsScene *scene,
                                      const MapExtent  &canvasExtent,
                                      const SpatialReferenceSystem *canvasSRS)
{
    Q_UNUSED(canvasExtent)
    Q_UNUSED(canvasSRS)

    if (!isVisible() || !m_modelLayer || opacity() <= 0.0)
        return;

    if (!m_handle || m_totalSteps <= 0)
        return;

    // Slice §Y.2 — entering a structural rebuild: invalidate every cached
    // item handle. Each paint helper resizes its slot to categoryCount and
    // overwrites entries as it adds items to the scene; rows that the
    // helper skips (NaN, missing geometry) stay null and restyleScene
    // will quietly skip them.
    for (int k = 0; k < SWMMModelLayer::NumCategories; ++k)
        m_itemByFeature[k].clear();

    // Tag value used by depopulateScene to identify this layer's items.
    const quintptr ownerTag = reinterpret_cast<quintptr>(this);

    auto tag = [ownerTag](QGraphicsItem *item) {
        item->setData(0, QVariant::fromValue<quintptr>(ownerTag));
        item->setZValue(10.0); // above the model layer
    };

    // Slice U-0 — granular per-Category paint dispatch. The legacy
    // four-block paint (node markers / conduit overlay / arrows / subcatch
    // fill) is replaced by a single loop over m_featureSublayers[]. Each
    // sublayer carries its own archetype-appropriate style bag and the
    // dispatcher below picks paintPoint / paintLine / paintPolygon based
    // on the sublayer's archetype. Per-feature override colours from
    // m_kindFeatureColors / m_kindUsesOverrides (Slice OUT.2) still win
    // over the ramp lookup so kind-level Categorized / Graduated renderers
    // continue to work.

    using OpenSWMM::Render::FeatureSublayer;
    using OpenSWMM::Render::PointFeatureSublayerStyle;
    using OpenSWMM::Render::LineFeatureSublayerStyle;
    using OpenSWMM::Render::PolygonFeatureSublayerStyle;

    auto makeCosmeticPen = [](const QColor &c, double widthPx, Qt::PenStyle dash) {
        QPen p(c);
        p.setCosmetic(true);
        p.setWidthF(widthPx);
        p.setStyle(dash);
        p.setCapStyle(Qt::FlatCap);
        p.setJoinStyle(Qt::RoundJoin);
        return p;
    };

    auto makeShapePolygon = [](PointFeatureSublayerStyle::MarkerShape s, double rPx) -> QPolygonF {
        QPolygonF p;
        switch (s) {
            case PointFeatureSublayerStyle::Square:
                p << QPointF(-rPx, -rPx) << QPointF(rPx, -rPx)
                  << QPointF( rPx,  rPx) << QPointF(-rPx, rPx);
                break;
            case PointFeatureSublayerStyle::Triangle:
                p << QPointF(0.0, -rPx) << QPointF( rPx, rPx) << QPointF(-rPx, rPx);
                break;
            case PointFeatureSublayerStyle::Diamond:
                p << QPointF(0.0, -rPx) << QPointF(rPx, 0.0)
                  << QPointF(0.0,  rPx) << QPointF(-rPx, 0.0);
                break;
            case PointFeatureSublayerStyle::Star: {
                constexpr double kPi = 3.14159265358979323846;
                const double inner = rPx * 0.45;
                for (int k = 0; k < 10; ++k) {
                    const double r = (k & 1) ? inner : rPx;
                    const double a = -kPi / 2.0 + k * (kPi / 5.0);
                    p << QPointF(r * std::cos(a), r * std::sin(a));
                }
                break;
            }
            case PointFeatureSublayerStyle::Circle:
            default:
                break;
        }
        return p;
    };

    // ── Point-archetype paint ───────────────────────────────────────────
    auto paintPoint = [&](FeatureSublayer *sub) {
        auto *st = sub->pointStyle();
        if (!st) return;

        const SWMMModelLayer::Category cat = sub->category();
        const int count = m_modelLayer->categoryCount(cat);
        if (count <= 0) return;

        const int catIdx = static_cast<int>(cat);
        // Slice §Y.2 — pre-size the cache slot to categoryCount; entries
        // start null and get overwritten as items are added.
        m_itemByFeature[catIdx].fill(nullptr, count);
        const bool useOverride = m_kindUsesOverrides[catIdx]
            && m_kindFeatureColors[catIdx].size() == count;

        const QString attr = st->attribute();
        const int outCode  = nodeOutCodeForAttribute(attr);
        const QVector<float> results = (!attr.isEmpty() && outCode >= 0)
            ? m_nodeResultsByVar.value(outCode) : QVector<float>{};
        const bool haveResults = !results.isEmpty();

        RasterColorRamp ramp = m_colorRamp;
        if (haveResults) {
            const auto r = ensureNodeAttributeRange(outCode);
            ramp.minValue = r.first;
            ramp.maxValue = r.second;
        }

        const double diameterPxDefault = std::max(2.0, st->markerSizePx());
        const auto   shapeDefault      = st->shape();
        const qreal  opMul    = sub->opacity();
        const bool   useRamp  = st->useColorRamp();
        const QColor singleCol = st->color();

        for (int row = 0; row < count; ++row) {
            const QString name = m_modelLayer->objectNameAt(cat, row);

            QColor col = singleCol;
            if (useOverride) {
                col = m_kindFeatureColors[catIdx][row];
            } else if (useRamp && haveResults) {
                const int outIdx = nodeOutputIndex(name);
                if (outIdx < 0 || outIdx >= results.size()) continue;
                const float val = results[outIdx];
                if (!std::isfinite(val)) continue;
                col = ramp.colorForValue(static_cast<double>(val));
            }
            if (!col.isValid()) continue;

            double mx = 0.0, my = 0.0;
            if (!m_modelLayer->elementPosition(name, &mx, &my)) continue;

            // Per-class override surfaces from the override cache (size /
            // shape come from extractStyleSize/Shape in
            // rebuildKindFeatureOverrides). Negative / -1 sentinels fall
            // back to the sublayer's archetype defaults.
            double diameterPx = diameterPxDefault;
            PointFeatureSublayerStyle::MarkerShape shape = shapeDefault;
            if (useOverride) {
                if (m_kindFeatureSizes[catIdx].value(row, -1.0) > 0.0)
                    diameterPx = std::max(2.0, m_kindFeatureSizes[catIdx][row]);
                if (m_kindFeatureShapes[catIdx].value(row, -1) >= 0)
                    shape = static_cast<PointFeatureSublayerStyle::MarkerShape>(
                        m_kindFeatureShapes[catIdx][row]);
            }
            const double rPx = diameterPx * 0.5;
            const QPolygonF poly = (shape == PointFeatureSublayerStyle::Circle)
                                    ? QPolygonF{} : makeShapePolygon(shape, rPx);

            QColor outline = col.darker(130);
            if (opMul < 1.0) {
                col.setAlphaF(std::clamp(col.alphaF() * opMul, 0.0, 1.0));
                outline.setAlphaF(std::clamp(outline.alphaF() * opMul, 0.0, 1.0));
            }

            const QPointF centre = toScene(mx, my);
            QGraphicsItem *item = nullptr;
            if (shape == PointFeatureSublayerStyle::Circle) {
                auto *e = new QGraphicsEllipseItem(-rPx, -rPx, 2.0 * rPx, 2.0 * rPx);
                e->setPen(QPen(outline, 0.5));
                e->setBrush(QBrush(col));
                item = e;
            } else {
                auto *p = new QGraphicsPolygonItem(poly);
                p->setPen(QPen(outline, 0.5));
                p->setBrush(QBrush(col));
                item = p;
            }
            item->setPos(centre);
            item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            scene->addItem(item);
            tag(item);
            // Slice §Y.2 — cache the per-row item handle so restyleScene
            // can recolour without destroying and recreating the scene.
            m_itemByFeature[catIdx][row] = item;
        }
    };

    // ── Line-archetype paint ────────────────────────────────────────────
    auto paintLine = [&](FeatureSublayer *sub) {
        auto *st = sub->lineStyle();
        if (!st) return;

        const SWMMModelLayer::Category cat = sub->category();
        const int count = m_modelLayer->categoryCount(cat);
        if (count <= 0) return;

        const int catIdx = static_cast<int>(cat);
        // Slice §Y.2 — pre-size the cache slot to categoryCount.
        m_itemByFeature[catIdx].fill(nullptr, count);
        const bool useOverride = m_kindUsesOverrides[catIdx]
            && m_kindFeatureColors[catIdx].size() == count;

        const QString attr = st->attribute();
        const int outCode  = linkOutCodeForAttribute(attr);
        const QVector<float> results = (!attr.isEmpty() && outCode >= 0)
            ? m_linkResultsByVar.value(outCode) : QVector<float>{};
        const bool haveResults = !results.isEmpty();

        RasterColorRamp ramp = m_colorRamp;
        if (haveResults) {
            const auto r = ensureLinkAttributeRange(outCode);
            ramp.minValue = r.first;
            ramp.maxValue = r.second;
        }

        const bool showArrows = st->showFlowArrows();
        const QVector<float> flowResults = showArrows
            ? m_linkResultsByVar.value(SWMM_OUT_LINK_FLOW) : QVector<float>{};
        const auto flowRange = showArrows
            ? ensureLinkAttributeRange(SWMM_OUT_LINK_FLOW)
            : QPair<double, double>{0.0, 1.0};
        const double magDenom = std::max(1e-12, std::max(std::abs(flowRange.first),
                                                          std::abs(flowRange.second)));

        const double lineWidthPxDefault = std::max(0.5, st->lineWidthPx());
        const Qt::PenStyle dashDefault  = st->dashPattern();
        const bool renderAsLine  = st->renderAsLine();
        const double dotRpx      = std::max(2.0, lineWidthPxDefault * 2.0) * 0.5;
        const qreal  opMul       = sub->opacity();
        const bool   useRamp     = st->useColorRamp();
        const QColor singleCol   = st->color();

        const double arrowLenPx   = std::max(2.0, st->arrowLengthPx());
        const double arrowHalfWPx = std::max(1.0, st->arrowWidthPx());
        const QColor arrowCol     = st->arrowColor();

        for (int row = 0; row < count; ++row) {
            const QString name = m_modelLayer->objectNameAt(cat, row);
            const int linkIdx = m_modelLayer->linkIndex(name);
            if (linkIdx < 0) continue;

            QColor col = singleCol;
            if (useOverride) {
                col = m_kindFeatureColors[catIdx][row];
            } else if (useRamp && haveResults) {
                const int outIdx = linkOutputIndex(name);
                if (outIdx < 0 || outIdx >= results.size()) continue;
                const float val = results[outIdx];
                if (!std::isfinite(val)) continue;
                col = ramp.colorForValue(static_cast<double>(val));
            }
            if (!col.isValid()) continue;
            if (opMul < 1.0)
                col.setAlphaF(std::clamp(col.alphaF() * opMul, 0.0, 1.0));

            double lineWidthPx = lineWidthPxDefault;
            Qt::PenStyle dash  = dashDefault;
            if (useOverride) {
                if (m_kindFeatureWidths[catIdx].value(row, -1.0) > 0.0)
                    lineWidthPx = std::max(0.5, m_kindFeatureWidths[catIdx][row]);
                if (m_kindFeatureDashes[catIdx].value(row, -1) >= 0)
                    dash = static_cast<Qt::PenStyle>(m_kindFeatureDashes[catIdx][row]);
            }

            if (renderAsLine) {
                const QVector<QPointF> polyMap = m_modelLayer->cachedLinkPolyline(linkIdx);
                if (polyMap.size() < 2) continue;
                QPainterPath path;
                path.moveTo(toScene(polyMap[0].x(), polyMap[0].y()));
                for (int p = 1; p < polyMap.size(); ++p)
                    path.lineTo(toScene(polyMap[p].x(), polyMap[p].y()));
                auto *item = new QGraphicsPathItem(path);
                item->setPen(makeCosmeticPen(col, lineWidthPx, dash));
                item->setBrush(Qt::NoBrush);
                scene->addItem(item);
                tag(item);
                // Slice §Y.2 — cache the path so restyleScene can re-pen
                // it without rebuilding the QPainterPath.
                m_itemByFeature[catIdx][row] = item;
            } else {
                double mx = 0.0, my = 0.0;
                if (!m_modelLayer->elementPosition(name, &mx, &my)) continue;
                const QPointF mid = toScene(mx, my);
                auto *dot = new QGraphicsEllipseItem(-dotRpx, -dotRpx, 2.0 * dotRpx, 2.0 * dotRpx);
                dot->setPen(QPen(Qt::NoPen));
                dot->setBrush(QBrush(col));
                dot->setPos(mid);
                dot->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                scene->addItem(dot);
                tag(dot);
                m_itemByFeature[catIdx][row] = dot;
            }

            if (showArrows && !flowResults.isEmpty()) {
                const int outIdx = linkOutputIndex(name);
                if (outIdx < 0 || outIdx >= flowResults.size()) continue;
                const float flow = flowResults[outIdx];
                if (!std::isfinite(flow) || std::abs(flow) < 1e-9f) continue;

                const QVector<QPointF> polyMap = m_modelLayer->cachedLinkPolyline(linkIdx);
                if (polyMap.size() < 2) continue;

                QVector<QPointF> polyScene;
                polyScene.reserve(polyMap.size());
                for (const QPointF &p : polyMap)
                    polyScene.append(toScene(p.x(), p.y()));

                double total = 0.0;
                std::vector<double> segLens; segLens.reserve(polyScene.size() - 1);
                for (int i = 1; i < polyScene.size(); ++i) {
                    const QPointF d = polyScene[i] - polyScene[i - 1];
                    segLens.push_back(std::hypot(d.x(), d.y()));
                    total += segLens.back();
                }
                if (total <= 0.0) continue;
                const double target = total * 0.5;
                double acc = 0.0;
                int segIdx = 0;
                for (segIdx = 0; segIdx < int(segLens.size()) - 1; ++segIdx) {
                    if (acc + segLens[segIdx] >= target) break;
                    acc += segLens[segIdx];
                }
                const double t = (segLens[segIdx] > 0.0)
                    ? (target - acc) / segLens[segIdx] : 0.0;
                const QPointF a = polyScene[segIdx];
                const QPointF b = polyScene[segIdx + 1];
                const QPointF mid = a + (b - a) * t;

                QPointF dir = b - a;
                const double dlen = std::hypot(dir.x(), dir.y());
                if (dlen <= 0.0) continue;
                dir /= dlen;
                constexpr double kPi = 3.14159265358979323846;
                double angleRad = std::atan2(dir.y(), dir.x());
                if (flow < 0.0f) angleRad += kPi;
                const double angleDeg = angleRad * 180.0 / kPi;

                const double magNorm = std::min(1.0, std::abs(flow) / magDenom);
                const double arrLen  = arrowLenPx * (0.25 + 0.75 * magNorm);

                QPolygonF head;
                head << QPointF(arrLen, 0.0)
                     << QPointF(0.0,     arrowHalfWPx)
                     << QPointF(0.0,    -arrowHalfWPx);

                QColor fillColor = arrowCol;
                if (opMul < 1.0)
                    fillColor.setAlphaF(std::clamp(fillColor.alphaF() * opMul, 0.0, 1.0));

                auto *arrow = new QGraphicsPolygonItem(head);
                arrow->setPen(QPen(fillColor.darker(140), 0.5));
                arrow->setBrush(QBrush(fillColor));
                arrow->setPos(mid);
                arrow->setRotation(angleDeg);
                arrow->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                scene->addItem(arrow);
                tag(arrow);
            }
        }
    };

    // ── Polygon-archetype paint ─────────────────────────────────────────
    auto paintPolygon = [&](FeatureSublayer *sub) {
        auto *st = sub->polygonStyle();
        if (!st) return;

        const SWMMModelLayer::Category cat = sub->category();
        const int count = m_modelLayer->categoryCount(cat);
        if (count <= 0) return;

        const int catIdx = static_cast<int>(cat);
        // Slice §Y.2 — pre-size the cache slot to categoryCount.
        m_itemByFeature[catIdx].fill(nullptr, count);
        const bool useOverride = m_kindUsesOverrides[catIdx]
            && m_kindFeatureColors[catIdx].size() == count;

        const QString attr = st->attribute();
        const int outCode  = subcatchOutCodeForAttribute(attr);
        const QVector<float> results = (!attr.isEmpty() && outCode >= 0)
            ? m_subcatchResultsByVar.value(outCode) : QVector<float>{};
        const bool haveResults = !results.isEmpty();

        RasterColorRamp ramp = m_colorRamp;
        if (haveResults) {
            const auto r = ensureSubcatchAttributeRange(outCode);
            ramp.minValue = r.first;
            ramp.maxValue = r.second;
        }

        const QColor outlineCol      = st->outlineColor();
        const double outlineWPxDefault = st->outlineWidthPx();
        const double fillAlphaMul    = std::clamp(st->fillOpacity(), 0.0, 1.0);
        const qreal  opMul           = sub->opacity();
        const bool   useRamp         = st->useColorRamp();
        const QColor singleCol       = st->color();

        auto buildOutlinePen = [&](double widthPx, Qt::PenStyle dash) -> QPen {
            QPen p(Qt::NoPen);
            if (widthPx > 0.0 && outlineCol.isValid() && outlineCol.alpha() > 0) {
                p = QPen(outlineCol);
                p.setCosmetic(true);
                p.setWidthF(widthPx);
                p.setJoinStyle(Qt::RoundJoin);
                if (dash >= 0) p.setStyle(dash);
            }
            return p;
        };
        const QPen defaultOutlinePen = buildOutlinePen(outlineWPxDefault, Qt::SolidLine);

        for (int row = 0; row < count; ++row) {
            const QString name = m_modelLayer->objectNameAt(cat, row);

            QColor col = singleCol;
            if (useOverride) {
                col = m_kindFeatureColors[catIdx][row];
            } else if (useRamp && haveResults) {
                const int outIdx = subcatchOutputIndex(name);
                if (outIdx < 0 || outIdx >= results.size()) continue;
                const float val = results[outIdx];
                if (!std::isfinite(val)) continue;
                col = ramp.colorForValue(static_cast<double>(val));
            }
            if (!col.isValid()) continue;

            const int alpha = std::clamp(
                int(255.0 * fillAlphaMul * opMul), 0, 255);
            const QColor fill(col.red(), col.green(), col.blue(), alpha);

            // Slice X.20 — per-category outline width/dash overrides
            // surface from the override caches (X.15 widths/dashes
            // populated by rebuildKindFeatureOverrides).  Negative
            // sentinels fall back to the sublayer default.
            QPen outlinePen = defaultOutlinePen;
            if (useOverride) {
                const double w  = m_kindFeatureWidths[catIdx].value(row, -1.0);
                const int    ds = m_kindFeatureDashes[catIdx].value(row, -1);
                if (w > 0.0 || ds >= 0)
                    outlinePen = buildOutlinePen(
                        w > 0.0 ? w : outlineWPxDefault,
                        ds >= 0 ? static_cast<Qt::PenStyle>(ds) : Qt::SolidLine);
            }

            const QVector<QPointF> verts = m_modelLayer->cachedSubcatchVertices(row);
            if (verts.size() >= 3) {
                QPolygonF poly; poly.reserve(verts.size());
                for (const QPointF &p : verts)
                    poly << toScene(p.x(), p.y());
                auto *item = new QGraphicsPolygonItem(poly);
                item->setPen(outlinePen);
                // VS.2b — compose the fill brush through the FillSymbolLayerSpec
                // primitive so the polygon honours the style's fill pattern
                // (solid / hatch / cross-hatch) rather than always solid.
                OpenSWMM::Render::FillSymbolLayerSpec fillSpec;
                fillSpec.fillColor = fill;
                fillSpec.fillStyle = static_cast<Qt::BrushStyle>(st->fillStyle());
                item->setBrush(fillSpec.toQBrush());
                scene->addItem(item);
                tag(item);
                // Slice §Y.2 — cache for restyle.
                m_itemByFeature[catIdx][row] = item;
            } else {
                const MapExtent ext = m_modelLayer->objectExtent(name);
                if (!std::isfinite(ext.xMin())) continue;
                const double x1 = ext.xMin(), y1 = ext.yMin();
                Q_UNUSED(y1);
                const double x2 = ext.xMax(), y2 = ext.yMax();
                auto *rect = scene->addRect(
                    x1, -y2, x2 - x1, y2 - y1, outlinePen, QBrush(fill));
                tag(rect);
                m_itemByFeature[catIdx][row] = rect;
            }
        }
    };

    // Dispatch — iterate sublayers in paint order from sublayers() and
    // route each through the matching archetype paint helper.
    for (auto *base : sublayers()) {
        auto *sub = qobject_cast<FeatureSublayer *>(base);
        if (!sub) continue;
        if (!sub->isVisible() || sub->opacity() <= 0.0) continue;
        switch (sub->archetype()) {
            case FeatureSublayer::Archetype::Point:   paintPoint(sub);   break;
            case FeatureSublayer::Archetype::Line:    paintLine(sub);    break;
            case FeatureSublayer::Archetype::Polygon: paintPolygon(sub); break;
        }
    }

    // L-1 — build labels after the feature items exist (positions anchor off
    // m_itemByFeature). No-op when labelConfig().enabled is false.
    refreshLabels(scene);
}

void SWMMResultsLayer::depopulateScene(QGraphicsScene *scene)
{
    OpenSWMMVisLayer::depopulateScene(scene);
    // Slice §Y.2 — base class destroyed every item we cached, so the
    // pointers are now dangling. Drop them; the next populateScene
    // refills the slots. L-1 — label items are tagged with the same owner
    // tag, so the base destroyed them too; drop the dangling handles.
    for (int k = 0; k < SWMMModelLayer::NumCategories; ++k) {
        m_itemByFeature[k].clear();
        m_labelByFeature[k].clear();
    }
}

// ---------------------------------------------------------------------------
// Slice §Y.2 — incremental refresh path
//
// refreshScene branches on m_sceneDirty:
//   • Clean      → no work.
//   • Values     → walk m_itemByFeature and apply setBrush / setPen in place
//                  (no QGraphicsItem allocation, no scene mutation).
//                  Falls back to Structural when any visible line sublayer
//                  has flow arrows on — arrow geometry depends on the live
//                  flow value, which restyle can't update without rebuilding.
//   • Structural → fall through to base class depopulate + populate.
//
// Restyle must stay in lockstep with populateScene's per-archetype colour
// logic.  The lookup sequence (override → ramp → singleColor, with NaN /
// invalid skips) is duplicated here intentionally; factoring it out would
// touch the populate helpers heavily and isn't worth the churn for v1.
// ---------------------------------------------------------------------------

bool SWMMResultsLayer::requiresStructuralRebuildForRestyle() const
{
    using OpenSWMM::Render::FeatureSublayer;
    for (auto *base : sublayers()) {
        auto *sub = qobject_cast<FeatureSublayer *>(base);
        if (!sub || !sub->isVisible() || sub->opacity() <= 0.0) continue;
        if (sub->archetype() != FeatureSublayer::Archetype::Line) continue;
        auto *st = sub->lineStyle();
        if (st && st->showFlowArrows()) return true;
    }
    return false;
}

void SWMMResultsLayer::refreshScene(QGraphicsScene *scene,
                                    const MapExtent &canvasExtent,
                                    const SpatialReferenceSystem *canvasSRS)
{
    if (m_sceneDirty == SceneDirty::Clean)
        return;

    if (m_sceneDirty == SceneDirty::Values
        && !requiresStructuralRebuildForRestyle()) {
        restyleScene(scene);
        m_sceneDirty = SceneDirty::Clean;
        return;
    }

    // Structural — or values dirty plus arrows present.
    OpenSWMMVisLayer::refreshScene(scene, canvasExtent, canvasSRS);
    m_sceneDirty = SceneDirty::Clean;
}

void SWMMResultsLayer::restyleScene(QGraphicsScene *scene)
{
    if (!isVisible() || !m_modelLayer || opacity() <= 0.0)
        return;
    if (!m_handle || m_totalSteps <= 0)
        return;

    using OpenSWMM::Render::FeatureSublayer;
    using OpenSWMM::Render::PointFeatureSublayerStyle;

    for (auto *base : sublayers()) {
        auto *sub = qobject_cast<FeatureSublayer *>(base);
        if (!sub) continue;
        if (!sub->isVisible() || sub->opacity() <= 0.0) continue;

        const SWMMModelLayer::Category cat = sub->category();
        const int catIdx = static_cast<int>(cat);
        const int count = m_modelLayer->categoryCount(cat);
        if (count <= 0) continue;
        // Defensive: a mid-session topology edit could have grown the
        // category since the last populateScene. Bail out — the next
        // structural refresh will rebuild correctly.
        if (m_itemByFeature[catIdx].size() != count) continue;

        switch (sub->archetype()) {
        case FeatureSublayer::Archetype::Point: {
            auto *st = sub->pointStyle();
            if (!st) break;

            const bool useOverride = m_kindUsesOverrides[catIdx]
                && m_kindFeatureColors[catIdx].size() == count;
            const QString attr = st->attribute();
            const int outCode  = nodeOutCodeForAttribute(attr);
            const QVector<float> results = (!attr.isEmpty() && outCode >= 0)
                ? m_nodeResultsByVar.value(outCode) : QVector<float>{};
            const bool haveResults = !results.isEmpty();

            RasterColorRamp ramp = m_colorRamp;
            if (haveResults) {
                const auto r = ensureNodeAttributeRange(outCode);
                ramp.minValue = r.first;
                ramp.maxValue = r.second;
            }

            const qreal  opMul    = sub->opacity();
            const bool   useRamp  = st->useColorRamp();
            const QColor singleCol = st->color();

            for (int row = 0; row < count; ++row) {
                QGraphicsItem *item = m_itemByFeature[catIdx][row];
                if (!item) continue;

                QColor col = singleCol;
                bool drop = false;
                if (useOverride) {
                    col = m_kindFeatureColors[catIdx][row];
                } else if (useRamp && haveResults) {
                    const QString name = m_modelLayer->objectNameAt(cat, row);
                    const int outIdx = nodeOutputIndex(name);
                    if (outIdx < 0 || outIdx >= results.size()) {
                        drop = true;
                    } else {
                        const float val = results[outIdx];
                        if (!std::isfinite(val)) drop = true;
                        else col = ramp.colorForValue(static_cast<double>(val));
                    }
                }
                if (drop || !col.isValid()) {
                    item->setVisible(false);
                    continue;
                }
                QColor outline = col.darker(130);
                if (opMul < 1.0) {
                    col.setAlphaF(std::clamp(col.alphaF() * opMul, 0.0, 1.0));
                    outline.setAlphaF(std::clamp(outline.alphaF() * opMul, 0.0, 1.0));
                }
                item->setVisible(true);
                if (auto *e = qgraphicsitem_cast<QGraphicsEllipseItem *>(item)) {
                    e->setPen(QPen(outline, 0.5));
                    e->setBrush(QBrush(col));
                } else if (auto *p = qgraphicsitem_cast<QGraphicsPolygonItem *>(item)) {
                    p->setPen(QPen(outline, 0.5));
                    p->setBrush(QBrush(col));
                }
            }
            break;
        }

        case FeatureSublayer::Archetype::Line: {
            auto *st = sub->lineStyle();
            if (!st) break;
            // requiresStructuralRebuildForRestyle() guarantees no arrows.

            const bool useOverride = m_kindUsesOverrides[catIdx]
                && m_kindFeatureColors[catIdx].size() == count;
            const QString attr = st->attribute();
            const int outCode  = linkOutCodeForAttribute(attr);
            const QVector<float> results = (!attr.isEmpty() && outCode >= 0)
                ? m_linkResultsByVar.value(outCode) : QVector<float>{};
            const bool haveResults = !results.isEmpty();

            RasterColorRamp ramp = m_colorRamp;
            if (haveResults) {
                const auto r = ensureLinkAttributeRange(outCode);
                ramp.minValue = r.first;
                ramp.maxValue = r.second;
            }

            const double lineWidthPxDefault = std::max(0.5, st->lineWidthPx());
            const Qt::PenStyle dashDefault  = st->dashPattern();
            const bool renderAsLine = st->renderAsLine();
            const qreal opMul = sub->opacity();
            const bool useRamp = st->useColorRamp();
            const QColor singleCol = st->color();

            for (int row = 0; row < count; ++row) {
                QGraphicsItem *item = m_itemByFeature[catIdx][row];
                if (!item) continue;

                QColor col = singleCol;
                bool drop = false;
                if (useOverride) {
                    col = m_kindFeatureColors[catIdx][row];
                } else if (useRamp && haveResults) {
                    const QString name = m_modelLayer->objectNameAt(cat, row);
                    const int outIdx = linkOutputIndex(name);
                    if (outIdx < 0 || outIdx >= results.size()) {
                        drop = true;
                    } else {
                        const float val = results[outIdx];
                        if (!std::isfinite(val)) drop = true;
                        else col = ramp.colorForValue(static_cast<double>(val));
                    }
                }
                if (drop || !col.isValid()) {
                    item->setVisible(false);
                    continue;
                }
                if (opMul < 1.0)
                    col.setAlphaF(std::clamp(col.alphaF() * opMul, 0.0, 1.0));
                item->setVisible(true);

                double lineWidthPx = lineWidthPxDefault;
                Qt::PenStyle dash  = dashDefault;
                if (useOverride) {
                    if (m_kindFeatureWidths[catIdx].value(row, -1.0) > 0.0)
                        lineWidthPx = std::max(0.5, m_kindFeatureWidths[catIdx][row]);
                    if (m_kindFeatureDashes[catIdx].value(row, -1) >= 0)
                        dash = static_cast<Qt::PenStyle>(m_kindFeatureDashes[catIdx][row]);
                }

                if (renderAsLine) {
                    if (auto *p = qgraphicsitem_cast<QGraphicsPathItem *>(item)) {
                        QPen pen(col);
                        pen.setCosmetic(true);
                        pen.setWidthF(lineWidthPx);
                        pen.setStyle(dash);
                        pen.setCapStyle(Qt::FlatCap);
                        pen.setJoinStyle(Qt::RoundJoin);
                        p->setPen(pen);
                    }
                } else {
                    if (auto *e = qgraphicsitem_cast<QGraphicsEllipseItem *>(item))
                        e->setBrush(QBrush(col));
                }
            }
            break;
        }

        case FeatureSublayer::Archetype::Polygon: {
            auto *st = sub->polygonStyle();
            if (!st) break;

            const bool useOverride = m_kindUsesOverrides[catIdx]
                && m_kindFeatureColors[catIdx].size() == count;
            const QString attr = st->attribute();
            const int outCode  = subcatchOutCodeForAttribute(attr);
            const QVector<float> results = (!attr.isEmpty() && outCode >= 0)
                ? m_subcatchResultsByVar.value(outCode) : QVector<float>{};
            const bool haveResults = !results.isEmpty();

            RasterColorRamp ramp = m_colorRamp;
            if (haveResults) {
                const auto r = ensureSubcatchAttributeRange(outCode);
                ramp.minValue = r.first;
                ramp.maxValue = r.second;
            }

            const QColor outlineCol        = st->outlineColor();
            const double outlineWPxDefault = st->outlineWidthPx();
            const double fillAlphaMul      = std::clamp(st->fillOpacity(), 0.0, 1.0);
            const qreal  opMul             = sub->opacity();
            const bool   useRamp           = st->useColorRamp();
            const QColor singleCol         = st->color();

            auto buildOutlinePen = [&](double widthPx, Qt::PenStyle dash) -> QPen {
                QPen p(Qt::NoPen);
                if (widthPx > 0.0 && outlineCol.isValid() && outlineCol.alpha() > 0) {
                    p = QPen(outlineCol);
                    p.setCosmetic(true);
                    p.setWidthF(widthPx);
                    p.setJoinStyle(Qt::RoundJoin);
                    if (dash >= 0) p.setStyle(dash);
                }
                return p;
            };
            const QPen defaultOutlinePen = buildOutlinePen(outlineWPxDefault, Qt::SolidLine);

            for (int row = 0; row < count; ++row) {
                QGraphicsItem *item = m_itemByFeature[catIdx][row];
                if (!item) continue;

                QColor col = singleCol;
                bool drop = false;
                if (useOverride) {
                    col = m_kindFeatureColors[catIdx][row];
                } else if (useRamp && haveResults) {
                    const QString name = m_modelLayer->objectNameAt(cat, row);
                    const int outIdx = subcatchOutputIndex(name);
                    if (outIdx < 0 || outIdx >= results.size()) {
                        drop = true;
                    } else {
                        const float val = results[outIdx];
                        if (!std::isfinite(val)) drop = true;
                        else col = ramp.colorForValue(static_cast<double>(val));
                    }
                }
                if (drop || !col.isValid()) {
                    item->setVisible(false);
                    continue;
                }
                item->setVisible(true);

                const int alpha = std::clamp(
                    int(255.0 * fillAlphaMul * opMul), 0, 255);
                const QColor fill(col.red(), col.green(), col.blue(), alpha);

                QPen outlinePen = defaultOutlinePen;
                if (useOverride) {
                    const double w  = m_kindFeatureWidths[catIdx].value(row, -1.0);
                    const int    ds = m_kindFeatureDashes[catIdx].value(row, -1);
                    if (w > 0.0 || ds >= 0)
                        outlinePen = buildOutlinePen(
                            w > 0.0 ? w : outlineWPxDefault,
                            ds >= 0 ? static_cast<Qt::PenStyle>(ds) : Qt::SolidLine);
                }

                if (auto *p = qgraphicsitem_cast<QGraphicsPolygonItem *>(item)) {
                    p->setPen(outlinePen);
                    p->setBrush(QBrush(fill));
                } else if (auto *r = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
                    r->setPen(outlinePen);
                    r->setBrush(QBrush(fill));
                }
            }
            break;
        }
        }
    }

    // L-1 — refresh per-feature labels with the new frame's values.
    refreshLabels(scene);
}

// ---------------------------------------------------------------------------
// L-1 — per-feature "name: value" labels for animated results
// ---------------------------------------------------------------------------

void SWMMResultsLayer::clearLabels()
{
    // Called when labels are disabled (no depopulate has run, so the cached
    // pointers are still live). Proactively remove + delete so the disable
    // takes effect on the next paint.
    for (int k = 0; k < SWMMModelLayer::NumCategories; ++k) {
        for (QGraphicsSimpleTextItem *lbl : m_labelByFeature[k]) {
            if (!lbl) continue;
            if (QGraphicsScene *sc = lbl->scene())
                sc->removeItem(lbl);
            delete lbl;
        }
        m_labelByFeature[k].clear();
    }
}

void SWMMResultsLayer::setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg)
{
    OpenSWMMVisLayer::setLabelConfig(cfg);   // store + emit labelConfigChanged/repaint
    // A label-only change leaves the scene "Clean"; escalate so the next
    // refresh runs restyleScene → refreshLabels and the change is applied.
    escalateSceneDirty(SceneDirty::Values);
}

void SWMMResultsLayer::refreshLabels(QGraphicsScene *scene)
{
    // L-1 — per-sublayer labels. Each FeatureSublayer carries its own
    // LabelConfig (enable / expression / colour / placement), so the user can
    // label, say, Junctions with "{name}: {depth} m" and Conduits with
    // "{flow} m³/s" independently. The label text is an expression template:
    // {name} → element name, {field} → that feature's current value for the
    // named result variable; literal text is kept verbatim.
    if (!scene || !m_modelLayer) { clearLabels(); return; }

    using OpenSWMM::Render::FeatureSublayer;
    const quintptr ownerTag = reinterpret_cast<quintptr>(this);
    const qreal    opMul = opacity();

    // Resolve any result field's current value for one feature in a scope
    // (0=node, 1=link, 2=subcatch). Empty when no data / non-finite.
    auto fieldValueStr = [&](int scope, const QString &field, const QString &name) -> QString {
        const QVector<float> *vec = nullptr;
        if (scope == 0) {
            const int oc = nodeOutCodeForAttribute(field);
            if (oc >= 0) { const auto it = m_nodeResultsByVar.constFind(oc);
                if (it != m_nodeResultsByVar.constEnd()) vec = &it.value(); }
        } else if (scope == 1) {
            const int oc = linkOutCodeForAttribute(field);
            if (oc >= 0) { const auto it = m_linkResultsByVar.constFind(oc);
                if (it != m_linkResultsByVar.constEnd()) vec = &it.value(); }
        } else {
            const int oc = subcatchOutCodeForAttribute(field);
            if (oc >= 0) { const auto it = m_subcatchResultsByVar.constFind(oc);
                if (it != m_subcatchResultsByVar.constEnd()) vec = &it.value(); }
        }
        if (!vec) return QString();
        const int oi = (scope == 0) ? nodeOutputIndex(name)
                     : (scope == 1) ? linkOutputIndex(name)
                                    : subcatchOutputIndex(name);
        if (oi < 0 || oi >= vec->size()) return QString();
        const float v = (*vec)[oi];
        return std::isfinite(v) ? QString::number(double(v), 'g', 4) : QString();
    };

    // Substitute {token} placeholders in an expression.
    auto evalExpr = [&](const QString &expr, int scope, const QString &name) -> QString {
        QString out;
        int i = 0;
        while (i < expr.size()) {
            if (expr.at(i) == QLatin1Char('{')) {
                const int j = expr.indexOf(QLatin1Char('}'), i + 1);
                if (j < 0) { out += expr.mid(i); break; }
                const QString tok = expr.mid(i + 1, j - i - 1).trimmed();
                if (tok.compare(QLatin1String("name"), Qt::CaseInsensitive) == 0)
                    out += name;
                else
                    out += fieldValueStr(scope, tok, name);
                i = j + 1;
            } else {
                out += expr.at(i);
                ++i;
            }
        }
        return out;
    };

    auto offsetFor = [](OpenSWMM::Render::LabelConfig::Placement pl,
                        qreal w, qreal h) -> QPointF {
        using LC = OpenSWMM::Render::LabelConfig;
        switch (pl) {
        case LC::Above:  return { -w * 0.5, -h - 2.0 };
        case LC::Below:  return { -w * 0.5,  4.0 };
        case LC::Left:   return { -w - 6.0, -h * 0.5 };
        case LC::Right:  return {  6.0,     -h * 0.5 };
        case LC::Centre: return { -w * 0.5, -h * 0.5 };
        case LC::AutoPlacement:
        default:         return {  6.0,     -h - 2.0 };
        }
    };

    bool labelled[SWMMModelLayer::NumCategories] = { false };

    for (auto *base : sublayers()) {
        auto *sub = qobject_cast<FeatureSublayer *>(base);
        if (!sub || !sub->isVisible() || sub->opacity() <= 0.0) continue;

        auto *stylePtr = sub->featureStyle();
        if (!stylePtr) continue;
        const OpenSWMM::Render::LabelConfig &lc = stylePtr->labelConfig();
        if (!lc.enabled) continue;   // per-sublayer toggle — left unlabelled

        const SWMMModelLayer::Category cat = sub->category();
        const int catIdx = static_cast<int>(cat);
        const int count  = m_modelLayer->categoryCount(cat);
        if (count <= 0 || m_itemByFeature[catIdx].size() != count) continue;

        const int scope = (sub->archetype() == FeatureSublayer::Archetype::Point)  ? 0
                        : (sub->archetype() == FeatureSublayer::Archetype::Line)   ? 1
                                                                                   : 2;
        const QString attr = stylePtr->attribute();   // default-text fallback
        QString unit;
        if (lc.expression.isEmpty())
            for (const auto &f : availableAttributes(cat))
                if (f.name == attr) { unit = f.unit; break; }

        const QFont  font = lc.effectiveFont();
        const QBrush textBrush(lc.color);

        if (m_labelByFeature[catIdx].size() != count) {
            for (QGraphicsSimpleTextItem *l : m_labelByFeature[catIdx]) {
                if (!l) continue;
                if (QGraphicsScene *sc = l->scene()) sc->removeItem(l);
                delete l;
            }
            m_labelByFeature[catIdx] = QVector<QGraphicsSimpleTextItem *>(count, nullptr);
        }
        labelled[catIdx] = true;

        for (int row = 0; row < count; ++row) {
            QGraphicsItem *fi = m_itemByFeature[catIdx][row];
            QGraphicsSimpleTextItem *&lbl = m_labelByFeature[catIdx][row];
            if (!fi || !fi->isVisible()) { if (lbl) lbl->setVisible(false); continue; }

            const QString name = m_modelLayer->objectNameAt(cat, row);
            QString text;
            if (!lc.expression.isEmpty()) {
                text = evalExpr(lc.expression, scope, name);
            } else {
                // No expression → default "name: value unit".
                text = name;
                const QString v = fieldValueStr(scope, attr, name);
                if (!v.isEmpty()) {
                    text += QStringLiteral(": ") + v;
                    if (!unit.isEmpty()) text += QChar(' ') + unit;
                }
            }
            if (text.isEmpty()) { if (lbl) lbl->setVisible(false); continue; }

            if (!lbl) {
                lbl = new QGraphicsSimpleTextItem();
                lbl->setData(0, QVariant::fromValue<quintptr>(ownerTag));
                lbl->setZValue(20.0);   // above markers (z=10)
                lbl->setFlag(QGraphicsItem::ItemIsSelectable, false);
                lbl->setAcceptedMouseButtons(Qt::NoButton);
                scene->addItem(lbl);
            }
            lbl->setText(text);
            lbl->setFont(font);
            lbl->setBrush(textBrush);
            lbl->setOpacity(opMul);
            const QPointF anchor = fi->sceneBoundingRect().center();
            const QRectF  br = lbl->boundingRect();
            lbl->setPos(anchor + offsetFor(lc.placement, br.width(), br.height()));
            lbl->setVisible(true);
        }
    }

    // Drop labels for categories not labelled this pass (sublayer disabled /
    // hidden / removed).
    for (int k = 0; k < SWMMModelLayer::NumCategories; ++k) {
        if (labelled[k]) continue;
        for (QGraphicsSimpleTextItem *l : m_labelByFeature[k]) {
            if (!l) continue;
            if (QGraphicsScene *sc = l->scene()) sc->removeItem(l);
            delete l;
        }
        m_labelByFeature[k].clear();
    }
}
