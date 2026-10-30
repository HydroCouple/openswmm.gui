/**
 * @file      swmm2dresultsstylepanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     O2-1 / VS.8 — styling panel for a SWMM2DResultsLayer.
 *
 *         One tab per sub-layer visualization type (Contour bands,
 *         Isolines, Velocity vectors), mirroring the
 *         QGIS mesh-layer styling idiom. Every control writes live into
 *         the owning sublayer / its style bag — the single source of
 *         truth the paint passes consult — so edits repaint immediately
 *         and survive sublayer-JSON round-trips.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H

#include <QWidget>
#include <QPointer>
#include <functional>

class SWMM2DResultsLayer;
class QListWidget;
class QComboBox;

namespace OpenSWMM::Render
{
class ISublayer;
class ScalarFillStyle;
}

namespace openswmmvis::ui
{

class Swmm2DResultsStylePanel : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build GUI-thread live water-display, built-in style and additional-result editors.
     * @param[in,out] layer Nullable borrowed results layer, guarded by QPointer; no ownership
     * transfer. Null builds an empty tab host. On layer destruction clears refresh state, disables
     * the panel, blocks/removes pages and defers their deletion to silence callbacks retaining
     * borrowed styles.
     * @param[in] parent Optional Qt widget owner.
     * Controls hydrate before write connections. Thin-film depth uses metres [0,1000], with -1 sent
     * for model dry depth. Edits follow normal layer/style notifications without a local undo
     * snapshot; changing display policy does not change simulation data.
     */
    explicit Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Focus a configured additional-result row and reveal its containing tab.
     * @param[in] id Exact configured sublayer ID stored in item data.
     * @return True when found and selected/focused; false with no list/matching row, leaving
     * selection unchanged.
     */
    bool focusResult(const QString &id);

private:
    /*! Shared builder for the Cell Depth Fill / Smooth Depth Fill tabs —
     *  both sublayers carry the same ScalarFillStyle. */
    /**
     * @brief Create a cell/smooth depth-fill page with live visibility/opacity and classification
     * controls.
     * @param[in,out] sub Nullable borrowed sublayer for the common header.
     * @param[in,out] st Nullable borrowed fill style; absent style omits attribute/classification
     * controls. Raw style/layer captures must outlive active page callbacks.
     * @param[in] showLabel Display text for the visibility checkbox.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page. Only depth can be chosen; preserves an unsupported current
     * attribute as a disabled item. Classification samples are empty and range comes from
     * [0,maxDepth], with continuous and range modes enabled.
     */
    [[nodiscard]] QWidget *buildScalarFillTab(OpenSWMM::Render::ISublayer *sub,
                                              OpenSWMM::Render::ScalarFillStyle *st,
                                              const QString &showLabel, QWidget *parent);
    /**
     * @brief Create the depth contour-band page for the surviving non-null layer.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page; missing sublayer/style leaves just the header. Uses empty
     * preview samples and [0,maxDepth], classified-only mode without range modes, and a live
     * smooth-boundary switch. Raw style/layer captures require page-lifetime validity.
     */
    [[nodiscard]] QWidget *buildContourBandTab(QWidget *parent);
    /**
     * @brief Create live depth-isoline levels, stroke and label controls for a non-null layer.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page, header-only if style absent. Count [1,64]; interval [1e-6,1e6]
     * m and base +/-1e6 m (three displayed decimals); line/index widths [.25,20] px, index
     * frequency [0,50], label decimals [0,9], font [4,72] pt. Mode gates count/method versus
     * interval/base. Raw style captures require page-lifetime validity.
     */
    [[nodiscard]] QWidget *buildIsolineTab(QWidget *parent);
    /**
     * @brief Create live velocity glyph size, color classification and placement controls for a
     * non-null layer.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page, header-only if style absent. Scale [.1,500] px per m/s, min
     * length [0,200] px, max [1,500] px, head [0,50] px, shaft [.1,10] px, spacing [1,500] px and
     * dry cutoff [0,1000] m. No cross-validation of min/max controls; style owns enforcement.
     * Classification uses empty samples and a positive-width speed range in m/s. Raw style captures
     * require page-lifetime validity.
     */
    [[nodiscard]] QWidget *buildVelocityTab(QWidget *parent);
    /**
     * @brief Create live streak presets, classification, advanced numeric controls and availability
     * diagnostics.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page. Requires a non-null layer and existing flow-streak
     * sublayer/style. Density presets are 56/40/28/14 px; trail presets .75/1.5/3 s; motion slider
     * .25..4 times reference, exact motion maps 48 px/s to multiplier one. Advanced property bounds
     * use FlowStreakStyle::maximumNumericValue. Style signals refresh controls, classification
     * queues scheme changes, and layer notifications refresh diagnostics; raw captures require
     * page-lifetime validity.
     */
    [[nodiscard]] QWidget *buildFlowStreakTab(QWidget *parent);
    /**
     * @brief Create live uniform edge color, width and dash controls for a non-null layer.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page, header-only without a style; width [.1,10] px. Omits
     * unsupported slope-emphasis controls; raw style captures require page-lifetime validity.
     */
    [[nodiscard]] QWidget *buildMeshEdgeTab(QWidget *parent); // Issue 6
    /**
     * @brief Create live fixed-marker color, size and outline controls for a non-null layer.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page, header-only without a style; marker [.5,20] px, outline [0,5]
     * px. Omits unsupported shape/tag controls; raw style captures require page-lifetime validity.
     */
    [[nodiscard]] QWidget *buildMeshNodeTab(QWidget *parent); // Issue 6

    /**
     * @brief Create additional-result list/catalog, add/remove actions and the selected detail
     * host.
     * @param[in] parent Optional Qt owner.
     * @return New parent-owned page. Requires a surviving layer; connects list/time-range changes
     * to rebuilds and current-time/highlight changes to the active data refresh. Layer owns
     * added/removed sublayers; no local undo wrapper.
     */
    QWidget *buildAdditionalResultsTab(QWidget *parent);
    /**
     * @brief Repopulate variable catalog and configured scalar-result rows under signal blockers.
     * No-op without layer/list; retains catalog key and selected sublayer ID where present,
     * otherwise selects the first configured row. Missing variables remain labeled unavailable;
     * updates add/remove gating and rebuilds details.
     */
    void refreshAdditionalResults();
    /**
     * @brief Replace selected-result controls and their active data refresh closure.
     * Immediately hides/disables/detaches old widgets before deferred deletion; missing
     * host/layer/selection/scalar sublayer leaves no details. Requires the builder-owned vertical
     * detail layout and a non-null scalar/contour style. Uses QPointer in retained layer/style
     * callbacks.
     * Edits variable, presentation, opacity (percent converted to fraction), contour
     * levels/strokes/labels, classification and availability colors live. Levels use native
     * variable units; widths are pixels and fonts points. Refresh retains invalid classification
     * drafts, inspects the lowest selected native cell (displayed one-based), and gates scientific
     * export on available data with known nonempty units. CSV export rechecks source
     * identity/revision/time/key after the save dialog; GIS export captures a frame snapshot for
     * its asynchronous dialog. Export/status failures are displayed without changing simulation
     * data.
     */
    void rebuildResultDetails();
    std::function<void()> m_refreshResultData;
    QListWidget *m_resultList = nullptr;
    QComboBox *m_resultCatalog = nullptr;
    QWidget *m_resultDetailHost = nullptr;
    QPointer<SWMM2DResultsLayer> m_layer;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SWMM2DRESULTSSTYLEPANEL_H
