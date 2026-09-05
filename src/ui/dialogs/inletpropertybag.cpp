/*!
 * \file   inletpropertybag.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/inletpropertybag.h"

#include "core/unitsystem.h"

namespace openswmmvis::ui {

using openswmmvis::inlet::InletDesignData;
using openswmmvis::inlet::InletProvider;
using openswmmvis::inlet::InletType;

namespace {

QString lengthUnit()
{
    return UnitSystem::instance()->lengthLabel();
}

QString velocityUnit()
{
    return UnitSystem::instance()->velocityLabel();
}

} // namespace

InletPropertyBag::InletPropertyBag(QObject *parent)
    : QObject(parent)
{
}

void InletPropertyBag::bind(InletProvider *p)
{
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<InletProvider>(p);
    if (m_provider) {
        connect(m_provider, &InletProvider::paramsChanged,
                this, &InletPropertyBag::onProviderParamsChanged_);
    }
    refreshFromProvider_();
}

InletProvider *InletPropertyBag::provider() const noexcept
{
    return m_provider.data();
}

void InletPropertyBag::setCommitHandler(CommitFn fn)
{
    m_commit = std::move(fn);
}

// ── Group / visibility table ────────────────────────────────────────────────

InletPropertyBag::Groups InletPropertyBag::groupsFor(InletType t)
{
    switch (t) {
    case InletType::Grate:     return GrateGroup;
    case InletType::Curb:      return CurbGroup;
    case InletType::Combo:     return GrateGroup | CurbGroup;
    case InletType::Slotted:   return SlottedGroup;
    case InletType::DropGrate: return GrateGroup;
    case InletType::DropCurb:  return CurbGroup;
    case InletType::Custom:    return CustomGroup;
    }
    return NoGroup;
}

InletPropertyBag::Groups InletPropertyBag::visibleGroups() const
{
    if (!m_provider) return NoGroup;
    return groupsFor(m_provider->type());
}

InletPropertyBag::Group InletPropertyBag::groupOf(const QString &property)
{
    if (property == QLatin1String("grateType")
        || property == QLatin1String("grateLength")
        || property == QLatin1String("grateWidth")
        || property == QLatin1String("openFraction")
        || property == QLatin1String("splashVelocity"))
        return GrateGroup;

    if (property == QLatin1String("curbLength")
        || property == QLatin1String("curbHeight")
        || property == QLatin1String("throatAngle"))
        return CurbGroup;

    if (property == QLatin1String("slotLength")
        || property == QLatin1String("slotWidth"))
        return SlottedGroup;

    if (property == QLatin1String("curveKind")
        || property == QLatin1String("curveId"))
        return CustomGroup;

    return NoGroup;
}

bool InletPropertyBag::isPropertyVisible(const QString &property) const
{
    if (!m_provider) return false;
    const Group g = groupOf(property);
    if (g == NoGroup || !visibleGroups().testFlag(g)) return false;

    // Legacy exceptions (Dinlet.pas:241-298).
    if (property == QLatin1String("openFraction")
        || property == QLatin1String("splashVelocity"))
        return m_grateType == GrateTypeQ::GENERIC;

    if (property == QLatin1String("throatAngle"))
        return m_provider->type() != InletType::DropCurb;

    return true;
}

QString InletPropertyBag::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("grateType"))
        return tr("Grate — Type");
    if (property == QLatin1String("grateLength"))
        return tr("Grate — Length (%1)").arg(lengthUnit());
    if (property == QLatin1String("grateWidth"))
        return tr("Grate — Width (%1)").arg(lengthUnit());
    if (property == QLatin1String("openFraction"))
        return tr("Grate — Open Area Fraction");
    if (property == QLatin1String("splashVelocity"))
        return tr("Grate — Splash-over Velocity (%1)").arg(velocityUnit());

    if (property == QLatin1String("curbLength"))
        return tr("Curb Opening — Length (%1)").arg(lengthUnit());
    if (property == QLatin1String("curbHeight"))
        return tr("Curb Opening — Height (%1)").arg(lengthUnit());
    if (property == QLatin1String("throatAngle"))
        return tr("Curb Opening — Throat Angle");

    if (property == QLatin1String("slotLength"))
        return tr("Slotted Drain — Length (%1)").arg(lengthUnit());
    if (property == QLatin1String("slotWidth"))
        return tr("Slotted Drain — Width (%1)").arg(lengthUnit());

    if (property == QLatin1String("curveKind"))
        return tr("Custom — Curve Type");
    if (property == QLatin1String("curveId"))
        return tr("Custom — Curve");

    return {};
}

// ── Provider ↔ bag plumbing ─────────────────────────────────────────────────

InletDesignData InletPropertyBag::composed_() const
{
    InletDesignData d = m_provider ? m_provider->design() : InletDesignData{};
    d.grateType   = static_cast<inlet::GrateType>(m_grateType);
    d.grateLength = m_grateLength;
    d.grateWidth  = m_grateWidth;
    d.openArea    = m_openFraction;
    d.splashVeloc = m_splashVelocity;
    d.curbLength  = m_curbLength;
    d.curbHeight  = m_curbHeight;
    d.throat      = static_cast<inlet::ThroatType>(m_throatAngle);
    d.slotLength  = m_slotLength;
    d.slotWidth   = m_slotWidth;
    d.curveKind   = static_cast<inlet::InletCurveKind>(m_curveKind);
    d.curveId     = m_curveId;
    return d;
}

void InletPropertyBag::push_()
{
    if (m_suppressPush || !m_provider) return;
    const InletDesignData before = m_provider->design();
    const InletDesignData after  = composed_();
    if (after == before) return;
    if (m_commit) m_commit(before, after);
    else          m_provider->setDesign(after);
}

void InletPropertyBag::refreshFromProvider_()
{
    m_suppressPush = true;
    if (m_provider) {
        const InletDesignData &d = m_provider->design();
        setGrateType(static_cast<GrateTypeQ>(d.grateType));
        setGrateLength(d.grateLength);
        setGrateWidth(d.grateWidth);
        setOpenFraction(d.openArea);
        setSplashVelocity(d.splashVeloc);
        setCurbLength(d.curbLength);
        setCurbHeight(d.curbHeight);
        setThroatAngle(static_cast<ThroatTypeQ>(d.throat));
        setSlotLength(d.slotLength);
        setSlotWidth(d.slotWidth);
        setCurveKind(static_cast<CurveKindQ>(d.curveKind));
        setCurveId(d.curveId);
    } else {
        const InletDesignData d{};
        setGrateType(static_cast<GrateTypeQ>(d.grateType));
        setGrateLength(0.0);
        setGrateWidth(0.0);
        setOpenFraction(0.0);
        setSplashVelocity(0.0);
        setCurbLength(0.0);
        setCurbHeight(0.0);
        setThroatAngle(static_cast<ThroatTypeQ>(d.throat));
        setSlotLength(0.0);
        setSlotWidth(0.0);
        setCurveKind(CurveKindQ::NONE);
        setCurveId(QString());
    }
    m_suppressPush = false;
}

void InletPropertyBag::onProviderParamsChanged_()
{
    refreshFromProvider_();
}

// ── Setters ─────────────────────────────────────────────────────────────────

void InletPropertyBag::setGrateType(GrateTypeQ v)
{
    if (v == m_grateType) return;
    m_grateType = v;
    emit grateTypeChanged(v);
    push_();
}

void InletPropertyBag::setGrateLength(double v)
{
    if (v == m_grateLength) return;
    m_grateLength = v;
    emit grateLengthChanged(v);
    push_();
}

void InletPropertyBag::setGrateWidth(double v)
{
    if (v == m_grateWidth) return;
    m_grateWidth = v;
    emit grateWidthChanged(v);
    push_();
}

void InletPropertyBag::setOpenFraction(double v)
{
    if (v == m_openFraction) return;
    m_openFraction = v;
    emit openFractionChanged(v);
    push_();
}

void InletPropertyBag::setSplashVelocity(double v)
{
    if (v == m_splashVelocity) return;
    m_splashVelocity = v;
    emit splashVelocityChanged(v);
    push_();
}

void InletPropertyBag::setCurbLength(double v)
{
    if (v == m_curbLength) return;
    m_curbLength = v;
    emit curbLengthChanged(v);
    push_();
}

void InletPropertyBag::setCurbHeight(double v)
{
    if (v == m_curbHeight) return;
    m_curbHeight = v;
    emit curbHeightChanged(v);
    push_();
}

void InletPropertyBag::setThroatAngle(ThroatTypeQ v)
{
    if (v == m_throatAngle) return;
    m_throatAngle = v;
    emit throatAngleChanged(v);
    push_();
}

void InletPropertyBag::setSlotLength(double v)
{
    if (v == m_slotLength) return;
    m_slotLength = v;
    emit slotLengthChanged(v);
    push_();
}

void InletPropertyBag::setSlotWidth(double v)
{
    if (v == m_slotWidth) return;
    m_slotWidth = v;
    emit slotWidthChanged(v);
    push_();
}

void InletPropertyBag::setCurveKind(CurveKindQ v)
{
    if (v == m_curveKind) return;
    m_curveKind = v;
    emit curveKindChanged(v);
    push_();
}

void InletPropertyBag::setCurveId(const QString &v)
{
    if (v == m_curveId) return;
    m_curveId = v;
    emit curveIdChanged(v);
    push_();
}

} // namespace openswmmvis::ui
