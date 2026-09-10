/*!
 * \file   simoptionspage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/simoptionspage.h"

namespace openswmmvis::ui
{

SimOptionsPage::SimOptionsPage(SimOptionsContext &ctx, QWidget *parent)
    : QWidget(parent), ctx_(ctx)
{
}

void SimOptionsPage::tagOption(QWidget *w, const char *key)
{
    // Null-tolerant: 2D editors do not exist when OPENSWMM_HAS_2D is off, and
    // a page that failed to build should not take the dialog down with it.
    if (!w) return;
    // A few editors own more than one key — a QDateTimeEdit writes both the
    // DATE and the TIME half — so the property is a comma-separated list and
    // repeated tags accumulate rather than overwrite.
    const QString add = QString::fromLatin1(key);
    const QString cur = w->property("optionKey").toString();
    if (cur.isEmpty()) {
        w->setProperty("optionKey", add);
    } else if (!cur.split(QLatin1Char(',')).contains(add)) {
        w->setProperty("optionKey", cur + QLatin1Char(',') + add);
    }
}

const char *transport2DKey(int speciesClass)
{
    switch (speciesClass) {
    case SWMM_TRANSPORT_CLASS_POLLUTANTS:  return "TRANSPORT_POLLUTANTS";
    case SWMM_TRANSPORT_CLASS_MSX:         return "TRANSPORT_MSX";
    case SWMM_TRANSPORT_CLASS_AGE:         return "TRANSPORT_AGE";
    case SWMM_TRANSPORT_CLASS_TEMPERATURE: return "TRANSPORT_TEMPERATURE";
    default:                               return "";
    }
}

} // namespace openswmmvis::ui
