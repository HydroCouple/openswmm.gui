/*!
 * \file   swmmdatetimeformat.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The one date-time format string the GUI displays and edits SWMM
 *         times in.
 *
 * Split out of `swmmdatetime.h` on purpose: that header includes the engine's
 * `openswmm_datetime.h` for its conversion helpers, and several unit targets
 * (e.g. `test_timeseries_table_model`) compile view/model code with no engine
 * include path at all. A format string needs none, so it lives here and stays
 * reachable from those targets.
 */
#ifndef OPENSWMMVIS_CORE_SWMMDATETIMEFORMAT_H
#define OPENSWMMVIS_CORE_SWMMDATETIMEFORMAT_H

#include <QString>

namespace openswmmvis::core {

/*! \brief Canonical date-time format for display and editing: `MM/dd/yyyy HH:mm`.
 *
 *  The same MM/DD/YYYY HH:MM a SWMM `.inp` writes, so what a user reads in the
 *  GUI matches what they read in the file. Suitable for `QDateTime::toString()`
 *  and `QDateTimeEdit::setDisplayFormat()`.
 *
 *  Minute resolution — SWMM's own storage is whole seconds and the engine's
 *  `[TIMESERIES]` writer emits `HH:MM:SS`, so a *displayed* value can hide a
 *  non-zero seconds field. Editors built on this must therefore preserve the
 *  seconds they were handed (a QDateTimeEdit does: the sections omitted from
 *  the display format keep their value), or they silently truncate — the
 *  defect class GH #1 was about. */
inline QString swmmDateTimeDisplayFormat()
{
    return QStringLiteral("MM/dd/yyyy HH:mm");
}

} // namespace openswmmvis::core

#endif // OPENSWMMVIS_CORE_SWMMDATETIMEFORMAT_H
