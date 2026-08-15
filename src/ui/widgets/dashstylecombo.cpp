/*!
 * \file   dashstylecombo.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/dashstylecombo.h"

#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>

namespace openswmmvis::ui {

DashStyleCombo::DashStyleCombo(QWidget *parent)
    : QComboBox(parent)
{
    setIconSize(QSize(64, 18));
    addItem(iconFor(Qt::SolidLine),      tr("Solid"),       int(Qt::SolidLine));
    addItem(iconFor(Qt::DashLine),       tr("Dashed"),      int(Qt::DashLine));
    addItem(iconFor(Qt::DotLine),        tr("Dotted"),      int(Qt::DotLine));
    addItem(iconFor(Qt::DashDotLine),    tr("Dash-dot"),    int(Qt::DashDotLine));
    addItem(iconFor(Qt::DashDotDotLine), tr("Dash-dot-dot"),int(Qt::DashDotDotLine));
    addItem(iconFor(Qt::NoPen),          tr("None"),        int(Qt::NoPen));

    connect(this, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit penStyleChanged(penStyle()); });
}

Qt::PenStyle DashStyleCombo::penStyle() const
{
    const int idx = currentIndex();
    if (idx < 0) return Qt::SolidLine;
    return static_cast<Qt::PenStyle>(itemData(idx).toInt());
}

void DashStyleCombo::setPenStyle(Qt::PenStyle s)
{
    for (int i = 0; i < count(); ++i) {
        if (static_cast<Qt::PenStyle>(itemData(i).toInt()) == s) {
            setCurrentIndex(i);
            return;
        }
    }
}

QIcon DashStyleCombo::iconFor(Qt::PenStyle s, int wPx, int hPx)
{
    QPixmap pm(wPx, hPx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(40, 40, 40));
    pen.setWidthF(1.8);
    pen.setStyle(s);
    p.setPen(pen);
    p.drawLine(4, hPx / 2, wPx - 4, hPx / 2);
    return QIcon(pm);
}

} // namespace openswmmvis::ui
