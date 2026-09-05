/*!
 * \file   inletprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM inlet design ([INLETS]).
 *
 * One InletProvider per `[INLETS]` entry. Owned by a project-scoped
 * InletRegistry. Engine I/O walks the full-design surface of
 * openswmm_infrastructure.h — `swmm_inlet_get_design` / `swmm_inlet_set_design`
 * plus `swmm_inlet_get_comment` / `swmm_inlet_set_comment` — so every field of
 * the `[INLETS]` grammar (curb height, throat angle, combination inlets,
 * custom capture curves) round-trips.
 *
 * The provider is the single source of truth for one design: the editor's
 * list, property tree and drawing view all subscribe to its signals.
 * `InletDesignData` mirrors `SWMM_InletDesign` field-for-field so a snapshot
 * can be taken for undo (see inletundocommands.h) and pushed to the engine
 * without an intermediate translation table.
 */
#ifndef OPENSWMMVIS_INLET_INLETPROVIDER_H
#define OPENSWMMVIS_INLET_INLETPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::inlet {

/*! \brief Inlet design type. Values match `SWMM_InletType`. */
enum class InletType {
    Grate     = 0,
    Curb      = 1,
    Combo     = 2,
    Slotted   = 3,
    DropGrate = 4,
    DropCurb  = 5,
    Custom    = 6,
};

/*! \brief Grate bar pattern (HEC-22). Values match `SWMM_GrateType`. */
enum class GrateType {
    PBar50     = 0,
    PBar50x100 = 1,
    PBar30     = 2,
    CurvedVane = 3,
    TiltBar45  = 4,
    TiltBar30  = 5,
    Reticuline = 6,
    Generic    = 7,
};

/*! \brief Curb-opening throat orientation. Values match `SWMM_ThroatType`. */
enum class ThroatType {
    Horizontal = 0,
    Inclined   = 1,
    Vertical   = 2,
};

/*! \brief Custom-inlet capture-curve flavour. Values match
 *  `SWMM_InletCurveKind`. */
enum class InletCurveKind {
    None      = 0,
    Diversion = 1,   ///< captured flow vs approach flow
    Rating    = 2,   ///< captured flow vs water depth
};

/*! \brief Every parameter of one inlet design, mirroring `SWMM_InletDesign`.
 *  Snapshot type for the undo commands and the engine round-trip. */
struct InletDesignData
{
    InletType      type         = InletType::Grate;

    // GRATE / DROP_GRATE / COMBO
    double         grateLength  = 2.0;
    double         grateWidth   = 2.0;
    GrateType      grateType    = GrateType::PBar50;
    double         openArea     = 0.8;    ///< GENERIC only, fraction (0,1]
    double         splashVeloc  = 0.0;    ///< GENERIC only

    // CURB / DROP_CURB / COMBO
    double         curbLength   = 2.0;
    double         curbHeight   = 0.5;
    ThroatType     throat       = ThroatType::Vertical;

    // SLOTTED
    double         slotLength   = 2.0;
    double         slotWidth    = 0.2;

    // CUSTOM
    QString        curveId;
    InletCurveKind curveKind    = InletCurveKind::None;
};

class InletProvider : public QObject
{
    Q_OBJECT

public:
    explicit InletProvider(QString name, QObject *parent = nullptr);
    ~InletProvider() override;

    QString name()     const noexcept { return m_name; }
    QString comments() const          { return m_comments; }

    /*! \brief Whole-design snapshot (undo + engine flush). */
    const InletDesignData &design() const noexcept { return m_design; }

    InletType      type()        const noexcept { return m_design.type; }
    double         grateLength() const noexcept { return m_design.grateLength; }
    double         grateWidth()  const noexcept { return m_design.grateWidth; }
    GrateType      grateType()   const noexcept { return m_design.grateType; }
    double         openArea()    const noexcept { return m_design.openArea; }
    double         splashVeloc() const noexcept { return m_design.splashVeloc; }
    double         curbLength()  const noexcept { return m_design.curbLength; }
    double         curbHeight()  const noexcept { return m_design.curbHeight; }
    ThroatType     throat()      const noexcept { return m_design.throat; }
    double         slotLength()  const noexcept { return m_design.slotLength; }
    double         slotWidth()   const noexcept { return m_design.slotWidth; }
    QString        curveId()     const          { return m_design.curveId; }
    InletCurveKind curveKind()   const noexcept { return m_design.curveKind; }

    void setName(QString newName);
    void setComments(QString text);

    /*! \brief Replace every design field at once. Emits `paramsChanged()`
     *  once if anything actually changed. */
    void setDesign(const InletDesignData &d);

    void setType(InletType v);
    void setGrateLength(double v);
    void setGrateWidth(double v);
    void setGrateType(GrateType v);
    void setOpenArea(double v);
    void setSplashVeloc(double v);
    void setCurbLength(double v);
    void setCurbHeight(double v);
    void setThroat(ThroatType v);
    void setSlotLength(double v);
    void setSlotWidth(double v);
    void setCurveId(QString v);
    void setCurveKind(InletCurveKind v);

signals:
    void nameChanged(QString prev, QString now);
    void paramsChanged();
    void commentsChanged();

private:
    QString         m_name;
    QString         m_comments;
    InletDesignData m_design;
};

/*! \brief Field-by-field equality — used by `setDesign` to suppress no-op
 *  notifications and by the undo commands to skip empty pushes. */
bool operator==(const InletDesignData &a, const InletDesignData &b);
inline bool operator!=(const InletDesignData &a, const InletDesignData &b)
{ return !(a == b); }

/*! \brief Legacy editor label for a type ("GRATE", "CURB OPENING",
 *  "COMBINATION", "SLOTTED DRAIN", "DROP GRATE", "DROP CURB", "CUSTOM").
 *  These are the strings the type combo shows (Uinlet.pas:31-44). */
QString inletTypeLabel(InletType t);

/*! \brief `[INLETS]` type keyword handed to `swmm_inlet_add`. A COMBINATION
 *  design is created as a GRATE and then promoted by `swmm_inlet_set_design`,
 *  which is the authority on the stored type. */
const char *inletTypeKeyword(InletType t);

/*! \brief HEC-22 grate name ("P_BAR-50", "CURVED_VANE", "GENERIC", …). */
QString grateTypeLabel(GrateType t);

} // namespace openswmmvis::inlet

#endif // OPENSWMMVIS_INLET_INLETPROVIDER_H
