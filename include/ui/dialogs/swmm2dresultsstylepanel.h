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
class QMenu;
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
    /*! Shared builder for the depth row's cell and smooth fill passes —
     *  both sublayers carry the same ScalarFillStyle. */
    /**
     * @brief Create a cell/smooth depth-fill pass page: the pass opacity and its classification.
     * @param[in,out] sub Nullable borrowed sublayer for the opacity header.
     * @param[in,out] st Nullable borrowed fill style; absent style omits attribute/classification
     * controls. Raw style/layer captures must outlive active page callbacks.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page. Only depth can be chosen; preserves an unsupported current
     * attribute as a disabled item. Classification samples are empty and range comes from
     * [0,maxDepth], with continuous and range modes enabled; the editor follows outside edits.
     */
    [[nodiscard]] QWidget *buildScalarFillTab(OpenSWMM::Render::ISublayer *sub,
                                              OpenSWMM::Render::ScalarFillStyle *st,
                                              QWidget *parent);
    /**
     * @brief Create the depth contour-band pass content for the surviving non-null layer.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page; missing sublayer/style leaves just the opacity. Uses empty
     * preview samples and [0,maxDepth], classified-only mode without range modes, and a live
     * smooth-boundary switch. Raw style/layer captures require page-lifetime validity.
     */
    [[nodiscard]] QWidget *buildContourBandTab(QWidget *parent);
    /**
     * @brief Create live depth-isoline pass levels, stroke and label controls for a non-null layer.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page, opacity-only if style absent. Count [1,256]; interval
     * [1e-6,1e6] m and base +/-1e6 m (three displayed decimals); line/index widths [.25,20] px,
     * index frequency [0,50], label decimals [0,9], font [4,72] pt. Mode gates count/method versus
     * interval/base; label settings follow "label contours". Controls follow the style.
     */
    [[nodiscard]] QWidget *buildIsolineTab(QWidget *parent);
    /**
     * @brief Create live streak presets, classification, advanced numeric controls and availability
     * diagnostics.
     * @param[in] parent Optional Qt owner.
     * @param[in] withShow True for the listed Flow streaks item (its own Show box); false on the
     * velocity row's Streaks tab, whose Show box is the row's streak toggle.
     * @return New parent-owned page named "flowStreakTab". Requires a non-null layer and existing
     * flow-streak sublayer/style. Density presets are 56/40/28/14 px; trail presets .75/1.5/3 s;
     * motion slider .25..4 times reference, exact motion maps 48 px/s to multiplier one. Advanced
     * property bounds use FlowStreakStyle::maximumNumericValue. Style signals refresh controls;
     * the speed classification is hidden, disabled and refreshed (draft dropped) while coloring
     * by speed is off.
     */
    [[nodiscard]] QWidget *buildFlowStreakTab(QWidget *parent, bool withShow);
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
     * @brief Bring the catalog menu, the tree and the page up to date with the layer.
     * No-op without layer/tree. Rebuilds the Add menu only when the catalog keys changed and the
     * tree only when the listed rows or their groups changed (under the suppress flag, keeping the
     * selection, else the pending neighbor, else the first row, else Water visibility). Rebuilds
     * the page only when its signature changed; otherwise refreshes its data.
     */
    void refreshAdditionalResults();
    /** @brief Recreate the tree model from the layer's listed rows and the Layer group. */
    void rebuildTree();
    /** @brief Recreate the Add field menu: the scalar catalog, then the vector catalog. */
    void rebuildCatalogMenu();
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
    QMenu *m_resultCatalog = nullptr;
    QPushButton *m_addResult = nullptr;
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
