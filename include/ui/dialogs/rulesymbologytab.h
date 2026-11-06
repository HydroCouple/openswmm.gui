/**
 * @file      rulesymbologytab.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Rule-driven Symbology tab content (Slice Z.3).
 *
 *         Replaces the per-kind tree-on-left layout from §X.3.2 with an
 *         Active Rule combo + Rule List, applied uniformly across every
 *         layer kind. See RENDERING_RULE_MODEL_PLAN.md §4 for the layout
 *         spec.
 *
 *         The widget binds to a non-null OpenSWMM::Render::RuleList. Both
 *         the combo and the list view sync to the RuleList's
 *         activeIndex(); editing visibility checkboxes writes through to
 *         Rule::setVisible; drag-reordering writes through to
 *         RuleList::move; the [+] / [Duplicate] / [Delete] / [↑] / [↓]
 *         buttons mutate the RuleList directly.
 *
 *         The body region below the list is a QStackedWidget into which
 *         a renderer-class picker + IRendererPanel is mounted for the
 *         active Rule. Slice Z.3 ships the panel-host wiring; the
 *         per-Rule renderer-edit binding into the existing
 *         FeatureStyleEditor / SwmmElementSymbolEditor /
 *         GisVectorSymbolEditor / RasterColorRampEditor surface is
 *         delivered as part of the Rule-aware RendererPanelContext
 *         extension in a follow-up slice (Z.3a — flagged in the
 *         RuleSymbologyTab body placeholder until then).
 *
 *         Cancel rollback: the widget does not snapshot — that is the
 *         enclosing LayerStyleDialog's job (§W U-3 captures snapshots of
 *         every ILayerStyleSubject on open and restores on Cancel; the
 *         Rule's Q_PROPERTYs ride that path via the §W default
 *         JsonPropertySnapshot).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_RULESYMBOLOGYTAB_H
#define OPENSWMMVIS_UI_DIALOGS_RULESYMBOLOGYTAB_H

#include <QPointer>
#include <QWidget>

class QComboBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;

namespace OpenSWMM::Render
{
class Rule;
class RuleList;
}

namespace openswmmvis::ui
{

class RuleSymbologyTab : public QWidget
{
    Q_OBJECT
public:
    /*! Argument `ruleList`:  Non-null. Outlives the widget.
     *  Argument `parent`:    Standard QWidget parent. */
    /**
     * @brief Bind a GUI-thread live rule-list editor and build its active renderer panel.
     * @param[in,out] ruleList Required borrowed list, Q_ASSERT-checked, which must outlive this
     * widget and its panel callbacks. Rule-list mutations/active changes are written immediately;
     * no local undo or cancel snapshot.
     * @param[in] parent Optional Qt widget owner.
     * Subscribes to model changes and active-rule renderer replacement, while suppressing UI
     * hydration feedback.
     */
    explicit RuleSymbologyTab(OpenSWMM::Render::RuleList *ruleList, QWidget *parent = nullptr);
    /**
     * @brief Destroy child panels/widgets and QObject connections without taking ownership of the
     * borrowed rule list or restoring changes.
     */
    ~RuleSymbologyTab() override;

    /*! \brief Currently selected Rule (matches RuleList::activeRule()).
     *         Null when the list is empty. */
    /**
     * @brief Read the active rule from the bound model.
     * @return Borrowed list-owned rule, or nullptr for a missing/empty list; invalid after rule
     * removal or list destruction.
     */
    [[nodiscard]] OpenSWMM::Render::Rule *activeRule() const;

signals:
    /*! \brief Emitted when the active-rule UI selection is synchronized, including
     *         model rebuilds and programmatic changes. */
    /**
     * @brief Notify completion of a UI selection synchronization.
     * @param[in] index Requested active index forwarded by setListAndComboToIndex, including an
     * invalid value that clears UI selection.
     * Emitted on programmatic/model rebuilds as well as user-driven changes; receivers must not
     * assume a user edit or a valid selected rule.
     */
    void activeRuleChanged(int index);

private slots:
    /**
     * @brief Apply an active-rule combo index to the model unless UI synchronization is suppressed.
     * @param[in] idx Combo index passed directly to RuleList::setActiveIndex; model owns
     * validation.
     */
    void onComboIndexChanged(int idx);
    /**
     * @brief Apply the selected visual row as model active index unless synchronizing.
     * @param[in] row Selected row, potentially -1; forwarded to the list validation.
     */
    void onListRowChanged(int row);
    /**
     * @brief Apply a rule-row check state to its rule visibility only when different.
     * @param[in] item Nullable borrowed item expected to belong to this list;
     * null/suppressed/missing-rule rows are ignored.
     */
    void onItemChanged(QListWidgetItem *item);
    /**
     * @brief Replay the visual row pointer permutation as model moves and rebuild the UI.
     * Suppression guards model-driven rebuild callbacks during moves; ignores null list/suppressed
     * input and does not create undo commands.
     */
    void onListReordered();

    /**
     * @brief Append a uniquely owned New rule (N) to the list and select its last index; no-op
     * without a list.
     */
    void onAddClicked();
    /**
     * @brief Clone the active rule, append (copy) to its name, insert after it and select the
     * clone.
     * No-op without a list/active rule; ownership transfers to the list.
     */
    void onDuplicateClicked();
    /**
     * @brief Ask the list to remove its active rule, destroying list-owned rule state through
     * normal model notifications.
     */
    void onDeleteClicked();
    /**
     * @brief Move the active rule one position earlier when above index zero.
     */
    void onMoveUpClicked();
    /**
     * @brief Move the active rule one position later when it is valid and not last.
     */
    void onMoveDownClicked();

    /**
     * @brief Rebuild names/check states/selection and the active renderer body after structural
     * model changes.
     */
    void onModelRuleListChanged();
    /**
     * @brief Synchronize active selection, replace the renderer body and update button bounds.
     * @param[in] idx Model-provided active index, forwarded to selection synchronization.
     */
    void onModelActiveIndexChanged(int idx);
    /**
     * @brief Refresh the changed rule name/visibility under blocked UI signals without
     * reconstructing the renderer panel.
     * @param[in] index Zero-based rule row; invalid or null rule rows are ignored.
     */
    void onModelRuleChanged(int index);

    /**
     * @brief Replace the active renderer with a newly owned built-in factory result when its class
     * differs.
     * @param[in] comboIndex Built-in roster index, restricted to [0,kRendererChoicesCount);
     * invalid/suppressed/no-active-rule requests are ignored.
     * Replacement discards old renderer state; rendererReplaced remounts the editor.
     */
    void onRendererClassPicked(int comboIndex);
    /**
     * @brief Synchronize the built-in class picker and remount the body after external or user
     * renderer replacement.
     */
    void onActiveRuleRendererReplaced();

private:
    /**
     * @brief Rebuild rule names, visibility checks and stored borrowed rule identities under signal
     * suppression.
     * Synchronizes active selection (emitting activeRuleChanged), remounts the body and updates
     * buttons.
     */
    void rebuildFromModel();
    /**
     * @brief Remove/defer-delete old body widgets and reconnect the active renderer notification.
     * Mounts the registered renderer panel with rule context and, only for a correctly sized
     * model/results kind list, host/category context. Null active rule or absent/null-returning
     * factory produces a descriptive placeholder. New panel ownership belongs to the body stack.
     */
    void mountBodyForActive();
    /**
     * @brief Gate duplicate/delete/move buttons from list count and active-index bounds without
     * mutating the model.
     */
    void updateButtonsEnabled();
    /**
     * @brief Synchronize selection while blocking both widgets, then emit activeRuleChanged
     * unconditionally.
     * @param[in] index Requested index; each widget clears selection when outside its own bounds.
     * Does not write the model active index.
     */
    void setListAndComboToIndex(int index);
    /**
     * @brief Select the active renderer built-in roster entry while blocking picker signals.
     * No renderer/unknown renderer ID selects -1 without changing renderer state.
     */
    void syncRendererClassCombo();
    /**
     * @brief Disconnect rendererReplaced from the previously subscribed surviving rule and clear
     * its guarded identity.
     */
    void disconnectActiveRuleRendererSignal();
    /**
     * @brief Connect the active rule rendererReplaced signal uniquely and remember it through
     * QPointer.
     * No active rule is a no-op; caller disconnects any prior subscription first.
     */
    void connectActiveRuleRendererSignal();

    OpenSWMM::Render::RuleList *m_ruleList = nullptr;

    QComboBox *m_activeCombo = nullptr;
    QListWidget *m_ruleListView = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnDuplicate = nullptr;
    QPushButton *m_btnDelete = nullptr;
    QPushButton *m_btnUp = nullptr;
    QPushButton *m_btnDown = nullptr;
    QStackedWidget *m_body = nullptr;
    QComboBox *m_rendererCombo =
        nullptr; /*!< Renderer-class picker for the active Rule (Slice Z.3a). */

    /*! Tracks the Rule whose rendererReplaced signal we're listening to,
     *  so we disconnect cleanly when the active Rule changes. QPointer
     *  so that a Rule removed from RuleList (and destroyed) auto-clears
     *  itself here, preventing a use-after-free in the next
     *  disconnect/mount cycle. */
    QPointer<OpenSWMM::Render::Rule> m_subscribedRule;

    /*! Guards re-entrancy when programmatic sync writes into the combo /
     *  list widgets — itemChanged / currentIndexChanged signals would
     *  otherwise re-trigger model mutations. */
    bool m_suppressUiSignals = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_RULESYMBOLOGYTAB_H
