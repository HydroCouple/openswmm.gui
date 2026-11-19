/**
 * @file      temporalspec.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Typed config for the Layer Properties → Temporal tab (Slice Z.13).
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    Single = 0,     /*!< Single instant — paint the value at currentTime. */
    Range = 1,      /*!< Range — paint values in [currentTime - window, currentTime]. */
    Cumulative = 2, /*!< Cumulative — paint the integral from start up to currentTime. */
};

/** @brief Encode a temporal playback mode as a stable JSON token.
 * @param m Mode to encode; unrecognized enum values fall back to Single.
 * @return single, range, or cumulative. */
[[nodiscard]] QString temporalModeToString(TemporalMode m);
/** @brief Decode a temporal token by exact, case-sensitive comparison.
 * @param s Token without automatic trimming or case folding.
 * @return Recognized mode, or Single for any unknown/empty text. */
[[nodiscard]] TemporalMode temporalModeFromString(const QString &s);

/*!
 * \struct TemporalSpec
 * \brief Per-layer temporal-animation configuration.
 */
struct TemporalSpec {
    /*! \brief Whether temporal driving is enabled on this layer.
     *         When false, the layer paints at a static reference time
     *         (typically the start) and ignores animation ticks. */
    bool enabled = false;

    /*! \brief Datetime-typed attribute name for vector layers. For
     *         result layers the time axis is implicit (output timestep);
     *         this field is ignored. */
    QString timeField;

    /*! \brief Playback mode. */
    TemporalMode mode = TemporalMode::Single;

    /*! \brief Playback frame rate in frames per second. Clamped to
     *         [0.1, 60.0] at write time to prevent pathological values. */
    qreal frameRateFps = 12.0;

    /*! \brief Loop on reaching the end of the playback range. */
    bool loop = false;

    /*! \brief Ping-pong (reverse on reaching ends) instead of jumping
     *         back to start. Consumers may interpret this as looping;
     *         the value type and JSON loader do not force loop=true. */
    bool pingPong = false;

    /*! \brief Playback range — start and end times. Invalid (default-
     *         constructed) means "use the layer's full extent". */
    QDateTime startTime;
    QDateTime endTime;

    /*! \brief Range-mode window length in seconds (used only when
     *         mode == Range). Default 0 means a one-sample window. */
    qreal rangeWindowSec = 0.0;

    /** @brief Serialize nondefault playback configuration without changing this value.
     * @return Owned object; finite frame rates are clamped to [0.1,60] when
     * written, valid times use Qt ISODate text, and nonpositive windows are omitted.
     * No start/end ordering check or ping-pong/loop normalization occurs. */
    [[nodiscard]] QJsonObject toJson() const;
    /** @brief Load playback settings, using defaults for absent/mistyped fields.
     * @param j Temporal object; exact playback-mode tokens are decoded by temporalModeFromString().
     * @return Owned configuration with clamped frame rate, parsed ISODate times,
     * and otherwise unvalidated range/window values. Malformed date strings yield
     * invalid times, and pingPong does not force loop on. */
    static TemporalSpec fromJson(const QJsonObject &j);

    /** @brief Compare playback configuration for editor change detection.
     * @param other Value to inspect without modification.
     * @return True when ordinary fields match exactly and rate/window match via
     * qFuzzyCompare after adding one; invalid times compare with Qt semantics. */
    [[nodiscard]] bool operator==(const TemporalSpec &other) const;
    /** @brief Test whether playback configurations differ.
     * @param other Value to compare without modification.
     * @return Negation of operator==(), including its fuzzy numeric comparisons. */
    [[nodiscard]] bool operator!=(const TemporalSpec &other) const { return !(*this == other); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_TEMPORALSPEC_H
