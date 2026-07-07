/*!
 * \file   inletprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "inlet/inletprovider.h"

namespace openswmmvis::inlet {

InletProvider::InletProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

InletProvider::~InletProvider() = default;

void InletProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void InletProvider::setType(QString v)
{
    if (v == m_type) return;
    m_type = std::move(v);
    m_dirty = true;
    emit paramsChanged();
}

void InletProvider::setLength(double v)
{
    if (v == m_length) return;
    m_length = v;
    m_dirty = true;
    emit paramsChanged();
}

void InletProvider::setWidth(double v)
{
    if (v == m_width) return;
    m_width = v;
    m_dirty = true;
    emit paramsChanged();
}

void InletProvider::setGrateType(QString v)
{
    if (v == m_grateType) return;
    m_grateType = std::move(v);
    m_dirty = true;
    emit paramsChanged();
}

void InletProvider::setOpenArea(double v)
{
    if (v == m_openArea) return;
    m_openArea = v;
    m_dirty = true;
    emit paramsChanged();
}

void InletProvider::setSplashVeloc(double v)
{
    if (v == m_splashVeloc) return;
    m_splashVeloc = v;
    m_dirty = true;
    emit paramsChanged();
}

} // namespace openswmmvis::inlet
