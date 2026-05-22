/*!
 * \file   linkrenderingprefs.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "core/linkrenderingprefs.h"

#include "core/preferencesmanager.h"

LinkRenderingPrefs::LinkRenderingPrefs(QObject *parent)
    : QObject(parent)
{
}

QPen LinkRenderingPrefs::conduitPen() const
{
    return PreferencesManager::instance()->linkPen(QStringLiteral("conduit"));
}

QPen LinkRenderingPrefs::pumpPen() const
{
    return PreferencesManager::instance()->linkPen(QStringLiteral("pump"));
}

QPen LinkRenderingPrefs::orificePen() const
{
    return PreferencesManager::instance()->linkPen(QStringLiteral("orifice"));
}

QPen LinkRenderingPrefs::weirPen() const
{
    return PreferencesManager::instance()->linkPen(QStringLiteral("weir"));
}

QPen LinkRenderingPrefs::outletPen() const
{
    return PreferencesManager::instance()->linkPen(QStringLiteral("outlet"));
}

void LinkRenderingPrefs::setConduitPen(const QPen &pen)
{
    if (conduitPen() == pen) return;
    PreferencesManager::instance()->setLinkPen(QStringLiteral("conduit"), pen);
    emit conduitPenChanged(pen);
}

void LinkRenderingPrefs::setPumpPen(const QPen &pen)
{
    if (pumpPen() == pen) return;
    PreferencesManager::instance()->setLinkPen(QStringLiteral("pump"), pen);
    emit pumpPenChanged(pen);
}

void LinkRenderingPrefs::setOrificePen(const QPen &pen)
{
    if (orificePen() == pen) return;
    PreferencesManager::instance()->setLinkPen(QStringLiteral("orifice"), pen);
    emit orificePenChanged(pen);
}

void LinkRenderingPrefs::setWeirPen(const QPen &pen)
{
    if (weirPen() == pen) return;
    PreferencesManager::instance()->setLinkPen(QStringLiteral("weir"), pen);
    emit weirPenChanged(pen);
}

void LinkRenderingPrefs::setOutletPen(const QPen &pen)
{
    if (outletPen() == pen) return;
    PreferencesManager::instance()->setLinkPen(QStringLiteral("outlet"), pen);
    emit outletPenChanged(pen);
}
