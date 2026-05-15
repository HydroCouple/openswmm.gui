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
// Layer binding
// ---------------------------------------------------------------------------

void AnimationController::setLayer(SWMMResultsLayer *layer)
{
    if (m_layer == layer)
        return;

    pause();

    if (m_layer)
    {
        disconnect(m_layer, &SWMMResultsLayer::currentTimeStepChanged,
                   this, &AnimationController::onLayerPeriodChanged);
        disconnect(m_layer, &SWMMResultsLayer::currentDateTimeChanged,
                   this, &AnimationController::onLayerTimeChanged);
        disconnect(m_layer, &SWMMResultsLayer::totalTimeStepsChanged,
                   this, &AnimationController::onLayerTotalStepsChanged);
    }

    m_layer = layer;

    if (m_layer)
    {
        connect(m_layer, &SWMMResultsLayer::currentTimeStepChanged,
                this, &AnimationController::onLayerPeriodChanged);
        connect(m_layer, &SWMMResultsLayer::currentDateTimeChanged,
                this, &AnimationController::onLayerTimeChanged);
        connect(m_layer, &SWMMResultsLayer::totalTimeStepsChanged,
                this, &AnimationController::onLayerTotalStepsChanged);

        emit currentPeriodChanged(m_layer->currentTimeStep());
        emit currentTimeChanged(m_layer->currentDateTime());
        emit totalPeriodsChanged(m_layer->totalTimeSteps());
    }

    emit layerChanged(m_layer);
}

SWMMResultsLayer *AnimationController::layer() const
{
    return m_layer;
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
    // Fixed display frame rate: advance one period every (kDefaultStepMs / speed) ms.
    // Do NOT use the simulation report step — a 5-minute step would make the timer
    // fire every 5 minutes, making animation feel broken to the user.
    const int ms = qMax(kMinIntervalMs,
                        static_cast<int>(kDefaultStepMs / m_speed));
    m_timer->setInterval(ms);
}

// ---------------------------------------------------------------------------
// Playback control slots
// ---------------------------------------------------------------------------

void AnimationController::play()
{
    if (!m_layer || m_layer->totalTimeSteps() <= 0)
        return;

    if (m_playing)
        return;

    // If already at the last frame, rewind to the start automatically.
    if (m_layer->currentTimeStep() >= m_layer->totalTimeSteps() - 1)
        m_layer->setCurrentTimeStep(0);

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
    if (m_layer)
        m_layer->setCurrentTimeStep(0);
}

void AnimationController::stepForward()
{
    if (m_layer)
        m_layer->stepForward(false);
}

void AnimationController::stepBackward()
{
    if (m_layer)
        m_layer->stepBackward(false);
}

void AnimationController::seekToFirst()
{
    if (m_layer)
        m_layer->setCurrentTimeStep(0);
}

void AnimationController::seekToLast()
{
    if (m_layer)
        m_layer->setCurrentTimeStep(m_layer->totalTimeSteps() - 1);
}

void AnimationController::seekToPeriod(int period)
{
    if (m_layer)
        m_layer->setCurrentTimeStep(period);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void AnimationController::onTimerTick()
{
    if (!m_layer)
    {
        pause();
        return;
    }

    const int next = m_layer->currentTimeStep() + 1;
    if (next >= m_layer->totalTimeSteps())
    {
        // Reached end — stop (don't loop; user can re-press play).
        pause();
        return;
    }

    m_layer->setCurrentTimeStep(next);
}

void AnimationController::onLayerPeriodChanged(int step)
{
    emit currentPeriodChanged(step);
}

void AnimationController::onLayerTotalStepsChanged(int total)
{
    emit totalPeriodsChanged(total);
}

void AnimationController::onLayerTimeChanged(const QDateTime &dt)
{
    emit currentTimeChanged(dt);
}
