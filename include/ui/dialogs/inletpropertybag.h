/*!
 * \file   inletpropertybag.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QObject Q_PROPERTY surface backing the InletEditorDialog middle-pane
 *         property tree (QPropertyModel). Mirrors TransectPropertyBag.
 *
 * Bidirectional wiring: on bind(), the bag pulls the current design from the
 * bound InletProvider; a setter slot rebuilds the whole `InletDesignData` and
 * either hands it to the dialog's commit handler (which wraps it in a
 * `SetInletParamsCommand`) or, when no handler is installed, writes it
 * straight to the provider. The provider's `paramsChanged()` refreshes the
 * cache with pushes suppressed, breaking the feedback loop.
 *
 * The four groups of the legacy tab-visibility table (Dinlet.pas:241-298) are
 * encoded as `Group` flags; `visibleGroups()` answers which apply to the bound
 * design's type and `isPropertyVisible()` folds in the two per-property
 * exceptions (open fraction / splash-over velocity are GENERIC-grate only;
 * the throat angle does not apply to a DROP CURB). The dialog hides the rows
 * that answer false.
 *
 * Exposed groups (the displayLabelFor() prefix drives the row labels):
 *
 *   Grate         — grateType, grateLength, grateWidth,
 *                   openFraction, splashVelocity
 *   Curb Opening  — curbLength, curbHeight, throatAngle
 *   Slotted Drain — slotLength, slotWidth
 *   Custom        — curveKind, curveId
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_INLETPROPERTYBAG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETPROPERTYBAG_H

#include "inlet/inletprovider.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace openswmmvis::ui {

class InletPropertyBag : public QObject
{
    Q_OBJECT

    // ---- Grate -------------------------------------------------------------
    Q_PROPERTY(GrateTypeQ grateType      READ grateType      WRITE setGrateType      NOTIFY grateTypeChanged)
    Q_PROPERTY(double     grateLength    READ grateLength    WRITE setGrateLength    NOTIFY grateLengthChanged)
    Q_PROPERTY(double     grateWidth     READ grateWidth     WRITE setGrateWidth     NOTIFY grateWidthChanged)
    Q_PROPERTY(double     openFraction   READ openFraction   WRITE setOpenFraction   NOTIFY openFractionChanged)
    Q_PROPERTY(double     splashVelocity READ splashVelocity WRITE setSplashVelocity NOTIFY splashVelocityChanged)

    // ---- Curb opening ------------------------------------------------------
    Q_PROPERTY(double      curbLength  READ curbLength  WRITE setCurbLength  NOTIFY curbLengthChanged)
    Q_PROPERTY(double      curbHeight  READ curbHeight  WRITE setCurbHeight  NOTIFY curbHeightChanged)
    Q_PROPERTY(ThroatTypeQ throatAngle READ throatAngle WRITE setThroatAngle NOTIFY throatAngleChanged)

    // ---- Slotted drain -----------------------------------------------------
    Q_PROPERTY(double slotLength READ slotLength WRITE setSlotLength NOTIFY slotLengthChanged)
    Q_PROPERTY(double slotWidth  READ slotWidth  WRITE setSlotWidth  NOTIFY slotWidthChanged)

    // ---- Custom ------------------------------------------------------------
    Q_PROPERTY(CurveKindQ curveKind READ curveKind WRITE setCurveKind NOTIFY curveKindChanged)
    Q_PROPERTY(QString    curveId   READ curveId   WRITE setCurveId   NOTIFY curveIdChanged)

public:
    /*! \brief Q_ENUM mirrors of the `openswmmvis::inlet` design enums so
     *  QPropertyModel renders a combo-box for each. Q_ENUM requires the enum
     *  to live in a QObject, hence the duplicates (same idiom as
     *  `SeriesStyleObject::MarkerShapeQ`). Values track 1:1. */
    enum class GrateTypeQ : int {
        P_BAR_50     = static_cast<int>(inlet::GrateType::PBar50),
        P_BAR_50x100 = static_cast<int>(inlet::GrateType::PBar50x100),
        P_BAR_30     = static_cast<int>(inlet::GrateType::PBar30),
        CURVED_VANE  = static_cast<int>(inlet::GrateType::CurvedVane),
        TILT_BAR_45  = static_cast<int>(inlet::GrateType::TiltBar45),
        TILT_BAR_30  = static_cast<int>(inlet::GrateType::TiltBar30),
        RETICULINE   = static_cast<int>(inlet::GrateType::Reticuline),
        GENERIC      = static_cast<int>(inlet::GrateType::Generic),
    };
    Q_ENUM(GrateTypeQ)

    enum class ThroatTypeQ : int {
        HORIZONTAL = static_cast<int>(inlet::ThroatType::Horizontal),
        INCLINED   = static_cast<int>(inlet::ThroatType::Inclined),
        VERTICAL   = static_cast<int>(inlet::ThroatType::Vertical),
    };
    Q_ENUM(ThroatTypeQ)

    enum class CurveKindQ : int {
        NONE      = static_cast<int>(inlet::InletCurveKind::None),
        DIVERSION = static_cast<int>(inlet::InletCurveKind::Diversion),
        RATING    = static_cast<int>(inlet::InletCurveKind::Rating),
    };
    Q_ENUM(CurveKindQ)

    /*! \brief Legacy tab-visibility groups (Dinlet.pas:241-298). */
    enum Group {
        NoGroup      = 0x0,
        GrateGroup   = 0x1,
        CurbGroup    = 0x2,
        SlottedGroup = 0x4,
        CustomGroup  = 0x8,
    };
    Q_DECLARE_FLAGS(Groups, Group)

    /*! \brief Commit hook. When installed, a bag edit is handed to the host
     *  (which wraps it in a SetInletParamsCommand) instead of being written
     *  straight to the provider. */
    using CommitFn = std::function<void(const inlet::InletDesignData &before,
                                        const inlet::InletDesignData &after)>;

    explicit InletPropertyBag(QObject *parent = nullptr);

    void bind(inlet::InletProvider *p);
    inlet::InletProvider *provider() const noexcept;

    void setCommitHandler(CommitFn fn);

    /*! \brief Groups that apply to the bound design's type. */
    Groups visibleGroups() const;

    /*! \brief Groups that apply to \p t. Pure — used by the tests. */
    static Groups groupsFor(inlet::InletType t);

    /*! \brief The group a raw Q_PROPERTY name belongs to. */
    static Group groupOf(const QString &property);

    /*! \brief Row-level visibility: group visibility plus the two legacy
     *  exceptions (open fraction / splash-over velocity are GENERIC only;
     *  the throat angle does not apply to a DROP CURB). */
    Q_INVOKABLE bool isPropertyVisible(const QString &property) const;

    /*! \brief Group-prefixed, unit-suffixed label for the QPropertyModel row.
     *  Empty for an unknown name so the generated name is used. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

    GrateTypeQ  grateType()      const noexcept { return m_grateType; }
    double      grateLength()    const noexcept { return m_grateLength; }
    double      grateWidth()     const noexcept { return m_grateWidth; }
    double      openFraction()   const noexcept { return m_openFraction; }
    double      splashVelocity() const noexcept { return m_splashVelocity; }
    double      curbLength()     const noexcept { return m_curbLength; }
    double      curbHeight()     const noexcept { return m_curbHeight; }
    ThroatTypeQ throatAngle()    const noexcept { return m_throatAngle; }
    double      slotLength()     const noexcept { return m_slotLength; }
    double      slotWidth()      const noexcept { return m_slotWidth; }
    CurveKindQ  curveKind()      const noexcept { return m_curveKind; }
    QString     curveId()        const          { return m_curveId; }

public slots:
    void setGrateType(GrateTypeQ v);
    void setGrateLength(double v);
    void setGrateWidth(double v);
    void setOpenFraction(double v);
    void setSplashVelocity(double v);
    void setCurbLength(double v);
    void setCurbHeight(double v);
    void setThroatAngle(ThroatTypeQ v);
    void setSlotLength(double v);
    void setSlotWidth(double v);
    void setCurveKind(CurveKindQ v);
    void setCurveId(const QString &v);

signals:
    void grateTypeChanged(GrateTypeQ);
    void grateLengthChanged(double);
    void grateWidthChanged(double);
    void openFractionChanged(double);
    void splashVelocityChanged(double);
    void curbLengthChanged(double);
    void curbHeightChanged(double);
    void throatAngleChanged(ThroatTypeQ);
    void slotLengthChanged(double);
    void slotWidthChanged(double);
    void curveKindChanged(CurveKindQ);
    void curveIdChanged(const QString &);

private slots:
    void onProviderParamsChanged_();

private:
    void refreshFromProvider_();

    /*! \brief Build the design implied by the bag's cached fields and hand it
     *  to the commit handler (or straight to the provider). No-op while
     *  `m_suppressPush` is set or nothing changed. */
    void push_();

    /*! \brief The design the bag's cached fields describe. */
    inlet::InletDesignData composed_() const;

    QPointer<inlet::InletProvider> m_provider;
    CommitFn                       m_commit;

    GrateTypeQ  m_grateType      = GrateTypeQ::P_BAR_50;
    double      m_grateLength    = 0.0;
    double      m_grateWidth     = 0.0;
    double      m_openFraction   = 0.0;
    double      m_splashVelocity = 0.0;
    double      m_curbLength     = 0.0;
    double      m_curbHeight     = 0.0;
    ThroatTypeQ m_throatAngle    = ThroatTypeQ::VERTICAL;
    double      m_slotLength     = 0.0;
    double      m_slotWidth      = 0.0;
    CurveKindQ  m_curveKind      = CurveKindQ::NONE;
    QString     m_curveId;

    bool m_suppressPush = false;
};

} // namespace openswmmvis::ui

Q_DECLARE_OPERATORS_FOR_FLAGS(openswmmvis::ui::InletPropertyBag::Groups)
Q_DECLARE_METATYPE(openswmmvis::ui::InletPropertyBag::GrateTypeQ)
Q_DECLARE_METATYPE(openswmmvis::ui::InletPropertyBag::ThroatTypeQ)
Q_DECLARE_METATYPE(openswmmvis::ui::InletPropertyBag::CurveKindQ)

#endif // OPENSWMMVIS_UI_DIALOGS_INLETPROPERTYBAG_H
