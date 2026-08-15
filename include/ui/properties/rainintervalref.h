/*!
 * \file   rainintervalref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * DA.2 parity follow-up — value type + shared helpers for the rain gage
 * "Recording Interval" Property Browser row. Legacy SWMM edits this field as
 * an editable combo (esComboEdit) showing the interval in clock form
 * (H:MM, e.g. "0:15"); see Epaswmm5 Uedit.pas EditRaingage.
 *
 * Same registration dance as `CulvertCodeRef`: declared as a Qt metatype so
 * QPropertyModel stores it in a QVariant, a QString converter renders the
 * H:MM label when the row isn't in edit mode, and a custom editor creator
 * (registered in propertiespanel.cpp) hands out a `RainIntervalComboBox`.
 *
 * The engine stores the interval as whole seconds (GageData.interval_sec) and
 * the C API exposes it as a double via swmm_gage_get/set_rain_interval. The
 * inline helpers here convert between that second count and the legacy clock
 * string, matching the engine's own parser (parse_time_seconds: H:MM[:SS]).
 */

#ifndef RAININTERVALREF_H
#define RAININTERVALREF_H

#include <QMetaType>
#include <QString>
#include <QStringList>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

class SWMMModelLayer;

/*! Identifies the recording-interval attribute on a single rain gage. */
struct RainIntervalRef
{
    SWMM_Engine     engine   = nullptr;  ///< Engine handle (borrow, not owned)
    QString         gageName;            ///< Owning gage id
    int             seconds  = 0;        ///< Recording interval in seconds
    /*! Owning model layer (borrow) — writes route through the layer when
     *  present so other views refresh; falls back to the bare engine
     *  setter (e.g. in tests). */
    SWMMModelLayer *layer    = nullptr;

    bool operator==(const RainIntervalRef &other) const noexcept
    {
        return engine == other.engine && gageName == other.gageName
               && seconds == other.seconds && layer == other.layer;
    }
};

Q_DECLARE_METATYPE(RainIntervalRef)

/*! Install the `RainIntervalRef → QString` converter (renders the H:MM
 *  clock label). Idempotent. */
void registerRainIntervalRefConverter();

// ---------------------------------------------------------------------------
// Shared H:MM helpers (used by the converter, the combo editor, and the
// attribute-table interval delegate). Header-inline so all three agree.
// ---------------------------------------------------------------------------

namespace rain_interval {

/*! The legacy [RAINGAGES] interval presets (Epaswmm5 Uedit.pas), in clock
 *  H:MM form. The combo is editable, so custom values are still accepted. */
inline QStringList presetsHMM()
{
    return {QStringLiteral("0:01"), QStringLiteral("0:05"),
            QStringLiteral("0:10"), QStringLiteral("0:15"),
            QStringLiteral("0:20"), QStringLiteral("0:30"),
            QStringLiteral("1:00"), QStringLiteral("6:00"),
            QStringLiteral("12:00"), QStringLiteral("24:00")};
}

/*! Format a second count as the legacy clock string: "H:MM" when on a whole
 *  minute, "H:MM:SS" otherwise. */
inline QString secondsToHMM(int secs)
{
    if (secs < 0) secs = 0;
    const int h = secs / 3600;
    const int m = (secs % 3600) / 60;
    const int s = secs % 60;
    if (s == 0)
        return QStringLiteral("%1:%2").arg(h).arg(m, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2:%3")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

/*! Parse a clock string into seconds, matching the engine's parser
 *  (parse_time_seconds): colon form is H:MM[:SS] (first token = hours); a
 *  bare number is decimal hours (engine [RAINGAGES] grammar). Returns -1 on
 *  malformed input so callers can reject the edit. */
inline int hmmToSeconds(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) return -1;

    if (!t.contains(QLatin1Char(':'))) {
        bool ok = false;
        const double hours = t.toDouble(&ok);
        if (!ok || hours < 0.0) return -1;
        return static_cast<int>(hours * 3600.0 + 0.5);
    }

    const QStringList parts = t.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3) return -1;
    int field[3] = {0, 0, 0};
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        const int v = parts[i].toInt(&ok);
        if (!ok || v < 0) return -1;
        field[i] = v;
    }
    return field[0] * 3600 + field[1] * 60 + field[2];
}

} // namespace rain_interval

#endif // RAININTERVALREF_H
