/**
 * @file      newprojectdialog.h
 * @brief     wizard-style single dialog for File → New Project.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice Y — wizard-style single dialog for File → New Project. Collects
 * the minimum set of options that go into a fresh, blank SWMM model:
 * project name, flow units, infiltration model, flow routing,
 * simulation window, and CRS. The result is consumed by SWMMVis to
 * synthesize a temp `.inp` and open it as an untitled project window.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QString>

class QComboBox;
class QDateTimeEdit;
class QLineEdit;
class QPushButton;

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \brief Inputs collected from the dialog. Strings are SWMM-INP
     *        keywords ready to inline into the synthesized `.inp`.
     */
    struct Result {
        QString name = QStringLiteral("Untitled");
        QString flowUnits = QStringLiteral("CFS");
        QString infiltrationModel = QStringLiteral("HORTON");
        QString flowRouting = QStringLiteral("DYNWAVE");
        QDateTime startDateTime;
        QDateTime endDateTime;
        QString crsAuthCode; ///< e.g. "EPSG:4326"; empty = no CRS.
    };

    /**
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Create the form and seed widgets from current preferences/local date without creating
     *   a project or input file.
     * @note Collected m_result remains its struct defaults until successful onAccept; no settings
     *   persistence or model ownership. GUI thread required.
     */
    explicit NewProjectDialog(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned form widgets without applying unaccepted edits or creating/saving a
     *   project.
     */
    ~NewProjectDialog() override = default;

    /**
     * @return Value copy of last successfully accepted input collection; before acceptance contains
     *   struct defaults with invalid start/end datetimes and empty CRS. Rejection does not reset
     *   prior accepted values when reusing the dialog.
     * @note This accessor hides QDialog::result; use exec return or qualified QDialog::result to
     *   check acceptance. Returned values do not indicate project creation success.
     */
    [[nodiscard]] Result result() const { return m_result; }

private slots:
    /**
     * @brief Run a child CRSSelectionDialog and copy a nonempty accepted authority token into the
     *   read-only field.
     * @note Does not preselect the field value, create/validate an SRS or transform coordinates.
     *   Cancellation/empty selection leaves field unchanged; CRS picker can update its recent
     *   preferences on acceptance.
     */
    void onChooseCrs();
    /**
     * @brief Require end strictly after start, then copy current inputs into m_result and accept.
     * @note Invalid interval shows warning and keeps dialog/result unchanged. Trim name/CRS and
     *   replace blank name with Untitled; enum keyword strings and dates are copied without extra
     *   semantic/CRS validation. No model creation, INP writing, timezone conversion or preference
     *   save occurs here.
     */
    void onAccept();

private:
    /**
     * @brief Create owned name, fixed SWMM keyword choices, minute-format date editors and
     *   read-only CRS picker field, wiring OK/Cancel.
     * @note Keywords include CFS/GPM/MGD/CMS/LPS/MLD, five infiltration choices and
     *   STEADY/KINWAVE/DYNWAVE/FV routing. Does not seed dates/preferences, validate engine support
     *   or create a model.
     */
    void buildUi();
    /**
     * @brief Populate widgets with Untitled, preferred simulation keywords, local today at midnight
     *   and start plus 86400 seconds.
     * @note Unknown preference keywords leave the noneditable combo current choice unchanged. Use
     *   preferred authority only with nonempty authority and positive code, otherwise EPSG:4326,
     *   without database validation. Does not update m_result or persist settings; displayed minute
     *   precision does not add timezone conversion.
     */
    void seedDefaults();

    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_flowUnitsCombo = nullptr;
    QComboBox *m_infiltrationCombo = nullptr;
    QComboBox *m_routingCombo = nullptr;
    QDateTimeEdit *m_startEdit = nullptr;
    QDateTimeEdit *m_endEdit = nullptr;
    QLineEdit *m_crsEdit = nullptr;
    QPushButton *m_crsBtn = nullptr;

    Result m_result;
};

#endif // NEWPROJECTDIALOG_H
