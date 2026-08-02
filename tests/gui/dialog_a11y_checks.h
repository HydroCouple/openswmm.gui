#ifndef DIALOG_A11Y_CHECKS_H
#define DIALOG_A11Y_CHECKS_H

// UI redesign iteration 2 (D4) — shared dialog accessibility assertions.
// Call swmmvis_test::assertDialogA11y(&dlg) from any dialog test to guard
// the sweep invariants: per-window mnemonic uniqueness, icon-only buttons
// reachable by assistive tech, and the persistence naming contract
// (dialog objectName + named splitters) that DialogLayoutWatcher keys on.
//
// The helpers use QVERIFY2, so they must run inside a QtTest slot.

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QDialog>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabBar>
#include <QToolButton>

namespace swmmvis_test {

inline QStringList mnemonicLetters(const QString &text)
{
    QString t = text;
    t.replace(QStringLiteral("&&"), QString());   // literal ampersands
    QStringList out;
    static const QRegularExpression rx(QStringLiteral("&([A-Za-z])"));
    auto it = rx.globalMatch(t);
    while (it.hasNext())
        out << it.next().captured(1).toLower();
    return out;
}

/// No two mnemonics in the window may claim the same Alt+letter.
inline void assertMnemonicsUnique(QWidget *root)
{
    QHash<QString, QString> seen;   // letter -> holder description
    const auto claim = [&](const QString &text, const QString &what) {
        const QStringList letters = mnemonicLetters(text);
        for (const QString &letter : letters) {
            QVERIFY2(!seen.contains(letter),
                     qPrintable(QStringLiteral(
                         "mnemonic '&%1' used by both [%2] and [%3]")
                             .arg(letter, seen.value(letter), what)));
            seen.insert(letter, what);
        }
    };
    const auto labels = root->findChildren<QLabel *>();
    for (const QLabel *w : labels)
        claim(w->text(), QStringLiteral("label ") + w->text());
    const auto buttons = root->findChildren<QAbstractButton *>();
    for (const QAbstractButton *w : buttons)
        claim(w->text(), QStringLiteral("button ") + w->text());
    const auto boxes = root->findChildren<QGroupBox *>();
    for (const QGroupBox *w : boxes)
        claim(w->title(), QStringLiteral("group ") + w->title());
    const auto tabBars = root->findChildren<QTabBar *>();
    for (const QTabBar *bar : tabBars)
        for (int i = 0; i < bar->count(); ++i)
            claim(bar->tabText(i), QStringLiteral("tab ") + bar->tabText(i));
}

/// Icon-only buttons must be reachable by assistive tech: an accessible
/// name, a tooltip, or a defaultAction carrying text.
inline void assertIconButtonsNamed(QWidget *root)
{
    const auto buttons = root->findChildren<QAbstractButton *>();
    for (const QAbstractButton *b : buttons) {
        // Qt-internal helper buttons (line-edit clear buttons, toolbar
        // extension chevrons, …) are outside the sweep's scope.
        const QLatin1String cls(b->metaObject()->className());
        if (b->objectName().startsWith(QLatin1String("qt_"))
            || cls == QLatin1String("QLineEditIconButton")
            || cls == QLatin1String("QToolBarExtension"))
            continue;
        if (!b->text().isEmpty() || b->icon().isNull())
            continue;
        bool named = !b->accessibleName().isEmpty() || !b->toolTip().isEmpty();
        if (!named) {
            if (auto *tb = qobject_cast<const QToolButton *>(b))
                named = tb->defaultAction() && !tb->defaultAction()->text().isEmpty();
        }
        QVERIFY2(named,
                 qPrintable(QStringLiteral(
                     "icon-only button '%1' has no accessible name, tooltip "
                     "or default action").arg(b->objectName())));
    }
}

/// The layout-persistence contract: the dialog and every splitter carry
/// objectNames (DialogLayoutWatcher keys on them).
inline void assertPersistenceNaming(QDialog *dlg)
{
    QVERIFY2(!dlg->objectName().isEmpty(),
             "dialog has no objectName — layout persistence silently off");
    const auto splitters = dlg->findChildren<QSplitter *>();
    for (const QSplitter *sp : splitters) {
        QVERIFY2(!sp->objectName().isEmpty(),
                 qPrintable(QStringLiteral(
                     "unnamed QSplitter in '%1' — its state won't persist")
                         .arg(dlg->objectName())));
    }
}

inline void assertDialogA11y(QDialog *dlg)
{
    assertMnemonicsUnique(dlg);
    assertIconButtonsNamed(dlg);
    assertPersistenceNaming(dlg);
}

}   // namespace swmmvis_test

#endif // DIALOG_A11Y_CHECKS_H
