/*!
 * \file   dialoglayoutpersistence.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/dialoglayoutpersistence.h"

#include <QAction>
#include <QByteArray>
#include <QDialog>
#include <QGuiApplication>
#include <QHeaderView>
#include <QList>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QWidget>

namespace openswmmvis::ui {

namespace {

/// Deliberate opt-in: only children the dialog author explicitly named
/// persist; Qt-internal names ("qt_tabwidget_stackedwidget", …) never do.
bool userNamed_(const QString &name)
{
    return !name.isEmpty() && !name.startsWith(QLatin1String("qt_"));
}

QString rootGroupName_(QWidget *root)
{
    if (!root) return {};
    QWidget *w = root->isWindow() ? root : root->window();
    return w ? w->objectName() : QString();
}

QRect clampToAvailableScreens_(const QRect &saved)
{
    // If the saved rect's center lies on a still-connected screen, keep it.
    // Otherwise clamp into the primary screen's available geometry so the
    // dialog at least lands somewhere the user can see + drag.
    for (const QScreen *s : QGuiApplication::screens()) {
        if (s && s->availableGeometry().contains(saved.center()))
            return saved;
    }
    const QScreen *primary = QGuiApplication::primaryScreen();
    if (!primary) return saved;
    const QRect avail = primary->availableGeometry();
    QRect r = saved;
    if (r.width()  > avail.width())  r.setWidth(avail.width());
    if (r.height() > avail.height()) r.setHeight(avail.height());
    r.moveCenter(avail.center());
    return r;
}

} // namespace

void saveDialogLayout(QWidget *root)
{
    const QString name = rootGroupName_(root);
    if (name.isEmpty()) return;

    QSettings s;
    s.beginGroup(QStringLiteral("Dialogs"));
    s.beginGroup(name);
    s.setValue(QStringLiteral("geometry"), root->window()->geometry());

    s.beginGroup(QStringLiteral("splitter"));
    const QList<QSplitter *> splitters = root->findChildren<QSplitter *>();
    for (QSplitter *sp : splitters) {
        const QString spName = sp ? sp->objectName() : QString();
        if (!userNamed_(spName)) continue;    // opt-in via objectName only
        s.setValue(spName, sp->saveState());
    }
    s.endGroup();   // splitter

    // Iteration 2 (D1) — the richer view/layout state, same naming rule.
    s.beginGroup(QStringLiteral("header"));
    const QList<QTableView *> tables = root->findChildren<QTableView *>();
    for (QTableView *v : tables) {
        if (!userNamed_(v->objectName())) continue;
        if (QHeaderView *h = v->horizontalHeader())
            s.setValue(v->objectName(), h->saveState());
    }
    const QList<QTreeView *> trees = root->findChildren<QTreeView *>();
    for (QTreeView *v : trees) {
        if (!userNamed_(v->objectName())) continue;
        if (QHeaderView *h = v->header())
            s.setValue(v->objectName(), h->saveState());
    }
    s.endGroup();   // header

    s.beginGroup(QStringLiteral("tab"));
    const QList<QTabWidget *> tabs = root->findChildren<QTabWidget *>();
    for (QTabWidget *t : tabs) {
        if (!userNamed_(t->objectName())) continue;
        s.setValue(t->objectName(), t->currentIndex());
    }
    s.endGroup();   // tab

    s.beginGroup(QStringLiteral("page"));
    const QList<QStackedWidget *> stacks = root->findChildren<QStackedWidget *>();
    for (QStackedWidget *st : stacks) {
        if (!userNamed_(st->objectName())) continue;
        s.setValue(st->objectName(), st->currentIndex());
    }
    s.endGroup();   // page

    s.beginGroup(QStringLiteral("toggle"));
    const QList<QAction *> actions = root->findChildren<QAction *>();
    for (QAction *a : actions) {
        if (!a->isCheckable() || !userNamed_(a->objectName())) continue;
        s.setValue(a->objectName(), a->isChecked());
    }
    s.endGroup();   // toggle
    s.endGroup();   // <name>
    s.endGroup();   // Dialogs
}

bool restoreDialogLayout(QWidget *root)
{
    const QString name = rootGroupName_(root);
    if (name.isEmpty()) return false;

    QSettings s;
    s.beginGroup(QStringLiteral("Dialogs"));
    s.beginGroup(name);
    const bool hadGeometry = s.contains(QStringLiteral("geometry"));
    bool restored = false;

    if (hadGeometry) {
        const QRect saved = s.value(QStringLiteral("geometry")).toRect();
        if (saved.isValid()) {
            root->window()->setGeometry(clampToAvailableScreens_(saved));
            restored = true;
        }
    }

    s.beginGroup(QStringLiteral("splitter"));
    const QList<QSplitter *> splitters = root->findChildren<QSplitter *>();
    for (QSplitter *sp : splitters) {
        const QString spName = sp ? sp->objectName() : QString();
        if (!userNamed_(spName)) continue;
        const QByteArray state = s.value(spName).toByteArray();
        if (!state.isEmpty() && sp->restoreState(state))
            restored = true;
    }
    s.endGroup();   // splitter

    // Iteration 2 (D1) — richer state, restored after geometry/splitters.
    // QHeaderView::restoreState rejects blobs from a different column
    // model; the int indices are bounds-checked, so stale settings from a
    // reworked dialog degrade to first-run defaults instead of misfiring.
    s.beginGroup(QStringLiteral("header"));
    const QList<QTableView *> tables = root->findChildren<QTableView *>();
    for (QTableView *v : tables) {
        if (!userNamed_(v->objectName())) continue;
        const QByteArray state = s.value(v->objectName()).toByteArray();
        QHeaderView *h = v->horizontalHeader();
        if (h && !state.isEmpty() && h->restoreState(state))
            restored = true;
    }
    const QList<QTreeView *> trees = root->findChildren<QTreeView *>();
    for (QTreeView *v : trees) {
        if (!userNamed_(v->objectName())) continue;
        const QByteArray state = s.value(v->objectName()).toByteArray();
        QHeaderView *h = v->header();
        if (h && !state.isEmpty() && h->restoreState(state))
            restored = true;
    }
    s.endGroup();   // header

    s.beginGroup(QStringLiteral("tab"));
    const QList<QTabWidget *> tabs = root->findChildren<QTabWidget *>();
    for (QTabWidget *t : tabs) {
        if (!userNamed_(t->objectName())) continue;
        const int idx = s.value(t->objectName(), -1).toInt();
        if (idx >= 0 && idx < t->count()) {
            t->setCurrentIndex(idx);
            restored = true;
        }
    }
    s.endGroup();   // tab

    s.beginGroup(QStringLiteral("page"));
    const QList<QStackedWidget *> stacks = root->findChildren<QStackedWidget *>();
    for (QStackedWidget *st : stacks) {
        if (!userNamed_(st->objectName())) continue;
        const int idx = s.value(st->objectName(), -1).toInt();
        if (idx >= 0 && idx < st->count()) {
            st->setCurrentIndex(idx);
            restored = true;
        }
    }
    s.endGroup();   // page

    s.beginGroup(QStringLiteral("toggle"));
    const QList<QAction *> actions = root->findChildren<QAction *>();
    for (QAction *a : actions) {
        if (!a->isCheckable() || !userNamed_(a->objectName())) continue;
        if (!s.contains(a->objectName())) continue;
        // setChecked fires toggled() so dependent views update themselves.
        a->setChecked(s.value(a->objectName()).toBool());
        restored = true;
    }
    s.endGroup();   // toggle
    s.endGroup();   // <name>
    s.endGroup();   // Dialogs

    return restored;
}

void applyAlwaysOnTopPolicy(QDialog *d)
{
    if (!d) return;
#ifndef Q_OS_MACOS
    // On macOS this hint also floats the dialog above other applications, so
    // it is skipped there; Qt::Tool already keeps such dialogs above the main
    // window (see floatingPanelFlags()).
    d->setWindowFlag(Qt::WindowStaysOnTopHint, true);
#endif
}

} // namespace openswmmvis::ui
