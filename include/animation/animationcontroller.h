/**
 * @file      animationcontroller.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Slice BA — Animation Controller for time-stepped result playback.
 *
 * @details A 1D primary supplies the authoritative cursor. Secondaries and any
 *          2D fallback follow its latest frame at or before that time. Without
 *          a 1D primary the fallback can drive playback directly. Layer pointers
 *          are borrowed and guarded by QPointer; the owned timer runs on the GUI
 *          thread. Playback uses enabled temporal frame-rate settings or a
 *          200 ms base tick, composed with the global speed multiplier and a
 *          50 ms minimum interval with batched frame advances.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
    /**
     * @brief Create a paused controller with a repeating owned timer, speed 1, and toolbar looping enabled.
     * @param[in] parent Optional QObject owner; use the controller/layers on the GUI thread.
     */
    explicit AnimationController(QObject *parent = nullptr);
    /**
     * @brief Destroy the owned timer and connections without destroying borrowed result layers.
     */
    ~AnimationController() override;

    // ----- Primary layer -------------------------------------------------

    /*!
     * \brief Set the primary (authoritative) results layer.
     * \details Stops playback first. Passing nullptr detaches the 1D primary only.
     *          Does NOT automatically demote the old primary to secondary —
     *          call addSecondaryLayer() explicitly if synchronised playback
     *          of the old layer is still desired.
     * @param[in] layer Borrowed QPointer-guarded 1D layer, or null to let any 2D fallback drive.
     * @details An unchanged pointer is a no-op. New primary resets ping-pong direction and publishes its current period/time/count. Secondary and fallback registrations are retained.
     */
    void setPrimaryLayer(SWMMResultsLayer *layer);
    /**
     * @brief Inspect the authoritative 1D driver.
     * @return Borrowed primary layer, or null when unavailable/destroyed.
     */
    [[nodiscard]] SWMMResultsLayer *primaryLayer() const;

    // ----- 2D fallback layer (Slice CF.MVP-fix.1) -----------------------
    //
    // When no 1D primary is set, the controller drives a SWMM2DResultsLayer
    // directly so 2D-only runs (no .out results loaded) still animate.
    // When a 1D primary becomes available the fallback is silently demoted;
    // the 2D layer keeps following via the QDateTime fan-out in
    // SWMMVis::onAnimationCurrentTimeChanged.
    /**
     * @brief Replace the 2D driver used only while no 1D primary is set.
     * @param[in] layer Borrowed QPointer-guarded layer, or null to detach fallback.
     * @details Pauses playback on a changed pointer, rewires signals, and publishes fallback range/time only when no 1D primary is active.
     */
    void setFallback2DLayer(SWMM2DResultsLayer *layer);
    /**
     * @brief Inspect the registered 2D fallback even when a 1D primary takes precedence.
     * @return Borrowed layer, or null when absent/destroyed.
     */
    [[nodiscard]] SWMM2DResultsLayer *fallback2DLayer() const { return m_fallback2D; }

    // ----- Secondary layers ----------------------------------------------

    /**
     * @brief Register another output for causal synchronization on primary 1D ticks.
     * @param[in] layer Borrowed QPointer-guarded layer; null/duplicate registration is ignored.
     * @details Does not immediately seek the secondary; the current primary can also be listed but is skipped during tick fan-out.
     */
    void addSecondaryLayer(SWMMResultsLayer *layer);
    /**
     * @brief Remove every registration matching a secondary pointer.
     * @param[in] layer Borrowed layer identity, never destroyed by removal.
     */
    void removeSecondaryLayer(SWMMResultsLayer *layer);
    /**
     * @brief Clear secondary registrations without changing layers or the active driver.
     */
    void clearSecondaryLayers();

    /*! Returns primary first, then secondary layers in insertion order.
     * @return Borrowed live 1D pointers in primary/secondary order, skipping destroyed secondaries and excluding the 2D fallback; a primary also registered as secondary can appear twice.
     */
    [[nodiscard]] QList<SWMMResultsLayer *> allLayers() const;

    // ----- State query ---------------------------------------------------

    /**
     * @brief Read the controller playback state.
     * @return True while playback timer is running, including waiting at the live tail.
     */
    [[nodiscard]] bool isPlaying() const { return m_playing; }
    [[nodiscard]] bool isStopped() const { return m_stopped; }
    /**
     * @brief Read the stored global playback multiplier.
     * @return Current speed value; no input validation is performed by setSpeed().
     */
    [[nodiscard]] double speed() const { return m_speed; }

    // ----- Sync window (causal "as-of within timespan") ------------------
    //
    // Non-driver outputs display the latest frame at or before the cursor
    // (causal floor). The window is the look-back span [t - w, t] used by the
    // range-slider UI and to classify an output as fresh/stale; under the
    // hold-last policy it does not change which frame shows.
    /**
     * @brief Read the look-back interval used for freshness classification.
     * @return Nonnegative stored milliseconds; this does not alter causal frame selection.
     */
    [[nodiscard]] qint64 windowMs() const { return m_windowMs; }

    /*! Run time span of the active driver (1D primary, else 2D fallback),
     *  used to normalise the range slider. Invalid when no driver.
     * @return First reported 1D timestamp or 2D frame-zero timestamp, invalid without a usable driver.
     */
    [[nodiscard]] QDateTime driverStartTime() const;
    /**
     * @brief Read the latest reported driver time.
     * @return 1D end time or final 2D frame timestamp; invalid without usable source frames.
     */
    [[nodiscard]] QDateTime driverEndTime() const;
    /*! Current cursor time of the active driver. Invalid when no driver.
     * @return Active driver cursor timestamp, invalid without a usable driver/source.
     */
    [[nodiscard]] QDateTime currentDateTime() const;

    // ----- Speed ---------------------------------------------------------

    /*!
     * \brief Set playback speed multiplier (0.25 / 0.5 / 1 / 2 / 4 / 8).
     * \details Restarts the timer at the new interval if currently playing.
     * @param[in] speed Finite positive playback multiplier with representable timer arithmetic; caller enforces validity.
     * @details Fuzzy-equal values are a no-op; otherwise stores the value and updates interval when playing. Disabled temporal specs use a 200 ms base tick; enabled frameRateFps composes with this multiplier.
     */
    void setSpeed(double speed);

    /*! Set the look-back window. \p ms < 0 is clamped to 0. Emits
     *  windowChanged when the value moves.
     * @param[in] ms Look-back interval in milliseconds; negative becomes zero.
     * @details Equal clamped values are a no-op. Emits windowChanged() only on a change.
     */
    void setWindowMs(qint64 ms);
    /**
     * @brief Convert a look-back interval from seconds to truncated milliseconds.
     * @param[in] sec Finite value whose milliseconds are representable as qint64; negative values ultimately clamp to zero.
     */
    void setWindowSec(double sec) { setWindowMs(static_cast<qint64>(sec * 1000.0)); }

    // ----- Looping (toolbar "Cycle" checkbox) ----------------------------

    /*! When on (the default), playback wraps to the start of the range on
     *  reaching the end instead of pausing. Composes with the primary's
     *  TemporalSpec: spec loop/pingPong still apply when this is off.
     * @param[in] on Toolbar cycle flag; enabled temporal-spec loop/pingPong can still loop when this is false.
     * @details Stores without a signal or timer restart.
     */
    void setLooping(bool on) { m_loop = on; }
    /**
     * @brief Read the toolbar cycle flag.
     * @return Stored flag, independent of any enabled temporal-spec looping.
     */
    [[nodiscard]] bool looping() const { return m_loop; }

public slots:
    /**
     * @brief Start forward playback when a driver has frames.
     * @details No-op when already playing or no data. A completed nonlive driver at its final frame first seeks raw frame zero; live drivers retain the tail. Starts the timer and emits playStateChanged(true).
     */
    void play();
    /**
     * @brief Stop the timer while retaining the driver cursor.
     * @details No-op when already paused; otherwise emits playStateChanged(false).
     */
    void pause();
    /**
     * @brief Pause and request raw driver frame zero, independent of a temporal-spec start bound.
     */
    void stop();
    /**
     * @brief Request the next frame capped by the effective range upper bound, without pausing playback.
     */
    void stepForward();
    /**
     * @brief Request the previous frame floored by the effective range lower bound, without pausing playback.
     */
    void stepBackward();
    /**
     * @brief Request the effective first frame without changing playback state.
     */
    void seekToFirst();
    /**
     * @brief Request the effective final frame; a 2D fallback reaching its newest frame resumes live-follow mode.
     */
    void seekToLast();
    /**
     * @brief Clamp and seek a driver frame without changing playback state.
     * @param[in] period Requested zero-based report/frame index.
     */
    void seekToPeriod(int period);
    /*! Seek the driver to \p dt (1D primary: nearest report step; 2D
     *  fallback: nearest frame). Used by the range-slider cursor handle.
     * @param[in] dt Requested cursor; invalid timestamps are ignored.
     * @details 1D uses nearest-period conversion then temporal-range clamping; 2D delegates to setCurrentSimTime() directly.
     */
    void seekToTime(const QDateTime &dt);

signals:
    /**
     * @brief Publish an active-driver cursor timestamp.
     * @param[in] dt Current time; the 2D fallback forwards its timestamp directly.
     */
    void currentTimeChanged(const QDateTime &dt);
    /**
     * @brief Publish active-driver frame movement after dynamic sublayer tick dispatch.
     * @param[in] period Zero-based driver report/frame index.
     */
    void currentPeriodChanged(int period);
    /**
     * @brief Publish a playback-state transition.
     * @param[in] playing True after timer start, false after pause.
     */
    void playStateChanged(bool playing);
    /**
     * @brief Publish an active-driver frame-count update.
     * @param[in] total Number of available periods.
     */
    void totalPeriodsChanged(int total);
    /**
     * @brief Publish a changed 1D primary registration.
     * @param[in] layer Borrowed new primary, or null; does not describe fallback changes.
     */
    void primaryLayerChanged(SWMMResultsLayer *layer);
    /**
     * @brief Publish a changed look-back classification window.
     * @param[in] windowMs New nonnegative interval in milliseconds.
     */
    void windowChanged(qint64 windowMs);

private slots:
    /**
     * @brief Advance playback with loop/ping-pong/live-tail handling in the effective range.
     * @details Pauses on no driver/frames. At a nonlooping live tail leaves timer running to await new frames. Ping-pong reverses direction on range boundaries.
     */
    void onTimerTick();
    /**
     * @brief Dispatch a dynamic-layer tick and causally synchronize registered secondary outputs.
     * @param[in] step New zero-based primary frame.
     * @details Uses latest frames at or before primary time, updates any 2D fallback as-of time, then emits period and valid timestamp signals.
     */
    void onPrimaryPeriodChanged(int step);
    /**
     * @brief Forward the primary frame count.
     * @param[in] total New number of primary periods.
     */
    void onPrimaryTotalStepsChanged(int total);

    // 2D fallback re-emit slots (mirror the 1D ones).
    /**
     * @brief Dispatch a dynamic 2D tick and forward its frame index when no primary exists.
     * @param[in] step Zero-based fallback frame; ignored while 1D is primary.
     */
    void onFallback2DPeriodChanged(int step);
    /**
     * @brief Forward a fallback timestamp only while it drives playback.
     * @param[in] dt Timestamp received from the fallback.
     */
    void onFallback2DTimeChanged(const QDateTime &dt);
    /**
     * @brief Convert the fallback frame-index range to a total count when it drives.
     * @param[in] lo Expected zero lower index, ignored by this implementation.
     * @param[in] hi Inclusive highest frame index; publishes hi+1.
     */
    void onFallback2DRangeChanged(int lo, int hi);

private:
    /**
     * @brief Compute timer interval and frames-per-tick from speed and active temporal settings.
     * @details Floors interval at 50 ms and batches frame advances for shorter positive requested intervals; does not start a stopped timer.
     */
    void updateTimerInterval();
    /**
     * @brief Wire current primary period/count/spec changes and reset forward direction.
     * @details No-op without primary; caller disconnects old wiring before replacement.
     */
    void connectPrimary();
    /**
     * @brief Remove controller period/count/spec observers from the current primary.
     */
    void disconnectPrimary();
    /**
     * @brief Wire fallback period/time/range/spec changes when registered.
     */
    void connectFallback2D();
    /**
     * @brief Remove controller observers from the registered 2D fallback.
     */
    void disconnectFallback2D();

    // Returns true when either a 1D primary or a 2D fallback is set.
    /**
     * @brief Test for any live layer pointer eligible as a driver.
     * @return True for registered 1D primary or 2D fallback, even if it currently has no frames.
     */
    [[nodiscard]] bool hasDriver() const;
    // Total step count from whichever driver is active (1D primary preferred).
    /**
     * @brief Count available frames with 1D primary precedence.
     * @return Driver count, or zero for no driver/source.
     */
    [[nodiscard]] int driverTotalSteps() const;
    // Current step index from whichever driver is active.
    /**
     * @brief Read the current index with 1D primary precedence.
     * @return Zero-based driver index, or zero without a driver.
     */
    [[nodiscard]] int driverCurrentStep() const;
    // Advance whichever driver is active to \p step.
    /**
     * @brief Dispatch a frame request to the preferred driver.
     * @param[in] step Requested zero-based index, validated/clamped by the driver path.
     * @details 2D requests disable live following except when the requested index reaches the current newest frame. No-op without a driver.
     */
    void driverSetStep(int step);
    /*! \brief True while the active driver is still streaming frames from a
     *  running simulation (1D live tail, or a live 2D mesh source). Used by
     *  \ref onTimerTick to wait at the newest frame instead of pausing when
     *  playback catches up to the writer with Cycle off.
     * @return Whether the preferred driver reports an actively streaming source; false without one.
     */
    [[nodiscard]] bool driverIsLive() const;

    /*! Slice Z.13-controller-range — effective [min, max] step range
     *  considering the primary's TemporalSpec.startTime/endTime. Falls
     *  back to [0, totalSteps-1] when the spec is disabled or the start/
     *  end times aren't valid. For the 2D fallback driver, currently
     *  always returns the full range (datetime-keyed clamping there is
     *  the Z.13-controller-2d-range follow-up).
     * @param[out] outMin Inclusive zero-based lower bound; zero for no data.
     * @param[out] outMax Inclusive upper bound, zero for no data.
     * @details Enabled valid 1D temporal times are converted to nearest indices and clamped; 2D currently retains the full frame range.
     */
    void effectiveStepRange(int &outMin, int &outMax) const;

    QPointer<SWMMResultsLayer> m_primaryLayer;
    QList<QPointer<SWMMResultsLayer>> m_secondaryLayers;
    QPointer<SWMM2DResultsLayer> m_fallback2D;
    QTimer *m_timer = nullptr;
    bool m_playing = false;
    bool m_stopped = true;
    bool m_loop = true; ///< toolbar Cycle checkbox (default on)
    double m_speed = 1.0;
    qint64 m_windowMs = 0; ///< look-back sync window (0 = floor with no band)

    /*! Frames advanced per timer tick. The tick interval floors at
     *  kMinIntervalMs (~20 Hz), so high speed multipliers that ask for a
     *  shorter interval instead advance multiple frames per tick (skipping
     *  intermediates) — that's what makes 8× actually faster than 4×, and it
     *  also keeps the slide moving when per-frame render cost dominates. */
    int m_stepsPerTick = 1;

    /*! Slice Z.13-controller — playback direction (+1 forward, -1
     *  backward). Only flips to -1 when the primary's TemporalSpec has
     *  pingPong=true and the playback hits an end of the range.
     *  Forward-only otherwise so existing UI behaviour is preserved. */
    int m_direction = 1;

    static constexpr int kMinIntervalMs = 50;
    static constexpr int kDefaultStepMs = 200; // fallback when primary has no report step
};

#endif // ANIMATIONCONTROLLER_H
