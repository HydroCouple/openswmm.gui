/*!
 * \file   newdataobjectdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.3 — Per-type creation dialog for non-spatial Data Objects.
 *
 * Replaces the legacy bare `QInputDialog::getText` flow (which hardcoded
 * one subtype per category — curves to Storage, patterns to Monthly,
 * LIDs to BioCell, pollutants to mg/L, inlets to GRATE) with a typed
 * mini-dialog per category that:
 *
 *   - Pre-fills a unique auto-suggested default name via
 *     `SWMMModelLayer::suggestUniqueDataObjectName`.
 *   - Exposes the required subtype combo (curve type, pattern type,
 *     LID type, pollutant units, inlet type).
 *   - For Control Rules: skeleton-template radio that pre-populates
 *     the RULE body (Empty / Pump on/off / Orifice setting / Weir
 *     bypass — 4 pre-canned headers).
 *   - For Unit Hydrographs: rain-gage picker (combo of existing gages)
 *     + response combo for the initial parameter row.
 *
 * Modelled on the legacy SWMM-GUI per-class D<type>.pas forms but
 * modernised to reuse QPropertyModel where the field set is scalar.
 */

#ifndef NEWDATAOBJECTDIALOG_H
#define NEWDATAOBJECTDIALOG_H

#include "layers/swmmmodellayer.h"   // DataCategory

#include <QDialog>
#include <QString>
#include <QVariant>

#include <optional>

class QStackedWidget;
class QLineEdit;
class QComboBox;
class QRadioButton;
class QSpinBox;
class QDoubleSpinBox;
class QDialogButtonBox;

/*!
 * \brief Returned by `NewDataObjectDialog::getNew` on Accept. Carries
 *        the user-chosen name and the type-specific creation
 *        parameters in a `QVariantMap` keyed by the per-category
 *        option name (see DA.3 table in the implementation plan).
 */
struct NewObjectSpec
{
    SWMMModelLayer::DataCategory category;
    QString                       name;
    QVariantMap                   options;
};

class NewDataObjectDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \brief Construct + run the dialog modally.  Returns the populated
     *        `NewObjectSpec` on Accept, or `std::nullopt` on Cancel /
     *        Reject.
     *
     * \param dc      The category we're creating.
     * \param layer   Used for the auto-suggested unique name + for the
     *                rain-gage picker on the Unit Hydrographs page.
     * \param parent  Standard parent.
     */
    static std::optional<NewObjectSpec> getNew(SWMMModelLayer::DataCategory dc,
                                                SWMMModelLayer *layer,
                                                QWidget *parent = nullptr);

private:
    explicit NewDataObjectDialog(SWMMModelLayer::DataCategory dc,
                                  SWMMModelLayer *layer,
                                  QWidget *parent = nullptr);

    /*! Collect the per-page options keyed by their canonical names. */
    [[nodiscard]] QVariantMap collectOptions() const;

    SWMMModelLayer::DataCategory m_category;
    SWMMModelLayer              *m_layer;

    // Shared name editor at the top of every page.
    QLineEdit       *m_nameEdit = nullptr;

    // Per-category controls — only the relevant ones are created
    // depending on m_category. nullptr means "not applicable".
    QComboBox       *m_curveTypeCombo    = nullptr;
    QComboBox       *m_patternTypeCombo  = nullptr;
    QComboBox       *m_lidTypeCombo      = nullptr;
    QComboBox       *m_pollutantUnitsCombo = nullptr;
    QComboBox       *m_inletTypeCombo    = nullptr;
    QComboBox       *m_tsSourceCombo     = nullptr;

    // Control-rule skeleton + Unit-Hydrograph extras.
    QComboBox       *m_ruleSkeletonCombo = nullptr;
    QComboBox       *m_uhRainGageCombo   = nullptr;
    QComboBox       *m_uhResponseCombo   = nullptr;

    // Rain Gage page extras.
    QComboBox       *m_gageRainTypeCombo  = nullptr;
    QDoubleSpinBox  *m_gageIntervalSpin   = nullptr;

    QDialogButtonBox *m_buttonBox = nullptr;
};

#endif // NEWDATAOBJECTDIALOG_H
