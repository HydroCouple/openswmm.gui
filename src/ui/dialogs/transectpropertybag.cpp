/*!
 * \file   transectpropertybag.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/transectpropertybag.h"

#include "layers/swmmmodellayer.h"     // complete type for QPointer<SWMMModelLayer>(layer)
#include "transect/transectprovider.h"

namespace openswmmvis::ui {

using openswmmvis::transect::TransectProvider;

TransectPropertyBag::TransectPropertyBag(QObject *parent)
    : QObject(parent)
{
}

void TransectPropertyBag::bind(TransectProvider *p, SWMMModelLayer *layer)
{
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<TransectProvider>(p);
    m_layer = QPointer<SWMMModelLayer>(layer);
    if (m_provider) {
        connect(m_provider, &TransectProvider::roughnessChanged,
                this, &TransectPropertyBag::onProviderRoughnessChanged_);
        connect(m_provider, &TransectProvider::bankStationsChanged,
                this, &TransectPropertyBag::onProviderBankStationsChanged_);
        connect(m_provider, &TransectProvider::encroachmentStationsChanged,
                this, &TransectPropertyBag::onProviderEncroachmentChanged_);
        connect(m_provider, &TransectProvider::modifiersChanged,
                this, &TransectPropertyBag::onProviderModifiersChanged_);
    }
    refreshFromProvider_();
}

TransectProvider *TransectPropertyBag::provider() const noexcept
{
    return m_provider.data();
}

void TransectPropertyBag::refreshFromProvider_()
{
    if (!m_provider) {
        // Reset to defaults; emit so views refresh.
        m_nLeft = 0.0;       emit nLeftBankChanged(0.0);
        m_nRight = 0.0;      emit nRightBankChanged(0.0);
        m_nChannel = 0.0;    emit nChannelChanged(0.0);
        m_xLeftBank = 0.0;   emit xLeftBankChanged(0.0);
        m_xRightBank = 0.0;  emit xRightBankChanged(0.0);
        m_xLeftEnc = 0.0;    emit xLeftEncroachmentChanged(0.0);
        m_xRightEnc = 0.0;   emit xRightEncroachmentChanged(0.0);
        m_xFactor = 1.0;     emit stationMultiplierChanged(1.0);
        m_yFactor = 0.0;     emit elevationOffsetChanged(0.0);
        m_lengthFactor = 1.0; emit meanderFactorChanged(1.0);
        return;
    }
    m_suppressPush = true;
    setNLeftBank(m_provider->nLeftBank());
    setNRightBank(m_provider->nRightBank());
    setNChannel(m_provider->nChannel());
    setXLeftBank(m_provider->xLeftBank());
    setXRightBank(m_provider->xRightBank());
    setXLeftEncroachment(m_provider->xLeftEncroachment());
    setXRightEncroachment(m_provider->xRightEncroachment());
    setStationMultiplier(m_provider->stationMultiplier());
    setElevationOffset(m_provider->elevationOffset());
    setMeanderFactor(m_provider->meanderFactor());
    m_suppressPush = false;
}

QString TransectPropertyBag::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("nLeftBank"))          return tr("Roughness — Left Bank n");
    if (property == QLatin1String("nRightBank"))         return tr("Roughness — Right Bank n");
    if (property == QLatin1String("nChannel"))           return tr("Roughness — Channel n");
    if (property == QLatin1String("xLeftBank"))          return tr("Bank Stations — Left");
    if (property == QLatin1String("xRightBank"))         return tr("Bank Stations — Right");
    if (property == QLatin1String("xLeftEncroachment"))  return tr("Encroachment Stations — Left");
    if (property == QLatin1String("xRightEncroachment")) return tr("Encroachment Stations — Right");
    if (property == QLatin1String("stationMultiplier"))  return tr("Modifiers — Stations Multiplier");
    if (property == QLatin1String("elevationOffset"))    return tr("Modifiers — Elevations Offset");
    if (property == QLatin1String("meanderFactor"))      return tr("Modifiers — Meander Factor");
    return {};
}

// ── Setters (push back to provider unless we are mid-refresh) ───────────────

void TransectPropertyBag::setNLeftBank(double v)
{
    if (v == m_nLeft) return;
    m_nLeft = v;
    emit nLeftBankChanged(v);
    if (!m_suppressPush) pushRoughness_();
}

void TransectPropertyBag::setNRightBank(double v)
{
    if (v == m_nRight) return;
    m_nRight = v;
    emit nRightBankChanged(v);
    if (!m_suppressPush) pushRoughness_();
}

void TransectPropertyBag::setNChannel(double v)
{
    if (v == m_nChannel) return;
    m_nChannel = v;
    emit nChannelChanged(v);
    if (!m_suppressPush) pushRoughness_();
}

void TransectPropertyBag::setXLeftBank(double v)
{
    if (v == m_xLeftBank) return;
    m_xLeftBank = v;
    emit xLeftBankChanged(v);
    if (!m_suppressPush) pushBank_();
}

void TransectPropertyBag::setXRightBank(double v)
{
    if (v == m_xRightBank) return;
    m_xRightBank = v;
    emit xRightBankChanged(v);
    if (!m_suppressPush) pushBank_();
}

void TransectPropertyBag::setXLeftEncroachment(double v)
{
    if (v == m_xLeftEnc) return;
    m_xLeftEnc = v;
    emit xLeftEncroachmentChanged(v);
    if (!m_suppressPush) pushEncroach_();
}

void TransectPropertyBag::setXRightEncroachment(double v)
{
    if (v == m_xRightEnc) return;
    m_xRightEnc = v;
    emit xRightEncroachmentChanged(v);
    if (!m_suppressPush) pushEncroach_();
}

void TransectPropertyBag::setStationMultiplier(double v)
{
    if (v == m_xFactor) return;
    m_xFactor = v;
    emit stationMultiplierChanged(v);
    if (!m_suppressPush) pushModifiers_();
}

void TransectPropertyBag::setElevationOffset(double v)
{
    if (v == m_yFactor) return;
    m_yFactor = v;
    emit elevationOffsetChanged(v);
    if (!m_suppressPush) pushModifiers_();
}

void TransectPropertyBag::setMeanderFactor(double v)
{
    if (v == m_lengthFactor) return;
    m_lengthFactor = v;
    emit meanderFactorChanged(v);
    if (!m_suppressPush) pushModifiers_();
}

// ── Provider → bag refresh (broken via m_suppressPush) ──────────────────────

void TransectPropertyBag::onProviderRoughnessChanged_()
{
    if (!m_provider) return;
    m_suppressPush = true;
    setNLeftBank(m_provider->nLeftBank());
    setNRightBank(m_provider->nRightBank());
    setNChannel(m_provider->nChannel());
    m_suppressPush = false;
}

void TransectPropertyBag::onProviderBankStationsChanged_()
{
    if (!m_provider) return;
    m_suppressPush = true;
    setXLeftBank(m_provider->xLeftBank());
    setXRightBank(m_provider->xRightBank());
    m_suppressPush = false;
}

void TransectPropertyBag::onProviderEncroachmentChanged_()
{
    if (!m_provider) return;
    m_suppressPush = true;
    setXLeftEncroachment(m_provider->xLeftEncroachment());
    setXRightEncroachment(m_provider->xRightEncroachment());
    m_suppressPush = false;
}

void TransectPropertyBag::onProviderModifiersChanged_()
{
    if (!m_provider) return;
    m_suppressPush = true;
    setStationMultiplier(m_provider->stationMultiplier());
    setElevationOffset(m_provider->elevationOffset());
    setMeanderFactor(m_provider->meanderFactor());
    m_suppressPush = false;
}

void TransectPropertyBag::pushRoughness_()
{
    if (!m_provider) return;
    m_provider->setRoughness(m_nLeft, m_nRight, m_nChannel);
}

void TransectPropertyBag::pushBank_()
{
    if (!m_provider) return;
    m_provider->setBankStations(m_xLeftBank, m_xRightBank);
}

void TransectPropertyBag::pushEncroach_()
{
    if (!m_provider) return;
    m_provider->setEncroachmentStations(m_xLeftEnc, m_xRightEnc);
}

void TransectPropertyBag::pushModifiers_()
{
    if (!m_provider) return;
    m_provider->setModifiers(m_xFactor, m_yFactor, m_lengthFactor);
}

} // namespace openswmmvis::ui
