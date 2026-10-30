// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      traceanalysislayer.h
 * @brief     Map rendering and styling of saved flow analysis results.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "layers/openswmmvislayer.h"
#include "output/tracedata.h"
#include "render/classificationscheme.h"
#include "render/isublayerhost.h"
#include "render/labelconfig.h"
#include <QPainterPath>
class SWMMResultsLayer;

namespace openswmmvis::trace
{
// Every visual channel has its own field, range, transform and palette.
struct Channel {
    QString field = QStringLiteral("ratio"), transform = QStringLiteral("linear");
    bool automatic = true;
    double minimum = 0, maximum = 1, minimumSize = 2, maximumSize = 14;
    bool proportional = true; // Link widths only: a shared zero-based linear scale.
    OpenSWMM::Render::ClassificationScheme colors;
    /**
     * @brief Initialize an independent visual channel with a continuous colour scheme and ratio/linear/automatic defaults.
     * @note Raw public fields are unvalidated; sizes are logical pixels and numeric ranges use the selected field's units.
     */
    Channel();
    /**
     * @return Owned complete channel/range/size/palette JSON without validation, file IO or notification.
     */
    QJsonObject toJson() const;
    /**
     * @param[in] j Permissive channel object; missing fields use defaults, sizes clamp to minimum0..80 and maximum0.1..80 pixels without ordering them.
     * @return Owned channel. Present colors uses ClassificationScheme decoding; legacy classes/ramp/range fields migrate when colors is absent. Field/transform names and numeric range order are not validated.
     */
    static Channel fromJson(const QJsonObject &j);
};
struct TraceLabels {
    OpenSWMM::Render::LabelConfig appearance;
    bool showId = true, percent = true;
    int precision = 2;
    /**
     * @brief Initialize ratio labels with a halo, two decimal digits and percent/ID formatting enabled.
     * @note Overall label visibility follows LabelConfig defaults; construction does not request painting.
     */
    TraceLabels();
    /**
     * @return Owned appearance/ID/percent/precision JSON without validation, file IO or notification.
     */
    QJsonObject toJson() const;
    /**
     * @param[in] j Permissive label object; appearance uses LabelConfig decoder, missing flags true and precision two.
     * @return Owned label policy with precision clamped0..8. No source lookup/signals or file IO.
     */
    static TraceLabels fromJson(const QJsonObject &j);
};
class TraceStyle final : public OpenSWMM::Render::SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(bool arrows READ arrows WRITE setArrows NOTIFY styleChanged)
    Q_PROPERTY(bool labels READ labels WRITE setLabels NOTIFY styleChanged)
public:
    /**
     * @param[in] p Optional QObject owner for the mutable aggregate style.
     * @note Initializes independent node/link channels and sets link minimum width to zero; public-field edits require changed() to announce them.
     */
    explicit TraceStyle(QObject *p = nullptr) : SublayerStyle(p) { linkWidth.minimumSize = 0; }
    Channel linkColor, linkWidth, nodeColor, nodeSize;
    TraceLabels linkLabels, nodeLabels;
    /**
     * @return Stored taper fraction, initially one; current trace painting/hit-testing use equal start/end widths and do not apply it.
     */
    double taper() const { return m_taper; }
    /**
     * @param[in] v Finite taper fraction clamped0.05..1; caller should exclude NaN because std::clamp has no independent finite guard.
     * @note Equality after normalization is a no-op; change calls setDirty(), advancing style revision and notifying attached views.
     */
    void setTaper(double v);
    /**
     * @return Whether downstream link arrows are requested, initially true; sublayer visibility also gates painting.
     */
    bool arrows() const { return m_arrows; }
    /**
     * @param[in] v Whether to request arrow glyphs.
     * @note Equality after normalization is a no-op; change calls setDirty(), advancing style revision and notifying attached views.
     */
    void setArrows(bool v);
    /// Elements carrying less than this fraction of the seed's flow are hidden and unlabelled.
    /**
     * @return Absolute minimum positive passage-ratio share, initially0.001; seed and unavailable/nonfinite ratios have special contribution rules.
     */
    double minContribution() const { return m_minContribution; }
    /**
     * @param[in] v Passage-ratio fraction clamped0..1; nonfinite input substitutes zero.
     * @note Equality after normalization is a no-op; change calls setDirty(), advancing style revision and notifying attached views.
     */
    void setMinContribution(double v);
    /**
     * @return Node-label appearance enabled flag only; link-label visibility is independent.
     */
    bool labels() const { return nodeLabels.appearance.enabled; }
    /**
     * @param[in] v Node-label enabled state only, leaving linkLabels unchanged.
     * @note Equality after normalization is a no-op; change calls setDirty(), advancing style revision and notifying attached views.
     */
    void setLabels(bool v);
    /**
     * @brief Announce direct public channel/label edits through SublayerStyle::setDirty(); no validation or source recalculation occurs.
     */
    void changed() { setDirty(); }
    /**
     * @return Owned schema-two channel/label/taper/arrow/contribution state. Part visibility/opacity is added separately by savedStyle(); no disk IO.
     */
    QJsonObject toJson() const override;
    /**
     * @param[in] j Empty is a no-op; otherwise replaces channels/labels with decoded values/defaults, clamps taper/contribution, and migrates older node-label flag/id selection.
     * @note Always setDirty() after nonempty restore, without strict schema validation/rollback. Unlike setMinContribution(), JSON restoration does not separately normalize NaN.
     */
    void fromJson(const QJsonObject &j) override;

private:
    double m_taper = 1;
    bool m_arrows = true;
    double m_minContribution = 0.001;
};
// Pure pixel-space geometry, shared by painting and hit testing. Duplicate
// vertices are discarded; segment joins use round disks to avoid miter spikes.
/**
 * @brief Build a filled pixel-space ribbon with segment quads and round join disks.
 * @param[in] input Logical-pixel polyline; nonfinite/consecutive near-duplicate points (distance<=1e-8) are discarded.
 * @param[in] startWidth Finite nonnegative total width at the first retained point in the same pixel unit.
 * @param[in] endWidth Finite nonnegative total width at the last retained point, linearly interpolated by cumulative arc length.
 * @return Owned WindingFill path, empty with fewer than two usable points. No arrowhead or precise endpoint cap is added; caller supplies widths and paints/tests it.
 */
QPainterPath taperedPath(const QVector<QPointF> &input, double startWidth, double endWidth);
/**
 * @param[in] f Exact lowercase field key ratio/flow/gross/time/local/coverage/volume, other strings treated as uniform.
 * @param[in] node Whether node-specific flow/gross wording is needed.
 * @return Translated field heading including SI flow/volume or minute time units; ratio/coverage are dimensionless.
 */
QString fieldLabel(const QString &f, bool node = false);
class TraceAnalysisLayer final : public OpenSWMMVisLayer, public OpenSWMM::Render::ISublayerHost
{
    Q_OBJECT
public:
    /**
     * @brief Create a styled view over shared saved trace results.
     * @param[in] r Required result with nonnull immutable dataset, valid seed/endpoints and parallel node/link estimates matching its snapshot. Retains shared ownership; the result's style JSON can be mutated by style notifications.
     * @param[in] w Optional workspace QObject owner.
     * @note Builds four owned parts and an owned TraceStyle, derives source extent/CRS, restores saved style and forwards invalidations. Use GUI-thread access; mutable colour caches and style have no internal locking.
     */
    explicit TraceAnalysisLayer(std::shared_ptr<Result> r, OpenSWMMVisWorkspace *w = nullptr);
    /**
     * @return Shared mutable result handle retained by value; dataset is shared immutable data. External in-place numeric mutation bypasses cache/scene invalidation, so use setResult() for recalculation replacement.
     */
    std::shared_ptr<Result> result() const { return m_result; }
    /*! True when the element is in the traced network and carries at least the
     *  style's minimum share of the flow (the seed always does). Drawing, labels,
     *  legend ranges, hit testing and the details table all use this.
     * @param[in] node True selects node estimate, false link.
     * @param[in] index Valid zero-based ordinal in that estimate vector; no bounds check.
     * @return Seed node always true; otherwise unreachable false, nonfinite ratio true for reported trapped/unavailable conditions, or positive absolute ratio at least minContribution(). Does not inspect geometry or part visibility.
     */
    bool contributes(bool node, int index) const;
    /*! Replace the numbers after a recalculation; keeps this view's own style.
     * @param[in] result Optional new valid result. Null or snapshot node/link count mismatch is ignored; count equality does not verify geometry/IDs/dataset equivalence.
     * @note Copies the Result (sharing its immutable dataset), preserves this view's saved style, clears colour caches/highlight and invalidates parts plus repaint. Does not rebuild scene geometry/extent/CRS; replacement should describe the same ordered network.
     */
    void setResult(std::shared_ptr<Result> result);
    /**
     * @return Borrowed layer-owned mutable style, valid until layer destruction; use changed() after public field edits to notify rendering/persistence.
     */
    TraceStyle *traceStyle() const { return m_style; }
    /**
     * @return Owned pointer-list copy of borrowed layer-owned links/nodes/arrows/labels parts in that order. Do not delete parts or outlive the layer.
     */
    QList<OpenSWMM::Render::ISublayer *> sublayers() const override { return m_parts; }
    /**
     * @return Owned subject wrappers borrowing the layer-owned TraceStyle; wrappers must not outlive this layer. One Flow analysis subject exposes independent view settings.
     */
    std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>> styleSubjects() override;
    /**
     * @return Owned saved dataset package path, without reading/revalidating the file.
     */
    QString sourceDescription() const override;
    /**
     * @param[in] scene Optional destination scene; null/hidden layer returns. Scene owns each newly appended item.
     * @param[in] canvas Optional borrowed target CRS read for geometry conversion; absent source/target CRS assumes compatible coordinates.
     * @note The unnamed extent argument is ignored. Copies/project-flips all snapshot geometry into a new item (no viewport cull); transformation creation failure/empty finite geometry returns. Link endpoints supply a fallback when its polyline is empty. Does not remove previous items; use inherited depopulateScene() before rebuilding. Items borrow this layer for painting and must be removed before destruction.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &,
                       const SpatialReferenceSystem *canvas) override;
    /**
     * @param[in] p Required active painter whose world transform maps supplied scene geometry to logical pixels; state is saved/restored.
     * @param[in] nodes Scene-coordinate node vector parallel to result estimates, with NaNs for absent geometry.
     * @param[in] links Scene-coordinate link polylines parallel to result estimates.
     * @note Paints screen-size ribbons/circles/arrows then halo labels according to contribution/part settings; supplied geometry and immutable results must remain stable. A containing output layer additionally gates visibility/opacity. Internal colour caches mutate, so serialize paint/range/style access. No engine reads or network IO occur.
     */
    void paint(QPainter *p, const QVector<QPointF> &nodes,
               const QVector<QVector<QPointF>> &links) const;
    /**
     * @brief Read a raw saved metric without contribution/visibility filtering.
     * @param[in] node True selects node values/style, false link.
     * @param[in] index Valid ordinal, not range-checked.
     * @param[in] field Exact field key; unknown/uniform returns one.
     * @return Ratio/coverage dimensionless, flow/gross m³/s, time/local minutes, volume node m³ (link volume NaN). Nonfinite saved values are retained.
     */
    double value(bool node, int index, const QString &field) const;
    /**
     * @brief Find numeric scaling bounds over contributing finite values.
     * @param[in] node True selects node values/style, false link.
     * @param[in] c Channel field/range policy.
     * @return Manual minimum/maximum verbatim when automatic=false; otherwise minimum extends to at most zero and maximum is finite sample maximum. No samples gives[0,1]; no transform is applied here.
     */
    QPair<double, double> range(bool node, const Channel &c) const;
    /**
     * @brief Map a raw metric into channel position.
     * @param[in] node True selects node values/style, false link.
     * @param[in] index Valid estimate ordinal, not range-checked.
     * @param[in] c Scaling field/transform/range.
     * @return Uniform always one; nonfinite metric NaN; otherwise transformed/clamped0..1, with equal/reversed transformed range giving0.5. sqrt/log clamp negative raw arguments to zero.
     */
    double normalized(bool node, int index, const Channel &c) const;
    /**
     * @brief Build colour and size legend rows for one element type.
     * @param[in] node True selects node values/style, false link.
     * @return Owned heading/classes-or-three-sample rows plus unavailable explanation. Uses effective colour range/proportional width reference; does not suppress rows merely because their scene part is hidden.
     */
    QList<OpenSWMM::Render::LegendSymbolItem> legend(bool node) const;
    /**
     * @brief Pick visible contributing nodes before links using display-size pixel geometry.
     * @param[in] pixel Logical viewport pixel.
     * @param[in] mapToPixel Canvas-map to pixel transform, consuming ordinary positive-Y map coordinates (no scene-Y flip here).
     * @param[in] canvas Optional borrowed canvas CRS; conversion assumes compatibility when either CRS is absent.
     * @param[out] node Required pointer assigned true for a node hit, false for link, unchanged on miss.
     * @param[out] index Required pointer assigned the first matching saved ordinal, unchanged on miss.
     * @return True for the first node within radius+3pixels or link ribbon width+6pixels containing the point; false when hidden, transform setup fails or no match. Part visibility/contribution gates checks, while part opacity does not. No featurePicked() emission occurs here.
     */
    bool hitTest(QPointF pixel, const QTransform &mapToPixel, const SpatialReferenceSystem *canvas,
                 bool *node, int *index) const;
    /**
     * @param[in] node Whether the stored highlight ordinal denotes a node.
     * @param[in] index Highlight ordinal, or minus one to clear; copied without bounds validation.
     * @note Always emits repaintRequested(), even for unchanged values; does not select engine objects or emit featurePicked().
     */
    void highlight(bool node, int index);
    /**
     * @return widthReference() for the current link-width channel; does not mutate selected style.
     */
    double widthReference() const { return widthReference(m_style->linkWidth); }
    /**
     * @param[in] c Link field and manual maximum policy.
     * @return Positive manual maximum when nonautomatic, otherwise maximum summed contributing finite nonnegative inflow/outflow over saved link directions, or one when no positive reference exists. Units follow the field; no transform/nonzero minimum is applied.
     */
    double widthReference(const Channel &c) const;
    /**
     * @return Full-length logical-pixel widths. Uniform uses maximumSize; nonproportional interpolates transformed range; proportional uses max(0,value)/shared reference*maximumSize with missing metrics width one. Contribution/visibility filtering is separate; proportional widths can exceed maximumSize for inconsistent manual bounds.
     */
    QVector<double> linkWidths() const;
    /**
     * @return Full-length logical-pixel diameters, interpolated linearly in squared diameter (area) over normalized channel position. Missing metrics use minimumSize; filtering is separate.
     */
    QVector<double> nodeSizes() const;
    /**
     * @brief Collect finite contributing raw classification samples.
     * @param[in] node True selects node values/style, false link.
     * @param[in] c Field selector; range/size settings do not filter samples.
     * @return Owned values in estimate order, omitting unreachable/below-threshold/nonfinite metrics.
     */
    QVector<double> samples(bool node, const Channel &c) const;
    /**
     * @brief Compute one colour per saved estimate using the selected colour channel.
     * @param[in] node True selects node values/style, false link.
     * @return Owned full-length colours, including noncontributing elements; nonfinite grey, below effective range transparent, otherwise classified/ramp colours. Callers separately apply contribution/part visibility.
     */
    QVector<QColor> colors(bool node) const;
    /**
     * @brief Format one enabled node/link label from saved values.
     * @param[in] node True selects node values/style, false link.
     * @param[in] index Valid estimate/snapshot ordinal.
     * @return Empty when that label appearance is disabled, otherwise optional ID plus fixed precision/units; ratio/coverage can be percent and partial expected time appends coverage. Does not itself enforce contribution/geometry/part visibility.
     */
    QString labelText(bool node, int index) const;
    /**
     * @return Owned style JSON plus ordered part IDs/visibility/opacity; no data export or file write.
     */
    QJsonObject savedStyle() const;
    /**
     * @param[in] j Permissive aggregate style and optional parts array. Matching part IDs receive visible(defaulttrue)/opacity(defaultone); unknown IDs ignored and unlisted parts retain state.
     * @note Style decode and each part mutation can emit multiple invalidations/repaint requests. Not atomic; style notification can persist a snapshot before subsequent part restores. No explicit disk write occurs.
     */
    void restoreStyle(const QJsonObject &j);
signals:
    /**
     * @param[in] node Whether a picked feature would be a node.
     * @param[in] index Saved ordinal payload.
     * @note Declared integration signal; current hitTest()/highlight()/scene Item implementation do not emit it automatically.
     */
    void featurePicked(bool node, int index);

private:
    /**
     * @brief Resolve cached class edges for the immutable saved dataset.
     * @param[in] node True selects node values/style, false link.
     * @param[in] channel Field and classification-scheme selector.
     * @return Owned edge vector; recalculated when cached field or classification revision differs. Contribution threshold changes alone do not participate in this cache key.
     */
    QVector<double> colorEdges(bool node, const Channel &channel) const;
    struct ColorCache {
        QString field;
        quint64 revision = 0;
        QVector<double> edges;
    };
    mutable ColorCache m_nodeColorCache, m_linkColorCache;
    std::shared_ptr<Result> m_result;
    TraceStyle *m_style;
    QList<OpenSWMM::Render::ISublayer *> m_parts;
    bool m_highlightNode = true;
    int m_highlight = -1;
};
// A static child of one immutable output run. The output owns both this
// tree row and its painter; no separate top-level analysis layer is needed.
class TraceSublayer final : public OpenSWMM::Render::ISublayer
{
public:
    /**
     * @brief Create an output-owned static tree row with an independently styled trace painter.
     * @param[in] owner Required output QObject owner with stable workspace.
     * @param[in] result Required valid result; copied so style changes do not alter another view's Result, while immutable dataset remains shared.
     * @param[in] travel Whether this row is the expected-time view. Empty saved style then defaults node/link colour and label fields to time.
     * @note Owns its TraceAnalysisLayer as a QObject child and forwards visibility/opacity/repaint to invalidate(). No engine recomputation or scene insertion occurs here.
     */
    TraceSublayer(SWMMResultsLayer *owner, std::shared_ptr<Result> result, bool travel);
    /**
     * @return LineKind for this aggregate tree row, despite its painter also rendering nodes/labels.
     */
    Kind kind() const override { return LineKind; }
    /**
     * @return Owned trace:<result-id>:time-or-flow identity; recalculated from the current painter result.
     */
    QString id() const override;
    /**
     * @return Translated flow/time description, seed ID/direction and partial-report suffix from the immutable dataset.
     */
    QString displayName() const override;
    /**
     * @return Current owned painter visibility.
     */
    bool isVisible() const override { return m_layer->isVisible(); }
    /**
     * @param[in] v New painter-layer visibility forwarded to its base setter.
     * @note Effective changes forward invalidation through the layer visibility connection; no source data is changed.
     */
    void setVisible(bool v) override { m_layer->setVisible(v); }
    /**
     * @return Current owned painter opacity.
     */
    qreal opacity() const override { return m_layer->opacity(); }
    /**
     * @param[in] v Painter opacity forwarded to its base setter, which clamps to[0,1].
     * @note Effective changes forward invalidation; no data recalculation.
     */
    void setOpacity(qreal v) override { m_layer->setOpacity(v); }
    /**
     * @return False because saved analysis values are static, independent of animation frame.
     */
    bool isDynamic() const override { return false; }
    /**
     * @return Borrowed painter-owned TraceStyle; lifetime follows this sublayer.
     */
    OpenSWMM::Render::SublayerStyle *style() override { return m_layer->traceStyle(); }
    /**
     * @return Owned combined heading/link/node legend rows with every row ID rewritten to this aggregate trace sublayer ID.
     */
    QList<OpenSWMM::Render::LegendSymbolItem> legendSymbolItems() const override;
    /**
     * @brief Decline Qt Quick scene-node rendering for this painter-based analysis.
     * @return Null; the two unnamed borrowed inputs (prior node and context) are unused and prior-node ownership is unchanged.
     */
    QSGNode *buildOrUpdateNode(QSGNode *, const OpenSWMM::Render::SublayerContext &) override
    {
        return nullptr;
    }
    /**
     * @return Borrowed owned trace painter; callers must not delete it or outlive this tree row.
     */
    TraceAnalysisLayer *layer() const { return m_layer; }
    /**
     * @return Immutable flag selecting time versus flow presentation identity.
     */
    bool travel() const { return m_travel; }

private:
    TraceAnalysisLayer *m_layer;
    bool m_travel;
};
} // namespace openswmmvis::trace
