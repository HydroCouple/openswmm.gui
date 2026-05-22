/*!
 * \file   maptoolplotpick.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptoolplotpick.h"

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "ui/widgets/attributepickermenu.h"

#include <QAction>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QVariantMap>
#include <QWidget>

#include <algorithm>
#include <cmath>

OpenSWMMVisMapToolPlotPick::OpenSWMMVisMapToolPlotPick(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("PlotPick"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolPlotPick::cursor() const
{
    return QCursor(Qt::CrossCursor);
}

void OpenSWMMVisMapToolPlotPick::mousePressEvent(QMouseEvent *event)
{
    if (!event || !m_canvas) return;
    if (event->button() != Qt::LeftButton) {
        OpenSWMMVisMapTool::mousePressEvent(event);
        return;
    }

    const QPoint pixel = event->pos();
    double mapX, mapY, mapX2, mapY2;
    toMapCoords(pixel.x(), pixel.y(), mapX, mapY);
    constexpr int kHitTolPx = 6;
    toMapCoords(pixel.x() + kHitTolPx, pixel.y() + kHitTolPx, mapX2, mapY2);
    const double tol = std::max(std::abs(mapX2 - mapX), std::abs(mapY2 - mapY));

    // Hit-test visible SWMM model layers — same walk as
    // OpenSWMMVisMapToolSelect::showContextMenu (canonical pattern).
    SWMMObjectRef ref{SWMMObjectRef::Unknown, {}};
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;
        const QVariantMap hit = sl->identifyAt(mapX, mapY, nullptr, tol);
        const QString name = hit.value(QStringLiteral("elementName")).toString();
        if (name.isEmpty()) continue;
        const QString type = hit.value(QStringLiteral("elementType")).toString();
        SWMMObjectRef::ObjectType t = SWMMObjectRef::Unknown;
        if      (type == QStringLiteral("Node"))         t = SWMMObjectRef::Node;
        else if (type == QStringLiteral("Link"))         t = SWMMObjectRef::Link;
        else if (type == QStringLiteral("Subcatchment")) t = SWMMObjectRef::Subcatchment;
        ref = {t, name};
        break;
    }

    auto *widget = qobject_cast<QWidget *>(m_canvas);
    const QPoint globalPt = widget ? widget->mapToGlobal(pixel) : pixel;

    // Background hit → offer the system-attribute submenu.
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty()) {
        QMenu *menu = openswmmvis::ui::AttributePickerMenu::createForSystem(
            openswmmvis::plot::UnitSystem::US, widget);
        if (!menu) { event->accept(); return; }
        menu->setTitle(QObject::tr("Plot System Variable…"));
        QAction *picked = menu->exec(globalPt);
        const auto attr = openswmmvis::ui::AttributePickerMenu::attributeFrom(picked);
        delete menu;
        if (attr != openswmmvis::plot::PlotAttribute::Unknown)
            emit plotSystemRequested(attr);
        event->accept();
        return;
    }

    // Object hit → attribute submenu valid for the hit object's kind.
    using PKind = openswmmvis::plot::ObjectRef::Kind;
    PKind kind = PKind::Unknown;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:         kind = PKind::Node;     break;
    case SWMMObjectRef::Link:         kind = PKind::Link;     break;
    case SWMMObjectRef::Subcatchment: kind = PKind::Subcatch; break;
    default: break;
    }
    if (kind == PKind::Unknown) { event->accept(); return; }

    QMenu *menu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
        kind, openswmmvis::plot::UnitSystem::US, widget);
    if (!menu) { event->accept(); return; }
    QAction *picked = menu->exec(globalPt);
    const auto attr = openswmmvis::ui::AttributePickerMenu::attributeFrom(picked);
    delete menu;
    if (picked) {
        // attr may be Unknown for the "All attributes" sentinel — owner handles.
        emit objectPicked(ref, attr);
    }
    event->accept();
}

void OpenSWMMVisMapToolPlotPick::keyPressEvent(QKeyEvent *event)
{
    if (event && event->key() == Qt::Key_Escape) {
        emit cancelled();
        event->accept();
        return;
    }
    OpenSWMMVisMapTool::keyPressEvent(event);
}
