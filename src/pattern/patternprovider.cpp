/*!
 * \file   patternprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "pattern/patternprovider.h"

#include <QCoreApplication>

#include <algorithm>
#include <numeric>

namespace openswmmvis::pattern {

int PatternProvider::factorCountFor(PatternType t) noexcept
{
    switch (t) {
    case PatternType::Monthly: return 12;
    case PatternType::Daily:   return 7;
    case PatternType::Hourly:  return 24;
    case PatternType::Weekend: return 24;
    }
    return 0;
}

QString PatternProvider::rowLabel(PatternType t, int i)
{
    switch (t) {
    case PatternType::Monthly: {
        static const char *kMonths[12] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        return (i >= 0 && i < 12) ? QCoreApplication::translate("PatternProvider", kMonths[i])
                                  : QString();
    }
    case PatternType::Daily: {
        static const char *kDays[7] = {
            "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
        };
        return (i >= 0 && i < 7) ? QCoreApplication::translate("PatternProvider", kDays[i])
                                 : QString();
    }
    case PatternType::Hourly:
    case PatternType::Weekend:
        return (i >= 0 && i < 24)
            ? QString::asprintf("%02d:00", i)
            : QString();
    }
    return QString();
}

PatternProvider::PatternProvider(QString name, PatternType type, QObject *parent)
    : QObject(parent)
    , m_name(std::move(name))
    , m_type(type)
    , m_factors(factorCountFor(type), 1.0)
{
}

PatternProvider::~PatternProvider() = default;

double PatternProvider::sumOfFactors() const noexcept
{
    return std::accumulate(m_factors.cbegin(), m_factors.cend(), 0.0);
}

bool PatternProvider::setFactor(int i, double v, QString *reasonOut)
{
    if (i < 0 || i >= m_factors.size()) {
        const auto reason = tr("Factor index %1 is out of range [0, %2).")
                              .arg(i).arg(m_factors.size());
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (v < 0.0) {
        const auto reason = tr("Pattern factors must be ≥ 0.");
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (m_factors[i] == v) return true;  // no-op
    m_factors[i] = v;
    emit factorChanged(i);
    return true;
}

bool PatternProvider::setFactorLive(int i, double v)
{
    if (i < 0 || i >= m_factors.size()) return false;
    if (v < 0.0) v = 0.0;   // clamp during live drag
    if (m_factors[i] == v) return true;
    m_factors[i] = v;
    emit factorChanged(i);
    return true;
}

bool PatternProvider::setAllFactors(QVector<double> f, QString *reasonOut)
{
    if (f.size() != m_factors.size()) {
        const auto reason = tr("Factor array size %1 does not match expected %2 for this pattern type.")
                              .arg(f.size()).arg(m_factors.size());
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    for (double v : f) {
        if (v < 0.0) {
            const auto reason = tr("Pattern factors must be ≥ 0.");
            if (reasonOut) *reasonOut = reason;
            emit mutationRejected(reason);
            return false;
        }
    }
    m_factors = std::move(f);
    emit factorsChanged();
    return true;
}

bool PatternProvider::normalize(double targetSum, QString *reasonOut)
{
    const double s = sumOfFactors();
    if (s <= 0.0) {
        const auto reason = tr("Cannot normalize a pattern whose factors sum to zero.");
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    if (targetSum <= 0.0) {
        const auto reason = tr("Normalization target sum must be > 0.");
        if (reasonOut) *reasonOut = reason;
        emit mutationRejected(reason);
        return false;
    }
    const double k = targetSum / s;
    for (double &v : m_factors) v *= k;
    emit factorsChanged();
    return true;
}

void PatternProvider::setName(QString newName)
{
    if (newName == m_name) return;
    const QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void PatternProvider::setType(PatternType t)
{
    if (t == m_type) return;
    const PatternType prev = m_type;
    m_type = t;
    m_factors = QVector<double>(factorCountFor(t), 1.0);
    emit typeChanged(prev, m_type);
    emit factorsChanged();
}

} // namespace openswmmvis::pattern
