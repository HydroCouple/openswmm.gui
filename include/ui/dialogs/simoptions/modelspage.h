/**
 * @file      modelspage.h
 * @brief     Simulation Options → Models / Processes.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Models / Processes.
 *
 * Tabs: Domains & Processes · Modules · Flags.
 *
 * FLOW_ROUTING was *built* here and *wired* on Routing & Hydraulics; T4 gave
 * the combo to the page that gates on it and left a read-only mirror behind
 * (PLAN §2.1) — one key, one editor, no synchronisation burden.
 *
 * The Domain × Species transport matrix is the engine's own view of what will
 * be carried where. Its 2D column is editable and mirrors the 2D page's
 * TRANSPORT_* boxes (CLAUDE.md §5.1 — one model, two views), reached through
 * the hooks below rather than by touching that page's widgets (PLAN §4.1).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MODELSPAGE_H
#define MODELSPAGE_H

#include <functional>

#include "ui/dialogs/simoptions/simoptionspage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QTableWidget;
class QTableWidgetItem;
class QTabWidget;

namespace openswmmvis::ui
{

class ModelsPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Construct model/process, module and flag draft controls.
     * @param ctx Borrowed GUI-thread context that must outlive this page and supply valid
     * engine/layer handles while accessed, including during retained hook calls.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Builds/tags controls only; read() performs hydration and summary queries.
     */
    explicit ModelsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Models / Processes string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Hydrate process drafts, availability and per-model 2D module intent.
     * Common fallbacks come from simulation preferences; unknown infiltration tokens
     * retain the prior selection. Active-process checks invert IGNORE_* booleans.
     * Object counts gate rainfall/groundwater/RDII/quality/routing. A layer-bound 2D flag
     * uses stored QSettings intent or a case-insensitive input-file section scan, with
     * IGNORE_2D YES overriding either; its checkbox is signal-blocked while seeded.
     * Other changed controls may refresh the matrix during hydration. Finally refreshes
     * matrix/snow and emits gateInputsChanged. Does not write options or QSettings.
     */
    void read() override;
    /**
     * @brief Apply infiltration, ponding and inverted active-process drafts independently.
     * @return Successful common-option setter count. Refusal/no-op contributes zero;
     * partial success is possible without rollback. IGNORE_2D is considered only after
     * known stored/user intent; then the layer's QSettings preference is written even
     * if the engine setter fails or is unchanged. Does not save project/input files
     * or report QSettings persistence errors. Disabled controls still contribute drafts.
     */
    int write() override;

    /*! \brief Tab order, so the dialog can name tabs without magic numbers. */
    enum Tab { TabDomains = 0, TabModules, TabFlags };

    /**
     * @brief Expose the owned tab container to the host.
     * @return Borrowed container for navigation/gating; do not delete or retain past page life.
     */
    [[nodiscard]] QTabWidget *tabs() const { return m_tabs; }

    // ---- 2D module toggle — the dialog's 2D row gate reads this ----------
    /**
     * @brief Read the current 2D module draft switch.
     * @return True only for an existing checked box, regardless of compiled solver support
     * or known intent. Does not prove that a mesh exists or a solver is active.
     */
    [[nodiscard]] bool module2DEnabled() const;
    /**
     * @brief Change the 2D module draft through its normal toggle connection.
     * @param on Desired checked state. A changed value marks explicit module intent and
     * emits gateInputsChanged; an equal value or absent control is a no-op and does not
     * promote inferred state to known intent. Does not immediately persist the option.
     */
    void setModule2DEnabled(bool on);

    /**
     * @brief Update the read-only routing mirror and its navigation link.
     * @param routing Display text inserted directly into rich-text markup; caller supplies
     * trusted display text. Empty text uses the localized unset label.
     * Missing label is a no-op. Does not change FLOW_ROUTING or query its owning page.
     */
    void setFlowRoutingText(const QString &routing);

    /**
     * @brief Replace retained callbacks connecting matrix checks to the 2D draft page.
     * @param getter Moved optional callable receiving a transport class enum/index and
     * returning its draft enabled state; empty leaves matrix cells without checkboxes.
     * @param setter Moved optional callable receiving class enum/index and checked bool;
     * empty makes item-change callbacks ignore requested changes.
     * Captures must remain valid until replacement/page destruction. Calls run
     * synchronously on the GUI thread; exceptions propagate. Assignment does not query,
     * refresh, apply or validate either hook, and getter alone can still show checks.
     */
    void setTransport2DHooks(std::function<bool(int)> getter,
                             std::function<void(int, bool)> setter);

public slots:
    /**
     * @brief Rebuild the domain-by-class preview and optional surface-2D checks.
     * Missing table is a no-op. With a draft hook, calls engine preview after overriding
     * ignore_quality/ignore_2d from local boxes; without one, queries the applied engine
     * matrix, so local ignore drafts are not used by that query. Failed/no engine shows
     * dashes. Existing cells are reused and new cells owned by the table. A getter makes
     * all successfully queried surface cells checkable regardless of cell availability;
     * labels distinguish saved-on inactive state. Suppresses local item-change handling
     * during synchronization. Does not write engine state or verify runtime activity;
     * caller-provided hooks must remain valid and must not throw during synchronization.
     */
    void refreshTransportMatrix();
    /**
     * @brief Query snow model/step for the navigation summary and snowmelt availability.
     * Missing engine or failed getter shows Unavailable without resetting the previous
     * snowmelt gate. Success enables that switch if any snowpack/2D-default/cell count is
     * positive and displays UEB for model one, degree day otherwise, with seconds.
     * Assumes owned labels/checkboxes exist; does not edit snow configuration.
     */
    void refreshSnowSummary();
    /**
     * @brief Replace the retained provider for pending transport options.
     * @param hook Moved optional zero-argument callable returning an owned SWMM_TransportDraft
     * snapshot. Empty selects the applied-engine query on subsequent matrix refreshes.
     * Caller keeps captures alive; synchronous callback exceptions propagate. Assignment
     * does not invoke the hook or refresh/write the matrix.
     */
    void setDraftHook(std::function<SWMM_TransportDraft()> hook) { m_draftHook = std::move(hook); }

signals:
    /**
     * @brief Request host navigation to the routing editor after the mirror link is clicked.
     * Carries no engine value and performs no option write itself.
     */
    void showFlowRoutingPageRequested();
    /**
     * @brief Request host navigation to snow settings after the summary link is clicked.
     * Carries no configuration and does not itself open a dialog or write options.
     */
    void showSnowSettingsRequested();

private:
    /**
     * @brief Create owned process/module controls, transport table and navigation connections
     * once.
     * Routing-link callback ignores its borrowed link string and requests navigation;
     * snow link does likewise. A borrowed changed matrix item updates only the surface
     * class through the retained setter when not synchronizing, then refreshes. Quality
     * toggle ignores its bool and refreshes preview; 2D toggle ignores its bool, records
     * explicit intent and emits gateInputsChanged. These connections do not persist
     * common options; the 1D module is permanently checked/disabled.
     */
    void buildUi();
    /**
     * @brief Tag infiltration, ponding, six ignore-process and 2D intent widgets with common
     * keys.
     * Supports reachability inspection without reading or writing their values.
     */
    void tagWidgets();

    QTabWidget *m_tabs = nullptr;

    QComboBox *m_infiltrationCombo = nullptr;
    QLabel *m_routingMirror = nullptr;
    QLabel *m_snowSummary = nullptr;
    QCheckBox *m_allowPondingBox = nullptr;
    QCheckBox *m_ignoreRainfallBox = nullptr;
    QCheckBox *m_ignoreSnowmeltBox = nullptr;
    QCheckBox *m_ignoreGroundwaterBox = nullptr;
    QCheckBox *m_ignoreRDIIBox = nullptr;
    QCheckBox *m_ignoreQualityBox = nullptr;
    QCheckBox *m_ignoreRoutingBox = nullptr;

    QCheckBox *m_module1DBox = nullptr; ///< Always-on, disabled (1D core).
    QCheckBox *m_module2DBox = nullptr; ///< Toggle 2D surface routing.

    /*! \brief True when the 2D-module checkbox reflects a real IGNORE_2D
     *         intent rather than one inferred from the .inp's contents.
     *
     *  Set when a per-project QSettings preference exists, or as soon as the
     *  user toggles the box. While it is false the checkbox is only a
     *  *description* of the model ("this .inp has no 2D sections"), and
     *  writing IGNORE_2D from it would invent a preference the user never
     *  expressed — which is what made an unedited Apply dirty every 1D
     *  project. */
    bool m_module2DIntentKnown = false;

    QTableWidget *m_transportMatrixTable = nullptr;
    std::function<SWMM_TransportDraft()> m_draftHook;
    bool m_matrixSyncing = false;
    std::function<bool(int)> m_transport2DGet;
    std::function<void(int, bool)> m_transport2DSet;
};

} // namespace openswmmvis::ui

#endif // MODELSPAGE_H
