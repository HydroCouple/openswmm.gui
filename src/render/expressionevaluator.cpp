/**
 * @file      expressionevaluator.cpp
 * @brief     RuleBasedRenderer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/expressionevaluator.h"

#include <QChar>
#include <QRegularExpression>
#include <QVariant>

#include <optional>

namespace OpenSWMM::Render
{

namespace
{

// ---------- Tokeniser ------------------------------------------------------

enum class Tok {
    End,
    LParen,
    RParen,
    Number,
    String,
    Ident,
    Op_Eq,
    Op_Ne,
    Op_Lt,
    Op_Le,
    Op_Gt,
    Op_Ge,
    Kw_And,
    Kw_Or,
    Kw_Not,
    Kw_Is,
    Kw_Null,
    Kw_Like
};

struct Token {
    Tok kind = Tok::End;
    QString text;
    double num = 0.0;
};

class Tokeniser
{
public:
    /** @brief Copy input and start scanning at its first character.
     * @param s Expression text owned by this tokenizer after construction. */
    explicit Tokeniser(const QString &s) : m_s(s) {}

    /** @brief Consume whitespace and return the next token, advancing the cursor.
     * @return Keyword/operator, unescaped single-quoted string, decimal number or
     * identifier. Bare ! means NOT. End also represents unknown characters, an
     * unterminated string or a failed number conversion; no separate lexical error
     * is exposed. Keywords ignore case; exponent and leading-plus syntax are unsupported. */
    Token next()
    {
        skipWs();
        if (m_i >= m_s.size())
            return {Tok::End};

        const QChar c = m_s.at(m_i);

        if (c == QLatin1Char('(')) {
            ++m_i;
            return {Tok::LParen};
        }
        if (c == QLatin1Char(')')) {
            ++m_i;
            return {Tok::RParen};
        }

        if (c == QLatin1Char('=')) {
            ++m_i;
            return {Tok::Op_Eq};
        }
        if (c == QLatin1Char('!')) {
            if (m_i + 1 < m_s.size() && m_s.at(m_i + 1) == QLatin1Char('=')) {
                m_i += 2;
                return {Tok::Op_Ne};
            }
            // bare '!' — treat as NOT
            ++m_i;
            return {Tok::Kw_Not};
        }
        if (c == QLatin1Char('<')) {
            if (m_i + 1 < m_s.size()) {
                if (m_s.at(m_i + 1) == QLatin1Char('=')) {
                    m_i += 2;
                    return {Tok::Op_Le};
                }
                if (m_s.at(m_i + 1) == QLatin1Char('>')) {
                    m_i += 2;
                    return {Tok::Op_Ne};
                }
            }
            ++m_i;
            return {Tok::Op_Lt};
        }
        if (c == QLatin1Char('>')) {
            if (m_i + 1 < m_s.size() && m_s.at(m_i + 1) == QLatin1Char('=')) {
                m_i += 2;
                return {Tok::Op_Ge};
            }
            ++m_i;
            return {Tok::Op_Gt};
        }

        // String literal: single-quoted, '' escape.
        if (c == QLatin1Char('\'')) {
            ++m_i;
            QString out;
            while (m_i < m_s.size()) {
                const QChar k = m_s.at(m_i);
                if (k == QLatin1Char('\'')) {
                    if (m_i + 1 < m_s.size() && m_s.at(m_i + 1) == QLatin1Char('\'')) {
                        out += QLatin1Char('\'');
                        m_i += 2;
                        continue;
                    }
                    ++m_i;
                    return {Tok::String, out};
                }
                out += k;
                ++m_i;
            }
            return {Tok::End}; // unterminated — caller will fail nicely
        }

        // Number.
        if (c.isDigit() ||
            (c == QLatin1Char('-') && m_i + 1 < m_s.size() && m_s.at(m_i + 1).isDigit())) {
            const int start = m_i;
            if (c == QLatin1Char('-'))
                ++m_i;
            bool sawDot = false;
            while (m_i < m_s.size()) {
                const QChar k = m_s.at(m_i);
                if (k.isDigit()) {
                    ++m_i;
                    continue;
                }
                if (k == QLatin1Char('.') && !sawDot) {
                    sawDot = true;
                    ++m_i;
                    continue;
                }
                break;
            }
            const QString s = m_s.mid(start, m_i - start);
            bool ok = false;
            double v = s.toDouble(&ok);
            return ok ? Token{Tok::Number, s, v} : Token{Tok::End};
        }

        // Identifier / keyword.
        if (c.isLetter() || c == QLatin1Char('_')) {
            const int start = m_i;
            while (m_i < m_s.size()) {
                const QChar k = m_s.at(m_i);
                if (k.isLetterOrNumber() || k == QLatin1Char('_') ||
                    k == QLatin1Char(' ') /* tolerated mid-ident — rare */) {
                    if (k == QLatin1Char(' '))
                        break;
                    ++m_i;
                    continue;
                }
                break;
            }
            const QString w = m_s.mid(start, m_i - start);
            const QString u = w.toUpper();
            if (u == QLatin1String("AND"))
                return {Tok::Kw_And, w};
            if (u == QLatin1String("OR"))
                return {Tok::Kw_Or, w};
            if (u == QLatin1String("NOT"))
                return {Tok::Kw_Not, w};
            if (u == QLatin1String("IS"))
                return {Tok::Kw_Is, w};
            if (u == QLatin1String("NULL"))
                return {Tok::Kw_Null, w};
            if (u == QLatin1String("LIKE"))
                return {Tok::Kw_Like, w};
            return {Tok::Ident, w};
        }

        // Unknown char — emit End so parser errors cleanly.
        ++m_i;
        return {Tok::End};
    }

private:
    /** @brief Advance past consecutive QChar::isSpace() characters without producing tokens. */
    void skipWs()
    {
        while (m_i < m_s.size() && m_s.at(m_i).isSpace())
            ++m_i;
    }
    QString m_s;
    int m_i = 0;
};

// ---------- Value type used during evaluation ------------------------------

struct Value {
    enum Type { Null, Num, Str } type = Null;
    double num = 0.0;
    QString str;

    /** @brief Convert an attribute variant to the evaluator's three-value representation.
     * @param v Borrowed value; invalid/null becomes Null, successful toDouble()
     * becomes Num (including numeric strings), otherwise toString() becomes Str.
     * @return Independent value used by comparisons and truth conversion. */
    static Value fromVariant(const QVariant &v)
    {
        if (!v.isValid() || v.isNull())
            return {Null};
        bool ok = false;
        const double n = v.toDouble(&ok);
        if (ok)
            return {Num, n, {}};
        return {Str, 0.0, v.toString()};
    }

    /** @brief Inspect the explicit Null tag.
     * @return True only for Null; zero and an empty string are not Null. */
    bool isNull() const { return type == Null; }

    // Comparison helpers — coerce to numeric if both sides parse as
    // numbers, otherwise fall back to string compare.
    /** @brief Compare numbers numerically only when both operands have the Num tag.
     * @param a Left operand; callers handle Null before calling.
     * @param b Right operand; callers handle Null before calling.
     * @return Negative, zero or positive ordering. Mixed types compare case-sensitive
     * strings after QString::number() conversion of numbers. Numeric NaN compares
     * as equal here because neither less-than nor greater-than succeeds. */
    static int cmp(const Value &a, const Value &b)
    {
        if (a.type == Num && b.type == Num)
            return a.num < b.num ? -1 : (a.num > b.num ? 1 : 0);
        const QString sa = (a.type == Num) ? QString::number(a.num) : a.str;
        const QString sb = (b.type == Num) ? QString::number(b.num) : b.str;
        return QString::compare(sa, sb);
    }
};

// ---------- Recursive-descent parser/evaluator -----------------------------

class Eval
{
public:
    /** @brief Copy attributes, construct a tokenizer and prime the first token.
     * @param expr Expression copied into tokenizer storage.
     * @param attrs Attribute map copied into evaluator storage; original map is not modified. */
    Eval(const QString &expr, const QVariantMap &attrs) : m_tok(expr), m_attrs(attrs) { advance(); }

    /** @brief Parse the complete token stream and convert its value to truth.
     * @param[out] err Optional diagnostic, written only on a detected parse/trailing-token
     * error; successful parsing and false expressions leave prior contents unchanged.
     * @return Final truthiness, or false on a detected error. Tokenizer End can also
     * represent malformed text, so complete lexical validity is not established. */
    bool parse(QString *err)
    {
        const Value v = expr();
        if (!m_error.isEmpty()) {
            if (err)
                *err = m_error;
            return false;
        }
        if (m_cur.kind != Tok::End) {
            if (err)
                *err = QStringLiteral("trailing tokens after expression");
            return false;
        }
        // Truthiness: Null → false, Num → !=0, Str → non-empty.
        switch (v.type) {
        case Value::Null:
            return false;
        case Value::Num:
            return v.num != 0.0;
        case Value::Str:
            return !v.str.isEmpty();
        }
        return false;
    }

private:
    /** @brief Consume one token into the current lookahead slot. */
    void advance() { m_cur = m_tok.next(); }
    /** @brief Retain the first parser diagnostic without throwing.
     * @param msg Message copied only if no earlier error was recorded. */
    void fail(const QString &msg)
    {
        if (m_error.isEmpty())
            m_error = msg;
    }

    /** @brief Parse the expression production, starting at OR precedence.
     * @return Evaluated value; errors are retained separately in m_error. */
    Value expr() { return orExpr(); }

    /** @brief Parse AND expressions separated by OR, consuming every operand.
     * @return Numeric Boolean after an OR, otherwise the first operand value.
     * Evaluation does not short-circuit parsing of the right operand. */
    Value orExpr()
    {
        Value v = andExpr();
        while (m_cur.kind == Tok::Kw_Or) {
            advance();
            const Value rhs = andExpr();
            v = boolValue(truthy(v) || truthy(rhs));
        }
        return v;
    }

    /** @brief Parse NOT expressions separated by AND, consuming every operand.
     * @return Numeric Boolean after an AND, otherwise the first operand value.
     * Evaluation does not short-circuit parsing of the right operand. */
    Value andExpr()
    {
        Value v = notExpr();
        while (m_cur.kind == Tok::Kw_And) {
            advance();
            const Value rhs = notExpr();
            v = boolValue(truthy(v) && truthy(rhs));
        }
        return v;
    }

    /** @brief Parse recursive NOT/! or defer to comparison precedence.
     * @return Numeric inverted truth value for NOT, otherwise the comparison value. */
    Value notExpr()
    {
        if (m_cur.kind == Tok::Kw_Not) {
            advance();
            const Value v = notExpr();
            return boolValue(!truthy(v));
        }
        return compExpr();
    }

    /** @brief Parse one primary with an optional comparison, IS NULL or LIKE suffix.
     * @return Evaluated value. Null comparisons are true only for !=/<>; LIKE
     * converts numeric left values to text and Null to empty text, using anchored,
     * case-sensitive regex wildcards (% and _). Chained comparisons leave trailing tokens.
     * Malformed IS/LIKE suffixes record an error and return Null. */
    Value compExpr()
    {
        Value left = primary();

        // IS [NOT] NULL
        if (m_cur.kind == Tok::Kw_Is) {
            advance();
            bool negate = false;
            if (m_cur.kind == Tok::Kw_Not) {
                negate = true;
                advance();
            }
            if (m_cur.kind != Tok::Kw_Null) {
                fail(QStringLiteral("expected NULL"));
                return {Value::Null};
            }
            advance();
            const bool isNull = left.isNull();
            return boolValue(negate ? !isNull : isNull);
        }

        // LIKE 'pattern' — '%' matches any run, '_' matches one char.
        if (m_cur.kind == Tok::Kw_Like) {
            advance();
            if (m_cur.kind != Tok::String) {
                fail(QStringLiteral("expected string after LIKE"));
                return {Value::Null};
            }
            const QString pat = m_cur.text;
            advance();
            const QString s = (left.type == Value::Num) ? QString::number(left.num) : left.str;
            QString re;
            re.reserve(pat.size() * 2 + 4);
            re += QLatin1String("^");
            for (const QChar c : pat) {
                if (c == QLatin1Char('%'))
                    re += QStringLiteral(".*");
                else if (c == QLatin1Char('_'))
                    re += QChar('.');
                else if (QString(".\\+*?[]{}()^$|").contains(c)) {
                    re += QChar('\\');
                    re += c;
                } else
                    re += c;
            }
            re += QLatin1String("$");
            return boolValue(QRegularExpression(re).match(s).hasMatch());
        }

        // Binary comparison.
        Tok op = m_cur.kind;
        if (op == Tok::Op_Eq || op == Tok::Op_Ne || op == Tok::Op_Lt || op == Tok::Op_Le ||
            op == Tok::Op_Gt || op == Tok::Op_Ge) {
            advance();
            const Value rhs = primary();
            if (left.isNull() || rhs.isNull())
                return boolValue(op == Tok::Op_Ne); // NULL ≠ x is true; everything else false.
            const int c = Value::cmp(left, rhs);
            switch (op) {
            case Tok::Op_Eq:
                return boolValue(c == 0);
            case Tok::Op_Ne:
                return boolValue(c != 0);
            case Tok::Op_Lt:
                return boolValue(c < 0);
            case Tok::Op_Le:
                return boolValue(c <= 0);
            case Tok::Op_Gt:
                return boolValue(c > 0);
            case Tok::Op_Ge:
                return boolValue(c >= 0);
            default:
                break;
            }
        }

        return left;
    }

    /** @brief Consume a parenthesized expression, number, string or attribute name.
     * @return Parsed value, or Null while recording an unexpected-token/missing-parenthesis
     * error. Missing attributes become Null; the NULL keyword itself is not a primary. */
    Value primary()
    {
        if (m_cur.kind == Tok::LParen) {
            advance();
            const Value v = expr();
            if (m_cur.kind != Tok::RParen) {
                fail(QStringLiteral("missing ')'"));
                return {Value::Null};
            }
            advance();
            return v;
        }
        if (m_cur.kind == Tok::Number) {
            const double v = m_cur.num;
            advance();
            return {Value::Num, v, {}};
        }
        if (m_cur.kind == Tok::String) {
            const QString s = m_cur.text;
            advance();
            return {Value::Str, 0.0, s};
        }
        if (m_cur.kind == Tok::Ident) {
            const QString name = m_cur.text.trimmed();
            advance();
            return Value::fromVariant(m_attrs.value(name));
        }
        fail(QStringLiteral("unexpected token"));
        return {Value::Null};
    }

    /** @brief Represent a Boolean using the numeric Value tag.
     * @param b Boolean to encode.
     * @return Num containing exactly 1.0 or 0.0. */
    static Value boolValue(bool b)
    {
        Value v;
        v.type = Value::Num;
        v.num = b ? 1.0 : 0.0;
        return v;
    }
    /** @brief Apply this filter language's truth conversion.
     * @param v Value to inspect; Null is false, Num tests !=0, Str tests nonempty.
     * @return Converted Boolean; numeric NaN therefore tests true. */
    static bool truthy(const Value &v)
    {
        if (v.type == Value::Null)
            return false;
        if (v.type == Value::Num)
            return v.num != 0.0;
        return !v.str.isEmpty();
    }

    Tokeniser m_tok;
    QVariantMap m_attrs;
    Token m_cur;
    QString m_error;
};

} // namespace

bool ExpressionEvaluator::eval(const QString &expression, const QVariantMap &attrs,
                               QString *errorOut)
{
    const QString trimmed = expression.trimmed();
    if (trimmed.isEmpty()) {
        if (errorOut)
            errorOut->clear();
        return true; // empty == match always
    }
    Eval e(trimmed, attrs);
    return e.parse(errorOut);
}

} // namespace OpenSWMM::Render
