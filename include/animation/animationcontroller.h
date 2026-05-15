/*!
 * \file   animationcontroller.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Slice BA — Animation Controller for time-stepped result playback.
 *
 * \details AnimationController drives playback of a SWMMResultsLayer, exposing
 *          play/pause/stop/step/seek controls and speed adjustment.  It owns a
 *          QTimer that advances the attached layer one period per tick; the tick
 *          interval equals (reportStepSec * 1000 / speed) but is clamped to a
 *          minimum of 50 ms so the UI stays responsive.
 *
 *          The controller is owned by SWMMVis and its layer pointer is updated
 *          whenever the active project changes or a new results layer is loaded.
 */

#ifndef ANIMATIONCONTROLLER_H
#define ANIMATIONCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QDateTime>

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

    // ----- Layer binding -------------------------------------------------

    /*!
     * \brief Attach the controller to a results layer.
     * \details Stops playback first if playing.  Passing nullptr detaches.
     */
    void setLayer(SWMMResultsLayer *layer);
    [[nodiscard]] SWMMResultsLayer *layer() const;

    // ----- State query ---------------------------------------------------

    [[nodiscard]] bool   isPlaying() const { return m_playing; }
    [[nodiscard]] double speed()     const { return m_speed;   }

    // ----- Speed ---------------------------------------------------------

    /*!
     * \brief Set playback speed multiplier (0.25 / 0.5 / 1 / 2 / 4 / 8).
     * \details Restarts the timer at the new interval if currently playing.
     */
    void setSpeed(double speed);

public slots:
    void play();
    void pause();
    void stop();
    void stepForward();
    void stepBackward();
    void seekToFirst();
    void seekToLast();
    void seekToPeriod(int period);

signals:
    void currentTimeChanged(const QDateTime &dt);
    void currentPeriodChanged(int period);
    void playStateChanged(bool playing);
    void totalPeriodsChanged(int total);
    void layerChanged(SWMMResultsLayer *layer);

private slots:
    void onTimerTick();
    void onLayerPeriodChanged(int step);
    void onLayerTotalStepsChanged(int total);
    void onLayerTimeChanged(const QDateTime &dt);

private:
    void updateTimerInterval();

    QPointer<SWMMResultsLayer> m_layer;
    QTimer *m_timer    = nullptr;
    bool    m_playing  = false;
    double  m_speed    = 1.0;

    static constexpr int kMinIntervalMs  = 50;
    static constexpr int kDefaultStepMs  = 200; // fallback when no report step
};

#endif // ANIMATIONCONTROLLER_H
