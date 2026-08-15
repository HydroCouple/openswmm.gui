/*!
 * \file   colorbutton.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/colorbutton.h"

#include <QColorDialog>
#include <QPaintEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QStyle>
#include <QStyleOptionButton>

namespace openswmmvis::ui {

ColorButton::ColorButton(QWidget *parent)
    : QPushButton(parent)
{
    setMinimumSize(60, 22);
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    connect(this, &QPushButton::clicked, this, &ColorButton::onClicked);
}

ColorButton::ColorButton(const QColor &initial, QWidget *parent)
    : ColorButton(parent)
{
    m_color = initial;
}

void ColorButton::setColor(const QColor &c)
{
    if (m_color == c) return;
    m_color = c;
    update();
    emit colorChanged(c);
}

void ColorButton::onClicked()
{
    const QColorDialog::ColorDialogOptions opts =
        m_showAlpha ? QColorDialog::ShowAlphaChannel
                    : QColorDialog::ColorDialogOptions();
    const QColor picked = QColorDialog::getColor(
        m_color.isValid() ? m_color : Qt::black,
        this,
        tr("Select colour"),
        opts);
    if (picked.isValid())
        setColor(picked);
}

void ColorButton::paintEvent(QPaintEvent *event)
{
    // Paint the standard button chrome first so the widget integrates with
    // the platform theme (focus ring, pressed-state shading), then overlay
    // a colour swatch in the middle.
    QPushButton::paintEvent(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int pad = 4;
    QRect inner = rect().adjusted(pad, pad, -pad, -pad);
    if (!inner.isValid()) return;

    // Checkered background so users can see transparency.
    if (m_color.alpha() < 255) {
        QBrush check(QColor(220, 220, 220));
        p.fillRect(inner, check);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255));
        const int cell = 4;
        for (int y = inner.top(); y < inner.bottom(); y += cell) {
            for (int x = inner.left(); x < inner.right(); x += cell) {
                if (((x / cell) + (y / cell)) & 1)
                    p.fillRect(QRect(x, y, cell, cell), QColor(255, 255, 255));
            }
        }
    }

    p.setBrush(m_color);
    p.setPen(QPen(palette().color(QPalette::WindowText), 0.7));
    p.drawRoundedRect(inner, 3, 3);
}

} // namespace openswmmvis::ui
