/*!
 * \file   swmmidentifier.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The one rule every SWMM object name has to satisfy.
 *
 *         The `.inp` format is whitespace-delimited, `;` opens a comment
 *         and `[`…`]` opens a section header, so a name carrying any of
 *         those survives in memory but not a save → reopen round trip:
 *         "Total Nitrogen" came back as "Total", the GUI registry still
 *         held the long name, and every lookup by name between the two
 *         returned -1 (feedback C10). Editors and the network rename path
 *         refuse such names up front and tell the user which character
 *         is the problem.
 */
#ifndef OPENSWMMVIS_CORE_SWMMIDENTIFIER_H
#define OPENSWMMVIS_CORE_SWMMIDENTIFIER_H

#include <QString>

namespace openswmmvis {

/*! Why \p name cannot be a SWMM identifier, as a sentence for the user,
 *  or an empty string when it can. */
[[nodiscard]] QString swmmIdentifierProblem(const QString &name);

[[nodiscard]] inline bool isValidSwmmIdentifier(const QString &name)
{
    return swmmIdentifierProblem(name).isEmpty();
}

} // namespace openswmmvis

#endif // OPENSWMMVIS_CORE_SWMMIDENTIFIER_H
