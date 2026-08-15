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
