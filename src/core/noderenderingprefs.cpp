/*!
 * \file   noderenderingprefs.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/noderenderingprefs.h"

#include "core/preferencesmanager.h"

namespace {
const QString kJunction = QStringLiteral("junction");
const QString kOutfall  = QStringLiteral("outfall");
const QString kStorage  = QStringLiteral("storage");
const QString kDivider  = QStringLiteral("divider");
const QString kVirtualJunction = QStringLiteral("virtual_junction");
} // anonymous

NodeRenderingPrefs::NodeRenderingPrefs(QObject *parent)
    : QObject(parent)
{
}

QPen   NodeRenderingPrefs::junctionPen()  const { return PreferencesManager::instance()->nodePen(kJunction); }
QBrush NodeRenderingPrefs::junctionFill() const { return PreferencesManager::instance()->nodeBrush(kJunction); }
double NodeRenderingPrefs::junctionSize() const { return PreferencesManager::instance()->nodeSize(kJunction); }

QPen   NodeRenderingPrefs::outfallPen()   const { return PreferencesManager::instance()->nodePen(kOutfall); }
QBrush NodeRenderingPrefs::outfallFill()  const { return PreferencesManager::instance()->nodeBrush(kOutfall); }
double NodeRenderingPrefs::outfallSize()  const { return PreferencesManager::instance()->nodeSize(kOutfall); }

QPen   NodeRenderingPrefs::storagePen()   const { return PreferencesManager::instance()->nodePen(kStorage); }
QBrush NodeRenderingPrefs::storageFill()  const { return PreferencesManager::instance()->nodeBrush(kStorage); }
double NodeRenderingPrefs::storageSize()  const { return PreferencesManager::instance()->nodeSize(kStorage); }

QPen   NodeRenderingPrefs::dividerPen()   const { return PreferencesManager::instance()->nodePen(kDivider); }
QBrush NodeRenderingPrefs::dividerFill()  const { return PreferencesManager::instance()->nodeBrush(kDivider); }
double NodeRenderingPrefs::dividerSize()  const { return PreferencesManager::instance()->nodeSize(kDivider); }

QPen   NodeRenderingPrefs::virtualJunctionPen()  const { return PreferencesManager::instance()->nodePen(kVirtualJunction); }
QBrush NodeRenderingPrefs::virtualJunctionFill() const { return PreferencesManager::instance()->nodeBrush(kVirtualJunction); }
double NodeRenderingPrefs::virtualJunctionSize() const { return PreferencesManager::instance()->nodeSize(kVirtualJunction); }

void NodeRenderingPrefs::setJunctionPen(const QPen &pen)
{
    if (junctionPen() == pen) return;
    PreferencesManager::instance()->setNodePen(kJunction, pen);
    emit junctionPenChanged(pen);
}
void NodeRenderingPrefs::setJunctionFill(const QBrush &brush)
{
    if (junctionFill() == brush) return;
    PreferencesManager::instance()->setNodeBrush(kJunction, brush);
    emit junctionFillChanged(brush);
}
void NodeRenderingPrefs::setJunctionSize(double sizePx)
{
    if (qFuzzyCompare(junctionSize(), sizePx)) return;
    PreferencesManager::instance()->setNodeSize(kJunction, sizePx);
    emit junctionSizeChanged(sizePx);
}

void NodeRenderingPrefs::setOutfallPen(const QPen &pen)
{
    if (outfallPen() == pen) return;
    PreferencesManager::instance()->setNodePen(kOutfall, pen);
    emit outfallPenChanged(pen);
}
void NodeRenderingPrefs::setOutfallFill(const QBrush &brush)
{
    if (outfallFill() == brush) return;
    PreferencesManager::instance()->setNodeBrush(kOutfall, brush);
    emit outfallFillChanged(brush);
}
void NodeRenderingPrefs::setOutfallSize(double sizePx)
{
    if (qFuzzyCompare(outfallSize(), sizePx)) return;
    PreferencesManager::instance()->setNodeSize(kOutfall, sizePx);
    emit outfallSizeChanged(sizePx);
}

void NodeRenderingPrefs::setStoragePen(const QPen &pen)
{
    if (storagePen() == pen) return;
    PreferencesManager::instance()->setNodePen(kStorage, pen);
    emit storagePenChanged(pen);
}
void NodeRenderingPrefs::setStorageFill(const QBrush &brush)
{
    if (storageFill() == brush) return;
    PreferencesManager::instance()->setNodeBrush(kStorage, brush);
    emit storageFillChanged(brush);
}
void NodeRenderingPrefs::setStorageSize(double sizePx)
{
    if (qFuzzyCompare(storageSize(), sizePx)) return;
    PreferencesManager::instance()->setNodeSize(kStorage, sizePx);
    emit storageSizeChanged(sizePx);
}

void NodeRenderingPrefs::setDividerPen(const QPen &pen)
{
    if (dividerPen() == pen) return;
    PreferencesManager::instance()->setNodePen(kDivider, pen);
    emit dividerPenChanged(pen);
}
void NodeRenderingPrefs::setDividerFill(const QBrush &brush)
{
    if (dividerFill() == brush) return;
    PreferencesManager::instance()->setNodeBrush(kDivider, brush);
    emit dividerFillChanged(brush);
}
void NodeRenderingPrefs::setDividerSize(double sizePx)
{
    if (qFuzzyCompare(dividerSize(), sizePx)) return;
    PreferencesManager::instance()->setNodeSize(kDivider, sizePx);
    emit dividerSizeChanged(sizePx);
}

void NodeRenderingPrefs::setVirtualJunctionPen(const QPen &pen)
{
    if (virtualJunctionPen() == pen) return;
    PreferencesManager::instance()->setNodePen(kVirtualJunction, pen);
    emit virtualJunctionPenChanged(pen);
}
void NodeRenderingPrefs::setVirtualJunctionFill(const QBrush &brush)
{
    if (virtualJunctionFill() == brush) return;
    PreferencesManager::instance()->setNodeBrush(kVirtualJunction, brush);
    emit virtualJunctionFillChanged(brush);
}
void NodeRenderingPrefs::setVirtualJunctionSize(double sizePx)
{
    if (qFuzzyCompare(virtualJunctionSize(), sizePx)) return;
    PreferencesManager::instance()->setNodeSize(kVirtualJunction, sizePx);
    emit virtualJunctionSizeChanged(sizePx);
}
