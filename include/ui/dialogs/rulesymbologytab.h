/*!
 * \file   rulesymbologytab.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Rule-driven Symbology tab content (Slice Z.3).
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

namespace OpenSWMM::Render {
class Rule;
class RuleList;
}

namespace openswmmvis::ui {

class RuleSymbologyTab : public QWidget
{
    Q_OBJECT
public:
    /*! \param ruleList  Non-null. Outlives the widget.
     *  \param parent    Standard QWidget parent. */
    explicit RuleSymbologyTab(OpenSWMM::Render::RuleList *ruleList,
                              QWidget *parent = nullptr);
    ~RuleSymbologyTab() override;

    /*! \brief Currently selected Rule (matches RuleList::activeRule()).
     *         Null when the list is empty. */
    [[nodiscard]] OpenSWMM::Render::Rule *activeRule() const;

signals:
    /*! \brief Emitted when the user changes the active Rule from the
     *         combo or the list. */
    void activeRuleChanged(int index);

private slots:
    void onComboIndexChanged(int);
    void onListRowChanged(int);
    void onItemChanged(QListWidgetItem *);
    void onListReordered();

    void onAddClicked();
    void onDuplicateClicked();
    void onDeleteClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();

    void onModelRuleListChanged();
    void onModelActiveIndexChanged(int);
    void onModelRuleChanged(int index);

    void onRendererClassPicked(int comboIndex);
    void onActiveRuleRendererReplaced();

private:
    void rebuildFromModel();
    void mountBodyForActive();
    void updateButtonsEnabled();
    void setListAndComboToIndex(int index);
    void syncRendererClassCombo();
    void disconnectActiveRuleRendererSignal();
    void connectActiveRuleRendererSignal();

    OpenSWMM::Render::RuleList *m_ruleList = nullptr;

    QComboBox      *m_activeCombo  = nullptr;
    QListWidget    *m_ruleListView = nullptr;
    QPushButton    *m_btnAdd       = nullptr;
    QPushButton    *m_btnDuplicate = nullptr;
    QPushButton    *m_btnDelete    = nullptr;
    QPushButton    *m_btnUp        = nullptr;
    QPushButton    *m_btnDown      = nullptr;
    QStackedWidget *m_body         = nullptr;
    QComboBox      *m_rendererCombo = nullptr;   /*!< Renderer-class picker for the active Rule (Slice Z.3a). */

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
