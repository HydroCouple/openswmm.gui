/*!
 * \file   expressionevaluator.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Minimal SQL-flavoured expression evaluator for the
 *         RuleBasedRenderer.  Slice X.25.
 *
 *         Goal: cover the QGIS / SQL-WHERE common cases so styles
 *         authored externally evaluate correctly, without rebuilding
 *         a full expression language.
 *
 *         Grammar (informal):
 *           expr     ::= or
 *           or       ::= and ( "OR" and )*
 *           and      ::= not ( "AND" not )*
 *           not      ::= "NOT" not | comparison
 *           comp     ::= primary [ ("=" | "!=" | "<>" | "<" | "<=" | ">" | ">=") primary
 *                                 | "IS" ["NOT"] "NULL"
 *                                 | "LIKE" string ]
 *           primary  ::= number | string | identifier | "(" expr ")"
 *
 *         Strings: single-quoted, with "''" as embedded quote.
 *         Numbers: integer or decimal, no exponent.
 *         Identifiers: bare attribute names — read out of the
 *                     QVariantMap by the same key.
 *
 *         The eval function returns true on match, false otherwise;
 *         malformed expressions return false and a non-empty error
 *         string in `lastError()`.
 */
#ifndef OPENSWMM_RENDER_EXPRESSIONEVALUATOR_H
#define OPENSWMM_RENDER_EXPRESSIONEVALUATOR_H

#include <QString>
#include <QVariantMap>

namespace OpenSWMM::Render {

class ExpressionEvaluator
{
public:
    /*! Evaluate \p expression against \p attrs.
     *  Empty / whitespace-only expression is treated as "match always". */
    static bool eval(const QString &expression,
                     const QVariantMap &attrs,
                     QString *errorOut = nullptr);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_EXPRESSIONEVALUATOR_H
