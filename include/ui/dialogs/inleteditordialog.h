/*!
 * \file   inleteditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Three-pane editor for SWMM inlet designs ([INLETS]).
 *
 * Modelled on TransectEditorDialog (Inlets plan §2.2). Layout (left → right,
 * a 1:3:3 QSplitter named "main" so the app-wide layout persistence picks it
 * up):
 *
 *   ┌────────────┬────────────────────────────┬────────────────────────┐
 *   │  Inlets    │  Name                      │  toolbar               │
 *   │  list view │  Description               │  (Fit / Zoom / Copy /  │
 *   │            │  Inlet Type (combo)        │   Export Image)        │
 *   │  [+ Add]   │  Property tree — the       │  InletDrawingView      │
 *   │  [- Delete]│  InletPropertyBag groups,  │  (plan + section with  │
 *   │            │  filtered by type          │   dimension callouts)  │
 *   └────────────┴────────────────────────────┴────────────────────────┘
 *
 * MVC contract: every mutation goes through InletProvider / InletRegistry,
 * wrapped in a QUndoCommand when the dialog has a QUndoStack (see
 * inletundocommands.h); the list, the tree and the drawing all re-render from
 * the provider's signals.
 *
 * Entry points mirror the transect editor: `createNew` (modeless, starts on a
 * fresh design), `openForInlet` (modeless, selects a design by name — used by
 * the Object Browser), and the modal `pickInlet` used by the inlet-usage
 * pickers, which additionally filters the list by the host conduit's
 * cross-section shape.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H

#include "inlet/inletprovider.h"

#include <QDialog>
#include <QPointer>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPropertyModel;
class QPushButton;
class QSplitter;
class QStatusBar;
class QTextEdit;
class QToolBar;
class QTreeView;
class QUndoStack;

class SWMMModelLayer;

namespace openswmmvis::inlet {
class InletProvider;
class InletRegistry;
}

namespace openswmmvis::ui {

class InletDrawingView;
class InletListModel;
class InletPropertyBag;

class InletEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    InletEditorDialog(openswmmvis::inlet::InletRegistry *registry,
                      SWMMModelLayer *layer,
                      QUndoStack *undoStack,
                      QWidget *parent = nullptr);
    ~InletEditorDialog() override;

    static InletEditorDialog *createNew(
        openswmmvis::inlet::InletRegistry *registry,
        SWMMModelLayer *layer,
        QUndoStack *undoStack,
        QWidget *parent = nullptr);

    /*! \brief Show (raising an already-open instance) and select \p name. */
    void openForInlet(const QString &name);

    /*! \brief Modal pick / create / edit entry point for the inlet-usage
     *  pickers. \p compatibleXsectShape is a `SWMM_XSectShape` id used to
     *  filter the list to the designs legal on that host cross-section
     *  (STREET → the gutter types, RECT_OPEN / TRAPEZOIDAL → the drop types,
     *  CUSTOM everywhere); pass -1 for "any". Returns the name of the design
     *  selected on close, or empty. Flushes the registry to the engine
     *  afterwards, as `pickTransect` does. */
    static QString pickInlet(openswmmvis::inlet::InletRegistry *registry,
                              SWMMModelLayer *layer,
                              QUndoStack    *undoStack,
                              QWidget       *parent,
                              int            compatibleXsectShape = -1);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::inlet::InletProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────
    QListView        *listView()     const noexcept { return m_listView; }
    InletListModel   *listModel()    const noexcept { return m_listModel; }
    QLineEdit        *nameEdit()     const noexcept { return m_nameEdit; }
    QComboBox        *typeCombo()    const noexcept { return m_typeCombo; }
    QTreeView        *propertyTree() const noexcept { return m_propertyTree; }
    InletDrawingView *drawingView()  const noexcept { return m_drawing; }

    void invokeNew();
    void deleteCurrentSilently();

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onCommentsEdited_();
    void onTypeComboChanged_(int index);
    void onPickCurveClicked_();
    void onCopyClicked_();
    void onExportImageClicked_();
    void onProviderRenamed_(openswmmvis::inlet::InletProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void buildToolbar_();
    void rebuildPropertyTree_();
    void applyRowVisibility_();
    void bindProvider_(openswmmvis::inlet::InletProvider *p);
    void selectProviderInList_(openswmmvis::inlet::InletProvider *p);
    void syncFromProvider_();
    void refreshCustomCurve_();
    void updateStatusBar_();
    QString suggestUniqueName_() const;

    /*! \brief The InletPropertyBag's commit hook: validates, then pushes a
     *  SetInletParamsCommand (or writes the provider directly when the dialog
     *  has no undo stack). A rejected edit is reported in the status bar and
     *  the bag is re-synced from the provider. */
    void commitDesign_(const openswmmvis::inlet::InletDesignData &before,
                        const openswmmvis::inlet::InletDesignData &after);

    /*! \brief Whether \p p may be used on the host cross-section this dialog
     *  was opened for. Always true when no shape filter is active. */
    bool passesShapeFilter_(openswmmvis::inlet::InletProvider *p) const;

    QPointer<openswmmvis::inlet::InletRegistry> m_registry;
    QPointer<SWMMModelLayer>                    m_layer;
    QUndoStack                                 *m_undoStack = nullptr;
    QPointer<openswmmvis::inlet::InletProvider> m_current;
    Mode                                        m_mode = Mode::Edit;
    int                                         m_shapeFilter = -1;

    QSplitter *m_splitter = nullptr;

    // Left pane.
    QListView      *m_listView  = nullptr;
    InletListModel *m_listModel = nullptr;
    QPushButton    *m_addBtn    = nullptr;
    QPushButton    *m_delBtn    = nullptr;

    // Middle pane.
    QLineEdit        *m_nameEdit      = nullptr;
    QTextEdit        *m_commentsEdit  = nullptr;
    QComboBox        *m_typeCombo     = nullptr;
    QTreeView        *m_propertyTree  = nullptr;
    QPropertyModel   *m_propertyModel = nullptr;
    InletPropertyBag *m_propertyBag   = nullptr;
    QPushButton      *m_pickCurveBtn  = nullptr;

    // Right pane.
    QToolBar         *m_toolBar = nullptr;
    InletDrawingView *m_drawing = nullptr;

    QStatusBar *m_status    = nullptr;
    QLabel     *m_hintLabel = nullptr;

    bool m_suppressSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H
