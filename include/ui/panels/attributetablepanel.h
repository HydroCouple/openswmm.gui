/*!
 * \file   attributetablepanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice Z.1 — Attribute Tables dock.  Holds a QTableView over the
 * active SWMM project's objects, partitioned by Category via a combo
 * at the top.  Two-way bound to the project's SelectionManager so
 * picking rows highlights on the canvas and vice-versa.
 *
 * Z.2 (SQL-like query bar) and Z.3 (selection ops) are layered on top
 * of this panel in later slices.  Z.4 (tabular file uploads) plugs in
 * a second model kind through the same view.
 */

#ifndef ATTRIBUTETABLEPANEL_H
#define ATTRIBUTETABLEPANEL_H

#include "selection/selectionmanager.h"
#include "layers/swmmmodellayer.h"

#include <QPointer>
#include <QSet>
#include <QWidget>

class QAction;
class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSortFilterProxyModel;
class QTableView;
class QToolBar;
class SWMMAttributeTableModel;
class TabularDataLayer;
class TabularDataTableModel;
class GISVectorLayer;
class GISVectorAttributeTableModel;   // read-only OGR-feature source (defined in the .cpp)
class MapCanvas;

class AttributeTablePanel : public QWidget
{
    Q_OBJECT
public:
    explicit AttributeTablePanel(QWidget *parent = nullptr);
    ~AttributeTablePanel() override;

    /*! Bind to the active project — model layer + selection bus +
     *  canvas (used for "Zoom to selected").  Pass nulls to detach. */
    void setProject(SWMMModelLayer *layer,
                    SelectionManager *selMgr,
                    MapCanvas *canvas = nullptr);

    /*! Rebuild the category combo + reload the table model.  Called
     *  on `modelLoaded`. */
    void refresh();

    /*! Switch the source combo to \p layer's entry (a GIS feature layer
     *  or tabular layer). Model / results layers keep the current SWMM
     *  category selection — the panel is already bound to the model.
     *  Called from the layer tree's "Open Attribute Table" action. */
    void showLayerSource(OpenSWMMVisLayer *layer);

    /*! The current selection rendered as TSV: a header line plus one
     *  line per selected row, in the view's current sort and column
     *  order, hidden columns skipped.  Falls back to every *visible*
     *  row (i.e. what the query / show-selected-only filters leave)
     *  when no row is selected.  Empty string when there is nothing
     *  to copy.
     *
     *  Kept separate from `copySelectionToClipboard()` so the format
     *  can be unit-tested without a platform clipboard.  Reads through
     *  the proxy, so it works for both the SWMM model source and the
     *  Z.4 tabular-file source. */
    [[nodiscard]] QString selectionAsTsv() const;

    /*! Delete the named objects of the current category from the model.
     *
     *  The non-interactive core behind the Delete key / right-click "Delete"
     *  (which add a confirmation dialog on top). When a map canvas + undo stack
     *  are present the deletes go through DeleteObjectCommand so they are
     *  undoable and cascade node→link exactly like the map; otherwise they fall
     *  back to the layer's applyXDelete helpers — the SAME mutation the undo
     *  command performs, minus the undo record. Names not present, or a
     *  non-deletable category (a read-only data-object list), are skipped.
     *  Returns the number actually deleted. Kept public and dialog-free so the
     *  delete path is unit-testable without a modal, mirroring
     *  `selectionAsTsv()`. */
    int deleteObjects(const QStringList &names);

    /*! True when the bound category maps to a deletable object kind
     *  (junction/outfall/storage/divider/conduit/pump/orifice/weir/outlet/
     *  subcatchment/rain gage); false for data-object categories. */
    [[nodiscard]] bool categoryIsDeletable() const;

    /*! The Copy action — surfaced on the panel toolbar and in the
     *  right-click menu.  It carries no shortcut of its own: Ctrl+C is
     *  registered once, on the main window's `actionCopy`, which routes
     *  to the focused panel (a second registration here would make Qt's
     *  shortcut map treat Ctrl+C as ambiguous). */
    [[nodiscard]] QAction *copyAction() const { return m_copyAct; }

public slots:
    /*! Put `selectionAsTsv()` on the clipboard.  No-op when empty. */
    void copySelectionToClipboard();

    /*! Round-4 follow-up 2026-05-12 — refresh the row for \p name in
     *  response to an external edit (e.g. via the Property Browser).
     *  No-op if the named object isn't in the current category. */
    void onObjectEditedExternally(const QString &name);

signals:
    /*! Forwarded from `SWMMAttributeTableModel::objectEdited` when
     *  the user commits a cell edit.  Wired by `SWMMVis` to the
     *  Property Browser so both views always show the same engine
     *  state. */
    void objectEdited(const QString &name);

private slots:
    void onCategoryChanged(int comboIdx);
    void onTableSelectionChanged();
    void onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                   const QSet<SWMMObjectRef> &added,
                                   const QSet<SWMMObjectRef> &removed);
    void onShowSelectedOnlyToggled(bool on);
    void onZoomToSelectedClicked();
    void onExportCsvClicked();
    void onContextMenuRequested(const QPoint &pos);
    void onChangeTypeTriggered();

    /*! Delete the objects of the currently selected rows. Triggered by the
     *  Delete/Backspace key or the right-click "Delete" action. Prompts for
     *  confirmation, then routes through the shared map undo stack (so Ctrl+Z
     *  works and every other view stays in sync) — see deleteObjects(). No-op
     *  for non-deletable categories (e.g. read-only data-object lists). */
    void deleteSelectedRows();

    /*! Slice Z.2 — Apply the WHERE-clause text to the proxy filter. */
    void onQueryApplyClicked();
    /*! Clear the query bar + filter. */
    void onQueryClearClicked();

    // Slice Z.3 — Selection ops driven by the current query.  The
    // five modes are presented as radios on one row + one Apply
    // button (Round-4 follow-up 2026-05-12); this slot reads the
    // active radio and dispatches.
    void onSelectionApplyClicked();

private:
    bool eventFilter(QObject *watched, QEvent *event) override;

    void buildUi();
    SWMMObjectRef::ObjectType objectTypeFor(SWMMModelLayer::Category cat) const;

    /*! Persist the current category's column widths to QSettings under
     *  `SWMMVis/AttributeTablePanel/cat<N>/columnWidths`.  Called when
     *  the user switches categories or the dock closes. */
    void saveColumnWidths(SWMMModelLayer::Category cat) const;

    /*! Restore column widths for the now-active category from
     *  QSettings.  No-op if none stored. */
    void restoreColumnWidths(SWMMModelLayer::Category cat);

    /*! Round-4 follow-up 2026-05-12 — ensure every column is at
     *  least as wide as its header's size hint, so the descriptive
     *  labels ("Invert Elevation (ft)", "Discharge Coefficient",
     *  etc.) never get clipped.  Called after `restoreColumnWidths`
     *  so user-saved widths still win when they're wider than the
     *  header. */
    void ensureMinColumnWidths();

    /*! Slice Z.5.3 — install the right `QStyledItemDelegate` per
     *  column based on the bound category's `ColumnSpec` list.
     *  Called after `setSource()`. */
    void installColumnDelegates();

    /*! Slice Z.3 — collect the SWMMObjectRefs of all source rows
     *  whose identify map satisfies the current query predicate.
     *  Ignores the "show selected only" filter so the selection
     *  ops always operate on the full population. */
    QSet<SWMMObjectRef> matchedRefs() const;

    /*! All refs in the bound category — used by Invert. */
    QSet<SWMMObjectRef> allCategoryRefs() const;

    /*! Bulk-edit helpers backing the right-click "Apply … to selected
     *  rows" actions. `selectedSourceRows` returns the distinct source
     *  (model) rows currently selected. `applyValueToSelectedRows` writes
     *  `value` into `column` for each, wrapped in one undo macro, skipping
     *  rows whose cell isn't editable (e.g. an inapplicable geom).
     *  `promptBulkValue` pops the right input dialog for the column's
     *  editor kind (double / int / enum / text). */
    QList<int> selectedSourceRows() const;
    void applyValueToSelectedRows(int column, const QList<int> &sourceRows,
                                  const QVariant &value);
    QVariant promptBulkValue(int column, const QVariant &current,
                             bool *ok) const;

    QPointer<SWMMModelLayer>   m_layer;
    QPointer<SelectionManager> m_selMgr;
    QPointer<MapCanvas>        m_canvas;

    QComboBox               *m_categoryCombo = nullptr;
    QTableView              *m_view          = nullptr;
    QToolBar                *m_toolbar       = nullptr;
    QAction                 *m_copyAct       = nullptr;
    SWMMAttributeTableModel *m_model         = nullptr;
    TabularDataTableModel   *m_tabularModel  = nullptr;  ///< Z.4.3 — alt source
    GISVectorAttributeTableModel *m_gisModel = nullptr;  ///< external OGR feature-layer source
    QSortFilterProxyModel   *m_proxy         = nullptr;

    // Slice Z.2 — Query bar
    QLineEdit               *m_queryEdit     = nullptr;
    QPushButton             *m_queryApply    = nullptr;
    QPushButton             *m_queryClear    = nullptr;
    QLabel                  *m_queryStatus   = nullptr;  ///< "47 of 1205 rows"

    // Slice Z.3 — Selection-op radios + single Apply button.  IDs
    // assigned to the buttons in `buildUi()` are read back in
    // `onSelectionApplyClicked()` and dispatched to the matching
    // SelectionManager op.
    enum SelectionMode {
        SelReplace   = 0,
        SelAdd       = 1,
        SelSubtract  = 2,
        SelIntersect = 3,
        SelInvert    = 4,
    };
    QButtonGroup            *m_selGroup      = nullptr;
    QRadioButton            *m_selReplaceR   = nullptr;
    QRadioButton            *m_selAddR       = nullptr;
    QRadioButton            *m_selSubtractR  = nullptr;
    QRadioButton            *m_selIntersectR = nullptr;
    QRadioButton            *m_selInvertR    = nullptr;

    bool m_applyingFromBus = false;   ///< Reentrancy guard mirroring ObjectBrowserPanel.
    bool m_showSelectedOnly = false;
    bool m_suppressEditForward = false;  ///< Set during onObjectEditedExternally to break loop.
};

#endif // ATTRIBUTETABLEPANEL_H
