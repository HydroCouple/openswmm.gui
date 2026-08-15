/*!
 * \file   animationcontroller.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Slice BA — Animation Controller for time-stepped result playback.
 *
 * \details AnimationController drives playback of one or more SWMMResultsLayers
 *          using a primary/secondary model.  The primary layer's time grid is
 *          authoritative; each secondary layer is snapped to its nearest period
 *          via SWMMResultsLayer::periodIndexForDateTime on every tick.
 *
 *          Ownership: the controller is owned by SWMMVis.  Whenever a new
 *          results layer is added to the canvas it is auto-promoted to primary
 *          and the prior primary silently becomes a secondary.  The user may
 *          re-designate via the layer-tree context menu "Set as Active Results
 *          Layer" → SWMMVis::setActiveResultsLayer.
 *
 *          Tick interval = (primaryLayer->reportStepSeconds() * 1000 / speed),
 *          clamped to kMinIntervalMs so the UI stays responsive.
 */

#ifndef ANIMATIONCONTROLLER_H
#define ANIMATIONCONTROLLER_H

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>

// QPointer<SWMM2DResultsLayer> in the private section requires the
// inheritance chain to be visible; include the layer header instead of just
// forward-declaring.
#include "layers/swmm2dresultslayer.h"

class QTimer;
class SWMMResultsLayer;

/*!
 * \class AnimationController
 * \brief Central coordinator for result-layer time-stepping and playback.
 */
class AnimationController : public QObject
{
    Q_OBJECT

public:
    explicit AnimationController(QObject *parent = nullptr);
    ~AnimationController() override;

    // ----- Primary layer -------------------------------------------------

    /*!
     * \brief Set the primary (authoritative) results layer.
     * \details Stops playback first. Passing nullptr detaches all layers.
     *          Does NOT automatically demote the old primary to secondary —
     *          call addSecondaryLayer() explicitly if synchronised playback
     *          of the old layer is still desired.
     */
    void setPrimaryLayer(SWMMResultsLayer *layer);
    [[nodiscard]] SWMMResultsLayer *primaryLayer() const;

    // ----- 2D fallback layer (Slice CF.MVP-fix.1) -----------------------
    //
    // When no 1D primary is set, the controller drives a SWMM2DResultsLayer
    // directly so 2D-only runs (no .out results loaded) still animate.
    // When a 1D primary becomes available the fallback is silently demoted;
    // the 2D layer keeps following via the QDateTime fan-out in
    // SWMMVis::onAnimationCurrentTimeChanged.
    void setFallback2DLayer(SWMM2DResultsLayer *layer);
    [[nodiscard]] SWMM2DResultsLayer *fallback2DLayer() const { return m_fallback2D; }

    // ----- Secondary layers ----------------------------------------------

    void addSecondaryLayer(SWMMResultsLayer *layer);
    void removeSecondaryLayer(SWMMResultsLayer *layer);
    void clearSecondaryLayers();

    /*! Returns primary first, then secondary layers in insertion order. */
    [[nodiscard]] QList<SWMMResultsLayer *> allLayers() const;

    // ----- State query ---------------------------------------------------

    [[nodiscard]] bool   isPlaying() const { return m_playing; }
    [[nodiscard]] double speed()     const { return m_speed;   }

    // ----- Sync window (causal "as-of within timespan") ------------------
    //
    // Non-driver outputs display the latest frame at or before the cursor
    // (causal floor). The window is the look-back span [t - w, t] used by the
    // range-slider UI and to classify an output as fresh/stale; under the
    // hold-last policy it does not change which frame shows.
    [[nodiscard]] qint64 windowMs() const { return m_windowMs; }

    /*! Run time span of the active driver (1D primary, else 2D fallback),
     *  used to normalise the range slider. Invalid when no driver. */
    [[nodiscard]] QDateTime driverStartTime() const;
    [[nodiscard]] QDateTime driverEndTime()   const;
    /*! Current cursor time of the active driver. Invalid when no driver. */
    [[nodiscard]] QDateTime currentDateTime() const;

    // ----- Speed ---------------------------------------------------------

    /*!
     * \brief Set playback speed multiplier (0.25 / 0.5 / 1 / 2 / 4 / 8).
     * \details Restarts the timer at the new interval if currently playing.
     */
    void setSpeed(double speed);

    /*! Set the look-back window. \p ms < 0 is clamped to 0. Emits
     *  windowChanged when the value moves. */
    void setWindowMs(qint64 ms);
    void setWindowSec(double sec) { setWindowMs(static_cast<qint64>(sec * 1000.0)); }

public slots:
    void play();
    void pause();
    void stop();
    void stepForward();
    void stepBackward();
    void seekToFirst();
    void seekToLast();
    void seekToPeriod(int period);
    /*! Seek the driver to \p dt (1D primary: nearest report step; 2D
     *  fallback: nearest frame). Used by the range-slider cursor handle. */
    void seekToTime(const QDateTime &dt);

signals:
    void currentTimeChanged(const QDateTime &dt);
    void currentPeriodChanged(int period);
    void playStateChanged(bool playing);
    void totalPeriodsChanged(int total);
    void primaryLayerChanged(SWMMResultsLayer *layer);
    void windowChanged(qint64 windowMs);

private slots:
    void onTimerTick();
    void onPrimaryPeriodChanged(int step);
    void onPrimaryTotalStepsChanged(int total);

    // 2D fallback re-emit slots (mirror the 1D ones).
    void onFallback2DPeriodChanged(int step);
    void onFallback2DTimeChanged(const QDateTime &dt);
    void onFallback2DRangeChanged(int lo, int hi);

private:
    void updateTimerInterval();
    void connectPrimary();
    void disconnectPrimary();
    void connectFallback2D();
    void disconnectFallback2D();

    // Returns true when either a 1D primary or a 2D fallback is set.
    [[nodiscard]] bool   hasDriver() const;
    // Total step count from whichever driver is active (1D primary preferred).
    [[nodiscard]] int    driverTotalSteps() const;
    // Current step index from whichever driver is active.
    [[nodiscard]] int    driverCurrentStep() const;
    // Advance whichever driver is active to \p step.
    void driverSetStep(int step);

    /*! Slice Z.13-controller-range — effective [min, max] step range
     *  considering the primary's TemporalSpec.startTime/endTime. Falls
     *  back to [0, totalSteps-1] when the spec is disabled or the start/
     *  end times aren't valid. For the 2D fallback driver, currently
     *  always returns the full range (datetime-keyed clamping there is
     *  the Z.13-controller-2d-range follow-up). */
    void effectiveStepRange(int &outMin, int &outMax) const;

    QPointer<SWMMResultsLayer>        m_primaryLayer;
    QList<QPointer<SWMMResultsLayer>> m_secondaryLayers;
    QPointer<SWMM2DResultsLayer>      m_fallback2D;
    QTimer *m_timer    = nullptr;
    bool    m_playing  = false;
    double  m_speed    = 1.0;
    qint64  m_windowMs = 0;   ///< look-back sync window (0 = floor with no band)

    /*! Frames advanced per timer tick. The tick interval floors at
     *  kMinIntervalMs (~20 Hz), so high speed multipliers that ask for a
     *  shorter interval instead advance multiple frames per tick (skipping
     *  intermediates) — that's what makes 8× actually faster than 4×, and it
     *  also keeps the slide moving when per-frame render cost dominates. */
    int     m_stepsPerTick = 1;

    /*! Slice Z.13-controller — playback direction (+1 forward, -1
     *  backward). Only flips to -1 when the primary's TemporalSpec has
     *  pingPong=true and the playback hits an end of the range.
     *  Forward-only otherwise so existing UI behaviour is preserved. */
    int     m_direction = 1;

    static constexpr int kMinIntervalMs  = 50;
    static constexpr int kDefaultStepMs  = 200; // fallback when primary has no report step
};

#endif // ANIMATIONCONTROLLER_H
