/*!
 * \file   groundwaterexchangedialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Groundwater Exchange editor for one subcatchment: the
 *         [GROUNDWATER] receiving node + surface elevation + standard
 *         lateral-flow coefficients (A1 B1 A2 B2 A3 Twgr Hstar) and the two
 *         [GWF] custom expressions (LATERAL / DEEP) with live engine
 *         validation. Replaces the Groundwater page that used to live in
 *         SubcatchCompoundEditDialog (AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN
 *         D2): the aquifer itself is assigned on the subcatchment's
 *         Aquifer property row, so this dialog only shows it.
 *
 * Modeless top-level dialog opened from the SubcatchCompoundEditButton
 * (property browser + attribute table) for Kind::Groundwater and from the
 * node-side "Groundwater Sources" row. Apply writes straight to the engine
 * and calls SWMMModelLayer::markEdited() like every other compound editor.
 */

#ifndef GROUNDWATEREXCHANGEDIALOG_H
#define GROUNDWATEREXCHANGEDIALOG_H

#include <QDialog>

#include "ui/properties/subcatchcompoundeditref.h"

class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QWidget;

namespace openswmmvis::ui { class GwfExpressionEdit; }

class GroundwaterExchangeDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \param ref  Engine + subcatchment name (+ optional layer for the
     *              edited notification). `ref.kind` is ignored. */
    explicit GroundwaterExchangeDialog(SubcatchCompoundEditRef ref,
                                       QWidget *parent = nullptr);

    /*! Live cell text (groundwaterSummary) — what the opening button shows
     *  after Apply. */
    [[nodiscard]] QString updatedSummary() const;

    /*! True when an aquifer is assigned and both expressions validate. */
    [[nodiscard]] bool canApply() const;

signals:
    /*! Emitted after every successful Apply (engine written, layer marked
     *  edited) so the opening cell can refresh its summary. */
    void applied();

private:
    void buildUi_();
    void loadFromEngine_();
    void apply_();
    void updateApplyState_();
    void bindExpression_(openswmmvis::ui::GwfExpressionEdit *edit,
                         QLabel *status, bool *okFlag);

    [[nodiscard]] int subIdx() const;   // swmm_subcatch_index, or -1

    SubcatchCompoundEditRef m_ref;

    QLabel   *m_header = nullptr;
    QLabel   *m_hint   = nullptr;
    QWidget  *m_form   = nullptr;   ///< everything below the header (gated on aquifer)

    QComboBox      *m_node   = nullptr;
    QDoubleSpinBox *m_surfEl = nullptr;
    QDoubleSpinBox *m_a1     = nullptr;
    QDoubleSpinBox *m_b1     = nullptr;
    QDoubleSpinBox *m_a2     = nullptr;
    QDoubleSpinBox *m_b2     = nullptr;
    QDoubleSpinBox *m_a3     = nullptr;
    QDoubleSpinBox *m_tw     = nullptr;
    QDoubleSpinBox *m_hstar  = nullptr;

    openswmmvis::ui::GwfExpressionEdit *m_lateral = nullptr;
    openswmmvis::ui::GwfExpressionEdit *m_deep    = nullptr;
    QLabel *m_lateralStatus = nullptr;
    QLabel *m_deepStatus    = nullptr;
    bool    m_lateralOk     = true;
    bool    m_deepOk        = true;
    bool    m_hasAquifer    = false;

    QDialogButtonBox *m_buttons  = nullptr;
    QPushButton      *m_applyBtn = nullptr;
};

#endif // GROUNDWATEREXCHANGEDIALOG_H
