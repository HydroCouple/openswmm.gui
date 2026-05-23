/*!
 * \file   animationcontroller.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "animation/animationcontroller.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"

#include <QTimer>

AnimationController::AnimationController(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &AnimationController::onTimerTick);
}

AnimationController::~AnimationController()
{
    // m_timer is a child QObject — deleted automatically
}

// ---------------------------------------------------------------------------
// Primary layer
// ---------------------------------------------------------------------------

void AnimationController::setPrimaryLayer(SWMMResultsLayer *layer)
{
    if (m_primaryLayer == layer)
        return;

    pause();
    disconnectPrimary();

    m_primaryLayer = layer;
    connectPrimary();

    if (m_primaryLayer) {
        emit currentPeriodChanged(m_primaryLayer->currentTimeStep());
        emit currentTimeChanged(m_primaryLayer->currentDateTime());
        emit totalPeriodsChanged(m_primaryLayer->totalTimeSteps());
    } else if (m_fallback2D) {
        // Demoting the 1D primary back to "no primary" — let the 2D fallback
        // continue driving by re-emitting its current state.
        emit currentPeriodChanged(m_fallback2D->currentTimeIndex());
        if (auto *src = m_fallback2D->source()) {
            emit currentTimeChanged(src->simTimeAt(m_fallback2D->currentTimeIndex()));
            emit totalPeriodsChanged(src->timeCount());
        }
    }

    emit primaryLayerChanged(m_primaryLayer);
}

SWMMResultsLayer *AnimationController::primaryLayer() const
{
    return m_primaryLayer;
}

void AnimationController::connectPrimary()
{
    if (!m_primaryLayer) return;
    connect(m_primaryLayer, &SWMMResultsLayer::currentTimeStepChanged,
            this, &AnimationController::onPrimaryPeriodChanged);
    connect(m_primaryLayer, &SWMMResultsLayer::currentDateTimeChanged,
            this, &AnimationController::onPrimaryTimeChanged);
    connect(m_primaryLayer, &SWMMResultsLayer::totalTimeStepsChanged,
            this, &AnimationController::onPrimaryTotalStepsChanged);
}

void AnimationController::disconnectPrimary()
{
    if (!m_primaryLayer) return;
    disconnect(m_primaryLayer, &SWMMResultsLayer::currentTimeStepChanged,
               this, &AnimationController::onPrimaryPeriodChanged);
    disconnect(m_primaryLayer, &SWMMResultsLayer::currentDateTimeChanged,
               this, &AnimationController::onPrimaryTimeChanged);
    disconnect(m_primaryLayer, &SWMMResultsLayer::totalTimeStepsChanged,
               this, &AnimationController::onPrimaryTotalStepsChanged);
}

// ---------------------------------------------------------------------------
// 2D fallback layer (Slice CF.MVP-fix.1)
// ---------------------------------------------------------------------------

void AnimationController::setFallback2DLayer(SWMM2DResultsLayer *layer)
{
    if (m_fallback2D == layer)
        return;

    pause();
    disconnectFallback2D();
    m_fallback2D = layer;
    connectFallback2D();

    // If no 1D primary is driving, refresh state off the fallback so the
    // slider + DateTime widget pick up the right range / current frame.
    if (!m_primaryLayer && m_fallback2D) {
        if (auto *src = m_fallback2D->source()) {
            emit totalPeriodsChanged(src->timeCount());
            emit currentPeriodChanged(m_fallback2D->currentTimeIndex());
            emit currentTimeChanged(src->simTimeAt(m_fallback2D->currentTimeIndex()));
        }
    }
}

void AnimationController::connectFallback2D()
{
    if (!m_fallback2D) return;
    connect(m_fallback2D, &SWMM2DResultsLayer::currentTimeChanged,
            this, &AnimationController::onFallback2DPeriodChanged);
    connect(m_fallback2D, &SWMM2DResultsLayer::currentDateTimeChanged,
            this, &AnimationController::onFallback2DTimeChanged);
    connect(m_fallback2D, &SWMM2DResultsLayer::timeRangeChanged,
            this, &AnimationController::onFallback2DRangeChanged);
}

void AnimationController::disconnectFallback2D()
{
    if (!m_fallback2D) return;
    disconnect(m_fallback2D, &SWMM2DResultsLayer::currentTimeChanged,
               this, &AnimationController::onFallback2DPeriodChanged);
    disconnect(m_fallback2D, &SWMM2DResultsLayer::currentDateTimeChanged,
               this, &AnimationController::onFallback2DTimeChanged);
    disconnect(m_fallback2D, &SWMM2DResultsLayer::timeRangeChanged,
               this, &AnimationController::onFallback2DRangeChanged);
}

// ---------------------------------------------------------------------------
// Driver-agnostic accessors (1D primary preferred, then 2D fallback)
// ---------------------------------------------------------------------------

bool AnimationController::hasDriver() const
{
    return m_primaryLayer != nullptr || m_fallback2D != nullptr;
}

int AnimationController::driverTotalSteps() const
{
    if (m_primaryLayer) return m_primaryLayer->totalTimeSteps();
    if (m_fallback2D)   return m_fallback2D->source()
                                  ? m_fallback2D->source()->timeCount()
                                  : 0;
    return 0;
}

int AnimationController::driverCurrentStep() const
{
    if (m_primaryLayer) return m_primaryLayer->currentTimeStep();
    if (m_fallback2D)   return m_fallback2D->currentTimeIndex();
    return 0;
}

void AnimationController::driverSetStep(int step)
{
    if (m_primaryLayer) {
        m_primaryLayer->setCurrentTimeStep(step);
    } else if (m_fallback2D) {
        m_fallback2D->setCurrentTimeIndex(step);
    }
}

// ---------------------------------------------------------------------------
// Secondary layers
// ---------------------------------------------------------------------------

void AnimationController::addSecondaryLayer(SWMMResultsLayer *layer)
{
    if (!layer || m_secondaryLayers.contains(layer)) return;
    m_secondaryLayers.append(layer);
}

void AnimationController::removeSecondaryLayer(SWMMResultsLayer *layer)
{
    m_secondaryLayers.removeAll(layer);
}

void AnimationController::clearSecondaryLayers()
{
    m_secondaryLayers.clear();
}

QList<SWMMResultsLayer *> AnimationController::allLayers() const
{
    QList<SWMMResultsLayer *> result;
    if (m_primaryLayer) result.append(m_primaryLayer);
    for (auto &ptr : m_secondaryLayers)
        if (ptr) result.append(ptr);
    return result;
}

// ---------------------------------------------------------------------------
// Speed
// ---------------------------------------------------------------------------

void AnimationController::setSpeed(double speed)
{
    if (qFuzzyCompare(m_speed, speed))
        return;

    m_speed = speed;
    if (m_playing)
        updateTimerInterval();
}

void AnimationController::updateTimerInterval()
{
    const int ms = qMax(kMinIntervalMs,
                        static_cast<int>(kDefaultStepMs / m_speed));
    m_timer->setInterval(ms);
}

// ---------------------------------------------------------------------------
// Playback control slots
// ---------------------------------------------------------------------------

void AnimationController::play()
{
    if (!hasDriver() || driverTotalSteps() <= 0)
        return;

    if (m_playing)
        return;

    if (driverCurrentStep() >= driverTotalSteps() - 1)
        driverSetStep(0);

    m_playing = true;
    updateTimerInterval();
    m_timer->start();
    emit playStateChanged(true);
}

void AnimationController::pause()
{
    if (!m_playing)
        return;

    m_playing = false;
    m_timer->stop();
    emit playStateChanged(false);
}

void AnimationController::stop()
{
    pause();
    driverSetStep(0);
}

void AnimationController::stepForward()
{
    if (m_primaryLayer) {
        m_primaryLayer->stepForward(false);
    } else if (m_fallback2D) {
        const int n = driverTotalSteps();
        if (n <= 0) return;
        const int next = std::min(driverCurrentStep() + 1, n - 1);
        m_fallback2D->setCurrentTimeIndex(next);
    }
}

void AnimationController::stepBackward()
{
    if (m_primaryLayer) {
        m_primaryLayer->stepBackward(false);
    } else if (m_fallback2D) {
        const int prev = std::max(driverCurrentStep() - 1, 0);
        m_fallback2D->setCurrentTimeIndex(prev);
    }
}

void AnimationController::seekToFirst()
{
    driverSetStep(0);
}

void AnimationController::seekToLast()
{
    const int n = driverTotalSteps();
    if (n > 0) driverSetStep(n - 1);
}

void AnimationController::seekToPeriod(int period)
{
    driverSetStep(period);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void AnimationController::onTimerTick()
{
    if (!hasDriver()) {
        pause();
        return;
    }

    const int total = driverTotalSteps();
    if (total <= 0) {
        pause();
        return;
    }

    const int next = driverCurrentStep() + 1;
    if (next >= total) {
        pause();
        return;
    }

    driverSetStep(next);
}

void AnimationController::onPrimaryPeriodChanged(int step)
{
    emit currentPeriodChanged(step);
}

void AnimationController::onPrimaryTotalStepsChanged(int total)
{
    emit totalPeriodsChanged(total);
}

void AnimationController::onPrimaryTimeChanged(const QDateTime &dt)
{
    emit currentTimeChanged(dt);
}

// ---------------------------------------------------------------------------
// 2D fallback re-emit slots — only emit when no 1D primary is driving so we
// don't fire duplicate signals on mixed-mode runs (1D + 2D both open).
// ---------------------------------------------------------------------------

void AnimationController::onFallback2DPeriodChanged(int step)
{
    if (m_primaryLayer) return;
    emit currentPeriodChanged(step);
}

void AnimationController::onFallback2DTimeChanged(const QDateTime &dt)
{
    if (m_primaryLayer) return;
    emit currentTimeChanged(dt);
}

void AnimationController::onFallback2DRangeChanged(int lo, int hi)
{
    if (m_primaryLayer) return;
    (void)lo;                          // range always starts at 0
    emit totalPeriodsChanged(hi + 1);  // total = (hi - lo + 1) with lo == 0
}
