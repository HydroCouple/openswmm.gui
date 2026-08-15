// UI redesign P8 — shortcut editor: assigning persists through the
// registry (live QAction + QSettings) and bolds the row, an exact
// duplicate of another command's binding hard-blocks Assign naming the
// holder, reserved sequences only warn, and reset one/all restores
// catalog defaults.
#include <QtTest/QtTest>

#include <QAction>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>

#include "ui/actionregistry.h"
#include "ui/widgets/shortcuteditorwidget.h"

using openswmmvis::ui::ActionRegistry;
using openswmmvis::ui::ShortcutEditorWidget;

class TestShortcutEditor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void assignPersistsAndApplies();
    void duplicateBindingBlocked();
    void reservedSequenceWarnsOnly();
    void resetRestoresDefault();
    void resetAllClearsEveryOverride();

private:
    void wipeShortcutSettings();

    QMainWindow *mWindow = nullptr;
    QAction *mRun = nullptr;      // sim.run,  default Ctrl+R
    QAction *mMeasure = nullptr;  // map.measure, default Ctrl+Shift+M
};

void TestShortcutEditor::wipeShortcutSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Shortcuts"));
    settings.remove(QString());
    settings.endGroup();
}

void TestShortcutEditor::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
    QCoreApplication::setApplicationName(QStringLiteral("shortcuteditor-test"));
    wipeShortcutSettings();

    mWindow = new QMainWindow;
    mRun = new QAction(QStringLiteral("&Execute"), mWindow);
    mRun->setObjectName(QStringLiteral("actionExecute"));
    mMeasure = new QAction(QStringLiteral("&Measure"), mWindow);
    mMeasure->setObjectName(QStringLiteral("actionMeasure"));
    ActionRegistry::instance()->registerFromCatalog(mWindow);
}

void TestShortcutEditor::cleanupTestCase()
{
    wipeShortcutSettings();
    delete mWindow;
}

void TestShortcutEditor::assignPersistsAndApplies()
{
    ShortcutEditorWidget editor;
    editor.selectCommand(QStringLiteral("sim.run"));
    QCOMPARE(editor.selectedCommandId(), QStringLiteral("sim.run"));

    editor.sequenceEdit()->setKeySequence(QKeySequence(QStringLiteral("Ctrl+7")));
    QVERIFY(editor.assignButton()->isEnabled());
    editor.assignButton()->click();

    QCOMPARE(mRun->shortcut(), QKeySequence(QStringLiteral("Ctrl+7")));
    QVERIFY(ActionRegistry::instance()->hasUserShortcut(QStringLiteral("sim.run")));
    QSettings settings;
    settings.beginGroup(QStringLiteral("SWMMVis::Shortcuts"));
    QCOMPARE(settings.value(QStringLiteral("sim.run")).toString(),
             QStringLiteral("Ctrl+7"));
    settings.endGroup();

    // Row bolded as a custom binding.
    editor.selectCommand(QStringLiteral("sim.run"));
    QVERIFY(editor.tree()->currentItem()->font(1).bold());
}

void TestShortcutEditor::duplicateBindingBlocked()
{
    ShortcutEditorWidget editor;
    editor.selectCommand(QStringLiteral("map.measure"));
    // Ctrl+7 currently belongs to sim.run (previous test).
    editor.sequenceEdit()->setKeySequence(QKeySequence(QStringLiteral("Ctrl+7")));
    QVERIFY(!editor.assignButton()->isEnabled());
    QVERIFY(editor.conflictLabel()->text().contains(QStringLiteral("Execute")));
}

void TestShortcutEditor::reservedSequenceWarnsOnly()
{
    ShortcutEditorWidget editor;
    editor.selectCommand(QStringLiteral("map.measure"));
    editor.sequenceEdit()->setKeySequence(QKeySequence(QStringLiteral("Ctrl+W")));
    QVERIFY(editor.assignButton()->isEnabled());   // warned, not blocked
    QVERIFY(!editor.conflictLabel()->text().isEmpty());
}

void TestShortcutEditor::resetRestoresDefault()
{
    ShortcutEditorWidget editor;
    editor.selectCommand(QStringLiteral("sim.run"));
    QVERIFY(editor.resetButton()->isEnabled());
    editor.resetButton()->click();
    QCOMPARE(mRun->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
    QVERIFY(!ActionRegistry::instance()->hasUserShortcut(QStringLiteral("sim.run")));
}

void TestShortcutEditor::resetAllClearsEveryOverride()
{
    auto *registry = ActionRegistry::instance();
    registry->setUserShortcut(QStringLiteral("sim.run"),
                              QKeySequence(QStringLiteral("Ctrl+8")));
    registry->setUserShortcut(QStringLiteral("map.measure"),
                              QKeySequence(QStringLiteral("Ctrl+9")));

    ShortcutEditorWidget editor;
    editor.resetAllButton()->click();

    QVERIFY(!registry->hasUserShortcut(QStringLiteral("sim.run")));
    QVERIFY(!registry->hasUserShortcut(QStringLiteral("map.measure")));
    QCOMPARE(mRun->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
    QCOMPARE(mMeasure->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+M")));
}

QTEST_MAIN(TestShortcutEditor)
#include "test_shortcut_editor.moc"
