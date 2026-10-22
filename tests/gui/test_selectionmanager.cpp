/**
 * @file      test_selectionmanager.cpp
 * @brief     Slice E QtTest: Phase 1.4 SelectionManager — Replace / Add / Toggle /
 *         Subtract semantics + selectionChanged signal payload.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "selection/selectionmanager.h"

#include <QObject>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

class TestSelectionManager : public QObject
{
    Q_OBJECT
private slots:
    void emptyAtStart();
    void replaceMode();
    void addMode();
    void toggleMode();
    void subtractMode();
    void clearEmpties();
    void noopWhenSetUnchanged();
    void signalPayloadCarriesAddedAndRemoved();
    void singleRefOverloadWorks();
    void invalidRefIsIgnored();
};

namespace
{

/**
 *
 * @brief Build a node selection identity without validating model membership.
 * @param name Borrowed object name copied into the result.
 * @return By-value Node reference; no engine ownership or lookup.
 *
 */
SWMMObjectRef N(const QString &name)
{
    return {SWMMObjectRef::Node, name};
}
/**
 *
 * @brief Build a link selection identity without validating model membership.
 * @param name Borrowed object name copied into the result.
 * @return By-value Link reference; no engine ownership or lookup.
 *
 */
SWMMObjectRef L(const QString &name)
{
    return {SWMMObjectRef::Link, name};
}
/**
 *
 * @brief Build a subcatchment selection identity without validating model membership.
 * @param name Borrowed object name copied into the result.
 * @return By-value Subcatchment reference; no engine ownership or lookup.
 *
 */
SWMMObjectRef S(const QString &name)
{
    return {SWMMObjectRef::Subcatchment, name};
}

/**
 *
 * @brief Copy fixture references into an unordered deduplicated set.
 * @param refs Borrowed initializer-list references copied during this call.
 * @return Owned set retaining reference values rather than list storage; invalid
 * references are not filtered here.
 *
 */
QSet<SWMMObjectRef> set(std::initializer_list<SWMMObjectRef> refs)
{
    QSet<SWMMObjectRef> s;
    for (auto &r : refs)
        s.insert(r);
    return s;
}

} // namespace

/**
 *
 * @brief Require a new selection manager to be empty with size zero.
 *
 */
void TestSelectionManager::emptyAtStart()
{
    SelectionManager m;
    QVERIFY(m.isEmpty());
    QCOMPARE(m.size(), 0);
}

/**
 *
 * @brief Require Replace to retain exactly the supplied node set, then replace it with a link.
 *
 */
void TestSelectionManager::replaceMode()
{
    SelectionManager m;
    m.select(set({N("J1"), N("J2")}), SelectionManager::Replace);
    QCOMPARE(m.size(), 2);
    QVERIFY(m.contains(N("J1")));
    QVERIFY(m.contains(N("J2")));

    m.select(set({L("C1")}), SelectionManager::Replace);
    QCOMPARE(m.size(), 1);
    QVERIFY(m.contains(L("C1")));
    QVERIFY(!m.contains(N("J1")));
}

/**
 *
 * @brief Require Add to preserve existing members, add new typed identities and deduplicate
 * repeated additions.
 *
 */
void TestSelectionManager::addMode()
{
    SelectionManager m;
    m.select(set({N("J1")}), SelectionManager::Replace);
    m.select(set({N("J2"), L("C1")}), SelectionManager::Add);
    QCOMPARE(m.size(), 3);
    QVERIFY(m.contains(N("J1")));
    QVERIFY(m.contains(N("J2")));
    QVERIFY(m.contains(L("C1")));

    // Adding an existing ref is a no-op size-wise.
    m.select(set({N("J1")}), SelectionManager::Add);
    QCOMPARE(m.size(), 3);
}

/**
 *
 * @brief Require Toggle to remove an existing node and add an absent node while preserving the
 * untouched member.
 *
 */
void TestSelectionManager::toggleMode()
{
    SelectionManager m;
    m.select(set({N("J1"), N("J2")}), SelectionManager::Replace);
    // J2 is selected → toggle removes; J3 not selected → toggle adds.
    m.select(set({N("J2"), N("J3")}), SelectionManager::Toggle);
    QCOMPARE(m.size(), 2);
    QVERIFY(m.contains(N("J1")));
    QVERIFY(!m.contains(N("J2")));
    QVERIFY(m.contains(N("J3")));
}

/**
 *
 * @brief Require Subtract to remove selected members and harmlessly ignore a nonmember
 * subcatchment.
 *
 */
void TestSelectionManager::subtractMode()
{
    SelectionManager m;
    m.select(set({N("J1"), N("J2"), L("C1")}), SelectionManager::Replace);
    m.select(set({N("J2"), L("C1"), S("X1")}), SelectionManager::Subtract);
    // Only J1 remains; subtracting non-members is fine.
    QCOMPARE(m.size(), 1);
    QVERIFY(m.contains(N("J1")));
}

/**
 *
 * @brief Require clear() to empty a populated selection.
 *
 */
void TestSelectionManager::clearEmpties()
{
    SelectionManager m;
    m.select(set({N("J1"), N("J2")}), SelectionManager::Replace);
    m.clear();
    QVERIFY(m.isEmpty());
}

/**
 *
 * @brief Require identical Replace and already-present Add to emit no selectionChanged signal.
 *
 */
void TestSelectionManager::noopWhenSetUnchanged()
{
    SelectionManager m;
    m.select(set({N("J1")}), SelectionManager::Replace);
    QSignalSpy spy(&m, &SelectionManager::selectionChanged);
    m.select(set({N("J1")}), SelectionManager::Replace); // identical
    QCOMPARE(spy.count(), 0);
    m.select(set({N("J1")}), SelectionManager::Add); // already there
    QCOMPARE(spy.count(), 0);
}

/**
 *
 * @brief Require one changed signal to carry the complete current set and exact added/removed
 * typed deltas.
 *
 */
void TestSelectionManager::signalPayloadCarriesAddedAndRemoved()
{
    SelectionManager m;
    m.select(set({N("J1"), N("J2")}), SelectionManager::Replace);

    QSignalSpy spy(&m, &SelectionManager::selectionChanged);
    m.select(set({N("J2"), L("C1")}), SelectionManager::Replace);

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    const auto cur = args.at(0).value<QSet<SWMMObjectRef>>();
    const auto added = args.at(1).value<QSet<SWMMObjectRef>>();
    const auto rem = args.at(2).value<QSet<SWMMObjectRef>>();
    QCOMPARE(cur, set({N("J2"), L("C1")}));
    QCOMPARE(added, set({L("C1")}));
    QCOMPARE(rem, set({N("J1")}));
}

/**
 *
 * @brief Require the single-reference overload to default Replace and support an explicit Add.
 *
 */
void TestSelectionManager::singleRefOverloadWorks()
{
    SelectionManager m;
    m.select(N("J1")); // single-ref Replace
    QCOMPARE(m.size(), 1);
    QVERIFY(m.contains(N("J1")));
    m.select(L("C1"), SelectionManager::Add);
    QCOMPARE(m.size(), 2);
    QVERIFY(m.contains(L("C1")));
}

/**
 *
 * @brief Require a default Unknown/empty identity to leave an initially empty selection empty.
 *
 */
void TestSelectionManager::invalidRefIsIgnored()
{
    SelectionManager m;
    m.select(SWMMObjectRef{}, SelectionManager::Replace); // Unknown + empty
    QVERIFY(m.isEmpty());
}

QTEST_MAIN(TestSelectionManager)
#include "test_selectionmanager.moc"
