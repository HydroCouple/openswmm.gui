/**
 * @file      crschangedialog.h
 * @brief     modal that asks the user how to apply a CRS change to a model with already-loaded
 *            geometry: reproject the stored coordinates, just re-render in the new CRS, or cancel.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Phase 0.7 — modal that asks the user how to apply a CRS change to a model
 * with already-loaded geometry: reproject the stored coordinates, just
 * re-render in the new CRS, or cancel.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef CRSCHANGEDIALOG_H
#define CRSCHANGEDIALOG_H

#include <QDialog>
#include <QString>

class QRadioButton;
class QLabel;
class QDialogButtonBox;

/*!
 * \class CRSChangeDialog
 * \brief Three-choice CRS change confirmation.
 *
 * Displayed by SWMMVis::onCRSButtonClicked when the active project has a
 * non-empty network and the user selects a different CRS.
 */
class CRSChangeDialog : public QDialog
{
    Q_OBJECT

public:
    enum Choice {
        Cancel = 0,
        Reproject = 1,  ///< Permanently rewrite stored coordinates via OGR transform.
        RenderOnly = 2, ///< Keep stored coordinates; only the canvas display CRS changes.
        Assign = 3,     ///< Declare the CRS of existing coordinates without transforming them.
    };

    /*!
     * \param oldAuth   Authority of the model's current CRS (e.g. "EPSG:6595").
     * \param newAuth   Authority of the newly-selected CRS.
     * \param sourceIsLocal If true, only assignment is available; a transform
     *                      requires a known source CRS.
     * @param[in] parent Optional QWidget owner; caller manages parentless result.
     * @brief Construct a modal choice form; authority strings are inserted into the rich-text
     *   summary without escaping, with empty strings displayed as none.
     * @note Ordinary source defaults to Reproject; local source disables both transform/display
     *   options and selects Assign. No CRS lookup, coordinate transformation, model mutation, dirty
     *   flag or undo action occurs here. Caller interprets accepted choice and performs the change.
     */
    explicit CRSChangeDialog(const QString &oldAuth, const QString &newAuth,
                             bool sourceIsLocal = false, QWidget *parent = nullptr);

    /**
     * @return Last choice recorded by onAccept, initially Cancel. Rejecting does not reset this
     *   value when reusing a previously accepted dialog; check the QDialog result as well. No model
     *   action or ownership transfer.
     */
    Choice choice() const { return m_choice; }

private slots:
    /**
     * @brief Record checked intent in Assign, Reproject, RenderOnly priority order, then accept the
     *   dialog.
     * @note With no checked button records Cancel but still accepts. Performs no CRS/coordinate
     *   validation or model edit; disabled options are enforced by UI setup only.
     */
    void onAccept();

private:
    Choice m_choice = Cancel;
    QRadioButton *m_radioReproject = nullptr;
    QRadioButton *m_radioRenderOnly = nullptr;
    QRadioButton *m_radioAssign = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
};

#endif // CRSCHANGEDIALOG_H
