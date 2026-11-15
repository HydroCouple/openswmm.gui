// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      customaccessible.h
 * @brief     Shared base for the accessibility interfaces of custom-drawn widgets (review A-01).
 * @details   Painted widgets (sliders, plots, charts, previews) have no Qt interface that knows
 *            their value. Each widget's .cpp defines a small interface derived from the classes
 *            here and registers it with QAccessible::installFactory through
 *            Q_COREAPP_STARTUP_FUNCTION, so any target compiling the widget gets its interface.
 *            Text is read from the widget when assistive technology asks, never cached. Widgets
 *            call announceValue() after a user action only, never for every playback frame.
 *            Header-only so the widgets need no new link dependency. GUI thread only.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <QAccessible>
#include <QAccessibleWidget>
#include <QLatin1StringView>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QWidget>

namespace openswmmvis::ui::accessibility
{

/**
 * @brief Interface whose value and description come from the live widget.
 * @details Subclasses override valueText() and descriptionText(). An empty description falls
 *          back to the widget's own accessible description, then its tooltip.
 */
class LiveWidgetAccessible : public QAccessibleWidget
{
public:
    LiveWidgetAccessible(QWidget *widget, QAccessible::Role role) : QAccessibleWidget(widget, role)
    {
    }

    QString text(QAccessible::Text t) const override
    {
        if (t == QAccessible::Value)
            return valueText();
        if (t == QAccessible::Description) {
            const QString description = descriptionText();
            if (!description.isEmpty())
                return description;
        }
        return QAccessibleWidget::text(t);
    }

protected:
    /** @return What the widget currently shows or selects, as a sentence. */
    virtual QString valueText() const { return {}; }
    /** @return What the widget contains and how to use it; empty uses the widget's own. */
    virtual QString descriptionText() const { return {}; }
};

/**
 * @brief Interface for a painted slider: a spoken value plus the numeric value interface.
 * @details Values are fractions of the track, 0 to 1. setCurrentValue() moves the slider the
 *          way the arrow keys do.
 */
class SliderAccessible : public LiveWidgetAccessible, public QAccessibleValueInterface
{
public:
    explicit SliderAccessible(QWidget *widget) : LiveWidgetAccessible(widget, QAccessible::Slider)
    {
    }

    void *interface_cast(QAccessible::InterfaceType t) override
    {
        if (t == QAccessible::ValueInterface)
            return static_cast<QAccessibleValueInterface *>(this);
        return LiveWidgetAccessible::interface_cast(t);
    }

    QVariant maximumValue() const override { return 1.0; }
    QVariant minimumValue() const override { return 0.0; }
    QVariant minimumStepSize() const override { return 0.01; }
};

/**
 * @brief Factory creating @p Accessible for objects whose class level is exactly @p Widget.
 * @details Qt asks the factories once per class level, most derived first, so subclasses of
 *          @p Widget get this interface unless a factory for their own class answers first.
 */
template <class Widget, class Accessible>
QAccessibleInterface *createAccessible(const QString &className, QObject *object)
{
    if (className != QLatin1StringView(Widget::staticMetaObject.className()))
        return nullptr;
    auto *widget = qobject_cast<Widget *>(object);
    return widget ? new Accessible(widget) : nullptr;
}

/**
 * @brief Tell assistive technology that a user action changed @p widget's value.
 * @note Not gated on QAccessible::isActive(): the platform bridge drops events when no
 *       assistive technology listens, and these fire only on user actions.
 */
inline void announceValue(QWidget *widget)
{
    if (!widget)
        return;
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(widget);
    QAccessibleValueChangeEvent event(widget, iface ? iface->text(QAccessible::Value) : QString());
    QAccessible::updateAccessibility(&event);
}

/** @brief Tell assistive technology that @p widget's description changed. */
inline void announceDescription(QWidget *widget)
{
    if (!widget)
        return;
    QAccessibleEvent event(widget, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&event);
}

} // namespace openswmmvis::ui::accessibility
