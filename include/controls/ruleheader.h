/**
 * @file      ruleheader.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     The rule name a control-rule body declares in its `RULE <name>`
 *         header, read the way the engine and the legacy [CONTROLS] parser
 *         read it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CONTROLS_RULEHEADER_H
#define OPENSWMMVIS_CONTROLS_RULEHEADER_H

#include <QString>

namespace openswmmvis::controls
{

/*! \brief The name the engine derives from \p body: the text after the
 *  `RULE` keyword on the first non-blank line, with any `;` comment removed.
 *
 *  Rule identity in the engine is that header, and the .inp format is
 *  whitespace-delimited, so a header such as `RULE my rule` would be saved
 *  as `my` and could never be found again under the name the user typed.
 *  Such headers, and bodies that do not start with `RULE <name>`, are
 *  therefore rejected: the function returns an empty string and, when
 *  \p problem is given, a user-facing explanation. \p problem is cleared on
 *  success.
 * @param[in] body Full rule text; skips blank/comment-only lines and strips semicolon comments before inspecting the first content line.
 * @param[out] problem Optional diagnostic cleared on entry, assigned for missing header or invalid SWMM identifier.
 * @return Valid declared name, or empty on failure; keyword matching is case-insensitive. Performs no full rule validation.
 */
[[nodiscard]] QString ruleHeaderName(const QString &body, QString *problem = nullptr);

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_RULEHEADER_H
