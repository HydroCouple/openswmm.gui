/**
 * @file      linkcompoundeditdialog.h
 * @brief     One dialog with a stacked-widget body, one page per compound link attribute
 *            (XSection / Inlet Usage).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice SC.1 — One dialog with a stacked-widget body, one page
 * per compound link attribute (XSection / Inlet Usage). The former
 * Culvert Code page moved to an inline `CulvertCodeComboBox` per
 * docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md Phase 0.
 *
 * Apply-as-you-go: each page commits changes to the engine via the
 * model layer's `applyLink*` helpers as the user edits. The dialog's
 * `updatedSummary()` reflects the final state on close so the cell
 * widget can refresh its label without re-querying the engine.
 *
 * Slice BN Phase 6.4.4 replaces the XSection page's inner body with
 * the rich multi-pane 26-shape live-preview editor (per §S.11); the
 * `LinkCompoundEditRef` plumbing + dialog host stay unchanged so this
 * is a drop-in widget swap.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef LINKCOMPOUNDEDITDIALOG_H
#define LINKCOMPOUNDEDITDIALOG_H

#include <QDialog>

#include "ui/properties/linkcompoundeditref.h"

class QStackedWidget;
class QDialogButtonBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QLabel;
class QListWidget;
class QSplitter;
class LabeledPickerCombo;

namespace openswmmvis::sectionview
{
class SectionPreviewWidget;
}

class LinkCompoundEditDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param[in] ref Moved link identity containing borrowed raw engine/layer, page kind and cached
     *   summary; keep live until invalidateContext, GUI-thread serialized engine access.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog, host chooses
     *   modal exec.
     * @brief Build both cross-section and inlet pages from current source, then display requested
     *   kind.
     * @note Xsection/barrels write directly per widget change (layer attribute notifications when
     *   bound), without undo; inlet changes use model edit-stack command when available, otherwise
     *   direct apply. Close never reverses prior edits. Layer destruction/engine-close invalidates,
     *   geometryChanged rechecks copied name; unbound engine callers must invalidate before
     *   closure. No general running-state/read-only UI gate or source ownership.
     */
    explicit LinkCompoundEditDialog(LinkCompoundEditRef ref, QWidget *parent = nullptr);

    /*! Refreshed summary string after the dialog closes. Equal to the
     *  constructor's `ref.summary` until a summary-changing apply/read.
     * @return Copied cached summary. Xsection cache updates only on successful applyXsect (initial
     *   label read does not replace ref.summary); InletUsage kind updates from initial/load usage,
     *   and successful inlet edit updates it. No fresh query or accepted/mutation-success flag.
 */
    [[nodiscard]] QString updatedSummary() const { return m_ref.summary; }

public slots:
    /*! Disable editing before the project engine or owning layer is released.
     * @brief Forget borrowed engine/layer, disable entire dialog and reject without closing source
     *   or rolling back previously applied changes.
 */
    void invalidateContext();

private slots:
    /**
     * @brief Resolve current link by copied name and invalidate when absent/null; rename is treated
     *   as old-name removal. Does not refresh shape/capabilities/usage or validate engine
     *   identity/state.
     */
    void checkContext();

private:
    /**
     * @brief Create owned shape palette, four geometry editors, barrel count, transect/street
     *   pickers and draft-driven section preview, hydrating engine state with write callbacks
     *   suppressed.
     * @note Orifice offers circular/closed rectangle; weir open rectangle/trapezoid/triangle; other
     *   link kinds all metadata shapes except CUSTOM lacking curve picker. Type read failure
     *   defaults conduit branch; unsupported stored shape selects first available without applying
     *   on construction. Geometry spins [0,1e9]/four decimals/step0.1, barrels 1..100, can
     *   round/clamp readback before later submit.
     * @note Length dimensions in authored project ft/m; slopes/exponent and FORCE_MAIN roughness
     *   follow shape-specific engine semantics, not universal length. IRREGULAR/STREET numeric
     *   geom1 hidden and name resolves engine index. All value changes directly attempt
     *   xsection/barrels, rejected xsection silently leaves draft/preview unchanged; no undo for
     *   those writes. Shape change fits preview, numeric edits retain zoom/highlight dimension.
     */
    void buildXSectionPage();
    /**
     * @brief Create inlet design/capture/number/clogging/limit/depression/placement controls and
     *   load usage; bound engine capability failure displays explanation instead.
     * @note Each completed identity/value edit immediately calls applyInletUsage, no separate Apply
     *   button. Number 1..5, clogging 0..99 percent/one decimal, flow limit 0..1e9/four decimals
     *   (zero unrestricted), depression height/width 0..1e6/four decimals. Labels snapshot GLOBAL
     *   UnitSystem flow and length units; numbers passed native project values without conversion.
     * @note Standalone with no layer still builds/loads empty/default form but cannot apply usage.
     *   Remove delegates undo-capable model path. Scope/shape compatibility validated by
     *   engine/command later; view owns controls, not engine/designs/nodes.
     */
    void buildInletUsagePage();

    /*! Look up the bound link's index on the engine. Returns -1 if the
     *  link has been removed since the dialog opened.
     * @return Current zero-based engine link index by stored name, -1 for null engine/empty/missing
     *   name. Nonnull raw handle must remain live; no engine identity/lifecycle/type guard here.
 */
    [[nodiscard]] int linkIdx() const;

    /*! Build the human-readable summary string for the current xsection
     *  state (e.g. "CIRCULAR (3.0 ft)"). Used both for the dialog title
     *  hint and for `m_ref.summary` after every apply.
     * @return Readback label from engine shape/geom, no link/unknown diagnostic on lookup failure.
     *   IRREGULAR uses current PICKER name (may differ from saved engine), STREET resolves rounded
     *   geom1 index to current name; other shapes show g1 or g1 times positive g2 with four
     *   significant digits, no units appended.
     * @note Unknown shape metadata falls back CIRCULAR row. Does not include geom3/4/barrels or
     *   update cache itself; no source writes.
 */
    QString computeXsectSummary() const;

    /*! Refresh visibility of geom2..geom4 + transect/curve picker rows
     *  based on the currently-selected shape.
     * @brief Set geometry labels/visibility from current palette metadata, substituting name picker
     *   for IRREGULAR/STREET geom1.
     * @note Unknown/no selection uses circular fallback; hidden geometry values retained and still
     *   submitted by applyXsect, not zeroed. No automatic unit suffix/range change, source write or
     *   picker repopulation.
 */
    void updateXsectFieldVisibility();

    /*! Apply the current shape+geom1..4 to the engine via
     *  `SWMMModelLayer::applyLinkXsect` and refresh the summary.
     * @brief For current resolved link/palette item, submit raw four geom values, resolving
     *   IRREGULAR/STREET picker name to current engine geom1 index first.
     * @note Missing/unresolved name or target is silent no-op. Bound layer applyLinkXsect emits
     *   attributeChanged on success; standalone calls C API directly. Rejection silent and leaves
     *   widgets/draft preview differing from saved state; success updates cached summary/label
     *   only. No undo, model dirty call, readback normalization or suppression check INSIDE helper;
     *   callbacks check suppression.
 */
    void applyXsect();

    /*! Slice SP.3 — rebuild the live section preview from the CURRENT WIDGET
     *  values (not from the engine), so the drawing tracks typing even before
     *  a value is committed. For IRREGULAR / STREET the geometry comes from
     *  the picked transect / street provider, which works while the model is
     *  still being built — unlike the engine's link-derived geometry.
     *
     *  \param highlightOrdinal 1..4 accents that geom's dimension line
     *                          (set from the spin box being edited), 0 none.
     * @note Uses current widget values independent of accepted engine state, so rejected
     *   intermediate geometry can still draw. IRREGULAR uses current registry
     *   station/elevations/banks/roughness/meander (station multiplier/elevation offset not
     *   explicitly passed here), STREET uses registry street geometry; both require matching
     *   provider name and can lazily create registry. Missing provider shows placeholder.
     * @note Other shapes delegate shared preview builder with raw geom fields/dimension highlight.
     *   Units queried from CURRENT global UnitSystem, no explicit model-unit conversion. No source
     *   edits, engine geometry binding, accepted-state check, redraw zoom reset or automatic
     *   external-provider subscription.
 */
    void refreshXsectPreview(int highlightOrdinal = 0);

    /*! Refresh the transect picker's items from the layer's
     *  TransectRegistry and select \p selected if non-empty. No-op when
     *  the picker hasn't been built; absent layer produces an empty list.
     * @param[in] selected Name passed to picker setItems after enumerating provider names; empty is
     *   passed explicitly rather than implicitly preserving current text.
     * @brief Replace transect choices from current bound layer registry under saved/restored
     *   xsection suppression.
     * @note Missing picker no-op; no layer/unavailable registry supplies empty names, not
     *   necessarily no-op. Lazy registry ensure can initialize local provider data. Does not commit
     *   xsection, save registry, sort names or subscribe external changes.
 */
    void refreshTransectPickerItems(const QString &selected = {});

    /*! Open `TransectEditorDialog::pickTransect` modally and, on a
     *  non-empty return, update the picker selection + commit the new
     *  transect index to the engine via `applyXsect`. Triggered by the
     *  picker's "..." button.
     * @brief For available bound registry, run child transect picker with current name, refresh
     *   choices after exit and explicitly apply/repreview nonempty selection.
     * @note Empty result still refreshes requesting original name, without apply/repreview.
     *   Transect picker receives null undo stack. Nested registry edits can survive regardless of
     *   result; raw dialog/layer/picker lifetime must remain valid through modal loop, no enclosing
     *   rollback.
 */
    void onTransectPickerClicked();

    /*! Refresh the street picker's items from the layer's StreetRegistry
     *  and select \p selected if non-empty. Mirrors
     *  refreshTransectPickerItems for the STREET shape.
     * @param[in] selected Name passed to picker setItems after enumerating provider names; empty is
     *   passed explicitly rather than implicitly preserving current text.
     * @brief Replace street choices from current bound layer registry under saved/restored xsection
     *   suppression.
     * @note Missing picker no-op; no layer/unavailable registry supplies empty names, not
     *   necessarily no-op. Lazy registry ensure can initialize local provider data. Does not commit
     *   xsection, save registry, sort names or subscribe external changes.
 */
    void refreshStreetPickerItems(const QString &selected = {});

    /*! Open `StreetEditorDialog::pickStreet` modally and, on a non-empty
     *  return, update the picker selection + commit the new street index
     *  to the engine via `applyXsect`. Triggered by the street picker's
     *  "..." button.
     * @brief For available bound registry, run child street picker with current name, refresh
     *   choices after exit and explicitly apply/repreview nonempty selection.
     * @note Empty result still refreshes requesting original name, without apply/repreview. Street
     *   picker owns its own edit semantics. Nested registry edits can survive regardless of result;
     *   raw dialog/layer/picker lifetime must remain valid through modal loop, no enclosing
     *   rollback.
 */
    void onStreetPickerClicked();

    // ── Inlet usage page (§2.4, closes BN-LINK-11) ───────────────────────
    //
    // One [INLET_USAGE] row per conduit. The page is populated from the
    // engine on construction and commits the whole row through a single
    // SetInletUsageCommand (SWMMModelLayer::pushInletUsageEdit) on edits, so
    // an edit here and the same edit on an inlet junction's property rows are
    // literally the same undoable operation.

    /*! Refresh the widgets from the conduit's current usage row (or the
     *  engine defaults when it has none). Sets m_iuSuppressApply while it
     *  runs so the change signals don't bounce back through applyInletUsage.
     * @brief Read current link usage through bound layer when present, repopulate identity choices
     *   and hydrate bounded controls under suppression.
     * @note Without layer reports no usage even when raw engine might have one. Defaults number 1,
     *   numeric zero, Automatic; native values clamp/round to editor limits without changing engine
     *   immediately. Suppression cleared unconditionally, not restored prior.
     * @note Remove enabled only when read usage exists; updates status and cached summary only for
     *   InletUsage page kind. Does not create/delete rows, dirty model, validate current saved
     *   capture eligibility or subscribe external changes.
 */
    void loadInletUsage();
    /*! Commit the widget values as one SWMM_InletUsage row. No-op while the
     *  design or capture node is unset — the engine rejects a partial row.
     * @brief When unsuppressed with live engine/layer and both resolvable design/capture names,
     *   assemble complete node-indexed LINK-host usage and dispatch model pushInletUsageEdit.
     * @note Empty identity shows incomplete status WITHOUT removing old saved usage; unknown
     *   nonempty identity silently returns. Uses current spin values and placement item data,
     *   engine validates shape/roles. Model pushes undo command when edit stack exists, returning
     *   true for stack acceptance even if command later rejects; without stack returns direct API
     *   status.
     * @note False shows generic rejection and preserves widgets; true enables Remove, reports inlet
     *   and updates cached summary, not an independent engine readback. No modal save gate, no row
     *   transaction across multiple edits, Close keeps applied state.
 */
    void applyInletUsage();
    /*! Drop the conduit's usage row (undoable), then reload the page.
     * @brief For bound layer/current link, request undo-capable usage removal then reload current
     *   authoritative form.
     * @note Ignore removal return, no confirmation or explicit rollback; absent layer/link no-op.
     *   Does not clear source design or remove capture node.
 */
    void removeInletUsage();
    /*! Refill the design combo from [INLETS], preserving the selection.
     * @param[in] selected Requested selected name passed to picker; empty not an explicit
     *   preserve-current lookup.
     * @brief Replace choices from current nonempty engine inlet IDs in engine order; missing
     *   picker/engine no-op.
     * @note No shape compatibility filter, signal block or internal suppression; caller such as
     *   load wraps suppression, direct invocation can trigger apply callbacks. No registry
     *   save/engine mutation itself.
 */
    void refreshInletDesignItems(const QString &selected = {});
    /*! Refill the capture-node combo with every node that is neither this
     *  conduit's own end node nor a virtual / inlet junction (rule 627).
     * @param[in] selected Name matched after list rebuild; absent selects prepended
     *   empty/unassigned entry.
     * @brief Repopulate current node IDs sorted case-insensitively, excluding link endpoints and
     *   nodes reported virtual.
     * @note Endpoint/virtual getter return statuses ignored with -1/false initial defaults, so
     *   failure can leave otherwise ineligible nodes. Inlet nodes excluded through their virtual
     *   role. Combo signals blocked during rebuild, no ownership transfer, source writes or live
     *   node subscription.
 */
    void refreshCaptureNodeItems(const QString &selected = {});
    /*! Open `InletEditorDialog::pickInlet` filtered by this conduit's
     *  cross-section shape, then select the returned design.
     * @brief Ensure bound inlet registry, read current conduit shape (or -1 on failure), open
     *   shape-filtered inlet picker and apply returned nonempty design when capture complete.
     * @note Pass null undo stack, preserve dialog/layer/engine lifetime through modal child. Empty
     *   result leaves combo unchanged; selection refresh suppresses apply then explicitly
     *   dispatches once. No independent compatibility confirmation or rollback of nested editor
     *   changes.
 */
    void onInletDesignPickerClicked();

    LinkCompoundEditRef m_ref;

    // XSection page widgets — §S.SC.1.a (2026-05-25) reworked: the
    // single QComboBox shape picker is replaced by a horizontal
    // QSplitter whose left pane is a QListWidget in IconMode (one item
    // per allowed shape, colored placeholder icon + label below) and
    // whose right pane is the existing per-shape params form. The
    // splitter + list expose the same `currentXsectShapeId()` selection
    // signal the old combo did so the suppress-apply / round-trip
    // contract is unchanged.
    QSplitter *m_xsSplitter = nullptr;
    QListWidget *m_xsShapeList = nullptr;
    QDoubleSpinBox *m_xsGeom1Spin = nullptr;
    QDoubleSpinBox *m_xsGeom2Spin = nullptr;
    QDoubleSpinBox *m_xsGeom3Spin = nullptr;
    QDoubleSpinBox *m_xsGeom4Spin = nullptr;
    QSpinBox *m_xsBarrelsSpin = nullptr;
    QLabel *m_xsSummaryLabel = nullptr;
    QLabel *m_xsGeom1Label = nullptr;
    QLabel *m_xsGeom2Label = nullptr;
    QLabel *m_xsGeom3Label = nullptr;
    QLabel *m_xsGeom4Label = nullptr;

    /*! Slice SP.3 — third splitter pane: engine-accurate live section
     *  drawing with labelled dimensions. */
    openswmmvis::sectionview::SectionPreviewWidget *m_xsPreview = nullptr;

    // §S.SC.1.b (2026-05-25) — When the user picks IRREGULAR the engine
    // expects geom1 to be a transect *index*. Bare numeric input is
    // hostile (the indices aren't stable across rename / reorder) so we
    // hide the geom1 spin and surface a LabeledPickerCombo whose
    // "..." button opens the TransectEditorDialog CRUD modal
    // (`pickTransect`). On commit we translate the picked name → engine
    // transect index → geom1.
    QLabel *m_xsTransectLabel = nullptr;
    LabeledPickerCombo *m_xsTransectPicker = nullptr;

    // STREET shape — same picker pattern as IRREGULAR: geom1 is a street
    // *index*, surfaced as a name dropdown + "..." button that opens the
    // StreetEditorDialog CRUD modal (`pickStreet`).
    QLabel *m_xsStreetLabel = nullptr;
    LabeledPickerCombo *m_xsStreetPicker = nullptr;

    // Re-entrancy guard: when we populate widgets from engine state
    // on dialog open / page activation, the QSignals fire and would
    // otherwise bounce right back through `applyXsect()`. Set true
    // while populating; the slot bails when set.
    bool m_xsSuppressApply = false;

    // Inlet usage page widgets — the legacy 8-row form (Dinletusage.pas).
    LabeledPickerCombo *m_iuDesignPicker = nullptr;
    QComboBox *m_iuCaptureCombo = nullptr;
    QSpinBox *m_iuNumInlets = nullptr;
    QDoubleSpinBox *m_iuPctClogged = nullptr;
    QDoubleSpinBox *m_iuFlowLimit = nullptr;
    QDoubleSpinBox *m_iuDepressHeight = nullptr;
    QDoubleSpinBox *m_iuDepressWidth = nullptr;
    QComboBox *m_iuPlacement = nullptr;
    QPushButton *m_iuRemoveBtn = nullptr;
    QLabel *m_iuStatus = nullptr;
    /*! Same re-entrancy guard the xsection page uses: true while the widgets
     *  are being populated from the engine. */
    bool m_iuSuppressApply = false;

    QStackedWidget *m_stack = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // LINKCOMPOUNDEDITDIALOG_H
