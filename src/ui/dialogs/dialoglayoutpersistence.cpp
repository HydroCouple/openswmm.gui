/*!
 * \file   dialoglayoutpersistence.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/dialoglayoutpersistence.h"

#include <QByteArray>
#include <QDialog>
#include <QGuiApplication>
#include <QList>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QString>
#include <QWidget>

namespace openswmmvis::ui {

namespace {

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
        if (spName.isEmpty()) continue;       // opt-in via objectName only
        s.setValue(spName, sp->saveState());
    }
    s.endGroup();   // splitter
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
        if (spName.isEmpty()) continue;
        const QByteArray state = s.value(spName).toByteArray();
        if (!state.isEmpty() && sp->restoreState(state))
            restored = true;
    }
    s.endGroup();   // splitter
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
