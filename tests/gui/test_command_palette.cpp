// UI redesign P7 — command palette: fuzzy-score ranking invariants,
// model filtering over registry-registered actions, end-to-end
// type-filter-Enter triggering (offscreen keyClicks), Esc dismissal,
// and disabled actions not triggerable.
#include <QtTest/QtTest>

#include <QAction>
#include <QLineEdit>
#include <QListView>
#include <QMainWindow>
#include <QSignalSpy>

#include "ui/actionregistry.h"
#include "ui/widgets/commandpalette.h"
#include "ui/widgets/commandpalettemodel.h"

using openswmmvis::ui::ActionRegistry;
using openswmmvis::ui::CommandPalette;
using openswmmvis::ui::CommandPaletteModel;
using openswmmvis::ui::fuzzyScore;

class TestCommandPalette : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void fuzzyScoreRanking();
    void modelFiltersAndSorts();
    void enterTriggersFilteredAction();
    void escapeCloses();
    void disabledActionNotTriggerable();

private:
    QMainWindow *mWindow = nullptr;
    QAction *mRun = nullptr;       // "Execute" (sim.run)
    QAction *mSaveAs = nullptr;    // "Save As…" (file.saveAs), disabled
};

void TestCommandPalette::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("commandpalette-test"));

    mWindow = new QMainWindow;
    mRun = new QAction(QStringLiteral("&Execute"), mWindow);
    mRun->setObjectName(QStringLiteral("actionExecute"));
    mSaveAs = new QAction(QStringLiteral("Save &As…"), mWindow);
    mSaveAs->setObjectName(QStringLiteral("actionSaveAs"));
    mSaveAs->setEnabled(false);
    ActionRegistry::instance()->registerFromCatalog(mWindow);
    QVERIFY(ActionRegistry::instance()->action(QStringLiteral("sim.run")) == mRun);
}

void TestCommandPalette::fuzzyScoreRanking()
{
    // Prefix beats word-boundary beats scattered subsequence.
    const int prefix    = fuzzyScore(QStringLiteral("exe"), QStringLiteral("Execute"));
    const int wordStart = fuzzyScore(QStringLiteral("exe"), QStringLiteral("Run Executable"));
    const int scattered = fuzzyScore(QStringLiteral("exe"), QStringLiteral("Trend Explorer Zone"));
    QVERIFY(prefix > wordStart);
    QVERIFY(wordStart > scattered);

    // Case-insensitive; non-matches are rejected.
    QVERIFY(fuzzyScore(QStringLiteral("EXE"), QStringLiteral("execute")) > 0);
    QCOMPARE(fuzzyScore(QStringLiteral("xyz"), QStringLiteral("Execute")), -1);
    // Empty pattern matches everything neutrally.
    QCOMPARE(fuzzyScore(QString(), QStringLiteral("anything")), 0);
}

void TestCommandPalette::modelFiltersAndSorts()
{
    CommandPaletteModel model;
    model.reload();
    QVERIFY(model.rowCount() >= 2);   // both registered actions, ampersands stripped

    model.setFilterPattern(QStringLiteral("execute"));
    QVERIFY(model.rowCount() >= 1);
    QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(),
             QStringLiteral("Execute"));
    QCOMPARE(model.actionAt(model.index(0, 0)), mRun);

    model.setFilterPattern(QStringLiteral("no such command zz"));
    QCOMPARE(model.rowCount(), 0);
}

void TestCommandPalette::enterTriggersFilteredAction()
{
    CommandPalette palette(mWindow);
    palette.popup();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));

    QSignalSpy spy(mRun, &QAction::triggered);
    QTest::keyClicks(palette.filterEdit(), QStringLiteral("execute"));
    QTest::keyClick(palette.filterEdit(), Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QVERIFY(palette.isHidden());
}

void TestCommandPalette::escapeCloses()
{
    CommandPalette palette(mWindow);
    palette.popup();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));
    QTest::keyClick(palette.filterEdit(), Qt::Key_Escape);
    QVERIFY(palette.isHidden());
}

void TestCommandPalette::disabledActionNotTriggerable()
{
    CommandPalette palette(mWindow);
    palette.popup();
    QVERIFY(QTest::qWaitForWindowExposed(&palette));

    QSignalSpy spy(mSaveAs, &QAction::triggered);
    QTest::keyClicks(palette.filterEdit(), QStringLiteral("save as"));
    QTest::keyClick(palette.filterEdit(), Qt::Key_Return);
    QCOMPARE(spy.count(), 0);          // grayed row, Enter refuses
    QVERIFY(!palette.isHidden());      // palette stays up for another pick
}

QTEST_MAIN(TestCommandPalette)
#include "test_command_palette.moc"
