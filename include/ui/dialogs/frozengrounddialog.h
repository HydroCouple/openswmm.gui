/**
 * @file      frozengrounddialog.h
 * @brief     Editor for the frozen-ground rows of subcatchments and mesh cells.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_FROZENGROUNDDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_FROZENGROUNDDIALOG_H

#include <QDialog>
#include <QList>

#include <openswmm/engine/openswmm_engine.h>

class QDialogButtonBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableView;

namespace openswmmvis::ui
{

class FrozenGroundRowModel;

/*! Two pages, one per section: [FROZEN_GROUND] rows by subcatchment and [2D_FROZEN_GROUND]
 *  rows by mesh tag. Edits are staged; Apply and OK validate both pages, write the changed
 *  ones and put every page back if any write fails. */
class FrozenGroundDialog : public QDialog
{
    Q_OBJECT
public:
    enum Page { SubcatchmentPage = 0, MeshPage = 1 };

    /**
     * @param[in] engine Borrowed engine, valid until invalidateContext() or destruction; null
     * opens an empty read-only dialog.
     * @param[in] parent Optional owner.
     */
    explicit FrozenGroundDialog(SWMM_Engine engine, QWidget *parent = nullptr);

    /** Show one page first. */
    void setCurrentPage(Page page);
    /** The staged model of a page (for tests and for views that share the data). */
    [[nodiscard]] FrozenGroundRowModel *model(Page page) const { return m_models.value(page); }
    /** Validate and write every changed page. False leaves the draft open and unchanged. */
    bool applyChanges();

public slots:
    /** Drop the borrowed engine and close; earlier Applies stay applied. */
    void invalidateContext();

signals:
    /** Writes are about to start (a later failure may leave them partly applied). */
    void changesMayHaveBeenApplied();
    /** Every changed section was written. */
    void applied();

private:
    void addRow(int page);
    void removeSelectedRows(int page);
    void setEditable(bool on, const QString &whyNot);

    SWMM_Engine m_engine = nullptr;
    bool m_editable = false;
    QLabel *m_banner = nullptr;
    QTabWidget *m_tabs = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QList<FrozenGroundRowModel *> m_models;
    QList<QTableView *> m_views;
    QList<QPushButton *> m_rowButtons;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_FROZENGROUNDDIALOG_H
