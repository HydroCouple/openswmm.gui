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

/// Minimum slice of a window that must remain on a connected screen for the
/// user to be able to grab and drag it back. Deliberately small — parking a
/// window mostly off the edge is a legitimate thing to do; being unable to
/// reach it ever again is not.
constexpr int kMinVisibleWidth  = 160;
constexpr int kMinVisibleHeight = 60;

/// Fallback height of the title bar when the real frame is not yet known
/// (the window has no native handle at restore time). macOS title bars are
/// 28 px at 1x; the exact value only has to be in the right ballpark.
constexpr int kDefaultTitleBarStrip = 28;

/// Height of the frame decoration ABOVE the client rect. QWidget::setGeometry
/// positions the CLIENT area, so the title bar — the only draggable part of
/// the window — sits above `geometry().top()` and can be pushed under the
/// menu bar by a naive restore.
int titleBarStripFor_(const QWidget *w)
{
    if (!w || !w->isVisible())
        return kDefaultTitleBarStrip;
    const int strip = w->frameGeometry().top() - w->geometry().top();
    // 0 on a not-yet-decorated window; absurd values mean the frame is stale.
    if (strip <= 0 || strip > 96)
        return kDefaultTitleBarStrip;
    return strip;
}

} // namespace

QRect clampToVisibleScreen(const QRect &saved, int titleBarStrip)
{
    if (!saved.isValid())
        return saved;

    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return saved;

    if (titleBarStrip < 0)
        titleBarStrip = kDefaultTitleBarStrip;

    // Work in FRAME coordinates (client rect grown upward by the title bar)
    // so every test below is about the region the user can actually grab.
    const QRect frame = saved.adjusted(0, -titleBarStrip, 0, 0);

    // Target screen = the one the window overlaps most. Beats testing the
    // center point, which lands in the dead gap between two monitors for a
    // window straddling them and yanks it to the primary screen needlessly.
    const QScreen *target = nullptr;
    qint64 bestArea = 0;
    for (const QScreen *s : screens) {
        if (!s) continue;
        const QRect inter = s->availableGeometry().intersected(frame);
        const qint64 area = qint64(inter.width()) * qint64(inter.height());
        if (area > bestArea) { bestArea = area; target = s; }
    }
    if (!target)
        target = QGuiApplication::primaryScreen();   // overlaps nothing at all
    if (!target)
        return saved;

    const QRect avail = target->availableGeometry();

    QRect f = frame;
    // Size clamp against the TARGET screen — a rect saved on a 27" display
    // must not stay 2560 px wide when restored onto a laptop panel, even
    // though its center is legitimately on that panel.
    f.setWidth (qMin(f.width(),  avail.width()));
    f.setHeight(qMin(f.height(), avail.height()));

    const QRect visible = avail.intersected(f);
    const bool enoughVisible = visible.width()  >= qMin(f.width(),  kMinVisibleWidth)
                            && visible.height() >= qMin(f.height(), kMinVisibleHeight);
    // The title bar is the drag handle: if its strip is above the available
    // area (under the menu bar) the window cannot be moved, however much of
    // its body happens to be visible.
    const bool titleReachable = f.top() >= avail.top();

    if (!enoughVisible || !titleReachable || f.size() != frame.size()) {
        // Translate minimally back inside rather than recentering, so a
        // window nudged off one edge returns to where the user had it.
        if (f.right()  > avail.right())  f.moveRight (avail.right());
        if (f.bottom() > avail.bottom()) f.moveBottom(avail.bottom());
        if (f.left()   < avail.left())   f.moveLeft  (avail.left());
        if (f.top()    < avail.top())    f.moveTop   (avail.top());
    }

    return f.adjusted(0, titleBarStrip, 0, 0);   // back to client coordinates
}

void ensureWindowOnScreen(QWidget *widget, const QScreen *preferred)
{
    if (!widget) return;
    QWidget *w = widget->window();
    if (!w) return;

    const int strip = titleBarStripFor_(w);
    const QRect current = w->geometry();
    QRect target = clampToVisibleScreen(current, strip);

    // Explicit relocation (the "Reset Window Positions" recovery path): the
    // window is moved onto the requested screen UNCONDITIONALLY, even if it
    // is currently sitting perfectly happily on another one. That is the
    // point of the action — the user has lost track of a window and wants
    // everything gathered in front of them, and "everything except the ones
    // I cannot find" would defeat it.
    if (preferred) {
        const QRect avail = preferred->availableGeometry();
        target.setWidth (qMin(target.width(),  avail.width()));
        // qMax guards a pathological screen shorter than the title bar,
        // which would otherwise produce an invalid rect.
        target.setHeight(qMin(target.height(), qMax(1, avail.height() - strip)));
        target.moveCenter(avail.center());
        if (target.top() - strip < avail.top())
            target.moveTop(avail.top() + strip);
    }

    if (target != current)
        w->setGeometry(target);
}

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
            QWidget *w = root->window();
            w->setGeometry(clampToVisibleScreen(saved, titleBarStripFor_(w)));
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
