/**
 * @file      expressionevaluator.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Minimal SQL-flavoured expression evaluator for the
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
 *         parser-detected errors return false and can be reported through
 *         the optional errorOut argument. Lexical truncation is not reliably
 *         diagnosed; callers must not use this evaluator as an input validator.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_EXPRESSIONEVALUATOR_H
#define OPENSWMM_RENDER_EXPRESSIONEVALUATOR_H

#include <QString>
#include <QVariantMap>

namespace OpenSWMM::Render
{

class ExpressionEvaluator
{
public:
    /** @brief Evaluate a SQL-like filter using a fresh local parser.
     * @param expression Filter text; trimmed empty input always matches. Keywords are
     * case-insensitive, attribute keys and string comparisons are case-sensitive.
     * @param attrs Attribute values copied into evaluation state. Missing/null variants
     * are Null; values convertible to double become numbers, others become strings.
     * @param[out] errorOut Optional diagnostic destination, set for parser errors and
     * cleared for empty input. Successful nonempty evaluation and ordinary nonmatches
     * leave its previous contents untouched; clear it before calling when needed.
     * @return Final truth value (Null false, numeric nonzero, string nonempty), or
     * false for parser-detected errors. AND/OR parse both operands. Unlike SQL, a
     * comparison involving Null is true only for !=/<> (even Null versus Null);
     * IS [NOT] NULL tests nullness. LIKE uses case-sensitive regex-backed %/_
     * wildcards. A bare NULL literal is not a supported primary expression.
     * @warning Unknown characters, unterminated strings and failed number tokens
     * can terminate tokenization as End; a valid prefix can therefore succeed
     * without a diagnostic. This is a style filter, not a syntax-validation API. */
    static bool eval(const QString &expression, const QVariantMap &attrs,
                     QString *errorOut = nullptr);
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_EXPRESSIONEVALUATOR_H
