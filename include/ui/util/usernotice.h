/**
 * @file      usernotice.h
 * @brief     One way to tell the user something that must not vanish with a status message.
 * @details   notifyUser() shows a notice on a dialog's status bar, keeps it in the main window's
 *            Message Log and announces it to assistive technology (review A-07, A-27). Header
 *            only: the Message Log is reached through a sink the main window installs, so code
 *            and tests without a main window need no extra sources.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_UTIL_USERNOTICE_H
#define OPENSWMMVIS_UI_UTIL_USERNOTICE_H

#include <QAccessible>
#include <QStatusBar>
#include <QString>
#include <QWidget>

#include <functional>
#include <utility>

namespace openswmmvis::ui
{

/*! How serious a notice is; maps onto the Message Log's Information / Warning / Error. */
enum class NoticeSeverity { Information, Warning, Error };

/*! Receives every notice: its severity and its text, prefixed with the window title. */
using NoticeSink = std::function<void(NoticeSeverity, const QString &)>;

/**
 * @brief The process-wide notice sink.
 * @return Borrowed sink, empty until the main window installs its Message Log.
 */
inline NoticeSink &noticeSink()
{
    static NoticeSink sink;
    return sink;
}

/**
 * @brief Install the notice sink, or remove it with an empty function.
 * @param[in] sink The main window installs its Message Log when it is built and removes it
 *   when it is destroyed. GUI thread only.
 */
inline void setNoticeSink(NoticeSink sink)
{
    noticeSink() = std::move(sink);
}

/**
 * @brief Tell the user something that must not vanish with a status message.
 * @details Shows \p text on \p statusBar, records it in the Message Log through the installed
 *   sink (prefixed with the title of \p context's window, so the log says where it came from)
 *   and announces it to assistive technology, assertively for an error. Use it for rejected
 *   edits and failures; routine confirmations can stay on the status bar alone. GUI thread only.
 * @param[in] context Widget the notice is about, usually the dialog; null skips the title
 *   prefix and the announcement.
 * @param[in] severity Message Log severity.
 * @param[in] text Notice text; empty does nothing.
 * @param[in,out] statusBar Optional status bar that shows \p text for \p timeoutMs.
 * @param[in] timeoutMs Status bar display time in milliseconds.
 */
inline void notifyUser(QWidget *context, NoticeSeverity severity, const QString &text,
                       QStatusBar *statusBar = nullptr, int timeoutMs = 6000)
{
    if (text.isEmpty())
        return;
    if (statusBar)
        statusBar->showMessage(text, timeoutMs);
    if (const NoticeSink &sink = noticeSink()) {
        QString title = context ? context->window()->windowTitle() : QString();
        title.remove(QStringLiteral("[*]"));
        title = title.trimmed();
        sink(severity, title.isEmpty() ? text : QStringLiteral("%1: %2").arg(title, text));
    }
    if (context) {
        QAccessibleAnnouncementEvent announcement(
            statusBar ? static_cast<QObject *>(statusBar) : context, text);
        announcement.setPoliteness(severity == NoticeSeverity::Error
                                       ? QAccessible::AnnouncementPoliteness::Assertive
                                       : QAccessible::AnnouncementPoliteness::Polite);
        QAccessible::updateAccessibility(&announcement);
    }
}

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_UTIL_USERNOTICE_H
