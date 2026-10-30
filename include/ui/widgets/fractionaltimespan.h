// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Caleb Buahin
#pragma once

#include <QTimeEdit>
#include <qcustomeditors.h>
#include <cmath>
#include <limits>

namespace openswmmvis::ui
{
/** Read milliseconds as well as the base widget's whole seconds. */
inline double fractionalTimespanSeconds(const QCustomTimespanEdit *edit)
{
    const auto *clock = edit ? edit->findChild<QTimeEdit *>() : nullptr;
    return edit ? double(edit->totalSeconds()) + (clock ? clock->time().msec() / 1000.0 : 0) : 0;
}

/** Set a duration using the calendar editor's millisecond precision.
 * Unedited engine precision beyond milliseconds is retained by SimOptionsContext.
 */
inline void setFractionalTimespanSeconds(QCustomTimespanEdit *edit, double seconds)
{
    if (!edit || !std::isfinite(seconds) || seconds < 0 ||
        seconds / 86400 > std::numeric_limits<int>::max())
        return;
    const auto milliseconds = qRound64(seconds * 1000);
    edit->setTotalSeconds(milliseconds / 1000);
    if (auto *clock = edit->findChild<QTimeEdit *>()) {
        clock->setDisplayFormat(QStringLiteral("HH:mm:ss.zzz"));
        clock->setTime(clock->time().addMSecs(int(milliseconds % 1000)));
    }
}
}
