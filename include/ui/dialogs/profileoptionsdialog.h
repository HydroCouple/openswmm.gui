/**
 * @file      profileoptionsdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Display Options dialog for the profile plot — a tabbed dialog with
 *         a QPropertyModel-backed tree view that edits a `ProfilePlotOptions`
 *         instance (Display tab) plus a Sources tab that lets the user
 *         toggle, recolour, rename and add comparison-output sources.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_OPTIONS_DIALOG_H
#define PROFILE_OPTIONS_DIALOG_H

#include <QDialog>
#include <QPointer>

class AnimationController;
class ProfileAttributeTrackOptions;
class ProfilePlotOptions;
class ProfileSourceStyleAdapter;
class SWMMVisProjectWindow;
class QAbstractItemDelegate;
class QAbstractItemModel;
class QStandardItemModel;
class QTabWidget;
class QTreeView;

class ProfileOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] options Optional borrowed guarded display-options QObject; not owned. Property
     *   editor binds it directly, caller coordinates its lifetime with property-model use.
     * @param[in] anim Optional borrowed guarded animation source registry used for ordered
     *   discovery.
     * @param[in] projectWindow Optional borrowed guarded project context for active/canvas sources,
     *   settings key and chooser directory.
     * @param[in] parent Optional QWidget owner, caller manages parentless nonmodal top-level
     *   dialog.
     * @brief Build Display/Sources tabs and restore per-project comparison preferences, applying
     *   saved style/name values to existing layers immediately.
     * @note Edits are live with Close only, no staged Apply/Cancel/undo rollback. Constructor may
     *   emit sourcesChanged during restore. Watches project canvas result-layer add/remove when
     *   available; animation-only changes require explicit refreshSources. GUI-thread/event-loop
     *   use; layer item payloads are raw and caller must refresh before their lifetime ends.
     */
    explicit ProfileOptionsDialog(ProfilePlotOptions *options, AnimationController *anim = nullptr,
                                  SWMMVisProjectWindow *projectWindow = nullptr,
                                  QWidget *parent = nullptr);

    /*! Adds an "Attribute Tracks" tab editing the given options object
     *  through the same QPropertyModel machinery as the Display tab.
     *  Call once, right after construction; null is a no-op.
     * @param[in] trackOptions Optional borrowed live track-options object; null is no-op, not a
     *   detach. QObject connections disconnect on destruction but property-model binding lifetime
     *   remains caller responsibility.
     * @brief Append another live Attribute Tracks property tab; each nonnull call adds a new tab
     *   rather than replacing an old one.
     * @note With QPropertyModel edits write directly and changed refreshes values; without it
     *   display unavailable message. No copy, ownership transfer, source options reset or delayed
     *   apply.
 */
    void setTrackOptions(ProfileAttributeTrackOptions *trackOptions);

public slots:
    /*! Refresh the Sources tab to mirror the AnimationController's current
        layer list.  Call after the host adds/removes a layer.
     * @brief Rebuild current discovery list, resetting rows checked, then restore persisted
     *   visibility/style/name for matching file paths.
     * @note Refresh can apply layer mutations and emits sourcesChanged through restore even without
     *   actual changes. Unpersisted checkbox state/row selection is not preserved. No new results
     *   files loaded or layers added, and no engine mutation.
 */
    void refreshSources();

signals:
    /*!
     * \brief Emitted when the user toggles visibility, edits a colour, or
     *        renames a source.  The host dialog should call rebindSources()
     *        + populateSourcesPanel() to update the plot + toolbar.
     * @note Also emitted after restore even with zero changes, and forwarded style-adapter
     *   binding/property notifications can cause additional emissions/persistence. Checkboxes alter
     *   table/persisted profile participation, not canvas layer visibility; host rebinds plot
     *   sources. No source lifetime or acceptance guarantee.
     */
    void sourcesChanged();

    /*!
     * \brief Emitted when the user picks an external .out file.  The host
     *        should construct an SWMMResultsLayer for the file and register
     *        it on the AnimationController as a secondary layer.
     * @param[in] path Nonempty selected output path copied by receiver as needed; no
     *   open/validation performed here.
     * @note Emitted synchronously after chooser. Host loads/registers layer; dialog rebuilds
     *   immediately after emission, so asynchronous insertion needs a later refresh. No ownership,
     *   model engine write or automatic animation registration supplied.
     */
    void addOutputFileRequested(const QString &path);

private:
    /**
     * @brief Append owned live display-options tree, bound to optional options when QPropertyModel
     *   available.
     * @note Source changed signal refreshes property values; setters perform immediate edits, no
     *   staging/undo. Without property library show empty standard-item model. Does not
     *   duplicate/own source options.
     */
    void buildDisplayTab();
    /**
     * @brief Build sortable raw-layer rows, optional live per-source style adapter, comparison file
     *   picker and immediate edit handlers.
     * @note Visibility persists row check only; nonempty scenario edits mutate layer (blank text
     *   remains visible but leaves layer name), color chooser mutates profileLineColor without
     *   changing independent pens/brushes. Both paths persist/notify; style-adapter changed also
     *   persists/notifies, including binding changes. No undo/rollback or engine changes.
     * @note Selected result layer is borrowed via raw QObject item payload; host must keep it alive
     *   or refresh promptly. Color chooser/event callbacks retain raw layer over nested dialog,
     *   requiring lifetime coordination. Add-output emits request before immediate list rebuild,
     *   and no-source style editor disabled.
     */
    void buildSourcesTab();
    /**
     * @brief Replace rows from profileResultSources, with every visibility checkbox initially
     *   Checked and per-layer color/name/file/project display.
     * @note Item model signals blocked during rebuild; items carry unguarded QObject layer pointers
     *   and source order can be overridden by view sorting. Does not restore saved state, emit
     *   explicit sourcesChanged, preserve row selection/checks or apply visibility to layer.
     *   Unsaved file gets explanatory label; project name is found by QObject ancestor scan.
     */
    void refreshSourcesModel();
    /**
     * @brief Replace sources array under persistenceKey with current rows having live payload and
     *   nonempty results-file path.
     * @note Save exact file string, checked state, scenario name, RGB color name (alpha discarded
     *   for this color key) and six complete QPen/QBrush style variants in current row order.
     *   Remove prior array first; unsaved sources omitted. No sync/error check, engine write or
     *   undo; raw payload objects must still live.
     */
    void persistSourcesState() const;
    /**
     * @brief Apply saved checkbox, valid color, nonempty scenario and available per-source styles
     *   to current exact-path rows, then emit sourcesChanged.
     * @note Duplicate current paths map to LAST row; missing files ignored, no result loading.
     *   Item-model signals blocked, but layer/adapter signals remain active and may trigger further
     *   notifications/persistence. Nonempty invalid color does not set layer color but still
     *   updates row swatch/text from invalid QColor. Missing keys retain current defaults; no undo
     *   or rollback.
     */
    void restoreSourcesState();

    /**
     * @return ProfilePlot/Comparison/ plus raw current project model path, or shared default when
     *   unavailable/unsaved. No canonicalization/escaping, so path spelling and QSettings
     *   separators affect key hierarchy. No mutation or storage access.
     */
    [[nodiscard]] QString persistenceKey() const;

    QPointer<ProfilePlotOptions> m_options;
    QPointer<AnimationController> m_anim;
    QPointer<SWMMVisProjectWindow> m_projectWindow;

    QTabWidget *m_tabs = nullptr;
    QTreeView *m_tree = nullptr;
    QAbstractItemModel *m_model = nullptr;
    QAbstractItemDelegate *m_delegate = nullptr;

    QTreeView *m_sourcesView = nullptr;
    QStandardItemModel *m_sourcesModel = nullptr;
    // Right-pane editor on the Sources tab — a QPropertyModel-backed
    // QTreeView bound to a ProfileSourceStyleAdapter that proxies the
    // selected layer's profile-style Q_PROPERTYs.  Empty (disabled) when
    // no source row is selected.
    QTreeView *m_styleView = nullptr;
    QAbstractItemModel *m_styleModel = nullptr;
    QAbstractItemDelegate *m_styleDelegate = nullptr;
    ProfileSourceStyleAdapter *m_styleAdapter = nullptr;
};

#endif // PROFILE_OPTIONS_DIALOG_H
