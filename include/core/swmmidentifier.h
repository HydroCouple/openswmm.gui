/**
 * @file      swmmidentifier.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     The one rule every SWMM object name has to satisfy.
 *
 *         The `.inp` format is whitespace-delimited, `;` opens a comment
 *         and `[`…`]` opens a section header, so a name carrying any of
 *         those survives in memory but not a save → reopen round trip:
 *         "Total Nitrogen" came back as "Total", the GUI registry still
 *         held the long name, and every lookup by name between the two
 *         returned -1 (feedback C10). Editors and the network rename path
 *         refuse such names up front and tell the user which character
 *         is the problem.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CORE_SWMMIDENTIFIER_H
#define OPENSWMMVIS_CORE_SWMMIDENTIFIER_H

#include <QString>

namespace openswmmvis
{

/**
 * @brief Explain the first lexical restriction violated by a SWMM object name.
 * @param[in] name Proposed identifier, examined exactly as supplied without trimming.
 * @return Empty text when accepted; otherwise a translated, user-facing explanation
 *         of an empty name, Unicode whitespace, a semicolon, or a square bracket.
 * @note This checks safe whitespace-delimited INP spelling only. It does not check
 *       uniqueness, maximum engine length, encoding, or existence of an object.
 */
[[nodiscard]] QString swmmIdentifierProblem(const QString &name);

/**
 * @brief Check whether a proposed name passes the shared lexical identifier rules.
 * @param[in] name Candidate object identifier; whitespace is neither removed nor replaced.
 * @return True exactly when swmmIdentifierProblem(name) returns an empty string.
 * @see swmmIdentifierProblem() for validation limits and a diagnostic suitable for users.
 */
[[nodiscard]] inline bool isValidSwmmIdentifier(const QString &name)
{
    return swmmIdentifierProblem(name).isEmpty();
}

} // namespace openswmmvis

#endif // OPENSWMMVIS_CORE_SWMMIDENTIFIER_H
