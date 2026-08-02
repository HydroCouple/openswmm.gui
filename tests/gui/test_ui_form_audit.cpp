// UI redesign P0 — standalone audit of forms/swmmvis.ui.
//
// The form declares MapCanvas as a <customwidget> but never instantiates
// one, so the generated Ui::SWMMVis can be set up on a bare QMainWindow
// with no app linkage. This guards the action table against the Designer
// copy-paste defect family that shipped six dead shortcuts (word strings
// like "Copy" parse to hardware media keys such as Qt::Key_Copy, which
// never fire on a normal keyboard) and against silent shortcut
// collisions as the catalog grows.
#include <QtTest/QtTest>

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QSet>

#include "ui_swmmvis.h"

class TestUiFormAudit : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void actionsHaveText();
    void noMediaKeyShortcuts();
    void noDuplicateShortcuts();
    void repairedShortcutsAnchor();
    void menuBarTitlesHaveMnemonics();
    void menusAreComplete();
    void everyActionIsMenuReachable();
    void actionsHaveTooltips();

private:
    QList<QAction *> formActions() const;

    QMainWindow *mWindow = nullptr;
    Ui::SWMMVis mUi;
};

void TestUiFormAudit::initTestCase()
{
    mWindow = new QMainWindow;
    mUi.setupUi(mWindow);
}

void TestUiFormAudit::cleanupTestCase()
{
    delete mWindow;
    mWindow = nullptr;
}

QList<QAction *> TestUiFormAudit::formActions() const
{
    // Every action authored in the .ui carries an objectName; unnamed
    // actions found under the window are Qt-internal helpers (menu bar,
    // line-edit clear buttons, ...) outside this audit's scope.
    QList<QAction *> out;
    const auto all = mWindow->findChildren<QAction *>();
    for (QAction *a : all) {
        if (!a->isSeparator() && !a->objectName().isEmpty())
            out.append(a);
    }
    return out;
}

void TestUiFormAudit::actionsHaveText()
{
    const auto actions = formActions();
    QVERIFY(actions.size() >= 76);   // the .ui action table; grows, never shrinks silently
    for (const QAction *a : actions) {
        QVERIFY2(!a->text().isEmpty(),
                 qPrintable(QStringLiteral("QAction '%1' has empty text")
                                .arg(a->objectName())));
    }
}

void TestUiFormAudit::noMediaKeyShortcuts()
{
    // The exact keys the historical bug produced, plus the rest of the
    // device/media-key family a bare-word shortcut string can parse into.
    static const QSet<int> kDeviceKeys = {
        Qt::Key_Copy,    Qt::Key_Cut,     Qt::Key_Paste,   Qt::Key_Search,
        Qt::Key_Execute, Qt::Key_Select,  Qt::Key_ZoomIn,  Qt::Key_ZoomOut,
        Qt::Key_Zoom,    Qt::Key_Open,    Qt::Key_Save,    Qt::Key_New,
        Qt::Key_Undo,    Qt::Key_Redo,    Qt::Key_Print,   Qt::Key_Play,
        Qt::Key_Find,    Qt::Key_Close,   Qt::Key_Exit,    Qt::Key_Cancel,
    };
    for (const QAction *a : formActions()) {
        const QKeySequence seq = a->shortcut();
        for (int i = 0; i < seq.count(); ++i) {
            const int key = seq[i].key();
            QVERIFY2(!kDeviceKeys.contains(key),
                     qPrintable(QStringLiteral(
                         "QAction '%1' shortcut '%2' resolves to a hardware "
                         "media key and will never fire on a normal keyboard")
                             .arg(a->objectName(), seq.toString())));
        }
    }
}

void TestUiFormAudit::noDuplicateShortcuts()
{
    QHash<QString, QString> seen;   // portable sequence -> objectName
    for (const QAction *a : formActions()) {
        const QString seq = a->shortcut().toString(QKeySequence::PortableText);
        if (seq.isEmpty())
            continue;
        QVERIFY2(!seen.contains(seq),
                 qPrintable(QStringLiteral(
                     "Shortcut '%1' bound to both '%2' and '%3'")
                         .arg(seq, seen.value(seq), a->objectName())));
        seen.insert(seq, a->objectName());
    }
}

void TestUiFormAudit::repairedShortcutsAnchor()
{
    // Regression anchors for the six shortcuts repaired in P0. Qt maps
    // Ctrl to Cmd on macOS at runtime, so string-built sequences compare
    // consistently across platforms.
    QCOMPARE(mUi.actionCopy->shortcut(),    QKeySequence(QStringLiteral("Ctrl+C")));
    QCOMPARE(mUi.actionSearch->shortcut(),  QKeySequence(QStringLiteral("Ctrl+F")));
    QCOMPARE(mUi.actionExecute->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));
    QCOMPARE(mUi.actionZoomIn->shortcut(),  QKeySequence(QStringLiteral("Ctrl++")));
    QCOMPARE(mUi.actionZoomOut->shortcut(), QKeySequence(QStringLiteral("Ctrl+-")));
    QVERIFY(mUi.actionSelect->shortcut().isEmpty());

    // The seven that always worked stay pinned too.
    QCOMPARE(mUi.actionNew->shortcut(),      QKeySequence(QStringLiteral("Ctrl+N")));
    QCOMPARE(mUi.actionOpen->shortcut(),     QKeySequence(QStringLiteral("Ctrl+O")));
    QCOMPARE(mUi.actionSave->shortcut(),     QKeySequence(QStringLiteral("Ctrl+S")));
    QCOMPARE(mUi.actionSaveAs->shortcut(),   QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    QCOMPARE(mUi.actionPrint->shortcut(),    QKeySequence(QStringLiteral("Ctrl+P")));
    QCOMPARE(mUi.actionSettings->shortcut(), QKeySequence(QStringLiteral("Ctrl+,")));
    QCOMPARE(mUi.actionExit->shortcut(),     QKeySequence(QStringLiteral("Ctrl+Q")));
}

void TestUiFormAudit::menuBarTitlesHaveMnemonics()
{
    // P5 — every top-level menu is Alt-key reachable.
    const auto topActions = mUi.menubarMain->actions();
    QVERIFY(topActions.size() >= 8);   // File Edit View Model Analysis Results Tools Help
    for (const QAction *a : topActions) {
        if (a->isSeparator())
            continue;
        QVERIFY2(a->text().contains(QLatin1Char('&')),
                 qPrintable(QStringLiteral("menu '%1' has no mnemonic")
                                .arg(a->text())));
    }
}

void TestUiFormAudit::menusAreComplete()
{
    // P5 — the historically-empty menus now carry the full IA.
    QVERIFY(mUi.menuEdit->actions().size() >= 8);
    QVERIFY(mUi.menuView->actions().size() >= 8);
    QVERIFY(mUi.menuModel->actions().size() >= 10);
    QVERIFY(mUi.menuAnalysis->actions().size() >= 12);
    QVERIFY(mUi.menuResults->actions().size() >= 6);
    QVERIFY(mUi.menuImport->actions().size() >= 6);
}

void TestUiFormAudit::everyActionIsMenuReachable()
{
    // P5 — no capability is toolbar-only: every .ui-authored action sits
    // in at least one QMenu (runtime code adds more on top: Panels,
    // Appearance, Add Data Object, Window, Mesh, Undo/Redo).
    for (QAction *a : formActions()) {
        bool inMenu = false;
        const auto objects = a->associatedObjects();
        for (QObject *o : objects) {
            if (qobject_cast<QMenu *>(o)) {
                inMenu = true;
                break;
            }
        }
        QVERIFY2(inMenu,
                 qPrintable(QStringLiteral("QAction '%1' is not in any menu")
                                .arg(a->objectName())));
    }
}

void TestUiFormAudit::actionsHaveTooltips()
{
    // P9 — tooltips are the broadest assistive text channel; every
    // named action must carry one (Qt derives it from text unless a
    // site explicitly cleared it).
    for (const QAction *a : formActions()) {
        QVERIFY2(!a->toolTip().isEmpty(),
                 qPrintable(QStringLiteral("QAction '%1' has empty tooltip")
                                .arg(a->objectName())));
    }
}

QTEST_MAIN(TestUiFormAudit)
#include "test_ui_form_audit.moc"
