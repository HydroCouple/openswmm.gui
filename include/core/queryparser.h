/**
 * @file      queryparser.h
 * @brief     Tiny SQL-like WHERE-clause parser for the Attribute Table's filter bar.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice Z.2 — Tiny SQL-like WHERE-clause parser for the Attribute
 * Table's filter bar.  Hand-rolled; no SQLite or other dependency.
 *
 * Grammar (recursive descent, AND binds tighter than OR):
 *
 *   where      := ['WHERE'] orExpr
 *   orExpr     := andExpr { 'OR' andExpr }
 *   andExpr    := notExpr { 'AND' notExpr }
 *   notExpr    := 'NOT' notExpr | primary
 *   primary    := '(' orExpr ')' | comparison
 *   comparison := ident op value
 *                | ident ['NOT'] 'LIKE' string
 *                | ident ['NOT'] 'IN' '(' value { ',' value } ')'
 *                | ident ['NOT'] 'BETWEEN' value 'AND' value
 *                | ident 'IS' ['NOT'] 'NULL'
 *   op         := '<' | '<=' | '=' | '!=' | '<>' | '>' | '>='
 *   ident      := bare-identifier | '[' anything-but-]+ ']' | '"' …'"'
 *   value      := number | quoted-string | TRUE | FALSE
 *
 * Keywords and field lookup are case-insensitive (exact field names win).
 * Optional leading WHERE is accepted. Doubled quote delimiters are escaped.
 * NULL comparisons are unknown, including under NOT; only TRUE rows match.
 *
 * The evaluator runs against a `QVariantMap` (one row's identify-map),
 * comparing values type-aware: numeric vs string coercion follows
 * QVariant's rules.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_QUERYPARSER_H
#define OPENSWMMVIS_QUERYPARSER_H

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <memory>

namespace openswmmvis
{

/*! AST node — public only so consumers can inspect on test failures. */
struct QueryNode {
    enum Kind {
        OrOp,
        AndOp,
        NotOp,
        Compare,
        Like,
        In,
        Between,
        IsNull,
    };
    Kind kind = Compare;
    std::shared_ptr<QueryNode> left;
    std::shared_ptr<QueryNode> right;
    QString fieldName;
    QString op;             ///< For Compare: "<", "<=", ...
    QVariant literal;       ///< For Compare / Like
    QList<QVariant> inList; ///< For In
    /*! For Like: the LIKE pattern compiled to a regex, built once at
     *  parse time.  It used to be rebuilt — and recompiled — on every
     *  row the predicate was evaluated against. */
    QRegularExpression likeRegex;
};

/*! Parse result.  Either `root` is non-null and `error` empty, or
 *  `error` describes the failure (1-based position + message). */
struct QueryPredicate {
    std::shared_ptr<QueryNode> root;
    QString error;
    int errorPos = -1;
    /**
     * @brief Test whether parsing produced an expression without an error.
     * @return True for a non-null root with no error; false for the valid empty match-all predicate as well as failed parses.
     */
    [[nodiscard]] bool isValid() const noexcept { return !!root && error.isEmpty(); }
};

/*! Parse a WHERE-clause string.  Empty input → predicate that
 *  matches everything (root is null, error is empty). * @param[in] whereClause SQL-like expression using the file-level grammar; leading WHERE is optional.
 * @return Owned shared AST and empty error on success. Empty/whitespace input has no root or error; failure has no root, a message, and a one-based UTF-16 error position.
 * @details Does not inspect a schema or mutate caller data; unknown field names are resolved only during evaluation.
 */
QueryPredicate parseQuery(const QString &whereClause);

/*! Field names the predicate actually references, deduplicated and in
 *  first-seen order.  Empty when `pred.root` is null.
 *
 *  Callers use this to build a row map holding ONLY the columns the
 *  query compares, instead of materialising every column of every row.
 *  On a 272k-row / 55-column table that is the difference between ~30M
 *  cell reads per filter pass and ~272k.
 *
 *  Names are returned exactly as the user spelled them, so the caller
 *  must key its row map by these same strings for `evaluateQuery` to
 *  find them (see `lookupField` in the .cpp — an exact-key hit avoids
 *  its case-insensitive linear fallback). * @param[in] pred Predicate whose AST is inspected; an error string does not suppress traversal of an existing root.
 * @return Distinct field spellings in left-to-right AST encounter order.
 */
QStringList queryFieldNames(const QueryPredicate &pred);

/*! Suggestions replace [start, cursor) without changing the rest of a query. */
struct QuerySuggestions {
    int start = 0;
    QStringList candidates;
};
/**
 * @brief Suggest context-sensitive fields or SQL tokens at an editing cursor.
 * @param[in] text Full current query; the suffix after the cursor is ignored for completion.
 * @param[in] cursor UTF-16 character offset; clamped to [0, text.size()].
 * @param[in] fields Available column names; suggestions quote/escape names for query syntax.
 * @return Deduplicated case-insensitive prefix matches and the start offset of the replacement span [start, cursor). Suggestions do not validate the full query.
 */
QuerySuggestions suggestQuery(const QString &text, int cursor, const QStringList &fields);

/*! Evaluate the predicate against one row.  When `pred.root` is
 *  null and no error exists, returns true (no filter). Invalid queries
 *  return false. NULL comparisons use SQL three-valued logic. * @param[in] pred Parsed predicate; any nonempty error rejects the row.
 * @param[in] row Column values for this row. Exact field keys win, followed by case-insensitive lookup; missing keys are NULL.
 * @return True only for SQL TRUE; FALSE and UNKNOWN reject the row. Empty error-free predicates accept all rows.
 * @details Does not alter the predicate or row. Numeric comparison preserves integer precision where supported; text comparisons are case-sensitive while LIKE is case-insensitive.
 */
bool evaluateQuery(const QueryPredicate &pred, const QVariantMap &row);

} // namespace openswmmvis

#endif // OPENSWMMVIS_QUERYPARSER_H
