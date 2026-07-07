/*!
 * \file   aquiferprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "aquifer/aquiferprovider.h"

namespace openswmmvis::aquifer {

AquiferProvider::AquiferProvider(QString name, QObject *parent)
    : QObject(parent), m_name(std::move(name))
{
}

AquiferProvider::~AquiferProvider() = default;

double AquiferProvider::param(int k) const noexcept
{
    if (k < 0 || k >= ParamCount) return 0.0;
    return m_param[k];
}

void AquiferProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void AquiferProvider::setParam(int k, double v)
{
    if (k < 0 || k >= ParamCount) return;
    if (v == m_param[k]) return;
    m_param[k] = v;
    emit paramsChanged();
}

} // namespace openswmmvis::aquifer
