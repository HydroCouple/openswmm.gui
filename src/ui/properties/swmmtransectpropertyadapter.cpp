/**
 * @file      swmmtransectpropertyadapter.cpp
 * @brief     Slice DA.2 / Phase 6.7.4 — Property-tree adapter for [TRANSECTS].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/properties/swmmtransectpropertyadapter.h"

#include <openswmm/engine/openswmm_infrastructure.h>

int SWMMTransectPropertyAdapter::idx() const
{
    if (!m_engine || m_name.isEmpty())
        return -1;
    return swmm_transect_index(m_engine, m_name.toUtf8().constData());
}

QString SWMMTransectPropertyAdapter::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("name"))
        return tr("Name");
    if (property == QLatin1String("comments"))
        return tr("Description");
    if (property == QLatin1String("nLeftBank"))
        return tr("Roughness — Left Bank n");
    if (property == QLatin1String("nRightBank"))
        return tr("Roughness — Right Bank n");
    if (property == QLatin1String("nChannel"))
        return tr("Roughness — Channel n");
    if (property == QLatin1String("xLeftBank"))
        return tr("Bank Stations — Left");
    if (property == QLatin1String("xRightBank"))
        return tr("Bank Stations — Right");
    if (property == QLatin1String("xLeftEncroachment"))
        return tr("Encroachment Stations — Left");
    if (property == QLatin1String("xRightEncroachment"))
        return tr("Encroachment Stations — Right");
    if (property == QLatin1String("stationMultiplier"))
        return tr("Modifiers — Stations Multiplier");
    if (property == QLatin1String("elevationOffset"))
        return tr("Modifiers — Elevations Offset");
    if (property == QLatin1String("meanderFactor"))
        return tr("Modifiers — Meander Factor");
    if (property == QLatin1String("stationCount"))
        return tr("Station Count");
    return {};
}

// ── Comments ─────────────────────────────────────────────────────────────────

QString SWMMTransectPropertyAdapter::comments() const
{
    const int i = idx();
    if (i < 0)
        return {};
    char buf[2048] = {};
    if (swmm_transect_get_comments(m_engine, i, buf, sizeof(buf)) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

void SWMMTransectPropertyAdapter::setComments(const QString &v)
{
    const int i = idx();
    if (i < 0)
        return;
    if (swmm_transect_set_comments(m_engine, i, v.toUtf8().constData()) == SWMM_OK)
        emit changed();
}

// ── Roughness triple (engine ships one atomic setter for all three) ──────────

namespace
{

/**
 * @brief Read one sibling tuple for transect property edits.
 * @param[in] eng Borrowed engine, possibly nullptr.
 * @param[in] i Zero-based transect index; negative fails without reading.
 * @param[out] nL Left-bank Manning coefficient.
 * @param[out] nR Right-bank Manning coefficient.
 * @param[out] nCh Channel Manning coefficient.
 * @return True only after a successful typed engine read; fallback components are initialized before the call (zero except modifier multipliers).
 */
bool readRoughness(SWMM_Engine eng, int i, double &nL, double &nR, double &nCh)
{
    nL = nR = nCh = 0.0;
    return eng && i >= 0 && swmm_transect_get_roughness(eng, i, &nL, &nR, &nCh) == SWMM_OK;
}

/**
 * @brief Read one sibling tuple for transect property edits.
 * @param[in] eng Borrowed engine, possibly nullptr.
 * @param[in] i Zero-based transect index; negative fails without reading.
 * @param[out] xL Left-bank station in authored station units.
 * @param[out] xR Right-bank station in authored station units.
 * @return True only after a successful typed engine read; fallback components are initialized before the call (zero except modifier multipliers).
 */
bool readBank(SWMM_Engine eng, int i, double &xL, double &xR)
{
    xL = xR = 0.0;
    return eng && i >= 0 && swmm_transect_get_bank_stations(eng, i, &xL, &xR) == SWMM_OK;
}

/**
 * @brief Read one sibling tuple for transect property edits.
 * @param[in] eng Borrowed engine, possibly nullptr.
 * @param[in] i Zero-based transect index; negative fails without reading.
 * @param[out] xL Left encroachment station.
 * @param[out] xR Right encroachment station.
 * @return True only after a successful typed engine read; fallback components are initialized before the call (zero except modifier multipliers).
 */
bool readEncroach(SWMM_Engine eng, int i, double &xL, double &xR)
{
    xL = xR = 0.0;
    return eng && i >= 0 && swmm_transect_get_encroachment_stations(eng, i, &xL, &xR) == SWMM_OK;
}

/**
 * @brief Read one sibling tuple for transect property edits.
 * @param[in] eng Borrowed engine, possibly nullptr.
 * @param[in] i Zero-based transect index; negative fails without reading.
 * @param[out] xF Dimensionless station multiplier, initialized to one.
 * @param[out] yF Elevation offset, initialized to zero.
 * @param[out] lF Dimensionless meander multiplier, initialized to one.
 * @return True only after a successful typed engine read; fallback components are initialized before the call (zero except modifier multipliers).
 */
bool readModifiers(SWMM_Engine eng, int i, double &xF, double &yF, double &lF)
{
    xF = 1.0;
    yF = 0.0;
    lF = 1.0;
    return eng && i >= 0 && swmm_transect_get_modifiers(eng, i, &xF, &yF, &lF) == SWMM_OK;
}

} // namespace

double SWMMTransectPropertyAdapter::nLeftBank() const
{
    double nL, nR, nCh;
    readRoughness(m_engine, idx(), nL, nR, nCh);
    return nL;
}

double SWMMTransectPropertyAdapter::nRightBank() const
{
    double nL, nR, nCh;
    readRoughness(m_engine, idx(), nL, nR, nCh);
    return nR;
}

double SWMMTransectPropertyAdapter::nChannel() const
{
    double nL, nR, nCh;
    readRoughness(m_engine, idx(), nL, nR, nCh);
    return nCh;
}

void SWMMTransectPropertyAdapter::setNLeftBank(double v)
{
    const int i = idx();
    double nL, nR, nCh;
    if (!readRoughness(m_engine, i, nL, nR, nCh))
        return;
    if (v == nL)
        return;
    if (swmm_transect_set_roughness(m_engine, i, v, nR, nCh) == SWMM_OK)
        emit changed();
}

void SWMMTransectPropertyAdapter::setNRightBank(double v)
{
    const int i = idx();
    double nL, nR, nCh;
    if (!readRoughness(m_engine, i, nL, nR, nCh))
        return;
    if (v == nR)
        return;
    if (swmm_transect_set_roughness(m_engine, i, nL, v, nCh) == SWMM_OK)
        emit changed();
}

void SWMMTransectPropertyAdapter::setNChannel(double v)
{
    const int i = idx();
    double nL, nR, nCh;
    if (!readRoughness(m_engine, i, nL, nR, nCh))
        return;
    if (v == nCh)
        return;
    if (swmm_transect_set_roughness(m_engine, i, nL, nR, v) == SWMM_OK)
        emit changed();
}

// ── Bank stations ────────────────────────────────────────────────────────────

double SWMMTransectPropertyAdapter::xLeftBank() const
{
    double xL, xR;
    readBank(m_engine, idx(), xL, xR);
    return xL;
}
double SWMMTransectPropertyAdapter::xRightBank() const
{
    double xL, xR;
    readBank(m_engine, idx(), xL, xR);
    return xR;
}

void SWMMTransectPropertyAdapter::setXLeftBank(double v)
{
    const int i = idx();
    double xL, xR;
    if (!readBank(m_engine, i, xL, xR))
        return;
    if (v == xL)
        return;
    if (swmm_transect_set_bank_stations(m_engine, i, v, xR) == SWMM_OK)
        emit changed();
}

void SWMMTransectPropertyAdapter::setXRightBank(double v)
{
    const int i = idx();
    double xL, xR;
    if (!readBank(m_engine, i, xL, xR))
        return;
    if (v == xR)
        return;
    if (swmm_transect_set_bank_stations(m_engine, i, xL, v) == SWMM_OK)
        emit changed();
}

// ── Encroachment stations ────────────────────────────────────────────────────

double SWMMTransectPropertyAdapter::xLeftEncroachment() const
{
    double xL, xR;
    readEncroach(m_engine, idx(), xL, xR);
    return xL;
}
double SWMMTransectPropertyAdapter::xRightEncroachment() const
{
    double xL, xR;
    readEncroach(m_engine, idx(), xL, xR);
    return xR;
}

void SWMMTransectPropertyAdapter::setXLeftEncroachment(double v)
{
    const int i = idx();
    double xL, xR;
    if (!readEncroach(m_engine, i, xL, xR))
        return;
    if (v == xL)
        return;
    if (swmm_transect_set_encroachment_stations(m_engine, i, v, xR) == SWMM_OK)
        emit changed();
}

void SWMMTransectPropertyAdapter::setXRightEncroachment(double v)
{
    const int i = idx();
    double xL, xR;
    if (!readEncroach(m_engine, i, xL, xR))
        return;
    if (v == xR)
        return;
    if (swmm_transect_set_encroachment_stations(m_engine, i, xL, v) == SWMM_OK)
        emit changed();
}

// ── Modifiers ────────────────────────────────────────────────────────────────

double SWMMTransectPropertyAdapter::stationMultiplier() const
{
    double xF, yF, lF;
    readModifiers(m_engine, idx(), xF, yF, lF);
    return xF;
}
double SWMMTransectPropertyAdapter::elevationOffset() const
{
    double xF, yF, lF;
    readModifiers(m_engine, idx(), xF, yF, lF);
    return yF;
}
double SWMMTransectPropertyAdapter::meanderFactor() const
{
    double xF, yF, lF;
    readModifiers(m_engine, idx(), xF, yF, lF);
    return lF;
}

void SWMMTransectPropertyAdapter::setStationMultiplier(double v)
{
    const int i = idx();
    double xF, yF, lF;
    if (!readModifiers(m_engine, i, xF, yF, lF))
        return;
    if (v == xF)
        return;
    if (swmm_transect_set_modifiers(m_engine, i, v, yF, lF) == SWMM_OK)
        emit changed();
}

void SWMMTransectPropertyAdapter::setElevationOffset(double v)
{
    const int i = idx();
    double xF, yF, lF;
    if (!readModifiers(m_engine, i, xF, yF, lF))
        return;
    if (v == yF)
        return;
    if (swmm_transect_set_modifiers(m_engine, i, xF, v, lF) == SWMM_OK)
        emit changed();
}

void SWMMTransectPropertyAdapter::setMeanderFactor(double v)
{
    const int i = idx();
    double xF, yF, lF;
    if (!readModifiers(m_engine, i, xF, yF, lF))
        return;
    if (v == lF)
        return;
    if (swmm_transect_set_modifiers(m_engine, i, xF, yF, v) == SWMM_OK)
        emit changed();
}

// ── Station count (read-only summary) ────────────────────────────────────────

int SWMMTransectPropertyAdapter::stationCount() const
{
    const int i = idx();
    if (i < 0)
        return 0;
    const int n = swmm_transect_get_station_count(m_engine, i);
    return n < 0 ? 0 : n;
}
