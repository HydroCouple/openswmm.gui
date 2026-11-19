/**
 * @file      test_auxiliarystoragespec.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.15-data — AuxiliaryStorageSpec value type.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest/QtTest>

#include "render/auxiliarystoragespec.h"

using namespace OpenSWMM::Render;

class TestAuxiliaryStorageSpec : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require auxiliary storage to default disabled with an empty database path.
     *
     */
    void defaults_areSensible();
    /**
     *
     * @brief Require default auxiliary-storage JSON to contain no fields.
     *
     */
    void defaults_jsonIsEmpty();
    /**
     *
     * @brief Round-trip enabled state and database-path text without opening a database.
     *
     */
    void roundTrip_preservesEnabledAndPath();
    /**
     *
     * @brief Require disabled enabled-flag omission even when a database path is supplied.
     *
     */
    void roundTrip_enabledDefaultOmitted();
    /**
     *
     * @brief Require empty database-path omission from enabled storage JSON.
     *
     */
    void roundTrip_emptyPathOmitted();
    /**
     *
     * @brief Require equal enabled states and database paths to compare equal.
     *
     */
    void equality_identicalSpecsAreEqual();
    /**
     *
     * @brief Require differing enabled states to compare unequal.
     *
     */
    void equality_differentEnabledNotEqual();
    /**
     *
     * @brief Require differing database-path text to compare unequal.
     *
     */
    void equality_differentPathNotEqual();
};

void TestAuxiliaryStorageSpec::defaults_areSensible()
{
    AuxiliaryStorageSpec s;
    QCOMPARE(s.enabled, false);
    QCOMPARE(s.dbPath, QString());
}

void TestAuxiliaryStorageSpec::defaults_jsonIsEmpty()
{
    AuxiliaryStorageSpec s;
    QVERIFY(s.toJson().isEmpty());
}

void TestAuxiliaryStorageSpec::roundTrip_preservesEnabledAndPath()
{
    AuxiliaryStorageSpec s;
    s.enabled = true;
    s.dbPath = QStringLiteral("/tmp/auxstore.db");
    AuxiliaryStorageSpec back = AuxiliaryStorageSpec::fromJson(s.toJson());
    QCOMPARE(back.enabled, s.enabled);
    QCOMPARE(back.dbPath, s.dbPath);
}

void TestAuxiliaryStorageSpec::roundTrip_enabledDefaultOmitted()
{
    AuxiliaryStorageSpec s;
    s.dbPath = QStringLiteral("x");
    QVERIFY(!s.toJson().contains(QStringLiteral("enabled")));
}

void TestAuxiliaryStorageSpec::roundTrip_emptyPathOmitted()
{
    AuxiliaryStorageSpec s;
    s.enabled = true;
    QVERIFY(!s.toJson().contains(QStringLiteral("dbPath")));
}

void TestAuxiliaryStorageSpec::equality_identicalSpecsAreEqual()
{
    AuxiliaryStorageSpec a, b;
    a.enabled = b.enabled = true;
    a.dbPath = b.dbPath = QStringLiteral("x");
    QCOMPARE(a, b);
}

void TestAuxiliaryStorageSpec::equality_differentEnabledNotEqual()
{
    AuxiliaryStorageSpec a, b;
    b.enabled = true;
    QVERIFY(a != b);
}

void TestAuxiliaryStorageSpec::equality_differentPathNotEqual()
{
    AuxiliaryStorageSpec a, b;
    b.dbPath = QStringLiteral("y");
    QVERIFY(a != b);
}

QTEST_MAIN(TestAuxiliaryStorageSpec)
#include "test_auxiliarystoragespec.moc"
