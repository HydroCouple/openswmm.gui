/*!
 * \file   queryparser.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice Z.2 — Tiny SQL-like WHERE-clause parser for the Attribute
 * Table's filter bar.  Hand-rolled; no SQLite or other dependency.
 *
 * Grammar (recursive descent, AND binds tighter than OR):
 *
 *   where      := orExpr
 *   orExpr     := andExpr { 'OR' andExpr }
 *   andExpr    := notExpr { 'AND' notExpr }
 *   notExpr    := 'NOT' notExpr | primary
 *   primary    := '(' orExpr ')' | comparison
 *   comparison := ident op value
 *                | ident 'LIKE' string
 *                | ident 'IN' '(' value { ',' value } ')'
 *   op         := '<' | '<=' | '=' | '!=' | '>' | '>='
 *   ident      := bare-identifier | '[' anything-but-]+ ']' | '"' …'"'
 *   value      := number | quoted-string
 *
 * Keywords AND / OR / NOT / LIKE / IN are case-insensitive.
 * Identifier matching against row keys is **case-sensitive** because
 * the column schema keys are themselves (e.g. "Invert elev" vs
 * "invert_elev").
 *
 * The evaluator runs against a `QVariantMap` (one row's identify-map),
 * comparing values type-aware: numeric vs string coercion follows
 * QVariant's rules.
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

namespace openswmmvis {

/*! AST node — public only so consumers can inspect on test failures. */
struct QueryNode {
    enum Kind {
        OrOp,
        AndOp,
        NotOp,
        Compare,
        Like,
        In,
    };
    Kind                       kind = Compare;
    std::shared_ptr<QueryNode> left;
    std::shared_ptr<QueryNode> right;
    QString                    fieldName;
    QString                    op;         ///< For Compare: "<", "<=", ...
    QVariant                   literal;    ///< For Compare / Like
    QList<QVariant>            inList;     ///< For In
    /*! For Like: the LIKE pattern compiled to a regex, built once at
     *  parse time.  It used to be rebuilt — and recompiled — on every
     *  row the predicate was evaluated against. */
    QRegularExpression         likeRegex;
};

/*! Parse result.  Either `root` is non-null and `error` empty, or
 *  `error` describes the failure (1-based position + message). */
struct QueryPredicate {
    std::shared_ptr<QueryNode> root;
    QString                    error;
    int                        errorPos = -1;
    [[nodiscard]] bool         isValid() const noexcept { return !!root && error.isEmpty(); }
};

/*! Parse a WHERE-clause string.  Empty input → predicate that
 *  matches everything (root is null, error is empty). */
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
 *  its case-insensitive linear fallback). */
QStringList queryFieldNames(const QueryPredicate &pred);

/*! Evaluate the predicate against one row.  When `pred.root` is
 *  null, returns true (no filter). */
bool evaluateQuery(const QueryPredicate &pred, const QVariantMap &row);

} // namespace openswmmvis

#endif // OPENSWMMVIS_QUERYPARSER_H
