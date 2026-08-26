/*!
 * \file   animationcontroller.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "animation/animationcontroller.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dresultslayer.h"

// Slice S2.5a (RENDERING_OUTPUT_SUBLAYERS_PLAN.md §2 Decision 3) — per-tick
// dispatch to ISublayerHost so only DYNAMIC sublayers invalidate on
// animation ticks. The dynamic_cast in the slot bodies activates this only
// when the layer actually inherits the host (1D layer adopted in S2.4;
// 2D layer adopts in S5).
#include "render/isublayerhost.h"

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
    // Slice §Y.1 — only one wire from the primary into the cascade.
    // onPrimaryPeriodChanged derives the wall-clock time from the layer and
    // emits currentTimeChanged itself, so the second
    // currentDateTimeChanged → onPrimaryTimeChanged bridge is gone. The
    // layer still emits currentDateTimeChanged for direct Q_PROPERTY / UI
    // consumers (DateTime edit widget binding).
    connect(m_primaryLayer, &SWMMResultsLayer::currentTimeStepChanged,
            this, &AnimationController::onPrimaryPeriodChanged);
    connect(m_primaryLayer, &SWMMResultsLayer::totalTimeStepsChanged,
            this, &AnimationController::onPrimaryTotalStepsChanged);
    // Slice Z.13-controller — refresh the timer interval whenever the
    // primary's TemporalSpec changes (Temporal tab edit, project load,
    // programmatic setter). The lambda gates on m_playing so we don't
    // restart a stopped timer; updateTimerInterval is idempotent when
    // the timer is idle.
    connect(m_primaryLayer, &OpenSWMMVisLayer::temporalSpecChanged,
            this, [this]() { updateTimerInterval(); });
    // Reset direction when the primary switches — pingPong state is
    // not preserved across layer swaps.
    m_direction = 1;
}

void AnimationController::disconnectPrimary()
{
    if (!m_primaryLayer) return;
    disconnect(m_primaryLayer, &SWMMResultsLayer::currentTimeStepChanged,
               this, &AnimationController::onPrimaryPeriodChanged);
    disconnect(m_primaryLayer, &SWMMResultsLayer::totalTimeStepsChanged,
               this, &AnimationController::onPrimaryTotalStepsChanged);
    // Slice Z.13-controller — drop the spec observer; the lambda we
    // installed in connectPrimary captured `this` so disconnect needs
    // the sender + receiver pair to undo it.
    disconnect(m_primaryLayer, &OpenSWMMVisLayer::temporalSpecChanged,
               this, nullptr);
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
    // Slice Z.13-controller-2d-fallback — refresh tick interval when the
    // 2D layer's TemporalSpec changes (mirrors the 1D primary path in
    // connectPrimary). Only meaningful when no 1D primary is set; when
    // a 1D primary takes over, updateTimerInterval ignores this layer's
    // spec.
    connect(m_fallback2D, &OpenSWMMVisLayer::temporalSpecChanged,
            this, [this]() { updateTimerInterval(); });
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
    disconnect(m_fallback2D, &OpenSWMMVisLayer::temporalSpecChanged,
               this, nullptr);
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
        // A driverSetStep on a live 2D source is always user/playback-initiated
        // (play, seek, step). Stop auto-following the newest streamed frame so
        // the requested frame stays put; re-arm follow only when the user lands
        // on the latest available frame, so "seek to last" snaps back to live.
        const int n = m_fallback2D->source() ? m_fallback2D->source()->timeCount()
                                             : 0;
        m_fallback2D->setFollowLive(n > 0 && step >= n - 1);
        m_fallback2D->setCurrentTimeIndex(step);
    }
}

QDateTime AnimationController::driverStartTime() const
{
    // 2026-07-19 — use the REPORTED start (period 0's time), not the
    // simulation start. endDateTime() is the last frame's time, so the
    // span must open at the first frame's time too; anchoring on
    // START_DATE left a dead zone at the left of the global slider
    // whenever REPORT_START != START_DATE (frame 0 normalised to > 0).
    if (m_primaryLayer) return m_primaryLayer->reportedStartDateTime();
    if (m_fallback2D && m_fallback2D->source())
        return m_fallback2D->source()->simTimeAt(0);
    return {};
}

QDateTime AnimationController::driverEndTime() const
{
    if (m_primaryLayer) return m_primaryLayer->endDateTime();
    if (m_fallback2D && m_fallback2D->source()) {
        const int n = m_fallback2D->source()->timeCount();
        if (n > 0) return m_fallback2D->source()->simTimeAt(n - 1);
    }
    return {};
}

QDateTime AnimationController::currentDateTime() const
{
    if (m_primaryLayer) return m_primaryLayer->currentDateTime();
    if (m_fallback2D && m_fallback2D->source())
        return m_fallback2D->source()->simTimeAt(m_fallback2D->currentTimeIndex());
    return {};
}

void AnimationController::setWindowMs(qint64 ms)
{
    if (ms < 0) ms = 0;
    if (ms == m_windowMs) return;
    m_windowMs = ms;
    emit windowChanged(m_windowMs);
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
    // Slice Z.13-controller — when the active driver's TemporalSpec is
    // enabled, the spec's `frameRateFps` overrides the global step-rate
    // calculation so the user gets the playback speed they set in the
    // Temporal tab. The global `m_speed` multiplier still composes on
    // top so the toolbar's 0.5×/2× buttons keep working.
    //
    // Active driver: 1D primary if set, else the 2D fallback (Slice
    // Z.13-controller-2d-fallback — when no 1D primary is present, the
    // 2D layer drives playback so its TemporalSpec should govern). Falls
    // back to the legacy formula (kDefaultStepMs / speed) when no spec
    // is enabled, preserving every existing project's animation
    // behaviour unchanged.
    int ms = static_cast<int>(kDefaultStepMs / m_speed);
    const OpenSWMM::Render::TemporalSpec *activeSpec = nullptr;
    if (m_primaryLayer)    activeSpec = &m_primaryLayer->temporalSpec();
    else if (m_fallback2D) activeSpec = &m_fallback2D->temporalSpec();
    if (activeSpec && activeSpec->enabled && activeSpec->frameRateFps > 0.0) {
        const double effectiveFps = activeSpec->frameRateFps * m_speed;
        ms = static_cast<int>(1000.0 / qMax(0.1, effectiveFps));
    }
    // When the requested interval falls below the floor, keep the timer at the
    // floor and advance several frames per tick so the effective playback rate
    // still scales with speed (otherwise 8× and 4× both clamp to ~20 Hz).
    m_stepsPerTick = (ms >= kMinIntervalMs || ms <= 0)
                         ? 1
                         : qMax(1, (kMinIntervalMs + ms / 2) / ms);
    m_timer->setInterval(qMax(kMinIntervalMs, ms));
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

    // Slice Z.13-controller — reset direction at the start of every
    // play so pingPong always begins forward. Otherwise a previous
    // ping-pong cycle could have left m_direction at -1 and the user
    // would press Play and watch the layer rewind.
    m_direction = 1;
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
    // Slice Z.13-controller-range — single path through the driver
    // abstractions so the temporal-spec clamp applies uniformly to both
    // 1D primary and 2D fallback drivers. The layer's own
    // stepForward(loop=false) ignores the spec; doing the math here
    // honors startTime/endTime.
    int rMin = 0, rMax = 0;
    effectiveStepRange(rMin, rMax);
    if (rMax < rMin) return;
    driverSetStep(std::min(driverCurrentStep() + 1, rMax));
}

void AnimationController::stepBackward()
{
    int rMin = 0, rMax = 0;
    effectiveStepRange(rMin, rMax);
    if (rMax < rMin) return;
    driverSetStep(std::max(driverCurrentStep() - 1, rMin));
}

void AnimationController::seekToFirst()
{
    int rMin = 0, rMax = 0;
    effectiveStepRange(rMin, rMax);
    driverSetStep(rMin);
}

void AnimationController::seekToLast()
{
    int rMin = 0, rMax = 0;
    effectiveStepRange(rMin, rMax);
    if (rMax >= rMin) driverSetStep(rMax);
}

void AnimationController::seekToPeriod(int period)
{
    int rMin = 0, rMax = 0;
    effectiveStepRange(rMin, rMax);
    driverSetStep(std::clamp(period, rMin, rMax));
}

void AnimationController::seekToTime(const QDateTime &dt)
{
    if (!dt.isValid()) return;
    if (m_primaryLayer) {
        seekToPeriod(m_primaryLayer->periodIndexForDateTime(dt));
    } else if (m_fallback2D) {
        m_fallback2D->setCurrentSimTime(dt);   // nearest frame; re-emits via fallback slots
    }
}

void AnimationController::effectiveStepRange(int &outMin, int &outMax) const
{
    const int total = driverTotalSteps();
    outMin = 0;
    outMax = total > 0 ? total - 1 : 0;

    // Only the 1D primary exposes a datetime → index converter. For the
    // 2D fallback, range clamping by datetime is the named
    // Z.13-controller-2d-range follow-up; for now the 2D path uses the
    // full [0, total-1] interval.
    if (!m_primaryLayer) return;
    const auto &spec = m_primaryLayer->temporalSpec();
    if (!spec.enabled) return;

    if (spec.startTime.isValid()) {
        const int idx = m_primaryLayer->periodIndexForDateTime(spec.startTime);
        outMin = std::clamp(idx, 0, outMax);
    }
    if (spec.endTime.isValid()) {
        const int idx = m_primaryLayer->periodIndexForDateTime(spec.endTime);
        outMax = std::clamp(idx, outMin, outMax);
    }
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

    // Slice Z.13-controller — end-of-range behaviour comes from the
    // toolbar Cycle checkbox (m_loop, default on) OR the primary's
    // TemporalSpec when enabled. Three behaviours:
    //   - m_loop=false AND (spec disabled OR loop=false, pingPong=false):
    //     pause on reaching the end. Preserves the legacy UX.
    //   - loop: wrap to rangeMin and keep playing.
    //   - pingPong=true (implies loop=true per the spec doc): reverse
    //     direction and step backwards until hitting rangeMin, then
    //     reverse again. Forward-and-back forever.
    bool loop     = m_loop;
    bool pingPong = false;
    if (m_primaryLayer) {
        const auto &spec = m_primaryLayer->temporalSpec();
        if (spec.enabled) {
            loop     = loop || spec.loop || spec.pingPong;
            pingPong = spec.pingPong;
        }
    } else if (m_fallback2D) {
        // Slice Z.13-controller-2d-fallback — 2D layer's spec drives end-of-
        // range behaviour when no 1D primary is set. The frame-rate path is
        // handled in updateTimerInterval below.
        const auto &spec = m_fallback2D->temporalSpec();
        if (spec.enabled) {
            loop     = loop || spec.loop || spec.pingPong;
            pingPong = spec.pingPong;
        }
    }

    // Slice Z.13-controller-range — use the effective range, not the
    // raw [0, total) bounds, so the end-of-range logic respects
    // startTime / endTime when set.
    int rMin = 0, rMax = total - 1;
    effectiveStepRange(rMin, rMax);

    const int cur = driverCurrentStep();
    int next = cur + m_direction * m_stepsPerTick;

    if (pingPong) {
        if (next > rMax) {
            m_direction = -1;
            next = qMax(rMin, cur - 1);
        } else if (next < rMin) {
            m_direction = 1;
            next = qMin(rMax, cur + 1);
        }
        driverSetStep(next);
        return;
    }

    if (next > rMax) {
        if (loop) {
            driverSetStep(rMin);
        } else {
            pause();
        }
        return;
    }
    if (next < rMin) {
        // Shouldn't happen in forward-only playback but guard anyway.
        driverSetStep(rMin);
        return;
    }

    driverSetStep(next);
}

void AnimationController::onPrimaryPeriodChanged(int step)
{
    // Slice S2.5a — give the primary layer a chance to invalidate its
    // dynamic sublayers BEFORE we fan out the public signal. Consumers
    // that observe currentPeriodChanged (renderers, comparison plot
    // dialog, status bar) then see a layer whose sublayer state is
    // already marked stale for the new period. Static sublayers stay
    // untouched — the perf-relevant cut from Decision 3.
    if (auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(m_primaryLayer.data()))
        host->dispatchAnimationTick(step);

    // Slice §Y.1 — derive wall-clock time once, fan it out to secondaries +
    // the 2D fallback, and emit currentTimeChanged ourselves. Previously
    // currentTimeChanged was emitted by onPrimaryTimeChanged via a separate
    // signal bridge (currentDateTimeChanged), which meant one timestep
    // produced two independent cascades from the layer into the controller.
    QDateTime now;
    if (m_primaryLayer) {
        now = m_primaryLayer->currentDateTime();
        if (now.isValid()) {
            // Causal "as-of" sync: each non-driver output shows its latest
            // frame at or before the cursor, so a coarser-stepped output never
            // jumps ahead of the primary clock.
            for (auto &sec : m_secondaryLayers) {
                if (sec && sec.data() != m_primaryLayer.data())
                    sec->setCurrentTimeStep(sec->periodIndexForDateTimeAsOf(now));
            }
            if (m_fallback2D)
                m_fallback2D->setCurrentSimTimeAsOf(now);
        }
    }

    emit currentPeriodChanged(step);
    if (now.isValid())
        emit currentTimeChanged(now);
}

void AnimationController::onPrimaryTotalStepsChanged(int total)
{
    emit totalPeriodsChanged(total);
}

// ---------------------------------------------------------------------------
// 2D fallback re-emit slots — only emit when no 1D primary is driving so we
// don't fire duplicate signals on mixed-mode runs (1D + 2D both open).
// ---------------------------------------------------------------------------

void AnimationController::onFallback2DPeriodChanged(int step)
{
    if (m_primaryLayer) return;

    // Slice S2.5a — same dispatch pattern as the 1D primary path. The
    // dynamic_cast is a no-op today because SWMM2DResultsLayer does NOT
    // yet inherit ISublayerHost (that's Slice S5). Wired here in advance
    // so when S5 lands, no AnimationController change is needed —
    // adoption alone activates the dispatch path.
    if (auto *host = dynamic_cast<OpenSWMM::Render::ISublayerHost *>(m_fallback2D.data()))
        host->dispatchAnimationTick(step);

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
