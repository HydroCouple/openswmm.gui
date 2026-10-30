/**
 * @file      sectionviewpanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Dockable section / profile view of the current selection.
 *
 * Slices SP.4 / SP.5 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * A standalone dock rather than a pane inside the Properties panel: the
 * drawing wants far more room than a property grid can spare, users want it
 * docked where it suits them (or floating on a second monitor), and keeping it
 * separate leaves PropertiesPanel untouched.
 *
 * Follows the active selection through the same `SelectionManager` dispatch
 * that drives the property browser, and repaints on the model layer's
 * `attributeChanged` so an edit made anywhere — property grid, attribute
 * table, cross-section dialog — is reflected immediately.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_SECTIONVIEWPANEL_H
#define OPENSWMMVIS_UI_PANELS_SECTIONVIEWPANEL_H

#include <QDockWidget>
#include <QPointer>
#include <QString>

class QComboBox;
class QLabel;
class QToolButton;
class SWMMModelLayer;

namespace openswmmvis::sectionview
{
class SectionPreviewWidget;
}

namespace openswmmvis::ui
{

/*!
 * \class SectionViewPanel
 * \brief Right-dock vector section / profile view for the selected object.
 */
class SectionViewPanel : public QDockWidget
{
    Q_OBJECT

public:
    /*! Which drawing is shown for a link. Nodes always draw a profile. */
    enum class Mode { Section, Profile };

    /**
     * @param[in] parent Optional QWidget owner.
     * @brief Build owned section/profile/VE controls and preview, clear selection; Section mode
     *   default VE 1 and independent Profile VE 10. GUI-thread only, no project/SelectionManager
     *   subscription installed until host calls bindings/showObject.
     */
    explicit SectionViewPanel(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls/connections; guarded model/engine externally owned, no model
     *   save or view-state persistence.
     */
    ~SectionViewPanel() override;

    /*! Bind to the active project's model layer (nullptr to detach).
     *  Idempotent; re-wires the attributeChanged connection safely.
     * @param[in] layer Optional borrowed guarded model layer, null detach.
     * @note Equality no-op, including already-null guard. Replace attributeChanged hook then
     *   clearSelection, preserving mode/ratios/preview zoom. No UnitSystem changes subscription or
     *   engine ownership. Host drives SelectionManager dispatch separately and clears on model
     *   destruction when needed.
     */
    void setProject(SWMMModelLayer *layer);

    /*! Show the object identified by \p objectType (a
     *  `SWMMObjectRef::ObjectType`, passed as int to keep the header free of
     *  the selection include) and \p name. Non-drawable kinds clear the view
     *  with an explanatory message.
     * @param[in] objectType SWMMObjectRef enum integer, Node/Link supported; Unknown clears model,
     *   other kinds explain no section.
     * @param[in] name Exact copied engine ID, empty clears current model on refresh.
     * @note Always update stored selection/buttons and refresh. Different kind/name resets preview
     *   zoom to extents AFTER refresh; same subject preserves zoom. No selection-manager mutation
     *   or external validation. Positive node/link lookup failure clearSelection.
     */
    void showObject(int objectType, const QString &name);

    /*! Clear to the "nothing selected" state.
     * @note Set Unknown/empty ID, hide mode/VE controls and replace preview with default empty
     *   model. Keeps mode, both VE ratios and preview zoom/pan; emits no selection signal/engine
     *   mutation.
     */
    void clearSelection();

    /**
     * @return Stored mode, Section initially; nodes ignore mode for drawing. No widget toggle
     *   query.
     */
    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    /**
     * @param[in] mode Target Section/Profile enum; invalid enum not rejected, nonProfile follows
     *   Section branch.
     * @note Equal mode only refreshes buttons. Transition stores, updates buttons/VE combo,
     *   refreshes model and resets preview zoom. No modeChanged/persistence write; per-mode ratios
     *   retained.
     */
    void setMode(Mode mode);

    /*! Scale (V:H) applied to LINK drawings, for the CURRENT mode:
     *  0 = automatic (fill the pane, ratio stated on the drawing), >0 = an
     *  explicit V:H ratio, 1.0 being true shape / true scale. Section and
     *  profile keep their own value — a cross-section is only honest at 1:1,
     *  while a profile is unreadable there — so switching mode restores that
     *  mode's ratio rather than carrying the other one across. Node drawings
     *  always fill the pane and ignore this.
     * @return Raw per-current-mode VE,0auto/positive explicit by convention; can be
     *   negative/nonfinite because setter does not validate. Nodes ignore value.
     */
    [[nodiscard]] double verticalExaggeration() const noexcept
    {
        return m_mode == Mode::Profile ? m_profileVe : m_sectionVe;
    }
    /**
     * @param[in] ve Raw desired V:H ratio, 0 auto; caller supplies finite meaningful value, no
     *   local clamp/validation.
     * @note Fuzzy compare(slot+1,ve+1)no-op; else store current-mode slot, sync combo if exact
     *   offered value and refresh without resetting zoom. Arbitrary/nonfinite stored values may not
     *   appear in fixed combo choices; painter normalizes some values later. No engine/model input
     *   mutation or signal.
     */
    void setVerticalExaggeration(double ve);

public slots:
    /*! Rebuild the current drawing from the engine. Cheap enough to call on
     *  every edit: one engine read plus one section sampling.
     * @brief Rebuild current supported object drawing synchronously from bound engine and GLOBAL
     *   UnitSystem label/SI convention.
     * @note Missing engine/name/Unknown gives default empty model; missing positive ID clears
     *   selection; unsupported kind explanatory model. Link Profile explicit positive VE overrides
     *   builder; auto removes target aspect/cap to fill pane. Section always nonuniform with stated
     *   VE (auto fills). Node ignores control. Preserves preview zoom and requests repaint via
     *   setModel. Can scan all incident engine links/use independent samplers, not just one engine
     *   read despite old cheap summary. No engine write, file save or run advance.
     */
    void refresh();

    /*! Refresh only when \p name is the object on display. Wired to
     *  `SWMMModelLayer::attributeChanged`.
     * @param[in] name Exact emitted object ID; only nonempty current-name match refreshes.
     * @note Does not catch connected endpoint/node edits for displayed link or old→new rename
     *   automatically; host selection dispatch must update subject. No object existence scan or
     *   global invalidation.
     */
    void onObjectEditedExternally(const QString &name);

private:
    /**
     * @brief Construct owned toggle/VE row and preview, wire clicks/current-index to setters;
     *   construction-only, not idempotent. No engine/model binding.
     */
    void buildUi();
    /**
     * @note Only Link shows/checks Section/Profile and VE widgets; every other kind hides them.
     *   Does not select mode or rebuild model itself.
     */
    void updateModeButtons();
    /*! Point the V:H combo at the current mode's ratio without re-entering
     *  setVerticalExaggeration.
     * @note Exact data-value lookup of stored ratio, blocked assignment only if known/different.
     *   Missing combo/unoffered ratio no-op, leaves prior visible choice rather than inserting
     *   custom value. No setter recursion, engine write or refresh.
     */
    void syncVeCombo();

    QPointer<SWMMModelLayer> m_layer;

    sectionview::SectionPreviewWidget *m_preview = nullptr;
    QToolButton *m_sectionBtn = nullptr;
    QToolButton *m_profileBtn = nullptr;
    QLabel *m_veLabel = nullptr;
    QComboBox *m_veCombo = nullptr;

    /*! Per-mode V:H; 0 = automatic. Persisted across selections so a user who
     *  works at one ratio is not put back on another by every click.
     *
     *  Section defaults to 1:1 because anything else distorts the true shape
     *  of the barrel — the one thing a cross-section exists to show. Profile
     *  defaults to 10:1, the conventional drainage-sheet exaggeration, since
     *  a 0.25 % invert slope is invisible at true scale. */
    double m_sectionVe = 1.0;
    double m_profileVe = 10.0;

    /*! `SWMMObjectRef::ObjectType` of what is displayed; 0 = Unknown. */
    int m_objectType = 0;
    QString m_objectName;
    Mode m_mode = Mode::Section;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_SECTIONVIEWPANEL_H
