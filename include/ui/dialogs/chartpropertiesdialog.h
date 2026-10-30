/**
 * @file      chartpropertiesdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice AT.3 — modeless dialog hosting a QPropertyModel-backed
 *         editor for one ChartProperties wrapper.
 *
 * Opened from the per-chart right-click "Chart Properties…" entry in
 * `ComparisonPlotDialog`. The dialog owns the `ChartProperties` it
 * displays; deleting the dialog deletes the wrapper (the underlying
 * QChart is unaffected). Qt::Tool is added to the window flags; no explicit
 * WindowStaysOnTopHint is set. The dialog deletes itself on close.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_CHARTPROPERTIESDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_CHARTPROPERTIESDIALOG_H

#include <QDialog>
#include <QPointer>

class QTreeView;

namespace openswmmvis::plot
{
class ChartProperties;
}

namespace openswmmvis::ui
{

class ChartPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @brief Build a modeless tool dialog with live property editing for an optional chart
     * wrapper.
     * @param props Optional ChartProperties transferred into QObject child ownership by
     * setParent(this); caller must pass a reparentable heap object and not delete it later.
     * Wrapper borrows its underlying chart rather than transferring chart ownership.
     * @param parent Optional Qt widget owner. Dialog enables WA_DeleteOnClose, so use heap
     * allocation when it can close; parentless instance is still deleted on close.
     * Creates an owned QPropertyModel/delegate/tree, names itself for application layout
     * persistence and starts360-by460. Close button routes rejected to accept; underlying
     * chart destruction routes to close when a chart existed at construction. Qt::Tool is
     * added without an explicit WindowStaysOnTopHint. Property edits follow wrapper setters
     * immediately, without Apply draft, undo, snapshot or Cancel rollback. GUI thread only.
     */
    explicit ChartPropertiesDialog(openswmmvis::plot::ChartProperties *props,
                                   QWidget *parent = nullptr);
    /**
     * @brief Destroy the property model/delegate/widgets and transferred ChartProperties child.
     * Does not own/destroy the wrapped chart or revert live property changes. Existing Qt
     * connections are removed through QObject lifetime management.
     */
    ~ChartPropertiesDialog() override = default;

private:
    QPointer<openswmmvis::plot::ChartProperties> m_props;
    QTreeView *m_tree = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CHARTPROPERTIESDIALOG_H
