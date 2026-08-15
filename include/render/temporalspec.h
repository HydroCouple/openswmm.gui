/*!
 * \file   temporalspec.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Typed config for the Layer Properties → Temporal tab (Slice Z.13).
 *
 *         Today the animation toolbar drives every result layer through
 *         a single hardcoded controller. RENDERING_RULE_MODEL_PLAN.md
 *         §11.2 calls for an explicit per-layer Temporal tab where the
 *         user can pick:
 *           - the time field (for vector layers — datetime attribute)
 *           - a playback mode (Single instant / Range / Cumulative)
 *           - frame rate (fps)
 *           - loop / ping-pong on reaching the end
 *           - playback range start / end
 *
 *         The status-bar animation widget reads this spec instead of
 *         the hardcoded controller config. Result layers default to
 *         Single mode + the file's full time range; vector layers
 *         default to disabled (no temporal column).
 *
 *         Slice Z.13-data ships the value type + JSON round-trip. The
 *         tab UI widget + LayerStyleDialog integration is Z.13-ui (a
 *         separate slice). Storing TemporalSpec on each OpenSWMMVisLayer
 *         is Z.13-attach.
 */

#ifndef OPENSWMM_RENDER_TEMPORALSPEC_H
#define OPENSWMM_RENDER_TEMPORALSPEC_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \enum TemporalMode
 * \brief How the time axis is consumed at paint time.
 */
enum class TemporalMode : int {
    Single      = 0,   /*!< Single instant — paint the value at currentTime. */
    Range       = 1,   /*!< Range — paint values in [currentTime - window, currentTime]. */
    Cumulative  = 2,   /*!< Cumulative — paint the integral from start up to currentTime. */
};

[[nodiscard]] QString temporalModeToString(TemporalMode m);
[[nodiscard]] TemporalMode temporalModeFromString(const QString &s);

/*!
 * \struct TemporalSpec
 * \brief Per-layer temporal-animation configuration.
 */
struct TemporalSpec
{
    /*! \brief Whether temporal driving is enabled on this layer.
     *         When false, the layer paints at a static reference time
     *         (typically the start) and ignores animation ticks. */
    bool          enabled = false;

    /*! \brief Datetime-typed attribute name for vector layers. For
     *         result layers the time axis is implicit (output timestep);
     *         this field is ignored. */
    QString       timeField;

    /*! \brief Playback mode. */
    TemporalMode  mode = TemporalMode::Single;

    /*! \brief Playback frame rate in frames per second. Clamped to
     *         [0.1, 60.0] at write time to prevent pathological values. */
    qreal         frameRateFps = 12.0;

    /*! \brief Loop on reaching the end of the playback range. */
    bool          loop = false;

    /*! \brief Ping-pong (reverse on reaching ends) instead of jumping
     *         back to start. Implies loop=true when set. */
    bool          pingPong = false;

    /*! \brief Playback range — start and end times. Invalid (default-
     *         constructed) means "use the layer's full extent". */
    QDateTime     startTime;
    QDateTime     endTime;

    /*! \brief Range-mode window length in seconds (used only when
     *         mode == Range). Default 0 means a one-sample window. */
    qreal         rangeWindowSec = 0.0;

    /*! \brief JSON round-trip. Empty fields are omitted to keep
     *         persistence diffs minimal. */
    [[nodiscard]] QJsonObject toJson() const;
    static TemporalSpec       fromJson(const QJsonObject &j);

    /*! \brief Equality — for change detection in the tab UI. */
    [[nodiscard]] bool operator==(const TemporalSpec &other) const;
    [[nodiscard]] bool operator!=(const TemporalSpec &other) const
    { return !(*this == other); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_TEMPORALSPEC_H
