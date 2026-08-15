/*!
 * \file   markershapecombo.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/markershapecombo.h"

#include "render/markershape.h"

#include <QBrush>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>

namespace openswmmvis::ui {

MarkerShapeCombo::MarkerShapeCombo(QWidget *parent)
    : QComboBox(parent)
{
    setIconSize(QSize(20, 20));
    connect(this, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { emit shapeValueChanged(shapeValue()); });
}

void MarkerShapeCombo::addShape(int value, BuiltinShape /*shape*/, const QString &label)
{
    // Icon rendered from the canonical value, not the legacy BuiltinShape.
    addItem(iconForValue(value), label, value);
}

void MarkerShapeCombo::addShape(int value, const QString &label)
{
    addItem(iconForValue(value), label, value);
}

void MarkerShapeCombo::populateDefault(bool includeCross)
{
    // Legacy small set (values 0–4, +Cross) for per-class 5-shape enums.
    clear();
    addShape(int(Circle),   tr("Circle"));
    addShape(int(Square),   tr("Square"));
    addShape(int(Triangle), tr("Triangle"));
    addShape(int(Diamond),  tr("Diamond"));
    addShape(int(Star),     tr("Star"));
    if (includeCross)
        addShape(int(Cross), tr("Cross"));
}

void MarkerShapeCombo::populateCanonical()
{
    using MS = OpenSWMM::Render::MarkerShape;
    clear();
    // Full canonical set — order follows the enum.
    addShape(int(MS::Circle),              tr("Circle"));
    addShape(int(MS::Square),              tr("Square"));
    addShape(int(MS::Triangle),            tr("Triangle (right)"));
    addShape(int(MS::Diamond),             tr("Diamond"));
    addShape(int(MS::Star),                tr("Star (5-point)"));
    addShape(int(MS::Cross),               tr("Cross (+)"));
    addShape(int(MS::Plus),                tr("Plus (filled)"));
    addShape(int(MS::XCross),              tr("X cross"));
    addShape(int(MS::Pentagon),            tr("Pentagon"));
    addShape(int(MS::Hexagon),             tr("Hexagon"));
    addShape(int(MS::Arrow),               tr("Arrow (right)"));
    addShape(int(MS::EquilateralTriangle), tr("Triangle (up)"));
    addShape(int(MS::HalfCircle),          tr("Half circle"));
    addShape(int(MS::TriangleDown),        tr("Triangle (down)"));
    addShape(int(MS::Octagon),             tr("Octagon"));
    addShape(int(MS::Hexagram),            tr("Star (6-point)"));
    addShape(int(MS::ArrowUp),             tr("Arrow (up)"));
    addShape(int(MS::ArrowDown),           tr("Arrow (down)"));
    addShape(int(MS::ArrowLeft),           tr("Arrow (left)"));
}

int MarkerShapeCombo::shapeValue() const
{
    const int idx = currentIndex();
    if (idx < 0) return -1;
    return itemData(idx).toInt();
}

void MarkerShapeCombo::setShapeValue(int v)
{
    for (int i = 0; i < count(); ++i) {
        if (itemData(i).toInt() == v) {
            setCurrentIndex(i);
            return;
        }
    }
}

QIcon MarkerShapeCombo::iconFor(BuiltinShape shape, int sizePx)
{
    // Legacy entry point — render via the canonical value path.
    return iconForValue(static_cast<int>(shape), sizePx);
}

QIcon MarkerShapeCombo::iconForValue(int markerShapeValue, int sizePx)
{
    QPixmap pm(sizePx, sizePx);
    pm.fill(Qt::transparent);
    QPainter p(&pm);

    const QPen   pen(QColor(40, 40, 40), 1.0);
    const QBrush brush(QColor(80, 130, 200));
    const QPointF c(sizePx * 0.5, sizePx * 0.5);
    // drawMarkerShape's sizePx is the bounding-box edge; 0.84·icon keeps the
    // glyph clear of the edges, matching the previous icon proportions.
    OpenSWMM::Render::drawMarkerShape(
        &p, static_cast<OpenSWMM::Render::MarkerShape>(markerShapeValue),
        c, sizePx * 0.84, brush, pen);

    return QIcon(pm);
}

} // namespace openswmmvis::ui
