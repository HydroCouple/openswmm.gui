/*!
 * \file   test_relativepathpicker.cpp
 * \brief  Slice IO-10 — RelativePathPicker widget unit tests.
 *
 * \details Pins the widget contract documented in
 *          include/ui/widgets/relativepathpicker.h:
 *            - absolutePath() is canonical; setPath() accepts either form.
 *            - displayPath() shows the relative-to-anchor form when reachable.
 *            - Changing the anchor refreshes the displayed text without
 *              touching the absolute.
 *            - Cross-volume / no-anchor scenarios fall back to absolute.
 *            - pathChanged() fires on real changes only.
 *
 *          No filesystem touches — the picker is a pure string-display
 *          widget; resolution semantics are tested without dialog use.
 */

#include "ui/widgets/relativepathpicker.h"

#include <QDir>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

class TestRelativePathPicker : public QObject
{
    Q_OBJECT
private slots:
    void emptyByDefault();
    void setAbsoluteShowsAbsoluteWithoutAnchor();
    void setAbsoluteUnderAnchorRendersRelative();
    void setRelativeResolvedAgainstAnchor();
    void changingAnchorRefreshesDisplay();
    void pathChangedSignalOnRealChange();
    void crossVolumeFallsBackToAbsoluteDisplay();
};

using openswmmvis::ui::RelativePathPicker;

void TestRelativePathPicker::emptyByDefault()
{
    RelativePathPicker w;
    QCOMPARE(w.absolutePath(), QString{});
    QCOMPARE(w.displayPath(),  QString{});
    QVERIFY(!w.isDisplayedRelatively());
}

void TestRelativePathPicker::setAbsoluteShowsAbsoluteWithoutAnchor()
{
    RelativePathPicker w;
    w.setPath("/tmp/data/rain.dat");
    QCOMPARE(w.absolutePath(), QStringLiteral("/tmp/data/rain.dat"));
    QCOMPARE(w.displayPath(),  QStringLiteral("/tmp/data/rain.dat"));
    QVERIFY(!w.isDisplayedRelatively());
}

void TestRelativePathPicker::setAbsoluteUnderAnchorRendersRelative()
{
    RelativePathPicker w;
    w.setProjectAnchor("/tmp/proj");
    w.setPath("/tmp/proj/data/rain.dat");
    QCOMPARE(w.absolutePath(), QStringLiteral("/tmp/proj/data/rain.dat"));
    QCOMPARE(w.displayPath(),  QStringLiteral("data/rain.dat"));
    QVERIFY(w.isDisplayedRelatively());
}

void TestRelativePathPicker::setRelativeResolvedAgainstAnchor()
{
    RelativePathPicker w;
    w.setProjectAnchor("/tmp/proj");
    w.setPath("data/rain.dat");
    QCOMPARE(w.absolutePath(), QStringLiteral("/tmp/proj/data/rain.dat"));
    QCOMPARE(w.displayPath(),  QStringLiteral("data/rain.dat"));
}

void TestRelativePathPicker::changingAnchorRefreshesDisplay()
{
    RelativePathPicker w;
    w.setPath("/tmp/proj/data/rain.dat");
    QCOMPARE(w.displayPath(), QStringLiteral("/tmp/proj/data/rain.dat"));
    w.setProjectAnchor("/tmp/proj");
    QCOMPARE(w.displayPath(),  QStringLiteral("data/rain.dat"));
    QCOMPARE(w.absolutePath(), QStringLiteral("/tmp/proj/data/rain.dat"));
}

void TestRelativePathPicker::pathChangedSignalOnRealChange()
{
    RelativePathPicker w;
    QSignalSpy spy(&w, &RelativePathPicker::pathChanged);
    w.setPath("/tmp/a.dat");
    QCOMPARE(spy.count(), 1);
    // Setting the same absolute path again must NOT re-emit.
    w.setPath("/tmp/a.dat");
    QCOMPARE(spy.count(), 1);
    // Setting a different path emits again.
    w.setPath("/tmp/b.dat");
    QCOMPARE(spy.count(), 2);
}

void TestRelativePathPicker::crossVolumeFallsBackToAbsoluteDisplay()
{
    // QDir::relativeFilePath returns the absolute string when paths
    // don't share a volume. The widget must detect that and display
    // the absolute form.
    RelativePathPicker w;
    w.setProjectAnchor("/tmp/proj");
#ifdef Q_OS_WIN
    w.setPath("D:/data/rain.dat");
    QVERIFY(w.displayPath().contains("D:"));
#else
    // On POSIX there's no cross-volume in this sense; emulate by using a
    // truly unrelated absolute root.
    w.setPath("/other/root/rain.dat");
    // Display either falls back to absolute, or shows a (legal) ".."-heavy
    // relative — both are acceptable per the widget's contract. The key
    // invariant is that absolutePath() is untouched.
    QCOMPARE(w.absolutePath(), QStringLiteral("/other/root/rain.dat"));
#endif
}

QTEST_MAIN(TestRelativePathPicker)
#include "test_relativepathpicker.moc"
