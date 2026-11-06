/**
 * @file      inletpropertybag.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QObject Q_PROPERTY surface backing the InletEditorDialog middle-pane
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_INLETPROPERTYBAG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETPROPERTYBAG_H

#include "inlet/inletprovider.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace openswmmvis::ui
{

class InletPropertyBag : public QObject
{
    Q_OBJECT

    // ---- Grate -------------------------------------------------------------
    Q_PROPERTY(GrateTypeQ grateType READ grateType WRITE setGrateType NOTIFY grateTypeChanged)
    Q_PROPERTY(double grateLength READ grateLength WRITE setGrateLength NOTIFY grateLengthChanged)
    Q_PROPERTY(double grateWidth READ grateWidth WRITE setGrateWidth NOTIFY grateWidthChanged)
    Q_PROPERTY(
        double openFraction READ openFraction WRITE setOpenFraction NOTIFY openFractionChanged)
    Q_PROPERTY(double splashVelocity READ splashVelocity WRITE setSplashVelocity NOTIFY
                   splashVelocityChanged)

    // ---- Curb opening ------------------------------------------------------
    Q_PROPERTY(double curbLength READ curbLength WRITE setCurbLength NOTIFY curbLengthChanged)
    Q_PROPERTY(double curbHeight READ curbHeight WRITE setCurbHeight NOTIFY curbHeightChanged)
    Q_PROPERTY(
        ThroatTypeQ throatAngle READ throatAngle WRITE setThroatAngle NOTIFY throatAngleChanged)

    // ---- Slotted drain -----------------------------------------------------
    Q_PROPERTY(double slotLength READ slotLength WRITE setSlotLength NOTIFY slotLengthChanged)
    Q_PROPERTY(double slotWidth READ slotWidth WRITE setSlotWidth NOTIFY slotWidthChanged)

    // ---- Custom ------------------------------------------------------------
    Q_PROPERTY(CurveKindQ curveKind READ curveKind WRITE setCurveKind NOTIFY curveKindChanged)
    Q_PROPERTY(QString curveId READ curveId WRITE setCurveId NOTIFY curveIdChanged)

public:
    /*! \brief Q_ENUM mirrors of the `openswmmvis::inlet` design enums so
     *  QPropertyModel renders a combo-box for each. Q_ENUM requires the enum
     *  to live in a QObject, hence the duplicates (same idiom as
     *  `SeriesStyleObject::MarkerShapeQ`). Values track 1:1. */
    enum class GrateTypeQ : int {
        P_BAR_50 = static_cast<int>(inlet::GrateType::PBar50),
        P_BAR_50x100 = static_cast<int>(inlet::GrateType::PBar50x100),
        P_BAR_30 = static_cast<int>(inlet::GrateType::PBar30),
        CURVED_VANE = static_cast<int>(inlet::GrateType::CurvedVane),
        TILT_BAR_45 = static_cast<int>(inlet::GrateType::TiltBar45),
        TILT_BAR_30 = static_cast<int>(inlet::GrateType::TiltBar30),
        RETICULINE = static_cast<int>(inlet::GrateType::Reticuline),
        GENERIC = static_cast<int>(inlet::GrateType::Generic),
    };
    Q_ENUM(GrateTypeQ)

    enum class ThroatTypeQ : int {
        HORIZONTAL = static_cast<int>(inlet::ThroatType::Horizontal),
        INCLINED = static_cast<int>(inlet::ThroatType::Inclined),
        VERTICAL = static_cast<int>(inlet::ThroatType::Vertical),
    };
    Q_ENUM(ThroatTypeQ)

    enum class CurveKindQ : int {
        NONE = static_cast<int>(inlet::InletCurveKind::None),
        DIVERSION = static_cast<int>(inlet::InletCurveKind::Diversion),
        RATING = static_cast<int>(inlet::InletCurveKind::Rating),
    };
    Q_ENUM(CurveKindQ)

    /*! \brief Legacy tab-visibility groups (Dinlet.pas:241-298). */
    enum Group {
        NoGroup = 0x0,
        GrateGroup = 0x1,
        CurbGroup = 0x2,
        SlottedGroup = 0x4,
        CustomGroup = 0x8,
    };
    Q_DECLARE_FLAGS(Groups, Group)

    /*! \brief Commit hook. When installed, a bag edit is handed to the host
     *  (which wraps it in a SetInletParamsCommand) instead of being written
     *  straight to the provider. */
    using CommitFn = std::function<void(const inlet::InletDesignData &before,
                                        const inlet::InletDesignData &after)>;

    /**
     * @param[in] parent Optional QObject owner; caller manages parentless adapter.
     * @brief Initialize unbound cache (numeric fields zero, default enum values, empty curve)
     *   without provider/engine mutation.
     * @note GUI-thread QObject/view adapter; no locking or UnitSystem change subscription. Provider
     *   deletion nulls guard but does not automatically clear cached fields.
     */
    explicit InletPropertyBag(QObject *parent = nullptr);

    /**
     * @param[in] p Optional borrowed provider guarded by QPointer, no ownership transfer; null
     *   resets cache.
     * @brief Disconnect all old provider-to-this connections, subscribe to new paramsChanged and
     *   refresh full cache with pushes suppressed.
     * @note Same-provider bind still reconnects/refreshes. Emits per-field change signals for
     *   differing values, but no commit/engine write. Commit handler is retained across rebinding;
     *   provider destruction only nulls pointer, leaving cache until explicit refresh/bind.
     */
    void bind(inlet::InletProvider *p);
    /**
     * @return Borrowed current guarded provider or null after no binding/destruction. Cache can
     *   retain stale values after destruction; no ownership or lifecycle guarantee beyond QPointer
     *   at this instant.
     */
    inlet::InletProvider *provider() const noexcept;

    /**
     * @param[in] fn Moved optional synchronous before/after callback; empty restores direct
     *   provider setDesign. Captured resources/lifetimes belong to host.
     * @brief Replace edit dispatch hook without applying current cache.
     * @note Hook returns no success value; adapter does not itself push undo, persist engine or
     *   revert cache when host refuses. Callback may trigger provider refresh synchronously;
     *   maintain adapter/provider lifetime during delivery.
     */
    void setCommitHandler(CommitFn fn);

    /*! \brief Groups that apply to the bound design's type.
     * @return Group mask from current live provider type, or NoGroup when null. Does not inspect
     *   cached type enums or validate dimensions/engine state.
 */
    Groups visibleGroups() const;

    /*! \brief Groups that apply to \p t. Pure — used by the tests.
     * @param[in] t Design geometry enum inspected without provider access.
     * @return Grate for Grate/DropGrate, Curb for Curb/DropCurb, their union for Combo, Slotted or
     *   Custom for respective type, NoGroup for unknown. No visibility notifications or mutation.
 */
    static Groups groupsFor(inlet::InletType t);

    /*! \brief The group a raw Q_PROPERTY name belongs to.
     * @param[in] property Exact case-sensitive raw Q_PROPERTY identifier, no prefix/label
     *   normalization.
     * @return Declared group for recognized adapter property, otherwise NoGroup. No
     *   instance/provider lookup.
 */
    static Group groupOf(const QString &property);

    /*! \brief Row-level visibility: group visibility plus the two legacy
     *  exceptions (open fraction / splash-over velocity are GENERIC only;
     *  the throat angle does not apply to a DROP CURB).
     * @param[in] property Exact case-sensitive raw property identifier.
     * @return False without live provider/recognized active group; openFraction/splashVelocity
     *   additionally require cached GENERIC grate, throatAngle excludes live DropCurb type. All
     *   other active-group properties visible.
     * @note Presentation filter only: setters remain callable for hidden fields and no dedicated
     *   visibility-changed signal is emitted.
 */
    Q_INVOKABLE bool isPropertyVisible(const QString &property) const;

    /*! \brief Group-prefixed, unit-suffixed label for the QPropertyModel row.
     *  Empty for an unknown name so the generated name is used.
     * @param[in] property Exact case-sensitive property identifier, independent of
     *   binding/visibility.
     * @return Translated group-prefixed label, length/velocity labels queried from CURRENT global
     *   UnitSystem on each call; empty for unknown.
     * @note Does not convert cached provider values, check whether global units match provider, or
     *   subscribe to unit changes.
 */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

    /**
     * @return Cached hEC-22 grate/bar-pattern enum, including GENERIC; raw enum is not validated.
     *   Read only, no live provider query or range validation; defaults/stale unbound cache are
     *   possible.
     */
    GrateTypeQ grateType() const noexcept { return m_grateType; }
    /**
     * @return Cached grate length in authored project length units (ft or m). Read only, no live
     *   provider query or range validation; defaults/stale unbound cache are possible.
     */
    double grateLength() const noexcept { return m_grateLength; }
    /**
     * @return Cached grate width in authored project length units (ft or m). Read only, no live
     *   provider query or range validation; defaults/stale unbound cache are possible.
     */
    double grateWidth() const noexcept { return m_grateWidth; }
    /**
     * @return Cached dimensionless open-area fraction, meaningful for GENERIC grates; intended
     *   (0,1] is not enforced here. Read only, no live provider query or range validation;
     *   defaults/stale unbound cache are possible.
     */
    double openFraction() const noexcept { return m_openFraction; }
    /**
     * @return Cached gENERIC-grate splash-over velocity in project ft/s or m/s. Read only, no live
     *   provider query or range validation; defaults/stale unbound cache are possible.
     */
    double splashVelocity() const noexcept { return m_splashVelocity; }
    /**
     * @return Cached curb-opening length in authored ft or m. Read only, no live provider query or
     *   range validation; defaults/stale unbound cache are possible.
     */
    double curbLength() const noexcept { return m_curbLength; }
    /**
     * @return Cached curb-opening height in authored ft or m. Read only, no live provider query or
     *   range validation; defaults/stale unbound cache are possible.
     */
    double curbHeight() const noexcept { return m_curbHeight; }
    /**
     * @return Cached horizontal/inclined/vertical throat category, not a numeric angle; ignored by
     *   DROP CURB visibility. Read only, no live provider query or range validation; defaults/stale
     *   unbound cache are possible.
     */
    ThroatTypeQ throatAngle() const noexcept { return m_throatAngle; }
    /**
     * @return Cached slotted-drain length in authored ft or m. Read only, no live provider query or
     *   range validation; defaults/stale unbound cache are possible.
     */
    double slotLength() const noexcept { return m_slotLength; }
    /**
     * @return Cached slotted-drain width in authored ft or m. Read only, no live provider query or
     *   range validation; defaults/stale unbound cache are possible.
     */
    double slotWidth() const noexcept { return m_slotWidth; }
    /**
     * @return Cached custom capture curve mode NONE/DIVERSION/RATING, raw enum not validated. Read
     *   only, no live provider query or range validation; defaults/stale unbound cache are
     *   possible.
     */
    CurveKindQ curveKind() const noexcept { return m_curveKind; }
    /**
     * @return Cached raw custom curve identifier copied without trimming or reference resolution.
     *   Read only, no live provider query or range validation; defaults/stale unbound cache are
     *   possible.
     */
    QString curveId() const { return m_curveId; }

public slots:
    /**
     * @param[in] v HEC-22 grate/bar-pattern enum, including GENERIC; raw enum is not validated.
     * @brief On exact inequality, store cache and emit grateTypeChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setGrateType(GrateTypeQ v);
    /**
     * @param[in] v Grate length in authored project length units (ft or m).
     * @brief On exact inequality, store cache and emit grateLengthChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setGrateLength(double v);
    /**
     * @param[in] v Grate width in authored project length units (ft or m).
     * @brief On exact inequality, store cache and emit grateWidthChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setGrateWidth(double v);
    /**
     * @param[in] v Dimensionless open-area fraction, meaningful for GENERIC grates; intended (0,1]
     *   is not enforced here.
     * @brief On exact inequality, store cache and emit openFractionChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setOpenFraction(double v);
    /**
     * @param[in] v GENERIC-grate splash-over velocity in project ft/s or m/s.
     * @brief On exact inequality, store cache and emit splashVelocityChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setSplashVelocity(double v);
    /**
     * @param[in] v Curb-opening length in authored ft or m.
     * @brief On exact inequality, store cache and emit curbLengthChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setCurbLength(double v);
    /**
     * @param[in] v Curb-opening height in authored ft or m.
     * @brief On exact inequality, store cache and emit curbHeightChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setCurbHeight(double v);
    /**
     * @param[in] v Horizontal/inclined/vertical throat category, not a numeric angle; ignored by
     *   DROP CURB visibility.
     * @brief On exact inequality, store cache and emit throatAngleChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setThroatAngle(ThroatTypeQ v);
    /**
     * @param[in] v Slotted-drain length in authored ft or m.
     * @brief On exact inequality, store cache and emit slotLengthChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setSlotLength(double v);
    /**
     * @param[in] v Slotted-drain width in authored ft or m.
     * @brief On exact inequality, store cache and emit slotWidthChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setSlotWidth(double v);
    /**
     * @param[in] v Custom capture curve mode NONE/DIVERSION/RATING, raw enum not validated.
     * @brief On exact inequality, store cache and emit curveKindChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setCurveKind(CurveKindQ v);
    /**
     * @param[in] v Raw custom curve identifier copied without trimming or reference resolution.
     * @brief On exact inequality, store cache and emit curveIdChanged before dispatching the
     *   complete composed design.
     * @note Equal value is a no-op; no finiteness, bounds, enum or reference validation. Without
     *   provider or while refresh suppression is active, cache/signals still update but no commit.
     *   Otherwise callback handles persistence/undo, or provider receives direct local setDesign.
     *   No automatic rollback on host rejection; NaN is always unequal for numeric fields.
     */
    void setCurveId(const QString &v);

signals:
    /**
     * @brief Publish new cached hEC-22 grate/bar-pattern enum, including GENERIC; raw enum is not
     *   validated.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void grateTypeChanged(GrateTypeQ);
    /**
     * @brief Publish new cached grate length in authored project length units (ft or m).
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void grateLengthChanged(double);
    /**
     * @brief Publish new cached grate width in authored project length units (ft or m).
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void grateWidthChanged(double);
    /**
     * @brief Publish new cached dimensionless open-area fraction, meaningful for GENERIC grates;
     *   intended (0,1] is not enforced here.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void openFractionChanged(double);
    /**
     * @brief Publish new cached gENERIC-grate splash-over velocity in project ft/s or m/s.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void splashVelocityChanged(double);
    /**
     * @brief Publish new cached curb-opening length in authored ft or m.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void curbLengthChanged(double);
    /**
     * @brief Publish new cached curb-opening height in authored ft or m.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void curbHeightChanged(double);
    /**
     * @brief Publish new cached horizontal/inclined/vertical throat category, not a numeric angle;
     *   ignored by DROP CURB visibility.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void throatAngleChanged(ThroatTypeQ);
    /**
     * @brief Publish new cached slotted-drain length in authored ft or m.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void slotLengthChanged(double);
    /**
     * @brief Publish new cached slotted-drain width in authored ft or m.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void slotWidthChanged(double);
    /**
     * @brief Publish new cached custom capture curve mode NONE/DIVERSION/RATING, raw enum not
     *   validated.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void curveKindChanged(CurveKindQ);
    /**
     * @brief Publish new cached raw custom curve identifier copied without trimming or reference
     *   resolution.
     * @note Unnamed signal argument is the newly stored value. Emitted before attempted push,
     *   including suppressed provider refresh for changed fields; it is not engine-commit
     *   confirmation. GUI-thread synchronous receivers must preserve adapter lifetime.
     */
    void curveIdChanged(const QString &);

private slots:
    /**
     * @brief Refresh every cached field from current guarded provider with pushes suppressed; no
     *   source mutation. Null provider uses reset defaults.
     */
    void onProviderParamsChanged_();

private:
    /**
     * @brief Suppress pushes while setting every cache field from live design, then clear
     *   suppression.
     * @note Null source resets numeric fields to zero, default grate/throat enums, NONE curve and
     *   empty ID (not complete InletDesignData geometry defaults). Per-field notifications still
     *   emit only on inequality. Does not preserve prior suppression state, commit, persist engine
     *   or emit a batch completion signal.
     */
    void refreshFromProvider_();

    /*! \brief Build the design implied by the bag's cached fields and hand it
     *  to the commit handler (or straight to the provider). No-op while
     *  `m_suppressPush` is set or nothing changed.
     * @brief When bound and unsuppressed, compare current complete provider design with composed
     *   cache and dispatch only unequal snapshots.
     * @note Call installed commit hook synchronously or provider setDesign directly; no result
     *   check, undo, persistence, engine validation or automatic cache rollback here. Provider
     *   type/unexposed fields are retained by composed_. NaN fields compare unequal.
 */
    void push_();

    /*! \brief The design the bag's cached fields describe.
     * @return Owned complete design snapshot based on live provider design (default design when
     *   null), replacing all twelve exposed fields with raw cache. Preserve unexposed/type fields;
     *   no validation, lookup, mutation or unit conversion.
 */
    inlet::InletDesignData composed_() const;

    QPointer<inlet::InletProvider> m_provider;
    CommitFn m_commit;

    GrateTypeQ m_grateType = GrateTypeQ::P_BAR_50;
    double m_grateLength = 0.0;
    double m_grateWidth = 0.0;
    double m_openFraction = 0.0;
    double m_splashVelocity = 0.0;
    double m_curbLength = 0.0;
    double m_curbHeight = 0.0;
    ThroatTypeQ m_throatAngle = ThroatTypeQ::VERTICAL;
    double m_slotLength = 0.0;
    double m_slotWidth = 0.0;
    CurveKindQ m_curveKind = CurveKindQ::NONE;
    QString m_curveId;

    bool m_suppressPush = false;
};

} // namespace openswmmvis::ui

Q_DECLARE_OPERATORS_FOR_FLAGS(openswmmvis::ui::InletPropertyBag::Groups)
Q_DECLARE_METATYPE(openswmmvis::ui::InletPropertyBag::GrateTypeQ)
Q_DECLARE_METATYPE(openswmmvis::ui::InletPropertyBag::ThroatTypeQ)
Q_DECLARE_METATYPE(openswmmvis::ui::InletPropertyBag::CurveKindQ)

#endif // OPENSWMMVIS_UI_DIALOGS_INLETPROPERTYBAG_H
