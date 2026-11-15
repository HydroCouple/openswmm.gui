// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      accessible_checks.h
 * @brief     Test helpers for the custom accessibility interfaces (review A-01 / A-08).
 * @details   AccessibleEventRecorder turns accessibility on and records the events widgets send,
 *            so tests can check what a screen reader would be told and when. Header-only.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QAccessible>
#include <QList>
#include <QObject>
#include <QString>
#include <QWidget>

namespace a11ytest
{

/** One event a widget sent to assistive technology. */
struct RecordedEvent {
    QAccessible::Event type;
    QObject *object = nullptr;
    QString value; ///< the spoken value, for ValueChanged events
};

inline QList<RecordedEvent> &recordedEvents()
{
    static QList<RecordedEvent> events;
    return events;
}

inline void recordEvent(QAccessibleEvent *event)
{
    RecordedEvent recorded{event->type(), event->object(), {}};
    if (event->type() == QAccessible::ValueChanged)
        recorded.value = static_cast<QAccessibleValueChangeEvent *>(event)->value().toString();
    recordedEvents().append(recorded);
}

/** Turns accessibility on and records events while it exists. */
class AccessibleEventRecorder
{
public:
    AccessibleEventRecorder()
    {
        recordedEvents().clear();
        m_previous = QAccessible::installUpdateHandler(&recordEvent);
        QAccessible::setActive(true);
    }
    ~AccessibleEventRecorder()
    {
        QAccessible::installUpdateHandler(m_previous);
        recordedEvents().clear();
    }
    AccessibleEventRecorder(const AccessibleEventRecorder &) = delete;
    AccessibleEventRecorder &operator=(const AccessibleEventRecorder &) = delete;

    void clear() { recordedEvents().clear(); }

    /** @return The values announced for @p object, oldest first. */
    QStringList valuesFor(QObject *object) const
    {
        QStringList values;
        for (const auto &event : recordedEvents())
            if (event.type == QAccessible::ValueChanged && event.object == object)
                values.append(event.value);
        return values;
    }

    /** @return How many events of @p type were sent for @p object. */
    int count(QAccessible::Event type, QObject *object) const
    {
        int n = 0;
        for (const auto &event : recordedEvents())
            if (event.type == type && event.object == object)
                ++n;
        return n;
    }

private:
    QAccessible::UpdateHandler m_previous = nullptr;
};

/** @return The interface Qt hands to assistive technology for @p widget. */
inline QAccessibleInterface *accessibleFor(QWidget *widget)
{
    return QAccessible::queryAccessibleInterface(widget);
}

} // namespace a11ytest
