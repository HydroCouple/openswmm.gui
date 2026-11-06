/**
 * @file      inletprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC model for a single SWMM inlet design ([INLETS]).
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_INLET_INLETPROVIDER_H
#define OPENSWMMVIS_INLET_INLETPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::inlet
{

/*! \brief Inlet design type. Values match `SWMM_InletType`. */
enum class InletType {
    Grate = 0,
    Curb = 1,
    Combo = 2,
    Slotted = 3,
    DropGrate = 4,
    DropCurb = 5,
    Custom = 6,
};

/*! \brief Grate bar pattern (HEC-22). Values match `SWMM_GrateType`. */
enum class GrateType {
    PBar50 = 0,
    PBar50x100 = 1,
    PBar30 = 2,
    CurvedVane = 3,
    TiltBar45 = 4,
    TiltBar30 = 5,
    Reticuline = 6,
    Generic = 7,
};

/*! \brief Curb-opening throat orientation. Values match `SWMM_ThroatType`. */
enum class ThroatType {
    Horizontal = 0,
    Inclined = 1,
    Vertical = 2,
};

/*! \brief Custom-inlet capture-curve flavour. Values match
 *  `SWMM_InletCurveKind`. */
enum class InletCurveKind {
    None = 0,
    Diversion = 1, ///< captured flow vs approach flow
    Rating = 2,    ///< captured flow vs water depth
};

/*! \brief Every parameter of one inlet design, mirroring `SWMM_InletDesign`.
 *  Snapshot type for the undo commands and the engine round-trip. */
struct InletDesignData {
    InletType type = InletType::Grate;

    // GRATE / DROP_GRATE / COMBO
    double grateLength = 2.0;
    double grateWidth = 2.0;
    GrateType grateType = GrateType::PBar50;
    double openArea = 0.8;    ///< GENERIC only, fraction (0,1]
    double splashVeloc = 0.0; ///< GENERIC only

    // CURB / DROP_CURB / COMBO
    double curbLength = 2.0;
    double curbHeight = 0.5;
    ThroatType throat = ThroatType::Vertical;

    // SLOTTED
    double slotLength = 2.0;
    double slotWidth = 0.2;

    // CUSTOM
    QString curveId;
    InletCurveKind curveKind = InletCurveKind::None;
};

class InletProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit InletProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~InletProvider() override;

    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }
    /**
     * @brief Read free-form INLETS comment text.
     * @return Copied comments, without engine retrieval or formatting changes.
     */
    QString comments() const { return m_comments; }

    /*! \brief Whole-design snapshot (undo + engine flush).
     * @return Borrowed local design reference; read before provider destruction or subsequent design edits. No engine validation occurs on access.
     */
    const InletDesignData &design() const noexcept { return m_design; }

    /**
     * @brief Read a local inlet-design parameter.
     * @return Design geometry category, Grate through Custom.
     */
    InletType type() const noexcept { return m_design.type; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Grate length in project length units (ft or m).
     */
    double grateLength() const noexcept { return m_design.grateLength; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Grate width in project length units (ft or m).
     */
    double grateWidth() const noexcept { return m_design.grateWidth; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return HEC-22 bar pattern, or Generic.
     */
    GrateType grateType() const noexcept { return m_design.grateType; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Generic-grate open-area fraction, expected in (0,1].
     */
    double openArea() const noexcept { return m_design.openArea; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Generic-grate splash-over velocity in project length units per second.
     */
    double splashVeloc() const noexcept { return m_design.splashVeloc; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Curb opening length in project length units (ft or m).
     */
    double curbLength() const noexcept { return m_design.curbLength; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Curb opening height in project length units (ft or m).
     */
    double curbHeight() const noexcept { return m_design.curbHeight; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Curb throat orientation; ignored by the engine for DropCurb.
     */
    ThroatType throat() const noexcept { return m_design.throat; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Slot length in project length units (ft or m).
     */
    double slotLength() const noexcept { return m_design.slotLength; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Slot width in project length units (ft or m).
     */
    double slotWidth() const noexcept { return m_design.slotWidth; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Custom capture-curve name, resolved when saving the engine design.
     */
    QString curveId() const { return m_design.curveId; }
    /**
     * @brief Read a local inlet-design parameter.
     * @return Custom capture relation: diversion flow-versus-flow, rating flow-versus-depth, or None for inference.
     */
    InletCurveKind curveKind() const noexcept { return m_design.curveKind; }

    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName New name copied verbatim, with no trimming/uniqueness/engine validation.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);
    /**
     * @brief Store the comment text and notify commentsChanged() when changed.
     * @param[in] text Free-form description copied verbatim; equal text is a no-op.
     */
    void setComments(QString text);

    /*! \brief Replace every design field at once. Emits `paramsChanged()`
     *  once if anything actually changed.
     * @param[in] d Complete authoring design copied without physical/type/reference validation.
     * @details Numeric equality is exact; NaN fields cause otherwise matching snapshots to compare unequal. No engine write occurs here.
     */
    void setDesign(const InletDesignData &d);

    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Design geometry category, Grate through Custom.
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setType(InletType v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Grate length in project length units (ft or m).
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setGrateLength(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Grate width in project length units (ft or m).
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setGrateWidth(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v HEC-22 bar pattern, or Generic.
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setGrateType(GrateType v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Generic-grate open-area fraction, expected in (0,1].
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setOpenArea(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Generic-grate splash-over velocity in project length units per second.
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setSplashVeloc(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Curb opening length in project length units (ft or m).
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setCurbLength(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Curb opening height in project length units (ft or m).
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setCurbHeight(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Curb throat orientation; ignored by the engine for DropCurb.
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setThroat(ThroatType v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Slot length in project length units (ft or m).
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setSlotLength(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Slot width in project length units (ft or m).
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setSlotWidth(double v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Custom capture-curve name, resolved when saving the engine design.
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setCurveId(QString v);
    /**
     * @brief Replace an inlet-design parameter and emit paramsChanged() when unequal.
     * @param[in] v Custom capture relation: diversion flow-versus-flow, rating flow-versus-depth, or None for inference.
     * @details No physical bounds, finiteness, enum validity, or reference checks occur locally; engine save validates fields used by the chosen type. Equal values are a no-op.
     */
    void setCurveKind(InletCurveKind v);

signals:
    /**
     * @brief Notify observers after the stored identifier changes.
     * @param[in] prev Previous identifier.
     * @param[in] now New identifier, already stored when emitted.
     */
    void nameChanged(QString prev, QString now);
    /**
     * @brief Notify observers after authoring parameter changes; does not itself guarantee a successful engine write.
     */
    void paramsChanged();
    /**
     * @brief Notify observers after stored description text changes; registry forwards this as providerParamsChanged().
     */
    void commentsChanged();

private:
    QString m_name;
    QString m_comments;
    InletDesignData m_design;
};

/*! \brief Field-by-field equality — used by `setDesign` to suppress no-op
 *  notifications and by the undo commands to skip empty pushes.
 * @param[in] a First design snapshot.
 * @param[in] b Second snapshot.
 * @return True when every field compares exactly equal; any NaN numeric field compares unequal.
 */
bool operator==(const InletDesignData &a, const InletDesignData &b);
/**
 * @param[in] a First design snapshot.
 * @param[in] b Second snapshot.
 * @return Logical inverse of exact field-wise equality.
 */
inline bool operator!=(const InletDesignData &a, const InletDesignData &b)
{
    return !(a == b);
}

/*! \brief Legacy editor label for a type ("GRATE", "CURB OPENING",
 *  "COMBINATION", "SLOTTED DRAIN", "DROP GRATE", "DROP CURB", "CUSTOM").
 *  These are the strings the type combo shows (Uinlet.pas:31-44).
 * @param[in] t Type code to describe.
 * @return Editor type label, falling back to GRATE for an unsupported code.
 */
QString inletTypeLabel(InletType t);

/*! \brief `[INLETS]` type keyword handed to `swmm_inlet_add`. COMBINATION uses
 *  the COMBO keyword; the full design setter validates the final fields.
 * @param[in] t Type code to describe.
 * @return Borrowed static INLETS keyword, including COMBO; falls back to GRATE for an unsupported code.
 */
const char *inletTypeKeyword(InletType t);

/*! \brief HEC-22 grate name ("P_BAR-50", "CURVED_VANE", "GENERIC", …).
 * @param[in] t Type code to describe.
 * @return HEC-22 grate token, falling back to GENERIC for an unsupported code.
 */
QString grateTypeLabel(GrateType t);

} // namespace openswmmvis::inlet

#endif // OPENSWMMVIS_INLET_INLETPROVIDER_H
