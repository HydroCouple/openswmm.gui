/*!
 * \file   snowpackprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "snowpack/snowpackprovider.h"

namespace openswmmvis::snowpack {

SnowpackProvider::SnowpackProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

SnowpackProvider::~SnowpackProvider() = default;

void SnowpackProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

} // namespace openswmmvis::snowpack
