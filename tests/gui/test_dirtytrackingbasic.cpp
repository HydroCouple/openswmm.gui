/**
 * @file      test_dirtytrackingbasic.cpp
 * @brief     Slice A QtTest: verifies the title-marker contract for unsaved changes.
 *
 * This is a self-contained test that imitates the SWMMVisProjectWindow's
 * dirty-tracking semantics without pulling in the rest of the app. The full
 * end-to-end QApplication+model load test ships with Slice B once the
 * test-harness scaffolding is extracted into a swmmvis_core static library.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

namespace
{

class DirtyModel : public QObject
{
    Q_OBJECT
public:
    /** @brief Replace the miniature model's base title and recompute its current marker.
     * @param[in] name New base text copied into owned QString storage.
     * Retains the dirty flag and emits no hasChangesChanged signal; this is a local
     * test model, without production project-window integration. */
    void setBaseName(const QString &name)
    {
        mBase = name;
        updateTitle();
    }
    /** @brief Read the miniature model's currently composed title.
     * @return Owned implicitly shared QString copy; later model mutation does not
     * change its logical value. No state or signal changes. */
    QString title() const { return mTitle; }
    /** @brief Read the miniature model's unsaved-change flag.
     * @return Current boolean dirty state without mutation or signal emission. */
    bool hasChanges() const { return mDirty; }

    /** @brief Apply a dirty-state transition and update the title before notifying observers.
     * @param[in] dirty Requested state, copied.
     * Equal-state requests are no-ops. A real transition updates owned state/title and
     * emits hasChangesChanged synchronously under the Qt connection convention. */
    void setHasChanges(bool dirty)
    {
        if (mDirty == dirty)
            return;
        mDirty = dirty;
        updateTitle();
        emit hasChangesChanged(dirty);
    }
signals:
    /** @brief Notify observers after a dirty-state transition in the miniature model.
     * @param[in] dirty New flag value emitted after title recomposition.
     * Signal declaration only; connected receivers follow their Qt connection type
     * and this signal transfers no model ownership. */
    void hasChangesChanged(bool dirty);

private:
    /** @brief Compose the owned title from base text and dirty state.
     * Appends one literal space/asterisk only when dirty; emits no signal and does not
     * change the base name or dirty flag. */
    void updateTitle() { mTitle = mDirty ? (mBase + QStringLiteral(" *")) : mBase; }
    QString mBase;
    QString mTitle;
    bool mDirty = false;
};

} // namespace

class TestDirtyTrackingBasic : public QObject
{
    Q_OBJECT
private slots:
    void titleHasNoMarkerInitially();
    void becomingDirtyAddsAsterisk();
    void becomingCleanRemovesAsterisk();
    void signalEmittedOnceOnTransition();
};

/** @brief Require a newly named clean miniature model to show its base title without a marker.
 * Exercises the local DirtyModel only; no production window or file is opened. */
void TestDirtyTrackingBasic::titleHasNoMarkerInitially()
{
    DirtyModel m;
    m.setBaseName("Example1");
    QCOMPARE(m.title(), QStringLiteral("Example1"));
    QVERIFY(!m.hasChanges());
}

/** @brief Require a false-to-true dirty transition to append one space/asterisk to the title. */
void TestDirtyTrackingBasic::becomingDirtyAddsAsterisk()
{
    DirtyModel m;
    m.setBaseName("Example1");
    m.setHasChanges(true);
    QCOMPARE(m.title(), QStringLiteral("Example1 *"));
}

/** @brief Require a dirty-to-clean transition to restore the unmarked base title. */
void TestDirtyTrackingBasic::becomingCleanRemovesAsterisk()
{
    DirtyModel m;
    m.setBaseName("Example1");
    m.setHasChanges(true);
    m.setHasChanges(false);
    QCOMPARE(m.title(), QStringLiteral("Example1"));
}

/** @brief Require exactly two ordered dirty-state signals for true/true/false/false requests.
 * Checks repeated equal values are no-ops and observed arguments are true then false. */
void TestDirtyTrackingBasic::signalEmittedOnceOnTransition()
{
    DirtyModel m;
    m.setBaseName("Example1");
    QSignalSpy spy(&m, &DirtyModel::hasChangesChanged);
    m.setHasChanges(true);
    m.setHasChanges(true); // No-op
    m.setHasChanges(false);
    m.setHasChanges(false); // No-op
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
}

QTEST_MAIN(TestDirtyTrackingBasic)
#include "test_dirtytrackingbasic.moc"
