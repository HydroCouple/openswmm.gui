/*!
 * \file   swmmlinkpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AG.3 — Property-tree adapter for SWMM links.  Mirrors the
 * per-node-type adapter design: a thin `Q_OBJECT` base over
 * `(engine, name)` exposing universal link attributes, plus one
 * subclass per LinkType with the Q_PROPERTYs that the corresponding
 * SWMM5 .inp section actually carries.  The Property Browser dock
 * picks the right subclass per identified link.
 */

#ifndef SWMMLINKPROPERTYADAPTER_H
#define SWMMLINKPROPERTYADAPTER_H

#include <QObject>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

/*! Base link adapter — exposes the attributes that ALL link types
 *  share (name, kind, endpoints).  Per-type Q_PROPERTYs live on
 *  subclasses below so the Property Browser only shows attributes
 *  applicable to the identified link's kind. */
class SWMMLinkPropertyAdapter : public QObject
{
    Q_OBJECT

public:
    /*! Type-safe enums declared with Q_ENUM so QPropertyModel
     *  renders the key name ("CONDUIT", "ON", "TRANSVERSE") instead
     *  of the raw integer.  Subclasses inherit these via
     *  metaObject() — no need to redeclare. */
    enum LinkKind     { Conduit = 0, Pump = 1, Orifice = 2, Weir = 3, Outlet = 4 };
    enum FlapGate     { NO = 0, YES = 1 };
    enum PumpInitState { OFF = 0, ON = 1 };
    Q_ENUM(LinkKind)
    Q_ENUM(FlapGate)
    Q_ENUM(PumpInitState)

    // Common to every link type.
    Q_PROPERTY(QString  name         READ name)
    Q_PROPERTY(LinkKind linkKind     READ linkKind)
    Q_PROPERTY(QString  fromNode     READ fromNode)
    Q_PROPERTY(QString  toNode       READ toNode)

    SWMMLinkPropertyAdapter(SWMM_Engine engine, QString name,
                              QObject *parent = nullptr);

    [[nodiscard]] QString  name() const     { return m_name; }
    [[nodiscard]] LinkKind linkKind() const;
    [[nodiscard]] QString  fromNode() const;
    [[nodiscard]] QString  toNode() const;

    [[nodiscard]] double length()           const;
    [[nodiscard]] double roughness()        const;
    [[nodiscard]] double offsetUp()         const;
    [[nodiscard]] double offsetDn()         const;
    [[nodiscard]] double crestHeight()      const;
    [[nodiscard]] double dischargeCoeff()   const;
    [[nodiscard]] double endContractions()  const;
    [[nodiscard]] FlapGate      flapGate()      const;
    [[nodiscard]] PumpInitState pumpInitState() const;
    [[nodiscard]] QString       pumpCurveName() const;

    /*! See SWMMNodePropertyAdapter::displayLabelFor — same contract,
     *  returns "" for unknown property names. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setLength(double v);
    void setRoughness(double v);
    void setOffsetUp(double v);
    void setOffsetDn(double v);
    void setCrestHeight(double v);
    void setDischargeCoeff(double v);
    void setEndContractions(double v);
    void setFlapGate(FlapGate v);
    void setPumpInitState(PumpInitState v);

    /*! Round-4 follow-up — see SWMMNodePropertyAdapter::refresh. */
    void refresh() { emit changed(); }

signals:
    void changed();
    /*! See SWMMNodePropertyAdapter::displayLabelsChanged. */
    void displayLabelsChanged();

protected:
    [[nodiscard]] int linkIdx() const;
    SWMM_Engine m_engine;
    QString     m_name;
};

/*! Conduit adapter — `[CONDUITS]` columns:
 *  Name, FromNode, ToNode, Length, Roughness, InOffset, OutOffset,
 *  InitFlow, MaxFlow.  Init/Max flow are set-only in the engine API
 *  today so they're not round-trippable; flap gate + culvert code
 *  are extensions surfaced from the table delegate. */
class SWMMConduitPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double length     READ length     WRITE setLength    NOTIFY changed)
    Q_PROPERTY(double roughness  READ roughness  WRITE setRoughness NOTIFY changed)
    Q_PROPERTY(double offsetUp   READ offsetUp   WRITE setOffsetUp  NOTIFY changed)
    Q_PROPERTY(double offsetDn   READ offsetDn   WRITE setOffsetDn  NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Pump adapter — `[PUMPS]` columns:
 *  Name, FromNode, ToNode, PumpCurve, Status, Startup, Shutoff.
 *  Startup/Shutoff depth accessors don't exist in the engine yet;
 *  pump curve is shown as a read-only name lookup. */
class SWMMPumpPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(QString pumpCurve READ pumpCurveName)
    Q_PROPERTY(SWMMLinkPropertyAdapter::PumpInitState initState
               READ pumpInitState WRITE setPumpInitState NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Orifice adapter — `[ORIFICES]` columns:
 *  Name, FromNode, ToNode, Type, Offset, Cd, Gated.  Type
 *  (SIDE/BOTTOM) has no engine accessor today; deferred to AG.4. */
class SWMMOrificePropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double offset         READ offsetUp        WRITE setOffsetUp        NOTIFY changed)
    Q_PROPERTY(double dischargeCoeff READ dischargeCoeff  WRITE setDischargeCoeff  NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Weir adapter — `[WEIRS]` columns:
 *  Name, FromNode, ToNode, Type, CrestHt, Cd, Gated, EndCon,
 *  EndCoeff.  Type (TRANSVERSE/SIDEFLOW/V-NOTCH/TRAPEZOIDAL) and
 *  EndCoeff have no engine accessors today; deferred to AG.4. */
class SWMMWeirPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double offsetUp        READ offsetUp        WRITE setOffsetUp        NOTIFY changed)
    Q_PROPERTY(double offsetDn        READ offsetDn        WRITE setOffsetDn        NOTIFY changed)
    Q_PROPERTY(double crestHeight     READ crestHeight     WRITE setCrestHeight     NOTIFY changed)
    Q_PROPERTY(double dischargeCoeff  READ dischargeCoeff  WRITE setDischargeCoeff  NOTIFY changed)
    Q_PROPERTY(double endContractions READ endContractions WRITE setEndContractions NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

/*! Outlet adapter — `[OUTLETS]` columns:
 *  Name, FromNode, ToNode, Offset, Type, Coeff, Expon.  Curve /
 *  functional type-specific params land in AG.4. */
class SWMMOutletPropertyAdapter : public SWMMLinkPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double offset         READ offsetUp        WRITE setOffsetUp        NOTIFY changed)
    Q_PROPERTY(SWMMLinkPropertyAdapter::FlapGate flapGate
               READ flapGate WRITE setFlapGate NOTIFY changed)
public:
    using SWMMLinkPropertyAdapter::SWMMLinkPropertyAdapter;
};

#endif // SWMMLINKPROPERTYADAPTER_H
