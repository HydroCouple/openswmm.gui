/*!
 * \file   newprojectdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice Y — wizard-style single dialog for File → New Project. Collects
 * the minimum set of options that go into a fresh, blank SWMM model:
 * project name, flow units, infiltration model, flow routing,
 * simulation window, and CRS. The result is consumed by SWMMVis to
 * synthesize a temp `.inp` and open it as an untitled project window.
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
    struct Result
    {
        QString   name              = QStringLiteral("Untitled");
        QString   flowUnits          = QStringLiteral("CFS");
        QString   infiltrationModel  = QStringLiteral("HORTON");
        QString   flowRouting        = QStringLiteral("DYNWAVE");
        QDateTime startDateTime;
        QDateTime endDateTime;
        QString   crsAuthCode;       ///< e.g. "EPSG:4326"; empty = no CRS.
    };

    explicit NewProjectDialog(QWidget *parent = nullptr);
    ~NewProjectDialog() override = default;

    [[nodiscard]] Result result() const { return m_result; }

private slots:
    void onChooseCrs();
    void onAccept();

private:
    void buildUi();
    void seedDefaults();

    QLineEdit       *m_nameEdit         = nullptr;
    QComboBox       *m_flowUnitsCombo   = nullptr;
    QComboBox       *m_infiltrationCombo = nullptr;
    QComboBox       *m_routingCombo     = nullptr;
    QDateTimeEdit   *m_startEdit        = nullptr;
    QDateTimeEdit   *m_endEdit          = nullptr;
    QLineEdit       *m_crsEdit          = nullptr;
    QPushButton     *m_crsBtn           = nullptr;

    Result m_result;
};

#endif // NEWPROJECTDIALOG_H
