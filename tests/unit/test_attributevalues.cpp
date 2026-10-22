/**
 * @file      test_attributevalues.cpp
 * @brief     Tests for attributevalues.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "core/attributevalue.h"
#include "core/queryparser.h"
#include "ui/panels/attributevalueproxy.h"
#include <QStandardItemModel>
#include <QTest>
#include <algorithm>
#include <limits>

using namespace openswmmvis;
namespace
{
/**
 *
 * @brief Populate one scalar fixture cell's display, value and sort roles.
 * @param[in,out] model Writable item model with column zero and valid row; ignores
 * setData status, so caller must provide an existing supported cell.
 * @param row Zero-based existing row.
 * @param v Borrowed typed QVariant copied into each of the three roles; invalid is
 * intentional missing data. Other roles/cells remain unchanged; no model ownership transfer.
 *
 */
void setScalar(QStandardItemModel &model, int row, const QVariant &v)
{
    const auto index = model.index(row, 0);
    model.setData(index, v, Qt::DisplayRole);
    model.setData(index, v, kAttributeValueRole);
    model.setData(index, v, kAttributeSortRole);
}
}
/**
 *
 * @brief Qt fixture for typed attribute sorting and query missing-value semantics.
 * Each slot owns local item/proxy models; QTEST_MAIN supplies the application.
 *
 */
class TestAttributeValues : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Exhaust all four-value insertion permutations in both sort directions.
     * Requires numeric ordering, null last, NULL display and missing-value foreground role.
     *
     */
    void everyPermutationSortsNumericallyWithNullLast()
    {
        const QList<QVariant> values{0.1, 0.65, 1.0, QVariant()};
        QList<int> order{0, 1, 2, 3};
        do {
            QStandardItemModel source(4, 1);
            for (int i = 0; i < 4; ++i)
                setScalar(source, i, values[order[i]]);
            AttributeValueProxy proxy;
            proxy.setSourceModel(&source);
            for (const auto direction : {Qt::AscendingOrder, Qt::DescendingOrder}) {
                proxy.sort(0, direction);
                for (int i = 0; i < 3; ++i)
                    QCOMPARE(proxy.index(i, 0).data(kAttributeValueRole),
                             values[direction == Qt::AscendingOrder ? i : 2 - i]);
                QVERIFY(!proxy.index(3, 0).data(kAttributeValueRole).isValid());
                QCOMPARE(proxy.index(3, 0).data().toString(), QStringLiteral("NULL"));
                QVERIFY(proxy.index(3, 0).data(Qt::ForegroundRole).isValid());
            }
        } while (std::next_permutation(order.begin(), order.end()));
    }
    /**
     *
     * @brief Verify stable source-row identity across signed/unsigned 64-bit, floating, boolean
     * and null sorting.
     * Checks exact comparisons around 2^53, signed/unsigned limits and fractional values.
     *
     */
    void identityAndNumericTypesArePreserved()
    {
        const QList<QVariant> values{qlonglong(9007199254740993LL),
                                     qlonglong(9007199254740992LL),
                                     qulonglong(18446744073709551615ULL),
                                     qlonglong(-1),
                                     0.0,
                                     2.5,
                                     qlonglong(2),
                                     QVariant(),
                                     false,
                                     qlonglong(9007199254740993LL)};
        QStandardItemModel source(values.size(), 1);
        for (int i = 0; i < values.size(); ++i)
            setScalar(source, i, values[i]);
        AttributeValueProxy proxy;
        proxy.setSourceModel(&source);
        const QList<int> ascending{3, 4, 8, 6, 5, 1, 0, 9, 2, 7};
        const QList<int> descending{2, 0, 9, 1, 5, 6, 4, 8, 3, 7};
        for (const auto direction : {Qt::AscendingOrder, Qt::DescendingOrder}) {
            proxy.sort(0, direction);
            const auto expected = direction == Qt::AscendingOrder ? ascending : descending;
            for (int i = 0; i < expected.size(); ++i)
                QCOMPARE(proxy.mapToSource(proxy.index(i, 0)).row(), expected[i]);
        }
        QCOMPARE(compareAttributeNumbers(qlonglong(9007199254740993LL), 9007199254740992.0), 1);
        QCOMPARE(
            compareAttributeNumbers(std::numeric_limits<qlonglong>::max(), std::ldexp(1.0, 63)),
            -1);
        QCOMPARE(
            compareAttributeNumbers(std::numeric_limits<qulonglong>::max(), std::ldexp(1.0, 64)),
            -1);
        QCOMPARE(compareAttributeNumbers(qlonglong(-2), -2.5), 1);
        QCOMPARE(compareAttributeNumbers(qulonglong(2), 2.5), -1);
    }
    /**
     *
     * @brief Distinguish literal dash/NULL/empty/zero-padded strings from invalid QVariant.
     * Requires query null semantics and exact text identity rather than numeric coercion.
     *
     */
    void literalTextIsNotMissing()
    {
        QStandardItemModel source(5, 1);
        const QList<QVariant> values{QStringLiteral("-"), QStringLiteral("NULL"),
                                     QStringLiteral(""), QStringLiteral("001"), QVariant()};
        for (int i = 0; i < values.size(); ++i)
            setScalar(source, i, values[i]);
        AttributeValueProxy proxy;
        proxy.setSourceModel(&source);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(proxy.index(i, 0).data(kAttributeValueRole), values[i]);
            QVERIFY(!evaluateQuery(parseQuery("field IS NULL"), {{"field", values[i]}}));
        }
        QVERIFY(evaluateQuery(parseQuery("field IS NULL"), {{"field", QVariant()}}));
        QVERIFY(!evaluateQuery(parseQuery("field = '1'"), {{"field", QStringLiteral("001")}}));
        QVERIFY(evaluateQuery(parseQuery("field = '001'"), {{"field", QStringLiteral("001")}}));
    }
    /**
     *
     * @brief Require null and bad numeric values to fail ordinary comparisons, including
     * negation.
     * Checks explicit null/boolean predicates, exact 64-bit literals and NaN-as-missing behavior.
     *
     */
    void nullPredicatesAndBadNumericOperands()
    {
        for (const auto &text : {"field > .5", "NOT field < 1", "field BETWEEN 0 AND 10",
                                 "field IN (0,1)", "field NOT LIKE '%'", "field != 0"}) {
            const auto p = parseQuery(text);
            QVERIFY2(p.error.isEmpty(), qPrintable(p.error));
            QVERIFY2(!evaluateQuery(p, {{"field", QVariant()}}), text);
        }
        for (const auto &text : {"field > .5", "NOT field < 1"})
            QVERIFY(!evaluateQuery(parseQuery(text), {{"field", QStringLiteral("—")}}));
        QVERIFY(evaluateQuery(parseQuery("field IS NOT NULL AND field = 0"), {{"field", 0}}));
        QVERIFY(evaluateQuery(parseQuery("field = FALSE"), {{"field", false}}));
        QVERIFY(evaluateQuery(parseQuery("field = 9007199254740993"),
                              {{"field", qlonglong(9007199254740993LL)}}));
        QVERIFY(!evaluateQuery(parseQuery("field = 9007199254740992"),
                               {{"field", qlonglong(9007199254740993LL)}}));
        QVERIFY(evaluateQuery(parseQuery("field IS NULL"),
                              {{"field", std::numeric_limits<double>::quiet_NaN()}}));
    }
};
QTEST_MAIN(TestAttributeValues)
#include "test_attributevalues.moc"
