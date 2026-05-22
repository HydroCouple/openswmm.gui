/*!
 * \file   animationcontroller.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "animation/animationcontroller.h"
#include "layers/swmmresultslayer.h"

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
    if (!m_primaryLayer || m_primaryLayer->totalTimeSteps() <= 0)
        return;

    if (m_playing)
        return;

    if (m_primaryLayer->currentTimeStep() >= m_primaryLayer->totalTimeSteps() - 1)
        m_primaryLayer->setCurrentTimeStep(0);

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
    if (m_primaryLayer)
        m_primaryLayer->setCurrentTimeStep(0);
}

void AnimationController::stepForward()
{
    if (m_primaryLayer)
        m_primaryLayer->stepForward(false);
}

void AnimationController::stepBackward()
{
    if (m_primaryLayer)
        m_primaryLayer->stepBackward(false);
}

void AnimationController::seekToFirst()
{
    if (m_primaryLayer)
        m_primaryLayer->setCurrentTimeStep(0);
}

void AnimationController::seekToLast()
{
    if (m_primaryLayer)
        m_primaryLayer->setCurrentTimeStep(m_primaryLayer->totalTimeSteps() - 1);
}

void AnimationController::seekToPeriod(int period)
{
    if (m_primaryLayer)
        m_primaryLayer->setCurrentTimeStep(period);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void AnimationController::onTimerTick()
{
    if (!m_primaryLayer) {
        pause();
        return;
    }

    const int next = m_primaryLayer->currentTimeStep() + 1;
    if (next >= m_primaryLayer->totalTimeSteps()) {
        pause();
        return;
    }

    m_primaryLayer->setCurrentTimeStep(next);
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
