/*!
 * \file   inletprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "inlet/inletprovider.h"

namespace openswmmvis::inlet {

bool operator==(const InletDesignData &a, const InletDesignData &b)
{
    return a.type        == b.type
        && a.grateLength == b.grateLength
        && a.grateWidth  == b.grateWidth
        && a.grateType   == b.grateType
        && a.openArea    == b.openArea
        && a.splashVeloc == b.splashVeloc
        && a.curbLength  == b.curbLength
        && a.curbHeight  == b.curbHeight
        && a.throat      == b.throat
        && a.slotLength  == b.slotLength
        && a.slotWidth   == b.slotWidth
        && a.curveId     == b.curveId
        && a.curveKind   == b.curveKind;
}

QString inletTypeLabel(InletType t)
{
    switch (t) {
    case InletType::Grate:     return QStringLiteral("GRATE");
    case InletType::Curb:      return QStringLiteral("CURB OPENING");
    case InletType::Combo:     return QStringLiteral("COMBINATION");
    case InletType::Slotted:   return QStringLiteral("SLOTTED DRAIN");
    case InletType::DropGrate: return QStringLiteral("DROP GRATE");
    case InletType::DropCurb:  return QStringLiteral("DROP CURB");
    case InletType::Custom:    return QStringLiteral("CUSTOM");
    }
    return QStringLiteral("GRATE");
}

const char *inletTypeKeyword(InletType t)
{
    // Exactly the engine's kInletTypeWords (openswmm_infrastructure_impl.cpp),
    // which is also the legacy [INLETS] vocabulary.
    switch (t) {
    case InletType::Grate:     return "GRATE";
    case InletType::Curb:      return "CURB";
    case InletType::Combo:     return "COMBO";
    case InletType::Slotted:   return "SLOTTED";
    case InletType::DropGrate: return "DROP_GRATE";
    case InletType::DropCurb:  return "DROP_CURB";
    case InletType::Custom:    return "CUSTOM";
    }
    return "GRATE";
}

QString grateTypeLabel(GrateType t)
{
    switch (t) {
    case GrateType::PBar50:     return QStringLiteral("P_BAR-50");
    case GrateType::PBar50x100: return QStringLiteral("P_BAR-50x100");
    case GrateType::PBar30:     return QStringLiteral("P_BAR-30");
    case GrateType::CurvedVane: return QStringLiteral("CURVED_VANE");
    case GrateType::TiltBar45:  return QStringLiteral("TILT_BAR-45");
    case GrateType::TiltBar30:  return QStringLiteral("TILT_BAR-30");
    case GrateType::Reticuline: return QStringLiteral("RETICULINE");
    case GrateType::Generic:    return QStringLiteral("GENERIC");
    }
    return QStringLiteral("GENERIC");
}

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

void InletProvider::setComments(QString text)
{
    if (text == m_comments) return;
    m_comments = std::move(text);
    emit commentsChanged();
}

void InletProvider::setDesign(const InletDesignData &d)
{
    if (d == m_design) return;
    m_design = d;
    emit paramsChanged();
}

void InletProvider::setType(InletType v)
{
    if (v == m_design.type) return;
    m_design.type = v;
    emit paramsChanged();
}

void InletProvider::setGrateLength(double v)
{
    if (v == m_design.grateLength) return;
    m_design.grateLength = v;
    emit paramsChanged();
}

void InletProvider::setGrateWidth(double v)
{
    if (v == m_design.grateWidth) return;
    m_design.grateWidth = v;
    emit paramsChanged();
}

void InletProvider::setGrateType(GrateType v)
{
    if (v == m_design.grateType) return;
    m_design.grateType = v;
    emit paramsChanged();
}

void InletProvider::setOpenArea(double v)
{
    if (v == m_design.openArea) return;
    m_design.openArea = v;
    emit paramsChanged();
}

void InletProvider::setSplashVeloc(double v)
{
    if (v == m_design.splashVeloc) return;
    m_design.splashVeloc = v;
    emit paramsChanged();
}

void InletProvider::setCurbLength(double v)
{
    if (v == m_design.curbLength) return;
    m_design.curbLength = v;
    emit paramsChanged();
}

void InletProvider::setCurbHeight(double v)
{
    if (v == m_design.curbHeight) return;
    m_design.curbHeight = v;
    emit paramsChanged();
}

void InletProvider::setThroat(ThroatType v)
{
    if (v == m_design.throat) return;
    m_design.throat = v;
    emit paramsChanged();
}

void InletProvider::setSlotLength(double v)
{
    if (v == m_design.slotLength) return;
    m_design.slotLength = v;
    emit paramsChanged();
}

void InletProvider::setSlotWidth(double v)
{
    if (v == m_design.slotWidth) return;
    m_design.slotWidth = v;
    emit paramsChanged();
}

void InletProvider::setCurveId(QString v)
{
    if (v == m_design.curveId) return;
    m_design.curveId = std::move(v);
    emit paramsChanged();
}

void InletProvider::setCurveKind(InletCurveKind v)
{
    if (v == m_design.curveKind) return;
    m_design.curveKind = v;
    emit paramsChanged();
}

} // namespace openswmmvis::inlet
