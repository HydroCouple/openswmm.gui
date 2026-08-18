/*!
 * \file   test_queryparser.cpp
 * \brief  Unit tests for the Slice Z.2 WHERE-clause parser + evaluator.
 *
 * Pure-Qt — links only against the queryparser .cpp.  No engine /
 * widgets needed.
 */

#include <gtest/gtest.h>

#include <QVariantMap>

#include "core/queryparser.h"

using openswmmvis::parseQuery;
using openswmmvis::queryFieldNames;
using openswmmvis::evaluateQuery;

namespace {

QVariantMap row() {
    QVariantMap r;
    r["Name"]        = "J1";
    r["Node type"]   = "Junction";
    r["Invert elev"] = 99.5;
    r["Max depth"]   = 4.0;
    return r;
}

}  // anonymous

// ---------------------------------------------------------------------------
// Parser — basic
// ---------------------------------------------------------------------------

TEST(QueryParser, EmptyClauseMatchesEverything) {
    auto p = parseQuery("");
    EXPECT_TRUE(p.error.isEmpty());
    EXPECT_FALSE(p.root);
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, SimpleNumericComparison) {
    auto p = parseQuery("\"Invert elev\" > 99");
    ASSERT_TRUE(p.isValid()) << p.error.toStdString();
    EXPECT_TRUE (evaluateQuery(p, row()));
}

TEST(QueryParser, FailsWithoutOperator) {
    auto p = parseQuery("\"Invert elev\"");
    EXPECT_FALSE(p.isValid());
    EXPECT_GT(p.errorPos, 0);
}

TEST(QueryParser, BracketIdentifier) {
    auto p = parseQuery("[Max depth] <= 4.0");
    ASSERT_TRUE(p.isValid()) << p.error.toStdString();
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, StringEquality) {
    auto p = parseQuery("\"Node type\" = 'Junction'");
    ASSERT_TRUE(p.isValid()) << p.error.toStdString();
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, NotEqualWorks) {
    auto p = parseQuery("\"Node type\" != 'Outfall'");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

// ---------------------------------------------------------------------------
// Logical composition
// ---------------------------------------------------------------------------

TEST(QueryParser, AndCombinesPredicates) {
    auto p = parseQuery("\"Invert elev\" > 50 AND \"Max depth\" < 10");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, AndFailsWhenEitherSideFalse) {
    auto p = parseQuery("\"Invert elev\" > 1000 AND \"Max depth\" < 10");
    ASSERT_TRUE(p.isValid());
    EXPECT_FALSE(evaluateQuery(p, row()));
}

TEST(QueryParser, OrPasesWhenEitherSideTrue) {
    auto p = parseQuery("\"Invert elev\" > 1000 OR \"Max depth\" > 1");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, AndBindsTighterThanOr) {
    // Should parse as:  (false AND false) OR true  => true
    auto p = parseQuery(
        "\"Invert elev\" > 1000 AND \"Max depth\" > 1000 OR \"Max depth\" > 1");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, NotInverts) {
    auto p = parseQuery("NOT \"Invert elev\" > 1000");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, Parentheses) {
    auto p = parseQuery(
        "(\"Invert elev\" > 1000 OR \"Invert elev\" > 50) AND \"Max depth\" > 1");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

// ---------------------------------------------------------------------------
// LIKE + IN
// ---------------------------------------------------------------------------

TEST(QueryParser, LikePercentWildcard) {
    auto p = parseQuery("Name LIKE 'J%'");
    ASSERT_TRUE(p.isValid()) << p.error.toStdString();
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, LikeUnderscoreWildcard) {
    auto p = parseQuery("Name LIKE 'J_'");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, LikeCaseInsensitive) {
    auto p = parseQuery("Name LIKE 'j%'");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, InList) {
    auto p = parseQuery("\"Node type\" IN ('Junction', 'Outfall')");
    ASSERT_TRUE(p.isValid());
    EXPECT_TRUE(evaluateQuery(p, row()));
}

TEST(QueryParser, InListMissesWhenNoMatch) {
    auto p = parseQuery("\"Node type\" IN ('Storage', 'Divider')");
    ASSERT_TRUE(p.isValid());
    EXPECT_FALSE(evaluateQuery(p, row()));
}

// ---------------------------------------------------------------------------
// Missing fields → false
// ---------------------------------------------------------------------------

TEST(QueryParser, MissingFieldEvaluatesFalse) {
    auto p = parseQuery("\"Doesnt exist\" = 1");
    ASSERT_TRUE(p.isValid());
    EXPECT_FALSE(evaluateQuery(p, row()));
}

// ---------------------------------------------------------------------------
// Referenced-field collection
// ---------------------------------------------------------------------------
//
// The Attribute Table's filter proxy uses this to materialise ONLY the
// columns a predicate compares, instead of every column of every row.  If
// it under-reports a field, that column silently goes missing from the row
// map and the predicate reads it as absent — i.e. quietly wrong results,
// not a crash — so the traversal is pinned here.

TEST(QueryParser, FieldNamesEmptyForEmptyQuery) {
    EXPECT_TRUE(queryFieldNames(parseQuery("")).isEmpty());
}

TEST(QueryParser, FieldNamesCollectsCompareLikeAndIn) {
    auto p = parseQuery(
        "\"Invert elev\" > 1 AND (Name LIKE 'J%' OR \"Node type\" IN ('a','b'))");
    ASSERT_TRUE(p.isValid());
    const QStringList f = queryFieldNames(p);
    EXPECT_EQ(f.size(), 3);
    EXPECT_TRUE(f.contains("Invert elev"));
    EXPECT_TRUE(f.contains("Name"));
    EXPECT_TRUE(f.contains("Node type"));
}

TEST(QueryParser, FieldNamesDeduplicatesAndDescendsNot) {
    auto p = parseQuery("NOT (\"Max depth\" > 1 AND \"Max depth\" < 9)");
    ASSERT_TRUE(p.isValid());
    const QStringList f = queryFieldNames(p);
    EXPECT_EQ(f.size(), 1);
    EXPECT_EQ(f.at(0), QStringLiteral("Max depth"));
}

// A LIKE pattern is compiled once at parse time now; evaluating the same
// predicate repeatedly must keep matching (i.e. the compiled regex is
// carried on the node, not left default-constructed).
TEST(QueryParser, LikeRegexSurvivesRepeatedEvaluation) {
    auto p = parseQuery("Name LIKE 'J%'");
    ASSERT_TRUE(p.isValid());
    for (int i = 0; i < 3; ++i) EXPECT_TRUE(evaluateQuery(p, row()));
}

// Copying a predicate (matchedRefs parses its own, proxies store one by
// value) must carry the compiled pattern with it.
TEST(QueryParser, LikeRegexSurvivesPredicateCopy) {
    auto p = parseQuery("Name LIKE 'J%'");
    ASSERT_TRUE(p.isValid());
    auto copy = p;
    EXPECT_TRUE(evaluateQuery(copy, row()));
}
