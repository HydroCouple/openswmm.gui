/*!
 * \file   swmmvisproject.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "project/swmmvisproject.h"

#include "swmmvisprojectwindow.h"

SWMMVisProject::SWMMVisProject(QObject *parent)
    : QObject(parent)
{
}

SWMMVisProject::~SWMMVisProject() = default;

void SWMMVisProject::setOswpPath(const QString &path)
{
    if (mOswpPath == path) return;
    mOswpPath = path;
    emit oswpPathChanged(mOswpPath);
}

void SWMMVisProject::setTitle(const QString &title)
{
    if (mTitle == title) return;
    mTitle = title;
    emit titleChanged(mTitle);
}

QVector<SWMMVisProjectWindow *> SWMMVisProject::dirtyInstances() const
{
    QVector<SWMMVisProjectWindow *> out;
    out.reserve(mInstances.size());
    for (auto *pw : mInstances) {
        if (pw && pw->hasChanges())
            out.append(pw);
    }
    return out;
}

void SWMMVisProject::addInstance(SWMMVisProjectWindow *pw)
{
    if (!pw || mInstances.contains(pw)) return;
    mInstances.append(pw);
    emit instanceAdded(pw);
}

bool SWMMVisProject::removeInstance(SWMMVisProjectWindow *pw)
{
    const int idx = mInstances.indexOf(pw);
    if (idx < 0) return false;
    mInstances.removeAt(idx);
    emit instanceRemoved(pw);
    return true;
}

bool SWMMVisProject::isDirty() const
{
    for (auto *pw : mInstances) {
        if (pw && pw->hasChanges())
            return true;
    }
    return false;
}
