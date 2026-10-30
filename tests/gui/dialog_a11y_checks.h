/**
 * @file      dialog_a11y_checks.h
 * @brief     Tests for dialog a11y checks.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef DIALOG_A11Y_CHECKS_H
#define DIALOG_A11Y_CHECKS_H

// UI redesign iteration 2 (D4) — shared dialog accessibility assertions.
// Call swmmvis_test::assertDialogA11y(&dlg) from any dialog test to guard
// the sweep invariants: per-window mnemonic uniqueness, textless buttons
// reachable by assistive tech, and the persistence naming contract
// (dialog objectName + named splitters) that DialogLayoutWatcher keys on.
//
// The helpers use QVERIFY2, so they must run inside a QtTest slot.

#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QAccessible>
#include <QDialog>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QRegularExpression>
#include <QSplitter>
#include <QTabBar>

namespace swmmvis_test
{

/**
 *
 * @brief Extract ASCII Alt-key mnemonic letters after removing escaped ampersands.
 * @param text Borrowed label/title text, copied for parsing.
 * @return Owned lowercase letters in occurrence order; duplicates remain. Matches
 * only ampersand followed by A-Z/a-z, without Unicode mnemonic normalization.
 *
 */
inline QStringList mnemonicLetters(const QString &text)
{
    QString t = text;
    t.replace(QStringLiteral("&&"), QString()); // literal ampersands
    QStringList out;
    static const QRegularExpression rx(QStringLiteral("&([A-Za-z])"));
    auto it = rx.globalMatch(t);
    while (it.hasNext())
        out << it.next().captured(1).toLower();
    return out;
}

/// No two mnemonics in the window may claim the same Alt+letter.
/**
 *
 * @brief Assert unique mnemonic letters across descendant labels, buttons, group boxes and tabs.
 * @param root Borrowed non-null widget whose entire descendant tree is scanned,
 * including hidden controls; the root widget itself is not claimed.
 * Run inside QtTest. The local claim callback borrows its text/holder description
 * and records letters in a shared map; a failed QVERIFY returns from that callback
 * only, so scanning can continue and report further failures.
 *
 */
inline void assertMnemonicsUnique(QWidget *root)
{
    QHash<QString, QString> seen; // letter -> holder description
    const auto claim = [&](const QString &text, const QString &what) {
        const QStringList letters = mnemonicLetters(text);
        for (const QString &letter : letters) {
            QVERIFY2(!seen.contains(letter),
                     qPrintable(QStringLiteral("mnemonic '&%1' used by both [%2] and [%3]")
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

/// Textless buttons, including painted controls without a QIcon, must expose
/// a name through Qt's accessibility interface. Explicit names, label buddies
/// and action labels qualify when Qt exposes them; tooltips alone do not.
/**
 *
 * @brief Assert textless application buttons expose a nonempty Qt accessibility name.
 * @param root Borrowed non-null widget whose descendant buttons are inspected.
 * Skips qt_-named and known internal line-edit/toolbar/table buttons; visible text
 * exempts a button. Requires an active QtTest context and does not mutate controls.
 *
 */
inline void assertIconButtonsNamed(QWidget *root)
{
    const auto buttons = root->findChildren<QAbstractButton *>();
    for (QAbstractButton *b : buttons) {
        // Qt-internal helper buttons (line-edit clear buttons, toolbar
        // extension chevrons, …) are outside the sweep's scope.
        const QLatin1String cls(b->metaObject()->className());
        if (b->objectName().startsWith(QLatin1String("qt_")) ||
            cls == QLatin1String("QLineEditIconButton") ||
            cls == QLatin1String("QToolBarExtension") || cls == QLatin1String("QTableCornerButton"))
            continue;
        if (!b->text().trimmed().isEmpty())
            continue;
        QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(b);
        const bool named = accessible && !accessible->text(QAccessible::Name).trimmed().isEmpty();
        QVERIFY2(named,
                 qPrintable(
                     QStringLiteral("textless button '%1' (%2) exposes no accessible Name")
                         .arg(b->objectName(), QString::fromLatin1(b->metaObject()->className()))));
    }
}

/// The layout-persistence contract: the dialog and every splitter carry
/// objectNames (DialogLayoutWatcher keys on them).
/**
 *
 * @brief Assert dialog and descendant splitters have nonempty persistence object names.
 * @param dlg Borrowed non-null dialog. Does not verify uniqueness, save settings
 * or prove restoration. A failed QVERIFY returns from this helper only.
 *
 */
inline void assertPersistenceNaming(QDialog *dlg)
{
    QVERIFY2(!dlg->objectName().isEmpty(),
             "dialog has no objectName — layout persistence silently off");
    const auto splitters = dlg->findChildren<QSplitter *>();
    for (const QSplitter *sp : splitters) {
        QVERIFY2(!sp->objectName().isEmpty(),
                 qPrintable(QStringLiteral("unnamed QSplitter in '%1' — its state won't persist")
                                .arg(dlg->objectName())));
    }
}

/// A lone '&' is a mnemonic marker. On a QLabel without a buddy Qt shows it
/// literally ("&CRS"); on a group box title before a space it is swallowed
/// ("Selection Pens Fills"). Literal ampersands must be written "&&".
/**
 *
 * @brief Assert plain-label mnemonic markers have buddies and group titles do not swallow
 * ampersand-space.
 * @param root Borrowed non-null widget whose descendant labels/group boxes are
 * scanned. Skips rich-text labels, accepts escaped ampersands and records QtTest
 * failures; it does not rewrite text or assign buddies.
 *
 */
inline void assertNoStrayAmpersands(QWidget *root)
{
    static const QRegularExpression lone(QStringLiteral("(^|[^&])&([^&]|$)"));
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->textFormat() == Qt::RichText || Qt::mightBeRichText(label->text()))
            continue;
        if (lone.match(label->text()).hasMatch())
            QVERIFY2(
                label->buddy(),
                qPrintable(QStringLiteral(
                               "label \"%1\" shows a literal '&': give it a buddy or write '&&'")
                               .arg(label->text())));
    }
    static const QRegularExpression swallowed(QStringLiteral("(^|[^&])& "));
    for (QGroupBox *box : root->findChildren<QGroupBox *>())
        QVERIFY2(
            !swallowed.match(box->title()).hasMatch(),
            qPrintable(
                QStringLiteral("group box \"%1\" loses its '&': write '&&'").arg(box->title())));
}

/**
 *
 * @brief Run mnemonic, ampersand, textless-button and persistence-name assertions.
 * @param dlg Borrowed non-null dialog; call inside an active QtTest case.
 * Child assertions may record failures and return locally; later categories still
 * run. This checks the shared conventions rather than all accessibility behavior.
 *
 */
inline void assertDialogA11y(QDialog *dlg)
{
    assertMnemonicsUnique(dlg);
    assertNoStrayAmpersands(dlg);
    assertIconButtonsNamed(dlg);
    assertPersistenceNaming(dlg);
}

} // namespace swmmvis_test

#endif // DIALOG_A11Y_CHECKS_H
