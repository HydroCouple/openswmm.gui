/**
 * @file      swmm2dresultsstylepanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     O2-1 / VS.8 — styling panel for a SWMM2DResultsLayer.
 *
 *         A field tree (the layer's result rows, grouped Surface water / Groundwater × Scalar /
 *         Vector as in the layer tree, then a Layer group: water visibility, mesh edges and
 *         vertices) beside the selected item's page. A row's page is a header (variable,
 *         visibility, opacity) above one tab per display its kind supports. Every control writes
 *         live into the owning sublayer / its style bag — the single source of truth the paint
 *         passes consult — and follows it, so edits elsewhere show here and survive sublayer-JSON
 *         round-trips.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H

#include <QWidget>
#include <QPointer>
#include <functional>

class SWMM2DResultsLayer;
class QLineEdit;
class QPushButton;
class QStandardItemModel;
class QTabWidget;
class QTreeView;
class QVBoxLayout;

namespace OpenSWMM::Render
{
class ISublayer;
class ResultScalarSublayer;
class ResultVectorSublayer;
class ScalarFillStyle;
}

namespace openswmmvis::ui
{

class Swmm2DResultsStylePanel : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build the GUI-thread field tree, Add/Remove controls and the selected item's page.
     * @param[in,out] layer Nullable borrowed results layer, guarded by QPointer; no ownership
     * transfer. Null builds an empty panel. On layer destruction disables the panel, drops the
     * tree's handlers and per-row connections, and detaches the page with its signals blocked and
     * its deletion deferred, silencing callbacks that retain borrowed styles.
     * @param[in] parent Optional Qt widget owner.
     * Controls hydrate before write connections. Thin-film depth uses meters [0,1000], with -1 sent
     * for model dry depth. Edits follow normal layer/style notifications without a local undo
     * snapshot; changing display policy does not change simulation data.
     */
    explicit Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Select an item and, for a hidden pass, the tab that edits it.
     * @param[in] id A result row, mesh edge/vertex or listed flow-streak id; or a depth pass id
     * (`results2d.cellDepthFill`, `smoothDepthFill`, `bands`, `isolines`), shown on the depth row's
     * Fill / Filled contours / Contour lines tab, or `results2d.flowStreaks` / `results2d.velocity`,
     * shown on the velocity row's Streaks / Arrows tab. Without the presenting row a pass id is
     * looked up as an item.
     * @return True when found and selected/focused; false otherwise, leaving selection unchanged.
     */
    bool focusResult(const QString &id);

private:
    /**
     * @brief Create the flow-streak controls every vector shares: color, density, motion and
     * trail sliders, the speed classification, advanced numeric controls and availability
     * diagnostics.
     * @param[in] parent Optional Qt owner.
     * @param[in] row Vector row whose own streak settings are edited (its tab's Show box is the
     * row's streak toggle); null for the listed native Flow streaks item, with its own Show box.
     * @return New parent-owned page named "flowStreakTab". Density slider 80..8 px spacing; trail
     * slider .25..5 s; motion slider .25..4 times reference, exact motion maps 48 px/s to
     * multiplier one. Advanced
     * property bounds use FlowStreakStyle::maximumNumericValue. Style signals refresh controls;
     * the speed classification is hidden, disabled and refreshed (draft dropped) while coloring
     * by speed is off.
     */
    [[nodiscard]] QWidget *buildFlowStreakTab(QWidget *parent,
                                              OpenSWMM::Render::ResultVectorSublayer *row);
    /**
     * @brief Create live uniform edge visibility, opacity, color, width and dash controls.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page, header-only without a style; width [.1,10] px. Omits
     * unsupported slope-emphasis controls; controls follow the sublayer and style.
     */
    [[nodiscard]] QWidget *buildMeshEdgeTab(QWidget *parent); // Issue 6
    /**
     * @brief Create live vertex visibility, opacity, marker color, size and outline controls.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page, header-only without a style; marker [.5,20] px, outline [0,5]
     * px. Omits unsupported shape/tag controls; controls follow the sublayer and style.
     */
    [[nodiscard]] QWidget *buildMeshNodeTab(QWidget *parent); // Issue 6
    /**
     * @brief Create the layer-wide thin-film (water visibility) controls.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page; follows waterDisplayPolicyChanged with the page as context.
     */
    [[nodiscard]] QWidget *buildWaterPage(QWidget *parent);

    /**
     * @brief Bring the field list and the page up to date with the layer.
     * No-op without layer/tree. Rebuilds the list only when the catalog or the listed rows, their
     * groups or keys changed (under the suppress flag, keeping the selection, else the pending
     * neighbor, else the first drawn row, else Water visibility). Rebuilds the page only when its
     * signature changed; otherwise refreshes its data.
     */
    void refreshAdditionalResults();
    /** @brief Recreate the field list: per domain, vector, scalar and saved run-statistic
     *   headings with the layer's rows (checked when shown) and, unchecked, every catalog field
     *   without a row; then the Layer group. Reapplies the filter. */
    void rebuildTree();
    /**
     * @brief Add the row of a listed field (a checked offer) and select it. Other rows are
     *   untouched: fields are drawn together.
     * @param[in] key Catalog scalar key, or a "vector:" key.
     */
    void addField(const QString &key);
    /** @brief Hide list items whose label or key does not contain the filter text, and field
     *   headings left without a visible item. */
    void applyFieldFilter();
    /**
     * @brief Rebuild the page when what it is built from changed: the selected item, its variable
     * key, whether it presents the depth/velocity passes, its availability, or the catalog.
     * @return True when the page was rebuilt.
     */
    bool rebuildPageIfStale();
    /**
     * @brief Postpone page swaps and data refreshes while a modal window other than the dialog
     * (a color or ramp picker opened from the page) runs its own event loop.
     * @return True when a modal window is open; a retry of refreshAdditionalResults() is then
     * scheduled.
     */
    bool deferWhileModal();
    /** @brief Run the page's data refresh closure, unless deferWhileModal() postpones it. */
    void refreshPageData();
    /** @return The signature rebuildPageIfStale() compares. */
    [[nodiscard]] QString pageSignature() const;
    /** @return The selected tree item's id, or empty. */
    [[nodiscard]] QString currentItemId() const;
    /**
     * @brief Select the tree item with this id.
     * @return False when absent, leaving selection unchanged.
     */
    bool selectItem(const QString &id);
    /** @brief Show the current page's tab named @p page, when it has one. */
    void selectTab(const QString &page);
    /**
     * @brief Replace the page with the current item's and its data refresh closure.
     * Immediately hides/disables/detaches the old page before deferred deletion. Restores the last
     * shown tab by page name.
     */
    void rebuildDetails();
    /**
     * @brief Detach the page for deferred deletion.
     * @param[in] silence Also block every child's signals (layer teardown).
     */
    void detachPage(bool silence);
    /**
     * @brief The header of a row page: variable, Show (row visibility), opacity and, for surface
     * vectors, the dry-depth cutoff.
     * @param[in] row Selected scalar or vector row.
     * @param[in] parent Owner widget.
     * @return New parent-owned header.
     */
    [[nodiscard]] QWidget *buildRowHeader(OpenSWMM::Render::ISublayer *row, QWidget *parent);
    /**
     * @brief Fill a scalar row's tabs: Fill, Filled contours, Contour lines, Labels and Data. The
     * depth row's display tabs edit the hidden depth passes.
     * @param[in] row Selected scalar row.
     * @param[in,out] tabs Page tab widget.
     */
    void buildScalarTabs(OpenSWMM::Render::ResultScalarSublayer *row, QTabWidget *tabs);
    /**
     * @brief Add a scalar row's Data tab (availability, selected-cell inspection, CSV/GIS export)
     * and set the page's refresh closure.
     * @param[in] row Selected scalar row.
     * @param[in,out] tabs Page tab widget.
     * @param[in] refreshEditors Called on every refresh: refreshes and enables the row's
     * classification editors.
     */
    void buildScalarDataTab(OpenSWMM::Render::ResultScalarSublayer *row, QTabWidget *tabs,
                            std::function<void()> refreshEditors);
    /**
     * @brief Fill a vector row's tabs: Arrows, Streaks (the animated streak editor for the velocity
     * row), Color and Data, and set the page's refresh closure.
     * @param[in] row Selected vector row.
     * @param[in,out] tabs Page tab widget.
     */
    void buildVectorTabs(OpenSWMM::Render::ResultVectorSublayer *row, QTabWidget *tabs);

    std::function<void()> m_refreshResultData;
    QTreeView *m_resultTree = nullptr;
    QStandardItemModel *m_resultModel = nullptr;
    QLineEdit *m_fieldFilter = nullptr;
    QPushButton *m_removeResult = nullptr;
    QWidget *m_resultDetailHost = nullptr;
    QObject *m_rowBindings = nullptr; ///< context of the tree's per-row connections
    QString m_treeSignature;
    QString m_catalogSignature;
    QString m_pageSignature;
    QString m_lastTab;   ///< page name of the last shown tab
    QString m_pendingId; ///< item to select when the selected one disappears
    bool m_suppressEdits = false;
    bool m_retryPending = false; ///< a deferWhileModal() retry is scheduled
    QPointer<SWMM2DResultsLayer> m_layer;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
