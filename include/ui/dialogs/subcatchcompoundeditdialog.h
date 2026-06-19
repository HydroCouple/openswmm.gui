/*!
 * \file   subcatchcompoundeditdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 3 — compound editor for subcatchment attributes that don't fit a
 * single property row: land-use coverage, groundwater configuration, and LID
 * usage. Mirror of NodeCompoundEditDialog: one QStackedWidget page per Kind,
 * apply-as-you-go engine writes, summary tracked in m_ref.
 */

#ifndef SUBCATCHCOMPOUNDEDITDIALOG_H
#define SUBCATCHCOMPOUNDEDITDIALOG_H

#include <QDialog>

#include "ui/properties/subcatchcompoundeditref.h"

class QStackedWidget;
class QTableWidget;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QDialogButtonBox;
class QPushButton;

class SubcatchCompoundEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SubcatchCompoundEditDialog(SubcatchCompoundEditRef ref,
                                        QWidget *parent = nullptr);

    [[nodiscard]] QString updatedSummary() const { return m_ref.summary; }

private:
    void buildLandUsePage();
    void buildGroundwaterPage();
    void buildLidUsagePage();
    void refreshActivePage();

    [[nodiscard]] int subIdx() const;   // swmm_subcatch_index, or -1

    SubcatchCompoundEditRef m_ref;

    // Land-use page
    QLabel       *m_luSummary  = nullptr;
    QTableWidget *m_luTable    = nullptr;
    QComboBox    *m_luCombo    = nullptr;
    QDoubleSpinBox *m_luCovSpin = nullptr;

    // Groundwater page
    QLabel         *m_gwSummary = nullptr;
    QComboBox      *m_gwAquifer = nullptr;
    QComboBox      *m_gwNode    = nullptr;
    QDoubleSpinBox *m_gwSurfEl  = nullptr;
    QDoubleSpinBox *m_gwA1      = nullptr;
    QDoubleSpinBox *m_gwB1      = nullptr;
    QDoubleSpinBox *m_gwA2      = nullptr;
    QDoubleSpinBox *m_gwB2      = nullptr;
    QDoubleSpinBox *m_gwA3      = nullptr;
    QDoubleSpinBox *m_gwTw      = nullptr;
    QDoubleSpinBox *m_gwHstar   = nullptr;

    // LID usage page
    QLabel       *m_lidSummary   = nullptr;
    QTableWidget *m_lidTable     = nullptr;
    QPushButton  *m_lidRemoveBtn = nullptr;
    QComboBox    *m_lidCombo     = nullptr;
    QSpinBox     *m_lidNumber    = nullptr;
    QDoubleSpinBox *m_lidArea    = nullptr;
    QDoubleSpinBox *m_lidWidth   = nullptr;
    QDoubleSpinBox *m_lidInitSat = nullptr;
    QDoubleSpinBox *m_lidFromImp = nullptr;

    QStackedWidget   *m_stack   = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // SUBCATCHCOMPOUNDEDITDIALOG_H
