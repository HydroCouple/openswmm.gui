/*!
 * \file   queryparser.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/queryparser.h"

#include <QChar>
#include <QRegularExpression>

#include <climits>

namespace openswmmvis {

namespace {

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

enum class TokenKind {
    End,
    Ident,
    Number,
    String,
    Op,         ///< < <= = != > >=
    LParen, RParen,
    Comma,
    KwAnd, KwOr, KwNot, KwLike, KwIn,
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

        // Quoted identifier:  "Invert elev"  or  [Invert elev]
        if (c == '"' || c == '[') {
            const QChar close = (c == '"') ? QChar('"') : QChar(']');
            ++m_pos;
            QString s;
            while (m_pos < m_in.size() && m_in[m_pos] != close)
                s += m_in[m_pos++];
            if (m_pos < m_in.size()) ++m_pos;  // consume closing
            return {TokenKind::Ident, s, start};
        }

        // String literal:  'value'
        if (c == '\'') {
            ++m_pos;
            QString s;
            while (m_pos < m_in.size() && m_in[m_pos] != '\'')
                s += m_in[m_pos++];
            if (m_pos < m_in.size()) ++m_pos;
            return {TokenKind::String, s, start};
        }

        // Number:  -?\d+(\.\d+)?
        if (c.isDigit() || (c == '-' && m_pos + 1 < m_in.size()
                              && m_in[m_pos + 1].isDigit())) {
            QString s;
            if (c == '-') s += m_in[m_pos++];
            while (m_pos < m_in.size()
                   && (m_in[m_pos].isDigit() || m_in[m_pos] == '.'))
                s += m_in[m_pos++];
            return {TokenKind::Number, s, start};
        }

        // Operators
        if (c == '<' || c == '>' || c == '=' || c == '!') {
            QString s; s += m_in[m_pos++];
            if (m_pos < m_in.size() && m_in[m_pos] == '=')
                s += m_in[m_pos++];
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
            return {TokenKind::Ident, s, start};
        }

        // Unrecognised character — let parser surface it.
        ++m_pos;
        return {TokenKind::End, QString(c), start};
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

class Parser {
public:
    explicit Parser(const QString &input) : m_tok(input) { advance(); }

    QueryPredicate parse() {
        QueryPredicate result;
        if (m_cur.kind == TokenKind::End) return result;  // empty == match-all
        try {
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

    void advance() { m_cur = m_tok.next(); }

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

        // LIKE
        if (m_cur.kind == TokenKind::KwLike) {
            advance();
            if (m_cur.kind != TokenKind::String)
                throwAt(QStringLiteral("Expected string after LIKE"));
            auto n = std::make_shared<QueryNode>();
            n->kind = QueryNode::Like;
            n->fieldName = field;
            n->literal = QVariant(m_cur.text);
            advance();
            return n;
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
            while (m_cur.kind != TokenKind::RParen) {
                if (m_cur.kind == TokenKind::End)
                    throwAt(QStringLiteral("Unterminated IN list"));
                if (m_cur.kind == TokenKind::Number)
                    n->inList.append(QVariant(m_cur.text.toDouble()));
                else if (m_cur.kind == TokenKind::String)
                    n->inList.append(QVariant(m_cur.text));
                else
                    throwAt(QStringLiteral("Expected number or string in IN list"));
                advance();
                if (m_cur.kind == TokenKind::Comma) advance();
            }
            advance();  // consume )
            return n;
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
        if (m_cur.kind == TokenKind::Number)
            n->literal = QVariant(m_cur.text.toDouble());
        else if (m_cur.kind == TokenKind::String)
            n->literal = QVariant(m_cur.text);
        else
            throwAt(QStringLiteral("Expected number or string after operator"));
        advance();
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
    if (!a.isValid()) return INT_MIN;
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

bool eval(const QueryNode &n, const QVariantMap &row) {
    switch (n.kind) {
    case QueryNode::OrOp:
        return (n.left  && eval(*n.left,  row))
            || (n.right && eval(*n.right, row));
    case QueryNode::AndOp:
        return (n.left  && eval(*n.left,  row))
            && (n.right && eval(*n.right, row));
    case QueryNode::NotOp:
        return !(n.left && eval(*n.left, row));
    case QueryNode::Compare: {
        const QVariant lhs = lookupField(row, n.fieldName);
        const int c = cmpVariants(lhs, n.literal);
        if (c == INT_MIN) return false;
        if (n.op == "<")  return c <  0;
        if (n.op == "<=") return c <= 0;
        if (n.op == "=")  return c == 0;
        if (n.op == "!=") return c != 0;
        if (n.op == ">")  return c >  0;
        if (n.op == ">=") return c >= 0;
        return false;
    }
    case QueryNode::Like: {
        const QVariant lhs = lookupField(row, n.fieldName);
        if (!lhs.isValid()) return false;
        return likeToRegex(n.literal.toString()).match(lhs.toString()).hasMatch();
    }
    case QueryNode::In: {
        const QVariant lhs = lookupField(row, n.fieldName);
        for (const QVariant &v : n.inList)
            if (cmpVariants(lhs, v) == 0) return true;
        return false;
    }
    }
    return false;
}

} // anonymous

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QueryPredicate parseQuery(const QString &whereClause) {
    Parser p(whereClause);
    return p.parse();
}

bool evaluateQuery(const QueryPredicate &pred, const QVariantMap &row) {
    if (!pred.root) return true;  // empty / invalid → match-all
    return eval(*pred.root, row);
}

} // namespace openswmmvis
