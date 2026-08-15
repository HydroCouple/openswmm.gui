/*!
 * \file   test_dirtytrackingbasic.cpp
 * \brief  Slice A QtTest: verifies the title-marker contract for unsaved changes.
 *
 * This is a self-contained test that imitates the SWMMVisProjectWindow's
 * dirty-tracking semantics without pulling in the rest of the app. The full
 * end-to-end QApplication+model load test ships with Slice B once the
 * test-harness scaffolding is extracted into a swmmvis_core static library.
 */

#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

namespace {

class DirtyModel : public QObject
{
    Q_OBJECT
public:
    void setBaseName(const QString &name) { mBase = name; updateTitle(); }
    QString title() const { return mTitle; }
    bool hasChanges() const { return mDirty; }

    void setHasChanges(bool dirty) {
        if (mDirty == dirty) return;
        mDirty = dirty;
        updateTitle();
        emit hasChangesChanged(dirty);
    }
signals:
    void hasChangesChanged(bool dirty);
private:
    void updateTitle() {
        mTitle = mDirty ? (mBase + QStringLiteral(" *")) : mBase;
    }
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

void TestDirtyTrackingBasic::titleHasNoMarkerInitially()
{
    DirtyModel m;
    m.setBaseName("Example1");
    QCOMPARE(m.title(), QStringLiteral("Example1"));
    QVERIFY(!m.hasChanges());
}

void TestDirtyTrackingBasic::becomingDirtyAddsAsterisk()
{
    DirtyModel m;
    m.setBaseName("Example1");
    m.setHasChanges(true);
    QCOMPARE(m.title(), QStringLiteral("Example1 *"));
}

void TestDirtyTrackingBasic::becomingCleanRemovesAsterisk()
{
    DirtyModel m;
    m.setBaseName("Example1");
    m.setHasChanges(true);
    m.setHasChanges(false);
    QCOMPARE(m.title(), QStringLiteral("Example1"));
}

void TestDirtyTrackingBasic::signalEmittedOnceOnTransition()
{
    DirtyModel m;
    m.setBaseName("Example1");
    QSignalSpy spy(&m, &DirtyModel::hasChangesChanged);
    m.setHasChanges(true);
    m.setHasChanges(true);   // No-op
    m.setHasChanges(false);
    m.setHasChanges(false);  // No-op
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
}

QTEST_MAIN(TestDirtyTrackingBasic)
#include "test_dirtytrackingbasic.moc"
