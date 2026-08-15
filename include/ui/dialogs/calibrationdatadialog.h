/*!
 * \file   calibrationdatadialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BK — Calibration data registration dialog.
 *
 * Maintains a table of (object, attribute, observed-CSV path) bindings.
 * Persisted in the active project window's .oswp under the
 * "calibrationData" key. When the user opens the Comparison Plot Dialog
 * for a calibrated object, the matching observed CSV is automatically
 * added as an Observed RunSource via BL-Polish.2's ObservedCsvRunLayer.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_CALIBRATIONDATADIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_CALIBRATIONDATADIALOG_H

#include "plot/plotattribute.h"

#include <QDialog>
#include <QPointer>
#include <QString>

class QTableWidget;
class SWMMVisProjectWindow;

namespace openswmmvis::ui {

struct CalibrationEntry {
    QString                        objectName;
    QString                        objectKind;   // "Node" / "Link" / "Subcatch"
    openswmmvis::plot::PlotAttribute attribute = openswmmvis::plot::PlotAttribute::Unknown;
    QString                        observedPath;
    QString                        observedColumn;   // column name in the CSV
};

class CalibrationDataDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CalibrationDataDialog(SWMMVisProjectWindow *projectWindow,
                                    QWidget *parent = nullptr);
    ~CalibrationDataDialog() override;

    /*! \brief Static helper to read the calibration entries persisted in
     *  the project window's .oswp. */
    static QVector<CalibrationEntry> entriesFromProject(SWMMVisProjectWindow *pw);

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onBrowseClicked();
    void accept() override;

private:
    void buildUi();
    void loadFromProject();
    void saveToProject();

    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QTableWidget                  *m_table = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CALIBRATIONDATADIALOG_H
