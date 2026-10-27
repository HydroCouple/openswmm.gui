/**
 * @file      maptoolplotpick.cpp
 * @brief     Dialog harvest object picks from the canvas without leaving the dialog open.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "map/tools/maptoolplotpick.h"

#include "layers/swmmmodellayer.h"
#include "layers/timeserieslayer.h"
#include "map/networkplotpick.h"
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
    if (!event || !m_canvas)
        return;
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

    const auto hit = openswmmvis::pickNetworkForPlot(m_canvas, mapX, mapY, tol);
    const SWMMObjectRef ref = hit.ref();

    auto *widget = qobject_cast<QWidget *>(m_canvas);
    const QPoint globalPt = widget ? widget->mapToGlobal(pixel) : pixel;

    const auto observations = timeSeriesHitsAt(m_canvas, mapX, mapY, tol);
    if (!observations.isEmpty()) {
        const QPointer<OpenSWMMVisMapToolPlotPick> owner(this);
        const QPointer<MapCanvas> canvasGuard(m_canvas);
        const QPointer<SWMMModelLayer> modelGuard(hit.model);
        const QPointer<SWMMResultsLayer> resultsGuard(hit.results);
        QMenu chooser;
        connect(this, &QObject::destroyed, &chooser, &QWidget::close);
        connect(m_canvas, &QObject::destroyed, &chooser, &QWidget::close);
        connect(m_canvas, &MapCanvas::layerRemoved, &chooser, &QWidget::close);
        QHash<QAction *, QPair<QPointer<TimeSeriesLayer>, QString>> targets;
        for (const auto &candidate : observations) {
            if (!candidate.layer) continue;
            auto *group = chooser.addMenu(candidate.layer->name());
            for (const auto &id : candidate.seriesIds) {
                auto *action = group->addAction(candidate.layer->seriesCaption(id));
                targets.insert(action, {candidate.layer, id});
            }
        }
        QAction *networkAction = nullptr;
        if (ref.isValid()) networkAction = chooser.addAction(QObject::tr("Model / result variables…"));
        if (targets.size() == 1 && !networkAction) {
            const auto target = targets.cbegin().value();
            if (target.first) emit timeSeriesPicked(target.first, {target.second});
            event->accept(); return;
        }
        auto *chosen = chooser.exec(globalPt);
        if (!owner || !canvasGuard)
            return;
        if (targets.contains(chosen)) {
            const auto target = targets.value(chosen);
            if (target.first) emit timeSeriesPicked(target.first, {target.second});
            event->accept(); return;
        }
        if (!networkAction || chosen != networkAction || !modelGuard ||
            (hit.results && !resultsGuard)) { event->accept(); return; }
    }

    // Background hit → offer the system-attribute submenu.
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty()) {
        QMenu *menu = openswmmvis::ui::AttributePickerMenu::createForSystem(
            openswmmvis::plot::UnitSystem::US, widget);
        if (!menu) {
            event->accept();
            return;
        }
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
    case SWMMObjectRef::Node:
        kind = PKind::Node;
        break;
    case SWMMObjectRef::Link:
        kind = PKind::Link;
        break;
    case SWMMObjectRef::Subcatchment:
        kind = PKind::Subcatch;
        break;
    default:
        break;
    }
    if (kind == PKind::Unknown) {
        event->accept();
        return;
    }

    QMenu *menu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
        kind, openswmmvis::plot::UnitSystem::US, widget,
        hit.results ? hit.results->speciesNames() : m_speciesNames);
    if (!menu) {
        event->accept();
        return;
    }
    QAction *picked = menu->exec(globalPt);
    const auto d = openswmmvis::ui::AttributePickerMenu::descriptorFrom(picked);
    delete menu;
    if (picked) {
        // d is INVALID for the "All attributes" sentinel — owner fans out.
        if (hit.results)
            emit objectPickedFromLayer(ref, d, hit.results);
        else
            emit objectPicked(ref, d);
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
