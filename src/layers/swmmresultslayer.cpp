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

#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QFile>
#include <QDataStream>
#include <QLinearGradient>
#include <QObject>
#include <cmath>
#include <algorithm>

// GDAL
#include <ogr_spatialref.h>

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

    // Default colour ramp: blue→green→red (CORINE-style)
    m_colorRamp = RasterColorRamp();  // default grayscale; can be replaced
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

    QFile f(m_resultsFilePath);
    if (!f.exists())
    {
        errors.append(QStringLiteral("Results file not found: ") + m_resultsFilePath);
        emit resultsError(errors.last());
        return false;
    }

    // TODO: Replace with actual OpenSWMMCore binary output file API calls.
    //   e.g.:  swmm_open(&m_handle, m_resultsFilePath.toUtf8().constData(), ...)
    //          swmm_getProjectSize(m_handle, NODES, &nNodes)
    //          m_totalSteps = swmm_getTotalSteps(m_handle)
    //          m_startDateTime = QDateTime::fromSecsSinceEpoch(swmm_getStartDateTime(m_handle))
    //
    // For now create a minimal stub that treats the file as opaque.

    if (!f.open(QIODevice::ReadOnly))
    {
        errors.append(QStringLiteral("Cannot open results file: ") + f.errorString());
        emit resultsError(errors.last());
        return false;
    }

    // Stub: read a 4-byte little-endian integer as total time steps
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    qint32 total = 0;
    ds >> total;
    f.close();

    m_totalSteps    = (total > 0) ? static_cast<int>(total) : 1;
    m_currentStep   = 0;
    m_startDateTime = QDateTime::currentDateTimeUtc();    // placeholder
    m_endDateTime   = m_startDateTime.addSecs(3600LL * m_totalSteps);

    emit totalTimeStepsChanged(m_totalSteps);
    emit currentTimeStepChanged(m_currentStep);
    emit currentDateTimeChanged(currentDateTime());
    emit resultsOpened();

    return true;
}

void SWMMResultsLayer::closeResults()
{
    // TODO: swmm_close(&m_handle) once real API integrated
    m_totalSteps  = 0;
    m_currentStep = 0;
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

int SWMMResultsLayer::currentTimeStep() const
{
    return m_currentStep;
}

QDateTime SWMMResultsLayer::currentDateTime() const
{
    if (m_totalSteps <= 0)
        return m_startDateTime;

    const qint64 rangeSecs = m_startDateTime.secsTo(m_endDateTime);
    const qint64 stepSecs  = (m_totalSteps > 1)
                             ? rangeSecs * m_currentStep / (m_totalSteps - 1)
                             : 0;
    return m_startDateTime.addSecs(stepSecs);
}

int SWMMResultsLayer::totalTimeSteps() const
{
    return m_totalSteps;
}

QDateTime SWMMResultsLayer::startDateTime() const
{
    return m_startDateTime;
}

QDateTime SWMMResultsLayer::endDateTime() const
{
    return m_endDateTime;
}

void SWMMResultsLayer::setCurrentTimeStep(int step)
{
    if (m_totalSteps <= 0)
        return;

    step = std::clamp(step, 0, m_totalSteps - 1);
    if (step == m_currentStep)
        return;

    m_currentStep = step;

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

SWMMResultVariable SWMMResultsLayer::variable() const
{
    return m_variable;
}

void SWMMResultsLayer::setVariable(SWMMResultVariable var)
{
    if (m_variable == var)
        return;

    m_variable = var;
    emit variableChanged(m_variable);
    emit repaintRequested();
}

RasterColorRamp SWMMResultsLayer::colorRamp() const
{
    return m_colorRamp;
}

void SWMMResultsLayer::setColorRamp(const RasterColorRamp &ramp)
{
    m_colorRamp = ramp;
    emit repaintRequested();
}

void SWMMResultsLayer::autoStretchColorRamp()
{
    // TODO: iterate all time steps, read variable values, compute global min/max
    //       then call m_colorRamp.setRange(min, max).
    //
    // Placeholder: no-op until OpenSWMMCore API is integrated.
}

// ---------------------------------------------------------------------------
// Legend
// ---------------------------------------------------------------------------

bool SWMMResultsLayer::showLegend() const
{
    return m_showLegend;
}

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
    // Rebuild the transform from this layer's own SRS to the canvas CRS
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
                                      const MapExtent &canvasExtent,
                                      const SpatialReferenceSystem *canvasSRS)
{
    if (!isVisible() || !m_modelLayer || opacity() <= 0.0)
        return;

    // The SWMMResultsLayer colour-maps simulation results over the network
    // geometry that already lives inside SWMMModelLayer.
    //
    // Full implementation requires the OpenSWMMCore binary output file API to
    // read per-element values at the current time step.  That API is not yet
    // integrated, so this method is currently a placeholder.
    //
    // TODO (once OpenSWMMCore API is available):
    //   1. Read current time-step values for each node/link/subcatchment:
    //        double val;
    //        swmm_getNodeResult(m_handle, m_currentStep, nodeIdx,
    //                           static_cast<int>(m_variable), &val);
    //   2. Call m_colorRamp.colorAt((val-min)/(max-min)) for each element.
    //   3. Create colour-mapped overlay items on top of the base SWMMModelLayer.

    Q_UNUSED(canvasExtent)
    Q_UNUSED(canvasSRS)
}

void SWMMResultsLayer::depopulateScene(QGraphicsScene *scene)
{
    // Default depopulate removes items by owner layer
    OpenSWMMVisLayer::depopulateScene(scene);
}
