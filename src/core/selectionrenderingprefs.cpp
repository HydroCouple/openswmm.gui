/*!
 * \file   selectionrenderingprefs.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/selectionrenderingprefs.h"

#include "core/preferencesmanager.h"

SelectionRenderingPrefs::SelectionRenderingPrefs(QObject *parent)
    : QObject(parent)
{
}

QPen SelectionRenderingPrefs::linkPen() const
{
    return PreferencesManager::instance()->selectionPen(QStringLiteral("link"));
}

QPen SelectionRenderingPrefs::subcatchmentPen() const
{
    return PreferencesManager::instance()->selectionPen(QStringLiteral("subcatchment"));
}

QBrush SelectionRenderingPrefs::subcatchmentFill() const
{
    return PreferencesManager::instance()->selectionBrush(QStringLiteral("subcatchment"));
}

QPen SelectionRenderingPrefs::nodePen() const
{
    return PreferencesManager::instance()->selectionPen(QStringLiteral("node"));
}

QBrush SelectionRenderingPrefs::nodeFill() const
{
    return PreferencesManager::instance()->selectionBrush(QStringLiteral("node"));
}

QPen SelectionRenderingPrefs::gagePen() const
{
    return PreferencesManager::instance()->selectionPen(QStringLiteral("gage"));
}

QBrush SelectionRenderingPrefs::gageFill() const
{
    return PreferencesManager::instance()->selectionBrush(QStringLiteral("gage"));
}

void SelectionRenderingPrefs::setLinkPen(const QPen &pen)
{
    if (linkPen() == pen) return;
    PreferencesManager::instance()->setSelectionPen(QStringLiteral("link"), pen);
    emit linkPenChanged(pen);
}

void SelectionRenderingPrefs::setSubcatchmentPen(const QPen &pen)
{
    if (subcatchmentPen() == pen) return;
    PreferencesManager::instance()->setSelectionPen(QStringLiteral("subcatchment"), pen);
    emit subcatchmentPenChanged(pen);
}

void SelectionRenderingPrefs::setSubcatchmentFill(const QBrush &brush)
{
    if (subcatchmentFill() == brush) return;
    PreferencesManager::instance()->setSelectionBrush(QStringLiteral("subcatchment"), brush);
    emit subcatchmentFillChanged(brush);
}

void SelectionRenderingPrefs::setNodePen(const QPen &pen)
{
    if (nodePen() == pen) return;
    PreferencesManager::instance()->setSelectionPen(QStringLiteral("node"), pen);
    emit nodePenChanged(pen);
}

void SelectionRenderingPrefs::setNodeFill(const QBrush &brush)
{
    if (nodeFill() == brush) return;
    PreferencesManager::instance()->setSelectionBrush(QStringLiteral("node"), brush);
    emit nodeFillChanged(brush);
}

void SelectionRenderingPrefs::setGagePen(const QPen &pen)
{
    if (gagePen() == pen) return;
    PreferencesManager::instance()->setSelectionPen(QStringLiteral("gage"), pen);
    emit gagePenChanged(pen);
}

void SelectionRenderingPrefs::setGageFill(const QBrush &brush)
{
    if (gageFill() == brush) return;
    PreferencesManager::instance()->setSelectionBrush(QStringLiteral("gage"), brush);
    emit gageFillChanged(brush);
}
