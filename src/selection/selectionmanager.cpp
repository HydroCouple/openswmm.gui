/*!
 * \file   selectionmanager.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "selection/selectionmanager.h"

SelectionManager::SelectionManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SWMMObjectRef>("SWMMObjectRef");
    qRegisterMetaType<QSet<SWMMObjectRef>>("QSet<SWMMObjectRef>");
}

SelectionManager::~SelectionManager() = default;

void SelectionManager::select(const SWMMObjectRef &ref, Mode mode)
{
    QSet<SWMMObjectRef> set;
    if (ref.isValid())
        set.insert(ref);
    select(set, mode);
}

void SelectionManager::select(const QSet<SWMMObjectRef> &refs, Mode mode)
{
    QSet<SWMMObjectRef> next = m_selection;
    switch (mode)
    {
    case Replace:
        next = refs;
        break;
    case Add:
        next.unite(refs);
        break;
    case Subtract:
        next.subtract(refs);
        break;
    case Toggle:
        for (const SWMMObjectRef &r : refs)
        {
            if (next.contains(r)) next.remove(r);
            else                  next.insert(r);
        }
        break;
    }
    applyChange(next);
}

void SelectionManager::clear()
{
    applyChange({});
}

void SelectionManager::applyChange(const QSet<SWMMObjectRef> &next)
{
    if (next == m_selection)
        return;

    QSet<SWMMObjectRef> added   = next;     added.subtract(m_selection);
    QSet<SWMMObjectRef> removed = m_selection; removed.subtract(next);
    m_selection = next;
    emit selectionChanged(m_selection, added, removed);
}
