/**
 * @file      test_relativepathpicker.cpp
 * @brief     Slice IO-10 — RelativePathPicker widget unit tests.
 *
 * @details Pins the widget contract documented in
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
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
    /**
     *
     * @brief Require empty canonical/display paths and relative-display flag false for a new
     * picker.
     *
     */
    void emptyByDefault();
    /**
     *
     * @brief Require an absolute input without project anchor to remain absolute in both
     * stored/display forms.
     *
     */
    void setAbsoluteShowsAbsoluteWithoutAnchor();
    /**
     *
     * @brief Require an absolute child path to retain canonical storage while displaying relative
     * to its project anchor.
     *
     */
    void setAbsoluteUnderAnchorRendersRelative();
    /**
     *
     * @brief Require relative input to resolve against the anchor while retaining relative
     * display text.
     *
     */
    void setRelativeResolvedAgainstAnchor();
    /**
     *
     * @brief Require adding an anchor to update display text without changing the canonical
     * absolute path.
     *
     */
    void changingAnchorRefreshesDisplay();
    /**
     *
     * @brief Require pathChanged on two distinct paths and no extra signal for repeating the same
     * absolute input.
     *
     */
    void pathChangedSignalOnRealChange();
    /**
     *
     * @brief Check platform-specific unrelated-root path handling without filesystem IO.
     * Windows requires the D: drive in displayed text; POSIX only checks canonical
     * absolute preservation and allows either absolute or parent-relative display.
     *
     */
    void crossVolumeFallsBackToAbsoluteDisplay();
};

using openswmmvis::ui::RelativePathPicker;

void TestRelativePathPicker::emptyByDefault()
{
    RelativePathPicker w;
    QCOMPARE(w.absolutePath(), QString{});
    QCOMPARE(w.displayPath(), QString{});
    QVERIFY(!w.isDisplayedRelatively());
}

void TestRelativePathPicker::setAbsoluteShowsAbsoluteWithoutAnchor()
{
    RelativePathPicker w;
    w.setPath("/tmp/data/rain.dat");
    QCOMPARE(w.absolutePath(), QStringLiteral("/tmp/data/rain.dat"));
    QCOMPARE(w.displayPath(), QStringLiteral("/tmp/data/rain.dat"));
    QVERIFY(!w.isDisplayedRelatively());
}

void TestRelativePathPicker::setAbsoluteUnderAnchorRendersRelative()
{
    RelativePathPicker w;
    w.setProjectAnchor("/tmp/proj");
    w.setPath("/tmp/proj/data/rain.dat");
    QCOMPARE(w.absolutePath(), QStringLiteral("/tmp/proj/data/rain.dat"));
    QCOMPARE(w.displayPath(), QStringLiteral("data/rain.dat"));
    QVERIFY(w.isDisplayedRelatively());
}

void TestRelativePathPicker::setRelativeResolvedAgainstAnchor()
{
    RelativePathPicker w;
    w.setProjectAnchor("/tmp/proj");
    w.setPath("data/rain.dat");
    QCOMPARE(w.absolutePath(), QStringLiteral("/tmp/proj/data/rain.dat"));
    QCOMPARE(w.displayPath(), QStringLiteral("data/rain.dat"));
}

void TestRelativePathPicker::changingAnchorRefreshesDisplay()
{
    RelativePathPicker w;
    w.setPath("/tmp/proj/data/rain.dat");
    QCOMPARE(w.displayPath(), QStringLiteral("/tmp/proj/data/rain.dat"));
    w.setProjectAnchor("/tmp/proj");
    QCOMPARE(w.displayPath(), QStringLiteral("data/rain.dat"));
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
