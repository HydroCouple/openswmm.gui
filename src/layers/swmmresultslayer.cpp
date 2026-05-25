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
#include "render/ifeaturerenderer.h"
#include "render/intervalbinner.h"   // Slice OUT.1 — default kind binner
#include "render/renderers/graduatedrenderer.h"

#include <QAtomicInteger>
#include <QSettings>
#include <QVariant>

#include <openswmm/engine/openswmm_output.h>

#include <QDateTime>
#include <QTimeZone>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QFile>
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
    return m_resultsFilePath;
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

    // Fetch node results if current variable is a node variable.
    if (isNodeVar(m_variable))
    {
        const int nv = nodeVar(m_variable);
        const int n  = swmm_output_get_node_count(m_handle);
        m_nodeResults.resize(n);
        swmm_output_get_node_result(m_handle, step, nv, m_nodeResults.data());
    }
    else if (isLinkVar(m_variable))
    {
        const int lv = linkVar(m_variable);
        const int n  = swmm_output_get_link_count(m_handle);
        m_linkResults.resize(n);
        swmm_output_get_link_result(m_handle, step, lv, m_linkResults.data());
    }
    else if (isSubcatchVar(m_variable))
    {
        const int sv = subcatchVar(m_variable);
        const int n  = swmm_output_get_subcatch_count(m_handle);
        m_subcatchResults.resize(n);
        swmm_output_get_subcatch_result(m_handle, step, sv, m_subcatchResults.data());
    }

    // Slice OUT.2 — refresh per-feature override caches for every kind
    // in the active variable's scope so animation frames pick up the new
    // values. Each rebuild is O(kindFeatures) and cheap relative to the
    // result-fetch above.
    rebuildAllActiveKindFeatureOverrides();
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

    emit currentTimeStepChanged(m_currentStep);
    emit currentDateTimeChanged(currentDateTime());
    emit repaintRequested();
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

    emit variableChanged(m_variable);
    emit repaintRequested();
}

RasterColorRamp SWMMResultsLayer::colorRamp() const { return m_colorRamp; }

void SWMMResultsLayer::setColorRamp(const RasterColorRamp &ramp)
{
    m_colorRamp = ramp;
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
    emit rendererChanged();
    emit repaintRequested();
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

} // namespace

void SWMMResultsLayer::rebuildKindFeatureOverrides(SWMMModelLayer::Category c)
{
    const int idx = static_cast<int>(c);
    if (idx < 0 || idx >= static_cast<int>(SWMMModelLayer::NumCategories))
        return;

    m_kindFeatureColors[idx].clear();
    m_kindFeatureSizes[idx].clear();
    m_kindUsesOverrides[idx] = false;

    if (!m_modelLayer || !m_handle)
        return;

    OpenSWMM::Render::IFeatureRenderer *kr = kindRenderer(c);
    if (!kr)
        return;                       // no override; paint falls back to ramp
    if (!catMatchesVariable(c, m_variable))
        return;                       // scope mismatch — silently skip

    const QString varName = variableEnumName(m_variable);
    if (varName.isEmpty())
        return;

    const int count = m_modelLayer->categoryCount(c);
    if (count <= 0)
        return;

    m_kindFeatureColors[idx].resize(count);
    m_kindFeatureSizes[idx].resize(count);

    // Choose the right result vector + output-id map for the scope.
    const QHash<QString, int> *outIdxMap = nullptr;
    const QVector<float>      *valuesPtr = nullptr;
    if (catIsNodeScope(c)) {
        outIdxMap = &m_nodeOutputIdx;
        valuesPtr = &m_nodeResults;
    } else if (catIsLinkScope(c)) {
        outIdxMap = &m_linkOutputIdx;
        valuesPtr = &m_linkResults;
    } else if (catIsSubcatchScope(c)) {
        outIdxMap = &m_subcatchOutputIdx;
        valuesPtr = &m_subcatchResults;
    } else {
        return;
    }

    for (int row = 0; row < count; ++row) {
        const QString name = m_modelLayer->objectNameAt(c, row);
        double value = std::numeric_limits<double>::quiet_NaN();
        const auto it = outIdxMap->constFind(name);
        if (it != outIdxMap->constEnd()) {
            const int outIdx = it.value();
            if (outIdx >= 0 && outIdx < valuesPtr->size())
                value = static_cast<double>(valuesPtr->at(outIdx));
        }

        QVariantMap attrs;
        if (std::isfinite(value))
            attrs.insert(varName, value);

        OpenSWMM::Render::FeatureRef ref;
        ref.featureIndex = row;
        ref.categoryHint = SWMMModelLayer::kindKey(c);

        const auto style = kr->symbolFor(ref, attrs);
        // Fallback color when the renderer didn't (or couldn't) classify
        // is the ramp-derived color — keeps a sensible visual instead of
        // a hole in the canvas.
        const QColor rampCol = std::isfinite(value)
            ? m_colorRamp.colorForValue(value)
            : QColor();
        m_kindFeatureColors[idx][row] = extractStyleColor(style, rampCol);
        m_kindFeatureSizes [idx][row] = extractStyleSize(style);
    }

    m_kindUsesOverrides[idx] = true;
}

void SWMMResultsLayer::rebuildAllActiveKindFeatureOverrides()
{
    for (int i = 0; i < static_cast<int>(SWMMModelLayer::NumCategories); ++i) {
        const auto cat = static_cast<SWMMModelLayer::Category>(i);
        if (catMatchesVariable(cat, m_variable))
            rebuildKindFeatureOverrides(cat);
        else {
            m_kindFeatureColors[i].clear();
            m_kindFeatureSizes[i].clear();
            m_kindUsesOverrides[i] = false;
        }
    }
}

void SWMMResultsLayer::autoStretchColorRamp()
{
    if (!m_handle || m_totalSteps <= 0)
        return;

    float globalMin = std::numeric_limits<float>::max();
    float globalMax = std::numeric_limits<float>::lowest();

    auto updateMinMax = [&](const QVector<float> &vals) {
        for (float v : vals) {
            if (std::isfinite(v)) {
                globalMin = std::min(globalMin, v);
                globalMax = std::max(globalMax, v);
            }
        }
    };

    // Scan every period (may be slow on very large outputs — acceptable for now).
    for (int p = 0; p < m_totalSteps; ++p)
    {
        if (isNodeVar(m_variable))
        {
            const int n = swmm_output_get_node_count(m_handle);
            QVector<float> tmp(n);
            if (swmm_output_get_node_result(m_handle, p, nodeVar(m_variable), tmp.data()) == 0)
                updateMinMax(tmp);
        }
        else if (isLinkVar(m_variable))
        {
            const int n = swmm_output_get_link_count(m_handle);
            QVector<float> tmp(n);
            if (swmm_output_get_link_result(m_handle, p, linkVar(m_variable), tmp.data()) == 0)
                updateMinMax(tmp);
        }
        else if (isSubcatchVar(m_variable))
        {
            const int n = swmm_output_get_subcatch_count(m_handle);
            QVector<float> tmp(n);
            if (swmm_output_get_subcatch_result(m_handle, p, subcatchVar(m_variable), tmp.data()) == 0)
                updateMinMax(tmp);
        }
    }

    if (globalMin <= globalMax)
    {
        m_colorRamp.minValue = static_cast<double>(globalMin);
        m_colorRamp.maxValue = static_cast<double>(globalMax);
        emit repaintRequested();
    }
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

    // Tag value used by depopulateScene to identify this layer's items.
    const quintptr ownerTag = reinterpret_cast<quintptr>(this);

    auto tag = [ownerTag](QGraphicsItem *item) {
        item->setData(0, QVariant::fromValue<quintptr>(ownerTag));
        item->setZValue(10.0); // above the model layer
    };

    // Node marker radius in scene units (same as SWMMLayerItem's glyph size).
    const double r = m_modelLayer->junctionSymbol().size * 0.75;

    // ----- Node variable ------------------------------------------------
    if (isNodeVar(m_variable) && !m_nodeResults.isEmpty())
    {
        // Iterate through all node categories.
        const SWMMModelLayer::Category nodeCats[] = {
            SWMMModelLayer::CatJunctions,
            SWMMModelLayer::CatOutfalls,
            SWMMModelLayer::CatStorage,
            SWMMModelLayer::CatDividers,
        };

        for (auto cat : nodeCats)
        {
            const int count = m_modelLayer->categoryCount(cat);
            // Slice OUT.2 — per-kind override cache. When the kind has
            // a custom renderer installed AND the scope matches, prefer
            // the cached color/size; else fall back to the layer-level
            // ramp.
            const int catIdx = static_cast<int>(cat);
            const bool useOverride = m_kindUsesOverrides[catIdx]
                && m_kindFeatureColors[catIdx].size() == count;
            for (int row = 0; row < count; ++row)
            {
                const QString name = m_modelLayer->objectNameAt(cat, row);
                const auto it = m_nodeOutputIdx.constFind(name);
                if (it == m_nodeOutputIdx.constEnd())
                    continue;

                const int outIdx = it.value();
                if (outIdx < 0 || outIdx >= m_nodeResults.size())
                    continue;

                const float val = m_nodeResults[outIdx];
                if (!std::isfinite(val))
                    continue;

                double mx = 0.0, my = 0.0;
                if (!m_modelLayer->elementPosition(name, &mx, &my))
                    continue;

                QColor col;
                double radius = r;
                if (useOverride) {
                    col = m_kindFeatureColors[catIdx][row];
                    const double sz = m_kindFeatureSizes[catIdx][row];
                    if (sz > 0.0) radius = sz * 0.5; // size is diameter
                } else {
                    col = m_colorRamp.colorForValue(static_cast<double>(val));
                }
                if (!col.isValid())
                    col = m_colorRamp.colorForValue(static_cast<double>(val));
                const QPointF center = toScene(mx, my);

                auto *ellipse = scene->addEllipse(
                    center.x() - radius, center.y() - radius,
                    2.0 * radius, 2.0 * radius,
                    QPen(col.darker(130), 0.5),
                    QBrush(col));
                tag(ellipse);
            }
        }
    }

    // ----- Link variable -----------------------------------------------
    if (isLinkVar(m_variable) && !m_linkResults.isEmpty())
    {
        const SWMMModelLayer::Category linkCats[] = {
            SWMMModelLayer::CatConduits,
            SWMMModelLayer::CatPumps,
            SWMMModelLayer::CatOrifices,
            SWMMModelLayer::CatWeirs,
            SWMMModelLayer::CatOutlets,
        };

        const double penWidth = m_modelLayer->conduitSymbol().size * 2.0;

        for (auto cat : linkCats)
        {
            const int count = m_modelLayer->categoryCount(cat);
            // Slice OUT.2 — per-kind override cache; see node loop above.
            const int catIdx = static_cast<int>(cat);
            const bool useOverride = m_kindUsesOverrides[catIdx]
                && m_kindFeatureColors[catIdx].size() == count;
            for (int row = 0; row < count; ++row)
            {
                const QString name = m_modelLayer->objectNameAt(cat, row);
                const auto it = m_linkOutputIdx.constFind(name);
                if (it == m_linkOutputIdx.constEnd())
                    continue;

                const int outIdx = it.value();
                if (outIdx < 0 || outIdx >= m_linkResults.size())
                    continue;

                const float val = m_linkResults[outIdx];
                if (!std::isfinite(val))
                    continue;

                QColor col;
                double pw = penWidth;
                if (useOverride) {
                    col = m_kindFeatureColors[catIdx][row];
                    const double sz = m_kindFeatureSizes[catIdx][row];
                    if (sz > 0.0) pw = sz;   // size acts as line width
                } else {
                    col = m_colorRamp.colorForValue(static_cast<double>(val));
                }
                if (!col.isValid())
                    col = m_colorRamp.colorForValue(static_cast<double>(val));
                QPen pen(col, pw);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);

                double lx = 0.0, ly = 0.0;
                if (!m_modelLayer->elementPosition(name, &lx, &ly))
                    continue;

                // Draw a colored dot at the link midpoint.
                const QPointF mid = toScene(lx, ly);
                auto *dot = scene->addEllipse(
                    mid.x() - pw, mid.y() - pw,
                    2.0 * pw, 2.0 * pw,
                    QPen(Qt::NoPen), QBrush(col));
                tag(dot);
            }
        }
    }

    // ----- Subcatchment variable ----------------------------------------
    if (isSubcatchVar(m_variable) && !m_subcatchResults.isEmpty())
    {
        const int count = m_modelLayer->categoryCount(SWMMModelLayer::CatSubcatchments);
        // Slice OUT.2 — per-kind override cache; see node loop above.
        const int catIdx = static_cast<int>(SWMMModelLayer::CatSubcatchments);
        const bool useOverride = m_kindUsesOverrides[catIdx]
            && m_kindFeatureColors[catIdx].size() == count;
        for (int row = 0; row < count; ++row)
        {
            const QString name = m_modelLayer->objectNameAt(SWMMModelLayer::CatSubcatchments, row);
            const auto it = m_subcatchOutputIdx.constFind(name);
            if (it == m_subcatchOutputIdx.constEnd())
                continue;

            const int outIdx = it.value();
            if (outIdx < 0 || outIdx >= m_subcatchResults.size())
                continue;

            const float val = m_subcatchResults[outIdx];
            if (!std::isfinite(val))
                continue;

            QColor col;
            if (useOverride)
                col = m_kindFeatureColors[catIdx][row];
            if (!col.isValid())
                col = m_colorRamp.colorForValue(static_cast<double>(val));
            const MapExtent ext = m_modelLayer->objectExtent(name);

            if (!std::isfinite(ext.xMin()))
                continue;

            // Fill the bounding-box rect as a colored overlay.
            const double x1 = ext.xMin(), y1 = ext.yMin();
            const double x2 = ext.xMax(), y2 = ext.yMax();
            auto *rect = scene->addRect(
                x1, -y2, x2 - x1, y2 - y1,
                QPen(Qt::NoPen),
                QBrush(QColor(col.red(), col.green(), col.blue(), 140)));
            tag(rect);
        }
    }
}

void SWMMResultsLayer::depopulateScene(QGraphicsScene *scene)
{
    OpenSWMMVisLayer::depopulateScene(scene);
}
