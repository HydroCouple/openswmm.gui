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

                const QColor col = m_colorRamp.colorForValue(static_cast<double>(val));
                const QPointF center = toScene(mx, my);

                auto *ellipse = scene->addEllipse(
                    center.x() - r, center.y() - r, 2.0 * r, 2.0 * r,
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

                const QColor col = m_colorRamp.colorForValue(static_cast<double>(val));
                QPen pen(col, penWidth);
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);

                double lx = 0.0, ly = 0.0;
                if (!m_modelLayer->elementPosition(name, &lx, &ly))
                    continue;

                // Draw a colored dot at the link midpoint.
                const QPointF mid = toScene(lx, ly);
                auto *dot = scene->addEllipse(
                    mid.x() - penWidth, mid.y() - penWidth,
                    2.0 * penWidth, 2.0 * penWidth,
                    QPen(Qt::NoPen), QBrush(col));
                tag(dot);
            }
        }
    }

    // ----- Subcatchment variable ----------------------------------------
    if (isSubcatchVar(m_variable) && !m_subcatchResults.isEmpty())
    {
        const int count = m_modelLayer->categoryCount(SWMMModelLayer::CatSubcatchments);
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

            const QColor col = m_colorRamp.colorForValue(static_cast<double>(val));
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
