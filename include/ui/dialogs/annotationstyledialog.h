/**
 * @file      annotationstyledialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QPropertyModel-backed style editor dialog for a single
 *         AnnotationTextItem.
 *
 * Mirrors the SeriesStyleEditor pattern: a QTreeView fed by a QPropertyModel
 * wrapping an owned draft AnnotationTextItem. Edits update that draft;
 * acceptance copies its JSON to the borrowed target, and cancellation
 * leaves the target unchanged. The caller records layer undo separately.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_ANNOTATIONSTYLEDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_ANNOTATIONSTYLEDIALOG_H

#include <QDialog>

class AnnotationTextItem;
class QDialogButtonBox;
class QPropertyModel;
class QTreeView;
class QPlainTextEdit;
class MapCanvas;

class AnnotationStyleDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \param item   The annotation being edited. Must outlive the dialog.
     * @param[in] parent Optional QWidget owner, caller manages parentless dialog.
     * @param[in] canvas Optional borrowed raw canvas for source choices/anchor compensation; must
     *   survive dialog and accept. Source layers/target remain caller-owned.
     * @brief Copy optional target JSON into a new owned AnnotationTextItem draft and build
     *   style/reference controls.
     * @note Null target is permitted but accepted draft has no external recipient/accessor. Edits
     *   affect detached draft until accept, not a live map preview; Cancel leaves target unchanged.
     *   With canvas, saved missing source references are preserved unless user changes source.
     *   GUI-thread/property-model dependency required.
 */
    explicit AnnotationStyleDialog(AnnotationTextItem *item, QWidget *parent = nullptr,
                                   MapCanvas *canvas = nullptr);
    /**
     * @brief Destroy owned draft/widgets/model without committing unaccepted edits, owning
     *   target/canvas or persisting settings.
     */
    ~AnnotationStyleDialog() override;

    /*! When true, the dialog title reads "Edit Text Annotation" and the OK
     *  button defaults to "Apply". When false (placement mode), the title
     *  reads "Add Text Annotation" and OK reads "Add".
     * @param[in] edit True chooses Edit/Apply captions, false Add/Add captions.
     * @note Caption-only state, no copying, validation, change to target ownership or immediate
     *   source mutation.
 */
    void setEditMode(bool edit);

private:
    /**
     * @brief Bind owned draft to quick text, halo/pin, source/feature/expression/callout controls
     *   and QPropertyModel style tree.
     * @note Text field and draft text property synchronize with feedback guards; numeric delegate
     *   commits changes into draft immediately. Source choices snapshot canvas SWMM/GIS layers,
     *   selecting feature writes reference; free text clears reference/expression/callout. Missing
     *   source/feature can remain represented without discarding saved reference.
     * @note No live source-list subscription, engine edits, undo or target mutation. Editable
     *   feature combo changes reference only on current-index change, not arbitrary typed text.
     *   Some quick controls are one-way draft bindings; external draft changes are not all
     *   reflected automatically.
     */
    void buildUi();
    /**
     * @brief Interpret visible numeric editor text, require nonblank draft text OR expression, then
     *   copy draft JSON into optional target and accept.
     * @note Blank keeps dialog open silently. No expression token/reference/transform validation
     *   gates acceptance. For changed feature reference with canvas, resolve old/new anchors into
     *   canvas CRS and compensate pixel offset to preserve current drawn location when both
     *   succeed. Use rendered target offset when available, otherwise stored; failed
     *   resolution/transform leaves existing draft offset.
     * @note Native free-text position uses containing annotation layer CRS or canvas fallback;
     *   pixel displacement computed through integer toPixelCoords. No undo command/persistence
     *   itself, caller snapshots target layer. Required raw target/canvas/source lifetimes extend
     *   through accept and notifications.
     */
    void accept() override;
    AnnotationTextItem *m_target = nullptr;
    MapCanvas *m_canvas = nullptr;

    AnnotationTextItem *m_item = nullptr;
    QPlainTextEdit *m_text = nullptr; ///< Quick-access editor for the most-edited field.
    QTreeView *m_tree = nullptr;
    QPropertyModel *m_model = nullptr;
    QDialogButtonBox *m_btns = nullptr;
};

#endif // OPENSWMMVIS_UI_DIALOGS_ANNOTATIONSTYLEDIALOG_H
