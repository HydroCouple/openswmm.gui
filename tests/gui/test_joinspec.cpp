/**
 * @file      test_joinspec.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.16-data — JoinSpec value type.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest/QtTest>

#include "render/joinspec.h"

using namespace OpenSWMM::Render;

class TestJoinSpec : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require disabled join defaults, empty source/key/prefix strings and no selected
     * fields.
     *
     */
    void defaults_areSensible();
    /**
     *
     * @brief Require a default join specification to serialize as an empty JSON object.
     *
     */
    void defaults_jsonIsEmpty();

    /**
     *
     * @brief Round-trip all explicitly assigned join metadata without opening the referenced
     * source.
     *
     */
    void roundTrip_preservesEveryField();
    /**
     *
     * @brief Require disabled enabled-flag omission even when a source path is present.
     *
     */
    void roundTrip_disabledOmitted();
    /**
     *
     * @brief Require empty source/layer/key/prefix strings to be omitted from enabled join JSON.
     *
     */
    void roundTrip_emptyStringsOmitted();
    /**
     *
     * @brief Require an empty selected-field list to be omitted from join JSON.
     *
     */
    void roundTrip_emptyJoinFieldsOmitted();
    /**
     *
     * @brief Require JSON round-trip to retain the selected field order c,a,b.
     *
     */
    void roundTrip_joinFieldsPreserveOrder();

    /**
     *
     * @brief Require matching source path and field selections to compare equal.
     *
     */
    void equality_identicalSpecsAreEqual();
    /**
     *
     * @brief Require differing enabled flags to compare unequal.
     *
     */
    void equality_differentEnabledNotEqual();
    /**
     *
     * @brief Require differing source paths to compare unequal.
     *
     */
    void equality_differentSourceNotEqual();
    /**
     *
     * @brief Require differing selected-field lists to compare unequal.
     *
     */
    void equality_differentJoinFieldsNotEqual();
    /**
     *
     * @brief Require differing output field prefixes to compare unequal.
     *
     */
    void equality_differentPrefixNotEqual();
};

void TestJoinSpec::defaults_areSensible()
{
    JoinSpec s;
    QCOMPARE(s.enabled, false);
    QCOMPARE(s.sourcePath, QString());
    QCOMPARE(s.sourceLayerName, QString());
    QCOMPARE(s.sourceKeyField, QString());
    QCOMPARE(s.targetKeyField, QString());
    QVERIFY(s.joinFields.isEmpty());
    QCOMPARE(s.fieldPrefix, QString());
}

void TestJoinSpec::defaults_jsonIsEmpty()
{
    JoinSpec s;
    QVERIFY(s.toJson().isEmpty());
}

void TestJoinSpec::roundTrip_preservesEveryField()
{
    JoinSpec s;
    s.enabled = true;
    s.sourcePath = QStringLiteral("/tmp/observations.csv");
    s.sourceLayerName = QStringLiteral("obs_2024");
    s.sourceKeyField = QStringLiteral("node_id");
    s.targetKeyField = QStringLiteral("name");
    s.joinFields = {QStringLiteral("depth"), QStringLiteral("flow")};
    s.fieldPrefix = QStringLiteral("obs_");

    JoinSpec back = JoinSpec::fromJson(s.toJson());
    QCOMPARE(back.enabled, s.enabled);
    QCOMPARE(back.sourcePath, s.sourcePath);
    QCOMPARE(back.sourceLayerName, s.sourceLayerName);
    QCOMPARE(back.sourceKeyField, s.sourceKeyField);
    QCOMPARE(back.targetKeyField, s.targetKeyField);
    QCOMPARE(back.joinFields, s.joinFields);
    QCOMPARE(back.fieldPrefix, s.fieldPrefix);
}

void TestJoinSpec::roundTrip_disabledOmitted()
{
    JoinSpec s;
    s.sourcePath = QStringLiteral("x");
    QVERIFY(!s.toJson().contains(QStringLiteral("enabled")));
}

void TestJoinSpec::roundTrip_emptyStringsOmitted()
{
    JoinSpec s;
    s.enabled = true;
    const QJsonObject j = s.toJson();
    QVERIFY(!j.contains(QStringLiteral("sourcePath")));
    QVERIFY(!j.contains(QStringLiteral("sourceLayerName")));
    QVERIFY(!j.contains(QStringLiteral("sourceKeyField")));
    QVERIFY(!j.contains(QStringLiteral("targetKeyField")));
    QVERIFY(!j.contains(QStringLiteral("fieldPrefix")));
}

void TestJoinSpec::roundTrip_emptyJoinFieldsOmitted()
{
    JoinSpec s;
    s.enabled = true;
    QVERIFY(!s.toJson().contains(QStringLiteral("joinFields")));
}

void TestJoinSpec::roundTrip_joinFieldsPreserveOrder()
{
    JoinSpec s;
    s.joinFields = {QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")};
    JoinSpec back = JoinSpec::fromJson(s.toJson());
    QCOMPARE(back.joinFields,
             QStringList({QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
}

void TestJoinSpec::equality_identicalSpecsAreEqual()
{
    JoinSpec a, b;
    a.sourcePath = b.sourcePath = QStringLiteral("x");
    a.joinFields = b.joinFields = {QStringLiteral("f1")};
    QCOMPARE(a, b);
}

void TestJoinSpec::equality_differentEnabledNotEqual()
{
    JoinSpec a, b;
    b.enabled = true;
    QVERIFY(a != b);
}

void TestJoinSpec::equality_differentSourceNotEqual()
{
    JoinSpec a, b;
    b.sourcePath = QStringLiteral("y");
    QVERIFY(a != b);
}

void TestJoinSpec::equality_differentJoinFieldsNotEqual()
{
    JoinSpec a, b;
    b.joinFields = {QStringLiteral("z")};
    QVERIFY(a != b);
}

void TestJoinSpec::equality_differentPrefixNotEqual()
{
    JoinSpec a, b;
    b.fieldPrefix = QStringLiteral("p_");
    QVERIFY(a != b);
}

QTEST_MAIN(TestJoinSpec)
#include "test_joinspec.moc"
