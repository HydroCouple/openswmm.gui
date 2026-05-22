/*!
 * \file   chartpropertiesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.3 — modeless dialog hosting a QPropertyModel-backed
 *         editor for one ChartProperties wrapper.
 *
 * Opened from the per-chart right-click "Chart Properties…" entry in
 * `ComparisonPlotDialog`. The dialog owns the `ChartProperties` it
 * displays; deleting the dialog deletes the wrapper (the underlying
 * QChart is unaffected). Window flags include Qt::Tool + StaysOnTop so
 * the editor floats above the main dialog.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_CHARTPROPERTIESDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_CHARTPROPERTIESDIALOG_H

#include <QDialog>
#include <QPointer>

class QTreeView;

namespace openswmmvis::plot { class ChartProperties; }

namespace openswmmvis::ui {

class ChartPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \brief Construct with the wrapper to edit. Dialog takes ownership
     *  of \p props (parents it). */
    explicit ChartPropertiesDialog(openswmmvis::plot::ChartProperties *props,
                                   QWidget *parent = nullptr);
    ~ChartPropertiesDialog() override = default;

private:
    QPointer<openswmmvis::plot::ChartProperties> m_props;
    QTreeView *m_tree = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CHARTPROPERTIESDIALOG_H
