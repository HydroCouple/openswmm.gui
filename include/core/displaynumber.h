/*!
 * \file   displaynumber.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One formatting rule for numbers shown to people.
 *
 * 17 significant digits is round-trip precision for SERIALIZATION; on screen
 * it exposes binary representation noise (3.25 stored as 3.2499999999999996).
 * 15 digits reproduces every decimal a user can type with up to 15
 * significant digits, so authored values display exactly as entered.
 * Keep 'g', 17 for anything written to a file or hashed.
 */
#ifndef OPENSWMMVIS_CORE_DISPLAYNUMBER_H
#define OPENSWMMVIS_CORE_DISPLAYNUMBER_H

#include <QString>

namespace openswmmvis {

[[nodiscard]] inline QString displayNumber(double value)
{
    return QString::number(value, 'g', 15);
}

} // namespace openswmmvis

#endif // OPENSWMMVIS_CORE_DISPLAYNUMBER_H
