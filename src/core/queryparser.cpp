/*!
 * \file   queryparser.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/queryparser.h"

#include <QChar>
#include <QRegularExpression>

#include <climits>
#include <cmath>

namespace openswmmvis {

namespace {

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

enum class TokenKind {
    End, Invalid,
    Ident,
    Number,
    String,
    Op,         ///< < <= = != > >=
    LParen, RParen,
    Comma,
    KwAnd, KwOr, KwNot, KwLike, KwIn, KwBetween, KwIs, KwNull, KwTrue, KwFalse, KwWhere,
};

struct Token {
    TokenKind kind = TokenKind::End;
    QString   text;
    int       pos = 0;
};

class Tokenizer {
public:
    explicit Tokenizer(const QString &input) : m_in(input) {}

    Token next() {
        skipWs();
        if (m_pos >= m_in.size()) return {TokenKind::End, {}, m_pos};
        const int start = m_pos;
        const QChar c = m_in[m_pos];

        // SQL quoting: doubled delimiters escape a delimiter inside the token.
        if (c == '"' || c == '[' || c == '\'') {
            const QChar close = c == '[' ? QChar(']') : c;
            const auto kind = c == '\'' ? TokenKind::String : TokenKind::Ident;
            ++m_pos;
            QString value;
            while (m_pos < m_in.size()) {
                const QChar ch = m_in[m_pos++];
                if (ch != close) { value += ch; continue; }
                if (m_pos < m_in.size() && m_in[m_pos] == close) {
                    value += close;
                    ++m_pos;
                    continue;
                }
                return {kind, value, start};
            }
            return {TokenKind::Invalid, QStringLiteral("Unterminated quoted token"), start};
        }

        // Decimal and scientific notation, including .5 and signed values.
        if (c.isDigit() || c == '.' || c == '-' || c == '+') {
            static const QRegularExpression number(
                QStringLiteral(R"(^[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)"));
            const auto match = number.match(m_in.mid(m_pos));
            if (match.hasMatch()) {
                m_pos += match.capturedLength();
                return {TokenKind::Number, match.captured(), start};
            }
        }

        // Operators
        if (c == '<' || c == '>' || c == '=' || c == '!') {
            QString s; s += m_in[m_pos++];
            if (m_pos < m_in.size() && (m_in[m_pos] == '='
                || (c == '<' && m_in[m_pos] == '>')))
                s += m_in[m_pos++];
            if (s == "!" || s == "==")
                return {TokenKind::Invalid, QStringLiteral("Invalid comparison operator"), start};
            return {TokenKind::Op, s, start};
        }
        if (c == '(') { ++m_pos; return {TokenKind::LParen, "(", start}; }
        if (c == ')') { ++m_pos; return {TokenKind::RParen, ")", start}; }
        if (c == ',') { ++m_pos; return {TokenKind::Comma,  ",", start}; }

        // Bare identifier / keyword
        if (c.isLetter() || c == '_') {
            QString s;
            while (m_pos < m_in.size()
                   && (m_in[m_pos].isLetterOrNumber() || m_in[m_pos] == '_'))
                s += m_in[m_pos++];
            const QString up = s.toUpper();
            if (up == "AND")  return {TokenKind::KwAnd,  s, start};
            if (up == "OR")   return {TokenKind::KwOr,   s, start};
            if (up == "NOT")  return {TokenKind::KwNot,  s, start};
            if (up == "LIKE") return {TokenKind::KwLike, s, start};
            if (up == "IN")   return {TokenKind::KwIn,   s, start};
            if (up == "BETWEEN") return {TokenKind::KwBetween, s, start};
            if (up == "IS") return {TokenKind::KwIs, s, start};
            if (up == "NULL") return {TokenKind::KwNull, s, start};
            if (up == "TRUE") return {TokenKind::KwTrue, s, start};
            if (up == "FALSE") return {TokenKind::KwFalse, s, start};
            if (up == "WHERE") return {TokenKind::KwWhere, s, start};
            return {TokenKind::Ident, s, start};
        }

        // Unrecognised character — let parser surface it.
        ++m_pos;
        return {TokenKind::Invalid, QStringLiteral("Unexpected character: %1").arg(c), start};
    }

private:
    void skipWs() {
        while (m_pos < m_in.size() && m_in[m_pos].isSpace()) ++m_pos;
    }
    const QString &m_in;
    int            m_pos = 0;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

// Defined with the evaluator below; the parser compiles LIKE patterns up
// front so eval() doesn't rebuild the regex once per row.
QRegularExpression likeToRegex(const QString &pattern);

class Parser {
public:
    explicit Parser(const QString &input) : m_tok(input) { m_cur = m_tok.next(); }

    QueryPredicate parse() {
        QueryPredicate result;
        if (m_cur.kind == TokenKind::End) return result;  // empty == match-all
        try {
            checkToken();
            if (m_cur.kind == TokenKind::KwWhere) advance();
            result.root = parseOr();
            if (m_cur.kind != TokenKind::End)
                throwAt(QStringLiteral("Unexpected trailing token"));
        } catch (const ParseException &e) {
            result.root.reset();
            result.error = e.message;
            result.errorPos = e.position;
        }
        return result;
    }

private:
    struct ParseException {
        int     position;
        QString message;
    };

    void checkToken() {
        if (m_cur.kind == TokenKind::Invalid) throwAt(m_cur.text);
    }
    void advance() { m_cur = m_tok.next(); checkToken(); }

    QVariant parseValue() {
        QVariant value;
        if (m_cur.kind == TokenKind::Number) {
            bool ok = false;
            const double number = m_cur.text.toDouble(&ok);
            if (!ok || !std::isfinite(number)) throwAt(QStringLiteral("Invalid number"));
            value = number;
        } else if (m_cur.kind == TokenKind::String) value = m_cur.text;
        else if (m_cur.kind == TokenKind::KwTrue) value = true;
        else if (m_cur.kind == TokenKind::KwFalse) value = false;
        else throwAt(QStringLiteral("Expected number, string or boolean"));
        advance();
        return value;
    }

    std::shared_ptr<QueryNode> negate(std::shared_ptr<QueryNode> node, bool negative) {
        if (!negative) return node;
        auto outer = std::make_shared<QueryNode>();
        outer->kind = QueryNode::NotOp;
        outer->left = node;
        return outer;
    }

    [[noreturn]] void throwAt(const QString &msg) {
        throw ParseException{m_cur.pos + 1, msg};
    }

    std::shared_ptr<QueryNode> parseOr() {
        auto left = parseAnd();
        while (m_cur.kind == TokenKind::KwOr) {
            advance();
            auto right = parseAnd();
            auto n = std::make_shared<QueryNode>();
            n->kind = QueryNode::OrOp;
            n->left = left; n->right = right;
            left = n;
        }
        return left;
    }

    std::shared_ptr<QueryNode> parseAnd() {
        auto left = parseNot();
        while (m_cur.kind == TokenKind::KwAnd) {
            advance();
            auto right = parseNot();
            auto n = std::make_shared<QueryNode>();
            n->kind = QueryNode::AndOp;
            n->left = left; n->right = right;
            left = n;
        }
        return left;
    }

    std::shared_ptr<QueryNode> parseNot() {
        if (m_cur.kind == TokenKind::KwNot) {
            advance();
            auto inner = parseNot();
            auto n = std::make_shared<QueryNode>();
            n->kind = QueryNode::NotOp;
            n->left = inner;
            return n;
        }
        return parsePrimary();
    }

    std::shared_ptr<QueryNode> parsePrimary() {
        if (m_cur.kind == TokenKind::LParen) {
            advance();
            auto inner = parseOr();
            if (m_cur.kind != TokenKind::RParen)
                throwAt(QStringLiteral("Expected ')'"));
            advance();
            return inner;
        }
        return parseComparison();
    }

    std::shared_ptr<QueryNode> parseComparison() {
        if (m_cur.kind != TokenKind::Ident)
            throwAt(QStringLiteral("Expected column name"));
        const QString field = m_cur.text;
        advance();

        if (m_cur.kind == TokenKind::KwIs) {
            advance();
            const bool negative = m_cur.kind == TokenKind::KwNot;
            if (negative) advance();
            if (m_cur.kind != TokenKind::KwNull) throwAt(QStringLiteral("Expected NULL after IS [NOT]"));
            advance();
            auto node = std::make_shared<QueryNode>();
            node->kind = QueryNode::IsNull;
            node->fieldName = field;
            return negate(node, negative);
        }
        const bool negative = m_cur.kind == TokenKind::KwNot;
        if (negative) {
            advance();
            if (m_cur.kind != TokenKind::KwLike && m_cur.kind != TokenKind::KwIn
                && m_cur.kind != TokenKind::KwBetween)
                throwAt(QStringLiteral("Expected LIKE, IN or BETWEEN after NOT"));
        }
        if (m_cur.kind == TokenKind::KwBetween) {
            advance();
            auto node = std::make_shared<QueryNode>();
            node->kind = QueryNode::Between;
            node->fieldName = field;
            node->inList.append(parseValue());
            if (m_cur.kind != TokenKind::KwAnd) throwAt(QStringLiteral("Expected AND in BETWEEN"));
            advance();
            node->inList.append(parseValue());
            return negate(node, negative);
        }

        // LIKE
        if (m_cur.kind == TokenKind::KwLike) {
            advance();
            if (m_cur.kind != TokenKind::String)
                throwAt(QStringLiteral("Expected string after LIKE"));
            auto n = std::make_shared<QueryNode>();
            n->kind = QueryNode::Like;
            n->fieldName = field;
            n->literal = QVariant(m_cur.text);
            n->likeRegex = likeToRegex(m_cur.text);
            advance();
            return negate(n, negative);
        }

        // IN ( v, v, ... )
        if (m_cur.kind == TokenKind::KwIn) {
            advance();
            if (m_cur.kind != TokenKind::LParen)
                throwAt(QStringLiteral("Expected '(' after IN"));
            advance();
            auto n = std::make_shared<QueryNode>();
            n->kind = QueryNode::In;
            n->fieldName = field;
            n->inList.append(parseValue());
            while (m_cur.kind == TokenKind::Comma) {
                advance();
                n->inList.append(parseValue());
            }
            if (m_cur.kind != TokenKind::RParen)
                throwAt(QStringLiteral("Expected ',' or ')' in IN list"));
            advance();
            return negate(n, negative);
        }

        // Comparison: op value
        if (m_cur.kind != TokenKind::Op)
            throwAt(QStringLiteral("Expected comparison operator"));
        const QString op = m_cur.text;
        advance();

        auto n = std::make_shared<QueryNode>();
        n->kind = QueryNode::Compare;
        n->fieldName = field;
        n->op = op;
        n->literal = parseValue();
        return n;
    }

    Tokenizer m_tok;
    Token     m_cur;
};

// ---------------------------------------------------------------------------
// Evaluator
// ---------------------------------------------------------------------------

// Compare two QVariants — try numeric first, fall back to string.
// Returns one of {-1, 0, 1, INT_MIN} where INT_MIN means
// "incomparable" (e.g. row missing the field).
int cmpVariants(const QVariant &a, const QVariant &b) {
    if (!a.isValid() || a.isNull() || !b.isValid() || b.isNull()) return INT_MIN;
    bool okA = false, okB = false;
    const double da = a.toDouble(&okA);
    const double db = b.toDouble(&okB);
    if (okA && okB) {
        if (da < db) return -1;
        if (da > db) return  1;
        return 0;
    }
    const QString sa = a.toString();
    const QString sb = b.toString();
    return QString::compare(sa, sb);
}

// SQL LIKE → regex.  `%` ⇒ `.*`, `_` ⇒ `.`.  Anchored.
QRegularExpression likeToRegex(const QString &pattern) {
    QString re = QStringLiteral("^");
    for (QChar c : pattern) {
        if (c == '%')      re += QStringLiteral(".*");
        else if (c == '_') re += QStringLiteral(".");
        else               re += QRegularExpression::escape(QString(c));
    }
    re += QStringLiteral("$");
    return QRegularExpression(re, QRegularExpression::CaseInsensitiveOption);
}

// Case-insensitive field lookup.  Users type column names with
// inconsistent casing ("max depth", "Max Depth", "MAX DEPTH"); SWMM
// column labels themselves use mixed case ("Invert elev").  Fall back
// to a linear case-insensitive scan if the exact key isn't present.
QVariant lookupField(const QVariantMap &row, const QString &name) {
    auto it = row.constFind(name);
    if (it != row.constEnd()) return it.value();
    for (auto cit = row.constBegin(); cit != row.constEnd(); ++cit)
        if (QString::compare(cit.key(), name, Qt::CaseInsensitive) == 0)
            return cit.value();
    return {};
}

// Walk the AST appending every referenced field name, first-seen order,
// no duplicates.  A predicate names one or two columns; the list stays
// short enough that the linear `contains` is cheaper than a QSet.
void collectFields(const QueryNode &n, QStringList &out) {
    switch (n.kind) {
    case QueryNode::OrOp:
    case QueryNode::AndOp:
        if (n.left)  collectFields(*n.left,  out);
        if (n.right) collectFields(*n.right, out);
        return;
    case QueryNode::NotOp:
        if (n.left) collectFields(*n.left, out);
        return;
    case QueryNode::Compare:
    case QueryNode::Like:
    case QueryNode::In:
    case QueryNode::Between:
    case QueryNode::IsNull:
        if (!out.contains(n.fieldName)) out.append(n.fieldName);
        return;
    }
}

enum class Truth { False, True, Unknown };
Truth truth(bool value) { return value ? Truth::True : Truth::False; }

Truth eval(const QueryNode &n, const QVariantMap &row) {
    switch (n.kind) {
    case QueryNode::OrOp:
    case QueryNode::AndOp: {
        const auto a = eval(*n.left, row);
        if (n.kind == QueryNode::OrOp && a == Truth::True) return a;
        if (n.kind == QueryNode::AndOp && a == Truth::False) return a;
        const auto b = eval(*n.right, row);
        if (n.kind == QueryNode::OrOp) {
            if (a == Truth::True || b == Truth::True) return Truth::True;
            if (a == Truth::Unknown || b == Truth::Unknown) return Truth::Unknown;
            return Truth::False;
        }
        if (a == Truth::False || b == Truth::False) return Truth::False;
        if (a == Truth::Unknown || b == Truth::Unknown) return Truth::Unknown;
        return Truth::True;
    }
    case QueryNode::NotOp: {
        const auto inner = eval(*n.left, row);
        return inner == Truth::Unknown ? inner : truth(inner == Truth::False);
    }
    case QueryNode::IsNull: {
        const auto value = lookupField(row, n.fieldName);
        return truth(!value.isValid() || value.isNull());
    }
    case QueryNode::Compare: {
        const int c = cmpVariants(lookupField(row, n.fieldName), n.literal);
        if (c == INT_MIN) return Truth::Unknown;
        if (n.op == "<")  return truth(c <  0);
        if (n.op == "<=") return truth(c <= 0);
        if (n.op == "=")  return truth(c == 0);
        if (n.op == "!=" || n.op == "<>") return truth(c != 0);
        if (n.op == ">")  return truth(c >  0);
        if (n.op == ">=") return truth(c >= 0);
        return Truth::False;
    }
    case QueryNode::Like: {
        const auto value = lookupField(row, n.fieldName);
        if (!value.isValid() || value.isNull()) return Truth::Unknown;
        return truth(n.likeRegex.match(value.toString()).hasMatch());
    }
    case QueryNode::In: {
        const auto value = lookupField(row, n.fieldName);
        if (!value.isValid() || value.isNull()) return Truth::Unknown;
        for (const auto &literal : n.inList)
            if (cmpVariants(value, literal) == 0) return Truth::True;
        return Truth::False;
    }
    case QueryNode::Between: {
        const auto value = lookupField(row, n.fieldName);
        const int lower = cmpVariants(value, n.inList[0]);
        const int upper = cmpVariants(value, n.inList[1]);
        if (lower == INT_MIN || upper == INT_MIN) return Truth::Unknown;
        return truth(lower >= 0 && upper <= 0);
    }
    }
    return Truth::False;
}

} // anonymous

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QueryPredicate parseQuery(const QString &whereClause) {
    Parser p(whereClause);
    return p.parse();
}

QuerySuggestions suggestQuery(const QString &text, int cursor, const QStringList &fields) {
    cursor = qBound(0, cursor, int(text.size()));
    const QString prefix = text.left(cursor);
    Tokenizer tokenizer(prefix);
    QList<Token> tokens;
    for (Token token = tokenizer.next(); token.kind != TokenKind::End;
         token = tokenizer.next()) {
        tokens.append(token);
        if (token.kind == TokenKind::Invalid) break;
    }
    QuerySuggestions result;
    result.start = cursor;
    QString partial;
    const bool unfinishedQuote = !tokens.isEmpty() && tokens.back().kind == TokenKind::Invalid
        && tokens.back().text == QStringLiteral("Unterminated quoted token");
    if (!tokens.isEmpty() && !prefix.isEmpty() && (!prefix.back().isSpace() || unfinishedQuote)) {
        const auto last = tokens.back();
        if (last.kind == TokenKind::Ident || last.kind == TokenKind::Invalid
            || (last.kind >= TokenKind::KwAnd && last.kind <= TokenKind::KwWhere)) {
            result.start = last.pos;
            partial = prefix.mid(last.pos);
            tokens.removeLast();
        }
    }
    const auto last = tokens.isEmpty() ? TokenKind::End : tokens.back().kind;
    const auto previous = tokens.size() < 2 ? TokenKind::End : tokens[tokens.size()-2].kind;
    QStringList candidates;
    const bool postfixNot = last == TokenKind::KwNot && previous == TokenKind::Ident;
    if (postfixNot) candidates = {QStringLiteral("IN"), QStringLiteral("LIKE"), QStringLiteral("BETWEEN")};
    else if (last == TokenKind::KwIs)
        candidates = {QStringLiteral("NULL"), QStringLiteral("NOT NULL")};
    else if (last == TokenKind::KwNot && previous == TokenKind::KwIs)
        candidates = {QStringLiteral("NULL")};
    else if (last == TokenKind::Ident)
        candidates = {QStringLiteral("="), QStringLiteral("!="), QStringLiteral("<>"),
                      QStringLiteral("<"), QStringLiteral("<="), QStringLiteral(">"), QStringLiteral(">="),
                      QStringLiteral("LIKE"), QStringLiteral("NOT LIKE"), QStringLiteral("IN"),
                      QStringLiteral("NOT IN"), QStringLiteral("BETWEEN"), QStringLiteral("NOT BETWEEN"),
                      QStringLiteral("IS NULL"), QStringLiteral("IS NOT NULL")};
    else if ((last == TokenKind::LParen && previous == TokenKind::KwIn) || last == TokenKind::Comma)
        candidates = {QStringLiteral("TRUE"), QStringLiteral("FALSE")};
    else if ((last == TokenKind::Number || last == TokenKind::String || last == TokenKind::KwTrue
              || last == TokenKind::KwFalse) && previous == TokenKind::KwBetween)
        candidates = {QStringLiteral("AND")};
    else if (last == TokenKind::End || last == TokenKind::KwWhere || last == TokenKind::KwAnd
             || last == TokenKind::KwOr || last == TokenKind::KwNot || last == TokenKind::LParen) {
        for (const QString &field : fields) {
            QString escaped = field;
            if (partial.startsWith('[')) {
                escaped.replace("]", "]]");
                candidates.append("[" + escaped + "]");
            } else {
                escaped.replace("\"", "\"\"");
                candidates.append("\"" + escaped + "\"");
                // Unquoted input can still discover fields containing spaces.
                if (!partial.startsWith('"') && field.startsWith(partial, Qt::CaseInsensitive))
                    result.candidates.append("\"" + escaped + "\"");
            }
        }
        candidates.append(QStringLiteral("NOT"));
    } else if (last == TokenKind::Number || last == TokenKind::String || last == TokenKind::RParen
               || last == TokenKind::KwTrue || last == TokenKind::KwFalse || last == TokenKind::KwNull)
        candidates = {QStringLiteral("AND"), QStringLiteral("OR")};
    else if (last == TokenKind::Op || last == TokenKind::KwBetween || last == TokenKind::Comma)
        candidates = {QStringLiteral("TRUE"), QStringLiteral("FALSE")};
    for (const QString &candidate : candidates)
        if (candidate.startsWith(partial, Qt::CaseInsensitive)) result.candidates.append(candidate);
    result.candidates.removeDuplicates();
    return result;
}

bool evaluateQuery(const QueryPredicate &pred, const QVariantMap &row) {
    if (!pred.error.isEmpty()) return false;
    if (!pred.root) return true;
    return eval(*pred.root, row) == Truth::True;
}

QStringList queryFieldNames(const QueryPredicate &pred) {
    QStringList out;
    if (pred.root) collectFields(*pred.root, out);
    return out;
}

} // namespace openswmmvis
