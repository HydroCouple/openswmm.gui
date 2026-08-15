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

double SnowpackProvider::param(int k) const noexcept
{
    if (k < 0 || k >= ParamCount) return 0.0;
    return m_param[k];
}

void SnowpackProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void SnowpackProvider::setParam(int k, double v)
{
    if (k < 0 || k >= ParamCount) return;
    if (v == m_param[k]) return;
    m_param[k] = v;
    emit paramsChanged();
}

void SnowpackProvider::setRemovalSubcatch(QString name)
{
    if (name == m_removalSubcatch) return;
    m_removalSubcatch = std::move(name);
    emit paramsChanged();
}

} // namespace openswmmvis::snowpack
