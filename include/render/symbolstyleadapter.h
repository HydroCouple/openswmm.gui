/**
 * @file      symbolstyleadapter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Q_PROPERTY view over a Rule's SingleSymbolRenderer (Slice B.6c).
 *
 *         SymbolStyleAdapter is what makes the Single Symbol case
 *         editable through the Rule Model. The existing
 *         `FeatureStyleEditor` / `SwmmElementSymbolEditor` editors bind
 *         to QObjects via QPropertyModel, but Rule's renderer hands out
 *         a plain `SymbolStyle` struct — not a QObject. This adapter
 *         bridges the gap: it owns a non-owning pointer to a Rule and
 *         exposes the most-common SymbolStyle fields as Q_PROPERTYs.
 *
 *         The adapter watches the Rule's `rendererReplaced` signal so
 *         external swaps (e.g. SymbologyTab class change) refresh the
 *         Q_PROPERTY view automatically.
 *
 *         Property keys mirror the canonical SymbolLayer prop keys from
 *         MarkerSymbolLayerSpec (Z.4) + LineSymbolLayerSpec (Z.5). Edits
 *         are written back to the first SymbolLayer's props map; multi-
 *         layer symbols (composite stacks) still work but only the
 *         bottom layer is exposed to the editor in this slice.
 *
 *         Slice B.6c shipped the read/write seam (this class). Slice SS.1
 *         adds the three archetype-aware adapters
 *         (\ref PointSymbolStyleAdapter / \ref LineSymbolStyleAdapter /
 *         \ref PolygonSymbolStyleAdapter) so the QPropertyModel-driven
 *         editor only surfaces properties that apply to the underlying
 *         geometry. The factory \ref SymbolStyleAdapter::createFor picks
 *         the right archetype subclass by inspecting the Rule's first
 *         SymbolLayer kind. See
 *         docs/RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SYMBOLSTYLEADAPTER_H
#define OPENSWMM_RENDER_SYMBOLSTYLEADAPTER_H

#include "render/markershape.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QtGlobal>

namespace OpenSWMM::Render
{

class Rule;
class IFeatureRenderer;

class SymbolStyleAdapter : public QObject
{
    Q_OBJECT

    /*! \brief Overall symbol opacity in [0, 1]. */
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)

    /*! \brief Fill colour of the first SymbolLayer's "fillColor" prop
     *         (markers, polygons). Invalid when the layer doesn't carry
     *         a convertible fillColor; writes insert the key when needed. */
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)

    /*! \brief Stroke colour ("color" prop on lines, "outlineColor" on
     *         markers / fills). The setter writes both keys to stay
     *         archetype-agnostic. */
    Q_PROPERTY(QColor strokeColor READ strokeColor WRITE setStrokeColor NOTIFY changed)

    /*! \brief Stroke / outline width in pixels. Reads "width" first
     *         (lines), falls back to "outlineWidth" (markers). Writes
     *         both. */
    Q_PROPERTY(qreal strokeWidth READ strokeWidth WRITE setStrokeWidth NOTIFY changed)

    /*! \brief Marker-size property in pixels. Writes insert the size key on the
     *         first pass of any SingleSymbolRenderer, without checking its kind. */
    Q_PROPERTY(qreal markerSize READ markerSize WRITE setMarkerSize NOTIFY changed)

    /*! \brief Marker shape (Z.4 canonical enum). Typed as MarkerShape so
     *         QPropertyModel surfaces it through a Q_ENUM-aware editor
     *         (the registered MarkerShapeEditor renders the shapes as
     *         icons in a combo). Stored in the SymbolLayer's prop bag as
     *         the underlying int. */
    Q_PROPERTY(OpenSWMM::Render::MarkerShape markerShape READ markerShape WRITE setMarkerShape
                   NOTIFY changed)

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit SymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~SymbolStyleAdapter() override;

    /*! \brief Slice SS.1 factory. Returns a freshly-allocated
     *         archetype-aware adapter (Point / Line / Polygon subclass)
     *         picked from \p rule's first SymbolLayer kind. Falls back
     *         to the generic SymbolStyleAdapter when the rule's symbol
     *         has no layers or the kind doesn't map to one of the three
     *         archetypes. Raster, hillshade, contour, mesh and velocity kinds
     *         select their dedicated adapters before the point/line/polygon dispatch.
     *
     *         The returned QObject* is owned by \p parent (if non-null)
     *         or by the caller. Concrete type is one of:
     *           - PointSymbolStyleAdapter
     *           - LineSymbolStyleAdapter
     *           - PolygonSymbolStyleAdapter
     *           - A dedicated raster, contour, mesh or velocity adapter
     *           - SymbolStyleAdapter (generic fallback)
     *         The QPropertyModel-driven editor only sees the properties
     *         declared on the concrete metaobject — "incompatible
     *         properties not visible at all" per
     *         RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md §3.
     * @param rule Optional borrowed rule that must outlive the allocated adapter.
     * Only the first pass of SingleSymbolRenderer is inspected; other renderers
     * and empty stacks select the generic adapter. Contour mode zero selects
     * isolines; any other converted mode value selects bands.
     * @param parent Optional QObject owner; null leaves deletion to the caller.
     * @return Newly allocated adapter. It retains no ownership of the rule and
     * will not change its own concrete type after a renderer replacement. */
    [[nodiscard]] static QObject *createFor(Rule *rule, QObject *parent = nullptr);

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's fill color setting.
     * @return Owned property value; fallback QColor() when no compatible first-pass value exists.
     * Reads the exact fillColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor fillColor() const;
    /** @brief Read the first pass's line color, falling back to its outline color.
     * @return Convertible color property when valid, otherwise convertible
     * outlineColor, or invalid when neither is available. Null/other renderers
     * and empty stacks return invalid; no other passes are searched. */
    [[nodiscard]] QColor strokeColor() const;
    /** @brief Read line width, falling back to outline width for negative/missing line width.
     * @return Logical-pixel width, or zero if both reads fail. Converted negative
     * outline widths/nonfinite values are retained; no validation occurs. */
    [[nodiscard]] qreal strokeWidth() const;
    /** @brief Read the first symbol pass's marker size setting.
     * @return Owned property value in logical pixels; fallback 0.0 when no compatible first-pass
     * value exists.
     * Reads the exact size key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal markerSize() const;
    /** @brief Read the first symbol pass's marker shape setting.
     * @return Owned property value; fallback `static_cast<int>(MarkerShape::Circle)` when no
     * compatible first-pass value exists.
     * Reads the exact shape key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] MarkerShape markerShape() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's fill color property.
     * @param c New fill color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact fillColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setFillColor(const QColor &c);
    /** @brief Write both line and outline color keys on the first symbol pass.
     * @param c Color copied verbatim, including invalid input. Missing keys/pass
     * are created for SingleSymbolRenderer; other renderer types ignore the write.
     * Each changed key notifies the rule separately, so this can emit two changed()
     * notifications. Other passes and the overall symbol opacity are retained. */
    void setStrokeColor(const QColor &c);
    /** @brief Write both line-width and outline-width keys on the first symbol pass.
     * @param v Logical-pixel width stored without finite/nonnegative checks.
     * Each effective key change separately notifies the rule; other renderers are
     * ignored and missing keys/pass are created through writeProp(). */
    void setStrokeWidth(qreal v);
    /** @brief Write the first symbol pass's marker size property.
     * @param v New marker size, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact size key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setMarkerSize(qreal v);
    /** @brief Write the first symbol pass's marker shape property.
     * @param v New marker shape. Stored as its integer enum value without range validation.
     * Writes the exact shape key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setMarkerShape(MarkerShape v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /*! Single coalesced signal — any property write fires it. The
     *  QPropertyModel walks the metaobject on each `changed` so per-
     *  property NOTIFY granularity isn't useful here. */
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    /** @brief Read a convertible first-pass property through the shared typed helper.
     * @tparam T QVariant-convertible requested result type.
     * @param key Exact case-sensitive property key, without normalization.
     * @param fallback Value copied for null/other renderer types, an empty stack,
     * a missing key or a nonconvertible variant.
     * @return Owned converted value or fallback; no semantic/range validation occurs. */
    template <typename T> [[nodiscard]] T readProp(const QString &key, const T &fallback) const;

    /** @brief Insert or replace a first-pass property through the shared typed helper.
     * @tparam T QVariant-storable type supporting equality comparison.
     * @param key Exact case-sensitive key to insert or replace.
     * @param value Value copied without validation; a missing pass is created as
     * a default SymbolLayer for a SingleSymbolRenderer.
     * @return True after replacing the copied style and notifying the rule;
     * false for null/other renderer types or an equal convertible current value.
     * changed() is emitted indirectly through rendererReplaced(), not directly here. */
    template <typename T> bool writeProp(const QString &key, const T &value);

    Rule *m_rule = nullptr;
};

// ===========================================================================
// Slice SS.1 — archetype-aware adapters
// ===========================================================================
//
// Each subclass declares ONLY the Q_PROPERTYs that apply to its archetype.
// QPropertyModel walks the concrete metaobject, so the editor sees exactly
// the matrix from RENDERING_SINGLE_SYMBOL_TRANSFER_PLAN.md §3 — no marker
// knobs on lines, no arrow knobs on points.
//
// All three subclasses share the same plumbing: hold a Rule*, watch
// rendererReplaced, read/write canonical prop keys on the Rule's
// SingleSymbolRenderer's first SymbolLayer. Common helpers live in the
// .cpp (free functions in an anonymous namespace) — these classes are
// siblings (not derived from SymbolStyleAdapter) precisely so the base
// metaobject doesn't leak its archetype-agnostic Q_PROPERTYs.

/*!
 * \class PointSymbolStyleAdapter
 * \brief Single-Symbol panel surface for point (marker) archetypes —
 *        Junctions / Outfalls / Storage / Dividers / RainGages on the
 *        SWMM model layer, and the equivalent point categories on the
 *        results layer.
 */
class PointSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(OpenSWMM::Render::MarkerShape markerShape READ markerShape WRITE setMarkerShape
                   NOTIFY changed)
    Q_PROPERTY(qreal markerSize READ markerSize WRITE setMarkerSize NOTIFY changed)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY changed)
    Q_PROPERTY(qreal outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY changed)
    Q_PROPERTY(bool showLabel READ showLabel WRITE setShowLabel NOTIFY changed)
    Q_PROPERTY(QFont labelFont READ labelFont WRITE setLabelFont NOTIFY changed)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:markerShape", "Symbology")
    Q_CLASSINFO("group:markerSize", "Symbology")
    Q_CLASSINFO("group:fillColor", "Fill")
    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidth", "Outline")
    Q_CLASSINFO("group:showLabel", "Labels")
    Q_CLASSINFO("group:labelFont", "Labels")
    Q_CLASSINFO("group:labelColor", "Labels")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit PointSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~PointSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read a marker shape from either its integer property or legacy token text.
     * @return Stored shape, or Circle when unavailable/unconvertible. Unknown string tokens also
     * resolve to Circle; integer enum bounds are not validated. */
    [[nodiscard]] MarkerShape markerShape() const;
    /** @brief Read the first symbol pass's marker size setting.
     * @return Owned property value in logical pixels; fallback 8.0 when no compatible first-pass
     * value exists.
     * Reads the exact size key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal markerSize() const;
    /** @brief Read the first symbol pass's fill color setting.
     * @return Owned property value; fallback QColor() when no compatible first-pass value exists.
     * Accepts QColor or parseable string color values. The color key is tried after the primary
     * key when needed. */
    [[nodiscard]] QColor fillColor() const;
    /** @brief Read the first symbol pass's outline color setting.
     * @return Owned property value; fallback QColor() when no compatible first-pass value exists.
     * Accepts QColor or parseable string color values. Invalid/unparseable values use the
     * fallback. */
    [[nodiscard]] QColor outlineColor() const;
    /** @brief Read the first symbol pass's outline width setting.
     * @return Owned property value in logical pixels; fallback 0.5 when no compatible first-pass
     * value exists.
     * Reads the exact outlineWidth key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal outlineWidth() const;
    /** @brief Read the first symbol pass's label visibility setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact showLabel key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool showLabel() const;
    /** @brief Read the first symbol pass's label font setting.
     * @return Owned property value; fallback QFont() when no compatible first-pass value exists.
     * Reads the exact labelFont key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QFont labelFont() const;
    /** @brief Read the first symbol pass's label color setting.
     * @return Owned property value; fallback QColor(Qt::black) when no compatible first-pass value
     * exists.
     * Accepts QColor or parseable string color values. Invalid/unparseable values use the
     * fallback. */
    [[nodiscard]] QColor labelColor() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's marker shape property.
     * @param v New marker shape. Stored as its integer enum value without range validation.
     * Writes the exact shape key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setMarkerShape(MarkerShape v);
    /** @brief Write the first symbol pass's marker size property.
     * @param v New marker size, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact size key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setMarkerSize(qreal v);
    /** @brief Write the first symbol pass's fill color property.
     * @param c New fill color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact fillColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setFillColor(const QColor &c);
    /** @brief Write the first symbol pass's outline color property.
     * @param c New outline color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact outlineColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setOutlineColor(const QColor &c);
    /** @brief Write the first symbol pass's outline width property.
     * @param v New outline width, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact outlineWidth key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setOutlineWidth(qreal v);
    /** @brief Write the first symbol pass's label visibility property.
     * @param v New label visibility. True enables the flag and false disables it; no other setting
     * is normalized. Writes the exact showLabel key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setShowLabel(bool v);
    /** @brief Write the first symbol pass's label font property.
     * @param f New label font. Copied without checking whether the requested font is installed.
     * Writes the exact labelFont key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLabelFont(const QFont &f);
    /** @brief Write the first symbol pass's label color property.
     * @param c New label color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact labelColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setLabelColor(const QColor &c);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class LineSymbolStyleAdapter
 * \brief Single-Symbol panel surface for line archetypes — Conduits /
 *        Pumps / Orifices / Weirs / Outlets on the SWMM model layer
 *        and the equivalent link categories on the results layer.
 */
class LineSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY changed)
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth NOTIFY changed)
    Q_PROPERTY(Qt::PenStyle dashPattern READ dashPattern WRITE setDashPattern NOTIFY changed)
    Q_PROPERTY(qreal offsetPx READ offsetPx WRITE setOffsetPx NOTIFY changed)
    Q_PROPERTY(bool showLabel READ showLabel WRITE setShowLabel NOTIFY changed)
    Q_PROPERTY(QFont labelFont READ labelFont WRITE setLabelFont NOTIFY changed)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY changed)
    Q_PROPERTY(bool showArrows READ showArrows WRITE setShowArrows NOTIFY changed)
    Q_PROPERTY(qreal arrowSize READ arrowSize WRITE setArrowSize NOTIFY changed)
    Q_PROPERTY(qreal arrowWidth READ arrowWidth WRITE setArrowWidth NOTIFY changed)
    Q_PROPERTY(QColor arrowColor READ arrowColor WRITE setArrowColor NOTIFY changed)
    Q_PROPERTY(bool arrowOnlyWhenFlowPos READ arrowOnlyWhenFlowPos WRITE setArrowOnlyWhenFlowPos
                   NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:lineColor", "Line")
    Q_CLASSINFO("group:lineWidth", "Line")
    Q_CLASSINFO("group:dashPattern", "Line")
    Q_CLASSINFO("group:offsetPx", "Line")
    Q_CLASSINFO("group:showLabel", "Labels")
    Q_CLASSINFO("group:labelFont", "Labels")
    Q_CLASSINFO("group:labelColor", "Labels")
    Q_CLASSINFO("group:showArrows", "Flow arrows")
    Q_CLASSINFO("group:arrowSize", "Flow arrows")
    Q_CLASSINFO("group:arrowWidth", "Flow arrows")
    Q_CLASSINFO("group:arrowColor", "Flow arrows")
    Q_CLASSINFO("group:arrowOnlyWhenFlowPos", "Flow arrows")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit LineSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~LineSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's paint color setting.
     * @return Owned property value; fallback QColor() when no compatible first-pass value exists.
     * Accepts QColor or parseable string color values. Invalid/unparseable values use the
     * fallback. */
    [[nodiscard]] QColor lineColor() const;
    /** @brief Read the first symbol pass's line width setting.
     * @return Owned property value in logical pixels; fallback 1.0 when no compatible first-pass
     * value exists.
     * Reads the exact width key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal lineWidth() const;
    /** @brief Read the first symbol pass's stroke dash style setting.
     * @return Owned property value; fallback `static_cast<int>(Qt::SolidLine)` when no compatible
     * first-pass value exists.
     * Reads the exact penStyle key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] Qt::PenStyle dashPattern() const;
    /** @brief Read the first symbol pass's perpendicular line offset setting.
     * @return Owned property value in signed logical pixels, positive to the forward line's right;
     * fallback 0.0 when no compatible first-pass value exists.
     * Reads the exact offsetPx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal offsetPx() const;
    /** @brief Read the first symbol pass's label visibility setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact showLabel key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool showLabel() const;
    /** @brief Read the first symbol pass's label font setting.
     * @return Owned property value; fallback QFont() when no compatible first-pass value exists.
     * Reads the exact labelFont key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QFont labelFont() const;
    /** @brief Read the first symbol pass's label color setting.
     * @return Owned property value; fallback QColor(Qt::black) when no compatible first-pass value
     * exists.
     * Accepts QColor or parseable string color values. Invalid/unparseable values use the
     * fallback. */
    [[nodiscard]] QColor labelColor() const;
    /** @brief Read the first symbol pass's directional-arrow visibility setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact drawArrows key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool showArrows() const;
    /** @brief Read the first symbol pass's arrow length setting.
     * @return Owned property value in logical pixels; fallback 16.0 when no compatible first-pass
     * value exists.
     * Reads the exact arrowLengthPx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal arrowSize() const;
    /** @brief Read the first symbol pass's arrow width setting.
     * @return Owned property value in logical pixels; fallback 8.0 when no compatible first-pass
     * value exists.
     * Reads the exact arrowWidthPx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal arrowWidth() const;
    /** @brief Read the first symbol pass's arrow color setting.
     * @return Owned property value; fallback QColor(34, 34, 34) when no compatible first-pass
     * value exists.
     * Accepts QColor or parseable string color values. Invalid/unparseable values use the
     * fallback. */
    [[nodiscard]] QColor arrowColor() const;
    /** @brief Read the first symbol pass's positive-flow arrow gate setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact arrowOnlyWhenFlowPos key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool arrowOnlyWhenFlowPos() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's paint color property.
     * @param c New paint color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact color key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setLineColor(const QColor &c);
    /** @brief Write the first symbol pass's line width property.
     * @param v New line width, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact width key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLineWidth(qreal v);
    /** @brief Write the first symbol pass's stroke dash style property.
     * @param s New stroke dash style. Stored as its integer enum value without range validation.
     * Writes the exact penStyle key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setDashPattern(Qt::PenStyle s);
    /** @brief Write the first symbol pass's perpendicular line offset property.
     * @param v New perpendicular line offset, expressed as signed logical pixels, positive to the
     * forward line's right. Copied verbatim without range, finiteness or semantic validation.
     * Writes the exact offsetPx key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setOffsetPx(qreal v);
    /** @brief Write the first symbol pass's label visibility property.
     * @param v New label visibility. True enables the flag and false disables it; no other setting
     * is normalized. Writes the exact showLabel key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setShowLabel(bool v);
    /** @brief Write the first symbol pass's label font property.
     * @param f New label font. Copied without checking whether the requested font is installed.
     * Writes the exact labelFont key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLabelFont(const QFont &f);
    /** @brief Write the first symbol pass's label color property.
     * @param c New label color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact labelColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setLabelColor(const QColor &c);
    /** @brief Write the first symbol pass's directional-arrow visibility property.
     * @param v New directional-arrow visibility. True enables the flag and false disables it; no
     * other setting is normalized. Writes the exact drawArrows key only for SingleSymbolRenderer.
     * A missing pass is created as a default SymbolLayer; missing keys are inserted. An equal
     * convertible stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setShowArrows(bool v);
    /** @brief Write the first symbol pass's arrow length property.
     * @param v New arrow length, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact arrowLengthPx key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setArrowSize(qreal v);
    /** @brief Write the first symbol pass's arrow width property.
     * @param v New arrow width, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact arrowWidthPx key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setArrowWidth(qreal v);
    /** @brief Write the first symbol pass's arrow color property.
     * @param c New arrow color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact arrowColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setArrowColor(const QColor &c);
    /** @brief Write the first symbol pass's positive-flow arrow gate property.
     * @param v New positive-flow arrow gate. True enables the flag and false disables it; no other
     * setting is normalized. Writes the exact arrowOnlyWhenFlowPos key only for
     * SingleSymbolRenderer. A missing pass is created as a default SymbolLayer; missing keys are
     * inserted. An equal convertible stored value is a no-op. A change replaces the copied style
     * and calls Rule::notifyRendererStateChanged(); other passes and renderer configuration
     * remain. */
    void setArrowOnlyWhenFlowPos(bool v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

// ===========================================================================
// Slice AN.2 — raster / mesh / glyph archetype adapters (2D results layer)
// ===========================================================================
//
// Same pattern as the Point / Line / Polygon trio above: each subclass
// declares only its 2D-style Q_PROPERTYs so QPropertyModel surfaces
// exactly the editable knobs for that sublayer. Factory dispatch lives
// in createFor (.cpp) and reads SymbolLayer.kind + the "mode" prop for
// the two Contour archetypes (bands vs lines).
//
// Each adapter's properties + groups mirror the matching legacy style
// class (MeshFillStyle / DepthColorRampStyle / ContourBandStyle /
// IsolineStyle / MeshEdgeStyle / MeshNodeStyle / VelocityVectorStyle).
// See docs/RENDERING_2D_RESULTS_STYLING_PLAN.md §3.

/*!
 * \class RasterColorRampSymbolStyleAdapter
 * \brief Depth color-ramp surface — `DepthColorRampStyle`.
 */
class RasterColorRampSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY changed)
    Q_PROPERTY(qreal minValue READ minValue WRITE setMinValue NOTIFY changed)
    Q_PROPERTY(qreal maxValue READ maxValue WRITE setMaxValue NOTIFY changed)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY changed)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY changed)
    Q_PROPERTY(QColor belowMinColor READ belowMinColor WRITE setBelowMinColor NOTIFY changed)
    Q_PROPERTY(QColor aboveMaxColor READ aboveMaxColor WRITE setAboveMaxColor NOTIFY changed)
    Q_PROPERTY(bool useLogScale READ useLogScale WRITE setUseLogScale NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:minValue", "Range")
    Q_CLASSINFO("group:maxValue", "Range")
    Q_CLASSINFO("group:useLogScale", "Range")
    Q_CLASSINFO("group:lowColor", "Ramp")
    Q_CLASSINFO("group:highColor", "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit RasterColorRampSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~RasterColorRampSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's classification attribute key setting.
     * @return Owned property value; fallback QString() when no compatible first-pass value exists.
     * Reads the exact attribute key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QString attribute() const;
    /** @brief Read the first symbol pass's lower classification bound setting.
     * @return Owned property value in selected attribute units; fallback 0.0 when no compatible
     * first-pass value exists.
     * Reads the exact minValue key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal minValue() const;
    /** @brief Read the first symbol pass's upper classification bound setting.
     * @return Owned property value in selected attribute units; fallback 1.0 when no compatible
     * first-pass value exists.
     * Reads the exact maxValue key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal maxValue() const;
    /** @brief Read the first symbol pass's low ramp endpoint color setting.
     * @return Owned property value; fallback QColor(0, 0, 255) when no compatible first-pass value
     * exists.
     * Reads the exact lowColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor lowColor() const;
    /** @brief Read the first symbol pass's high ramp endpoint color setting.
     * @return Owned property value; fallback QColor(255, 0, 0) when no compatible first-pass value
     * exists.
     * Reads the exact highColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor highColor() const;
    /** @brief Read the first symbol pass's below-range color metadata setting.
     * @return Owned property value; fallback QColor(0, 0, 0, 0) when no compatible first-pass
     * value exists.
     * Reads the exact belowMinColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor belowMinColor() const;
    /** @brief Read the first symbol pass's above-range color metadata setting.
     * @return Owned property value; fallback QColor(0, 0, 0, 0) when no compatible first-pass
     * value exists.
     * Reads the exact aboveMaxColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor aboveMaxColor() const;
    /** @brief Read the first symbol pass's logarithmic scaling flag setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact useLogScale key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool useLogScale() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's classification attribute key property.
     * @param v New classification attribute key. Copied verbatim without trimming, key resolution
     * or existence checks. Writes the exact attribute key only for SingleSymbolRenderer. A missing
     * pass is created as a default SymbolLayer; missing keys are inserted. An equal convertible
     * stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setAttribute(const QString &v);
    /** @brief Write the first symbol pass's lower classification bound property.
     * @param v New lower classification bound, expressed as selected attribute units. Copied
     * verbatim without range, finiteness or semantic validation.
     * Writes the exact minValue key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setMinValue(qreal v);
    /** @brief Write the first symbol pass's upper classification bound property.
     * @param v New upper classification bound, expressed as selected attribute units. Copied
     * verbatim without range, finiteness or semantic validation.
     * Writes the exact maxValue key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setMaxValue(qreal v);
    /** @brief Write the first symbol pass's low ramp endpoint color property.
     * @param c New low ramp endpoint color. Copied verbatim; invalid QColor values are accepted.
     * Writes the exact lowColor key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLowColor(const QColor &c);
    /** @brief Write the first symbol pass's high ramp endpoint color property.
     * @param c New high ramp endpoint color. Copied verbatim; invalid QColor values are accepted.
     * Writes the exact highColor key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setHighColor(const QColor &c);
    /** @brief Write the first symbol pass's below-range color metadata property.
     * @param c New below-range color metadata. Copied verbatim; invalid QColor values are
     * accepted. Writes the exact belowMinColor key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setBelowMinColor(const QColor &c);
    /** @brief Write the first symbol pass's above-range color metadata property.
     * @param c New above-range color metadata. Copied verbatim; invalid QColor values are
     * accepted. Writes the exact aboveMaxColor key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setAboveMaxColor(const QColor &c);
    /** @brief Write the first symbol pass's logarithmic scaling flag property.
     * @param v New logarithmic scaling flag. True enables the flag and false disables it; no other
     * setting is normalized. Writes the exact useLogScale key only for SingleSymbolRenderer. A
     * missing pass is created as a default SymbolLayer; missing keys are inserted. An equal
     * convertible stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setUseLogScale(bool v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class HillshadeSymbolStyleAdapter
 * \brief Mesh-fill / hillshade surface — `MeshFillStyle`.
 */
class HillshadeSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)
    Q_PROPERTY(
        qreal hillshadeStrength READ hillshadeStrength WRITE setHillshadeStrength NOTIFY changed)
    Q_PROPERTY(bool useElevationRamp READ useElevationRamp WRITE setUseElevationRamp NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:fillColor", "Fill")
    Q_CLASSINFO("group:useElevationRamp", "Fill")
    Q_CLASSINFO("group:hillshadeStrength", "Shading")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit HillshadeSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~HillshadeSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's fill color setting.
     * @return Owned property value; fallback QColor(180, 180, 180) when no compatible first-pass
     * value exists.
     * Reads the exact fillColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor fillColor() const;
    /** @brief Read the first symbol pass's hillshade blend strength setting.
     * @return Owned property value in a dimensionless blend strength; fallback 0.5 when no
     * compatible first-pass value exists.
     * Reads the exact hillshadeStrength key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal hillshadeStrength() const;
    /** @brief Read the first symbol pass's elevation-ramp flag setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact useElevationRamp key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool useElevationRamp() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's fill color property.
     * @param c New fill color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact fillColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setFillColor(const QColor &c);
    /** @brief Write the first symbol pass's hillshade blend strength property.
     * @param v New hillshade blend strength, expressed as a dimensionless blend strength. Copied
     * verbatim without range, finiteness or semantic validation.
     * Writes the exact hillshadeStrength key only for SingleSymbolRenderer. A missing pass is
     * created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setHillshadeStrength(qreal v);
    /** @brief Write the first symbol pass's elevation-ramp flag property.
     * @param v New elevation-ramp flag. True enables the flag and false disables it; no other
     * setting is normalized. Writes the exact useElevationRamp key only for SingleSymbolRenderer.
     * A missing pass is created as a default SymbolLayer; missing keys are inserted. An equal
     * convertible stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setUseElevationRamp(bool v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class ContourBandSymbolStyleAdapter
 * \brief Filled-isoband surface — `ContourBandStyle`.
 */
class ContourBandSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY changed)
    Q_PROPERTY(int bandCount READ bandCount WRITE setBandCount NOTIFY changed)
    Q_PROPERTY(QColor lowColor READ lowColor WRITE setLowColor NOTIFY changed)
    Q_PROPERTY(QColor highColor READ highColor WRITE setHighColor NOTIFY changed)
    Q_PROPERTY(QColor belowMinColor READ belowMinColor WRITE setBelowMinColor NOTIFY changed)
    Q_PROPERTY(QColor aboveMaxColor READ aboveMaxColor WRITE setAboveMaxColor NOTIFY changed)
    Q_PROPERTY(bool smoothBands READ smoothBands WRITE setSmoothBands NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:bandCount", "Classification")
    Q_CLASSINFO("group:lowColor", "Ramp")
    Q_CLASSINFO("group:highColor", "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:smoothBands", "Rendering")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit ContourBandSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~ContourBandSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's classification attribute key setting.
     * @return Owned property value; fallback QString() when no compatible first-pass value exists.
     * Reads the exact attribute key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QString attribute() const;
    /** @brief Read the first symbol pass's contour-band count setting.
     * @return Owned property value in a count of contour bands; fallback 8 when no compatible
     * first-pass value exists.
     * Reads the exact bandCount key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] int bandCount() const;
    /** @brief Read the first symbol pass's low ramp endpoint color setting.
     * @return Owned property value; fallback QColor(0, 0, 255) when no compatible first-pass value
     * exists.
     * Reads the exact lowColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor lowColor() const;
    /** @brief Read the first symbol pass's high ramp endpoint color setting.
     * @return Owned property value; fallback QColor(255, 0, 0) when no compatible first-pass value
     * exists.
     * Reads the exact highColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor highColor() const;
    /** @brief Read the first symbol pass's below-range color metadata setting.
     * @return Owned property value; fallback QColor(0, 0, 0, 0) when no compatible first-pass
     * value exists.
     * Reads the exact belowMinColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor belowMinColor() const;
    /** @brief Read the first symbol pass's above-range color metadata setting.
     * @return Owned property value; fallback QColor(0, 0, 0, 0) when no compatible first-pass
     * value exists.
     * Reads the exact aboveMaxColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor aboveMaxColor() const;
    /** @brief Read the first symbol pass's smooth-band flag setting.
     * @return Owned property value; fallback true when no compatible first-pass value exists.
     * Reads the exact smoothBands key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool smoothBands() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's classification attribute key property.
     * @param v New classification attribute key. Copied verbatim without trimming, key resolution
     * or existence checks. Writes the exact attribute key only for SingleSymbolRenderer. A missing
     * pass is created as a default SymbolLayer; missing keys are inserted. An equal convertible
     * stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setAttribute(const QString &v);
    /** @brief Write the first symbol pass's contour-band count property.
     * @param v New contour-band count, expressed as a count of contour bands. Copied verbatim
     * without range, finiteness or semantic validation.
     * Writes the exact bandCount key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setBandCount(int v);
    /** @brief Write the first symbol pass's low ramp endpoint color property.
     * @param c New low ramp endpoint color. Copied verbatim; invalid QColor values are accepted.
     * Writes the exact lowColor key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLowColor(const QColor &c);
    /** @brief Write the first symbol pass's high ramp endpoint color property.
     * @param c New high ramp endpoint color. Copied verbatim; invalid QColor values are accepted.
     * Writes the exact highColor key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setHighColor(const QColor &c);
    /** @brief Write the first symbol pass's below-range color metadata property.
     * @param c New below-range color metadata. Copied verbatim; invalid QColor values are
     * accepted. Writes the exact belowMinColor key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setBelowMinColor(const QColor &c);
    /** @brief Write the first symbol pass's above-range color metadata property.
     * @param c New above-range color metadata. Copied verbatim; invalid QColor values are
     * accepted. Writes the exact aboveMaxColor key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setAboveMaxColor(const QColor &c);
    /** @brief Write the first symbol pass's smooth-band flag property.
     * @param v New smooth-band flag. True enables the flag and false disables it; no other setting
     * is normalized. Writes the exact smoothBands key only for SingleSymbolRenderer. A missing
     * pass is created as a default SymbolLayer; missing keys are inserted. An equal convertible
     * stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setSmoothBands(bool v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class IsolineSymbolStyleAdapter
 * \brief Contour-line surface — `IsolineStyle`.
 */
class IsolineSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QString attribute READ attribute WRITE setAttribute NOTIFY changed)
    Q_PROPERTY(int isoValueCount READ isoValueCount WRITE setIsoValueCount NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(qreal lineWidthPx READ lineWidthPx WRITE setLineWidthPx NOTIFY changed)
    Q_PROPERTY(Qt::PenStyle dashPattern READ dashPattern WRITE setDashPattern NOTIFY changed)
    Q_PROPERTY(bool labels READ labels WRITE setLabels NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:attribute", "Classification")
    Q_CLASSINFO("group:isoValueCount", "Classification")
    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:lineWidthPx", "Symbology")
    Q_CLASSINFO("group:dashPattern", "Symbology")
    Q_CLASSINFO("group:labels", "Labels")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit IsolineSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~IsolineSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's classification attribute key setting.
     * @return Owned property value; fallback QString() when no compatible first-pass value exists.
     * Reads the exact attribute key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QString attribute() const;
    /** @brief Read the first symbol pass's isoline level count setting.
     * @return Owned property value in a count of isoline levels; fallback 5 when no compatible
     * first-pass value exists.
     * Reads the exact isoValueCount key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] int isoValueCount() const;
    /** @brief Read the first symbol pass's paint color setting.
     * @return Owned property value; fallback QColor(40, 40, 40) when no compatible first-pass
     * value exists.
     * Reads the exact color key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor color() const;
    /** @brief Read the first symbol pass's line width setting.
     * @return Owned property value in logical pixels; fallback 0.75 when no compatible first-pass
     * value exists.
     * Reads the exact width key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal lineWidthPx() const;
    /** @brief Read the first symbol pass's stroke dash style setting.
     * @return Owned property value; fallback `static_cast<int>(Qt::SolidLine)` when no compatible
     * first-pass value exists.
     * Reads the exact penStyle key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] Qt::PenStyle dashPattern() const;
    /** @brief Read the first symbol pass's contour-label visibility setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact labels key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool labels() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's classification attribute key property.
     * @param v New classification attribute key. Copied verbatim without trimming, key resolution
     * or existence checks. Writes the exact attribute key only for SingleSymbolRenderer. A missing
     * pass is created as a default SymbolLayer; missing keys are inserted. An equal convertible
     * stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setAttribute(const QString &v);
    /** @brief Write the first symbol pass's isoline level count property.
     * @param v New isoline level count, expressed as a count of isoline levels. Copied verbatim
     * without range, finiteness or semantic validation.
     * Writes the exact isoValueCount key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setIsoValueCount(int v);
    /** @brief Write the first symbol pass's paint color property.
     * @param c New paint color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact color key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setColor(const QColor &c);
    /** @brief Write the first symbol pass's line width property.
     * @param v New line width, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact width key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLineWidthPx(qreal v);
    /** @brief Write the first symbol pass's stroke dash style property.
     * @param s New stroke dash style. Stored as its integer enum value without range validation.
     * Writes the exact penStyle key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setDashPattern(Qt::PenStyle s);
    /** @brief Write the first symbol pass's contour-label visibility property.
     * @param v New contour-label visibility. True enables the flag and false disables it; no other
     * setting is normalized. Writes the exact labels key only for SingleSymbolRenderer. A missing
     * pass is created as a default SymbolLayer; missing keys are inserted. An equal convertible
     * stored value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setLabels(bool v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class MeshEdgeSymbolStyleAdapter
 * \brief Mesh-wireframe surface — `MeshEdgeStyle`.
 */
class MeshEdgeSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(qreal lineWidthPx READ lineWidthPx WRITE setLineWidthPx NOTIFY changed)
    Q_PROPERTY(Qt::PenStyle dashPattern READ dashPattern WRITE setDashPattern NOTIFY changed)
    Q_PROPERTY(bool useSlopeDrivenWidth READ useSlopeDrivenWidth WRITE setUseSlopeDrivenWidth NOTIFY
                   changed)
    Q_PROPERTY(qreal slopeBreak READ slopeBreak WRITE setSlopeBreak NOTIFY changed)
    Q_PROPERTY(qreal wideWidthPx READ wideWidthPx WRITE setWideWidthPx NOTIFY changed)
    Q_PROPERTY(QColor wideColor READ wideColor WRITE setWideColor NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:lineWidthPx", "Symbology")
    Q_CLASSINFO("group:dashPattern", "Symbology")
    Q_CLASSINFO("group:useSlopeDrivenWidth", "Slope emphasis")
    Q_CLASSINFO("group:slopeBreak", "Slope emphasis")
    Q_CLASSINFO("group:wideWidthPx", "Slope emphasis")
    Q_CLASSINFO("group:wideColor", "Slope emphasis")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit MeshEdgeSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~MeshEdgeSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's paint color setting.
     * @return Owned property value; fallback QColor(80, 80, 80) when no compatible first-pass
     * value exists.
     * Reads the exact color key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor color() const;
    /** @brief Read the first symbol pass's line width setting.
     * @return Owned property value in logical pixels; fallback 0.5 when no compatible first-pass
     * value exists.
     * Reads the exact width key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal lineWidthPx() const;
    /** @brief Read the first symbol pass's stroke dash style setting.
     * @return Owned property value; fallback `static_cast<int>(Qt::SolidLine)` when no compatible
     * first-pass value exists.
     * Reads the exact penStyle key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] Qt::PenStyle dashPattern() const;
    /** @brief Read the first symbol pass's slope-driven width flag setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact useSlopeDrivenWidth key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool useSlopeDrivenWidth() const;
    /** @brief Read the first symbol pass's slope emphasis threshold setting.
     * @return Owned property value in normalized slope units; fallback 0.5 when no compatible
     * first-pass value exists.
     * Reads the exact slopeBreak key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal slopeBreak() const;
    /** @brief Read the first symbol pass's emphasized edge width setting.
     * @return Owned property value in logical pixels; fallback 1.5 when no compatible first-pass
     * value exists.
     * Reads the exact wideWidthPx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal wideWidthPx() const;
    /** @brief Read the first symbol pass's emphasized edge color setting.
     * @return Owned property value; fallback QColor(40, 40, 40) when no compatible first-pass
     * value exists.
     * Reads the exact wideColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor wideColor() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's paint color property.
     * @param c New paint color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact color key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setColor(const QColor &c);
    /** @brief Write the first symbol pass's line width property.
     * @param v New line width, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact width key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLineWidthPx(qreal v);
    /** @brief Write the first symbol pass's stroke dash style property.
     * @param s New stroke dash style. Stored as its integer enum value without range validation.
     * Writes the exact penStyle key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setDashPattern(Qt::PenStyle s);
    /** @brief Write the first symbol pass's slope-driven width flag property.
     * @param v New slope-driven width flag. True enables the flag and false disables it; no other
     * setting is normalized. Writes the exact useSlopeDrivenWidth key only for
     * SingleSymbolRenderer. A missing pass is created as a default SymbolLayer; missing keys are
     * inserted. An equal convertible stored value is a no-op. A change replaces the copied style
     * and calls Rule::notifyRendererStateChanged(); other passes and renderer configuration
     * remain. */
    void setUseSlopeDrivenWidth(bool v);
    /** @brief Write the first symbol pass's slope emphasis threshold property.
     * @param v New slope emphasis threshold, expressed as normalized slope units. Copied verbatim
     * without range, finiteness or semantic validation.
     * Writes the exact slopeBreak key only for SingleSymbolRenderer. A missing pass is created as
     * a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setSlopeBreak(qreal v);
    /** @brief Write the first symbol pass's emphasized edge width property.
     * @param v New emphasized edge width, expressed as logical pixels. Copied verbatim without
     * range, finiteness or semantic validation.
     * Writes the exact wideWidthPx key only for SingleSymbolRenderer. A missing pass is created as
     * a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setWideWidthPx(qreal v);
    /** @brief Write the first symbol pass's emphasized edge color property.
     * @param c New emphasized edge color. Copied verbatim; invalid QColor values are accepted.
     * Writes the exact wideColor key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setWideColor(const QColor &c);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class MeshNodeSymbolStyleAdapter
 * \brief Mesh-vertex marker surface — `MeshNodeStyle`.
 */
class MeshNodeSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(qreal markerSizePx READ markerSizePx WRITE setMarkerSizePx NOTIFY changed)
    Q_PROPERTY(OpenSWMM::Render::MarkerShape shape READ shape WRITE setShape NOTIFY changed)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY changed)
    Q_PROPERTY(qreal outlineWidthPx READ outlineWidthPx WRITE setOutlineWidthPx NOTIFY changed)
    Q_PROPERTY(bool highlightTagged READ highlightTagged WRITE setHighlightTagged NOTIFY changed)
    Q_PROPERTY(QColor taggedColor READ taggedColor WRITE setTaggedColor NOTIFY changed)
    Q_PROPERTY(qreal taggedSizePx READ taggedSizePx WRITE setTaggedSizePx NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:markerSizePx", "Symbology")
    Q_CLASSINFO("group:shape", "Symbology")
    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidthPx", "Outline")
    Q_CLASSINFO("group:highlightTagged", "Tagged vertices")
    Q_CLASSINFO("group:taggedColor", "Tagged vertices")
    Q_CLASSINFO("group:taggedSizePx", "Tagged vertices")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit MeshNodeSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~MeshNodeSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's fill color setting.
     * @return Owned property value; fallback QColor(60, 120, 200) when no compatible first-pass
     * value exists.
     * Reads the exact fillColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor color() const;
    /** @brief Read the first symbol pass's marker size setting.
     * @return Owned property value in logical pixels; fallback 6.0 when no compatible first-pass
     * value exists.
     * Reads the exact size key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal markerSizePx() const;
    /** @brief Read the first symbol pass's marker shape setting.
     * @return Owned property value; fallback `static_cast<int>(MarkerShape::Circle)` when no
     * compatible first-pass value exists.
     * Reads the exact shape key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] MarkerShape shape() const;
    /** @brief Read the first symbol pass's outline color setting.
     * @return Owned property value; fallback QColor(40, 40, 40) when no compatible first-pass
     * value exists.
     * Reads the exact outlineColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor outlineColor() const;
    /** @brief Read the first symbol pass's outline width setting.
     * @return Owned property value in logical pixels; fallback 0.5 when no compatible first-pass
     * value exists.
     * Reads the exact outlineWidth key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal outlineWidthPx() const;
    /** @brief Read the first symbol pass's tagged-node highlighting flag setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact highlightTagged key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool highlightTagged() const;
    /** @brief Read the first symbol pass's tagged-node color setting.
     * @return Owned property value; fallback QColor(255, 140, 0) when no compatible first-pass
     * value exists.
     * Reads the exact taggedColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor taggedColor() const;
    /** @brief Read the first symbol pass's tagged-node size setting.
     * @return Owned property value in logical pixels; fallback 8.0 when no compatible first-pass
     * value exists.
     * Reads the exact taggedSizePx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal taggedSizePx() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's fill color property.
     * @param c New fill color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact fillColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setColor(const QColor &c);
    /** @brief Write the first symbol pass's marker size property.
     * @param v New marker size, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact size key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setMarkerSizePx(qreal v);
    /** @brief Write the first symbol pass's marker shape property.
     * @param s New marker shape. Stored as its integer enum value without range validation.
     * Writes the exact shape key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setShape(MarkerShape s);
    /** @brief Write the first symbol pass's outline color property.
     * @param c New outline color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact outlineColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setOutlineColor(const QColor &c);
    /** @brief Write the first symbol pass's outline width property.
     * @param v New outline width, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact outlineWidth key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setOutlineWidthPx(qreal v);
    /** @brief Write the first symbol pass's tagged-node highlighting flag property.
     * @param v New tagged-node highlighting flag. True enables the flag and false disables it; no
     * other setting is normalized. Writes the exact highlightTagged key only for
     * SingleSymbolRenderer. A missing pass is created as a default SymbolLayer; missing keys are
     * inserted. An equal convertible stored value is a no-op. A change replaces the copied style
     * and calls Rule::notifyRendererStateChanged(); other passes and renderer configuration
     * remain. */
    void setHighlightTagged(bool v);
    /** @brief Write the first symbol pass's tagged-node color property.
     * @param c New tagged-node color. Copied verbatim; invalid QColor values are accepted. Writes
     * the exact taggedColor key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setTaggedColor(const QColor &c);
    /** @brief Write the first symbol pass's tagged-node size property.
     * @param v New tagged-node size, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact taggedSizePx key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setTaggedSizePx(qreal v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class VelocityVectorSymbolStyleAdapter
 * \brief 2D velocity-arrow surface — `VelocityVectorStyle`.
 */
class VelocityVectorSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(qreal glyphLengthScalePxPerMps READ glyphLengthScalePxPerMps WRITE
                   setGlyphLengthScalePxPerMps NOTIFY changed)
    Q_PROPERTY(
        qreal glyphLengthMinPx READ glyphLengthMinPx WRITE setGlyphLengthMinPx NOTIFY changed)
    Q_PROPERTY(
        qreal glyphLengthMaxPx READ glyphLengthMaxPx WRITE setGlyphLengthMaxPx NOTIFY changed)
    Q_PROPERTY(qreal glyphSpacingPx READ glyphSpacingPx WRITE setGlyphSpacingPx NOTIFY changed)
    Q_PROPERTY(qreal headSizePx READ headSizePx WRITE setHeadSizePx NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(qreal dryDepthCutoff READ dryDepthCutoff WRITE setDryDepthCutoff NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:glyphLengthScalePxPerMps", "Glyph")
    Q_CLASSINFO("group:glyphLengthMinPx", "Glyph")
    Q_CLASSINFO("group:glyphLengthMaxPx", "Glyph")
    Q_CLASSINFO("group:headSizePx", "Glyph")
    Q_CLASSINFO("group:color", "Glyph")
    Q_CLASSINFO("group:glyphSpacingPx", "Placement")
    Q_CLASSINFO("group:dryDepthCutoff", "Filtering")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit VelocityVectorSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~VelocityVectorSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's velocity-to-length scale setting.
     * @return Owned property value in logical pixels per (meter per second); fallback 20.0 when no
     * compatible first-pass value exists.
     * Reads the exact glyphLengthScalePxPerMps key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal glyphLengthScalePxPerMps() const;
    /** @brief Read the first symbol pass's minimum velocity glyph length setting.
     * @return Owned property value in logical pixels; fallback 4.0 when no compatible first-pass
     * value exists.
     * Reads the exact glyphLengthMinPx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal glyphLengthMinPx() const;
    /** @brief Read the first symbol pass's maximum velocity glyph length setting.
     * @return Owned property value in logical pixels; fallback 40.0 when no compatible first-pass
     * value exists.
     * Reads the exact glyphLengthMaxPx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal glyphLengthMaxPx() const;
    /** @brief Read the first symbol pass's velocity glyph spacing setting.
     * @return Owned property value in logical pixels; fallback 30.0 when no compatible first-pass
     * value exists.
     * Reads the exact glyphSpacingPx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal glyphSpacingPx() const;
    /** @brief Read the first symbol pass's velocity arrowhead size setting.
     * @return Owned property value in logical pixels; fallback 5.0 when no compatible first-pass
     * value exists.
     * Reads the exact headSizePx key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal headSizePx() const;
    /** @brief Read the first symbol pass's paint color setting.
     * @return Owned property value; fallback QColor(20, 20, 20, 220) when no compatible first-pass
     * value exists.
     * Reads the exact color key through QVariant conversion. Null rules, non-SingleSymbolRenderer
     * instances, empty stacks, missing keys and nonconvertible types use the fallback. Range, enum
     * and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor color() const;
    /** @brief Read the first symbol pass's dry-depth arrow cutoff setting.
     * @return Owned property value in meters of water depth; fallback 0.01 when no compatible
     * first-pass value exists.
     * Reads the exact dryDepthCutoff key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal dryDepthCutoff() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's velocity-to-length scale property.
     * @param v New velocity-to-length scale, expressed as logical pixels per (meter per second).
     * Copied verbatim without range, finiteness or semantic validation.
     * Writes the exact glyphLengthScalePxPerMps key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setGlyphLengthScalePxPerMps(qreal v);
    /** @brief Write the first symbol pass's minimum velocity glyph length property.
     * @param v New minimum velocity glyph length, expressed as logical pixels. Copied verbatim
     * without range, finiteness or semantic validation.
     * Writes the exact glyphLengthMinPx key only for SingleSymbolRenderer. A missing pass is
     * created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setGlyphLengthMinPx(qreal v);
    /** @brief Write the first symbol pass's maximum velocity glyph length property.
     * @param v New maximum velocity glyph length, expressed as logical pixels. Copied verbatim
     * without range, finiteness or semantic validation.
     * Writes the exact glyphLengthMaxPx key only for SingleSymbolRenderer. A missing pass is
     * created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setGlyphLengthMaxPx(qreal v);
    /** @brief Write the first symbol pass's velocity glyph spacing property.
     * @param v New velocity glyph spacing, expressed as logical pixels. Copied verbatim without
     * range, finiteness or semantic validation.
     * Writes the exact glyphSpacingPx key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setGlyphSpacingPx(qreal v);
    /** @brief Write the first symbol pass's velocity arrowhead size property.
     * @param v New velocity arrowhead size, expressed as logical pixels. Copied verbatim without
     * range, finiteness or semantic validation.
     * Writes the exact headSizePx key only for SingleSymbolRenderer. A missing pass is created as
     * a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setHeadSizePx(qreal v);
    /** @brief Write the first symbol pass's paint color property.
     * @param c New paint color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact color key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setColor(const QColor &c);
    /** @brief Write the first symbol pass's dry-depth arrow cutoff property.
     * @param v New dry-depth arrow cutoff, expressed as meters of water depth. Copied verbatim
     * without range, finiteness or semantic validation.
     * Writes the exact dryDepthCutoff key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setDryDepthCutoff(qreal v);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

/*!
 * \class PolygonSymbolStyleAdapter
 * \brief Single-Symbol panel surface for polygon archetypes —
 *        Subcatchments on the SWMM model layer and the polygon
 *        categories on the results layer.
 */
class PolygonSymbolStyleAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY changed)
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY changed)
    Q_PROPERTY(qreal fillOpacity READ fillOpacity WRITE setFillOpacity NOTIFY changed)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY changed)
    Q_PROPERTY(qreal outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY changed)
    Q_PROPERTY(bool showLabel READ showLabel WRITE setShowLabel NOTIFY changed)
    Q_PROPERTY(QFont labelFont READ labelFont WRITE setLabelFont NOTIFY changed)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY changed)

    Q_CLASSINFO("group:opacity", "Symbology")
    Q_CLASSINFO("group:fillColor", "Fill")
    Q_CLASSINFO("group:fillOpacity", "Fill")
    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidth", "Outline")
    Q_CLASSINFO("group:showLabel", "Labels")
    Q_CLASSINFO("group:labelFont", "Labels")
    Q_CLASSINFO("group:labelColor", "Labels")

public:
    /** @brief Bind a property view to a borrowed Rule and watch renderer replacements.
     * @param rule Optional borrowed rule; it must outlive the adapter because the stored raw
     * pointer is not cleared on destruction. Null rules expose fallbacks and ignore writes.
     * @param parent Optional QObject owner; Qt destroys this adapter with its parent.
     * Connects rendererReplaced() to changed(); all access/mutation occurs on the owning Qt
     * thread. The adapter never owns the rule or renderer. */
    explicit PolygonSymbolStyleAdapter(Rule *rule, QObject *parent = nullptr);
    /** @brief Destroy the adapter and its signal connections without destroying the borrowed rule. */
    ~PolygonSymbolStyleAdapter() override;

    /** @brief Read overall symbol opacity without applying layer opacity.
     * @return Stored SingleSymbolRenderer opacity, otherwise one for null rules or other renderer
     * types. No clamping occurs on read. */
    [[nodiscard]] qreal opacity() const;
    /** @brief Read the first symbol pass's fill color setting.
     * @return Owned property value; fallback QColor() when no compatible first-pass value exists.
     * Accepts QColor or parseable string color values. The color key is tried after the primary
     * key when needed. */
    [[nodiscard]] QColor fillColor() const;
    /** @brief Read the first symbol pass's fill opacity setting.
     * @return Owned property value in a dimensionless opacity; fallback 0.55 when no compatible
     * first-pass value exists.
     * Reads the exact fillOpacity key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal fillOpacity() const;
    /** @brief Read the first symbol pass's outline color setting.
     * @return Owned property value; fallback QColor() when no compatible first-pass value exists.
     * Accepts QColor or parseable string color values. Invalid/unparseable values use the
     * fallback. */
    [[nodiscard]] QColor outlineColor() const;
    /** @brief Read the first symbol pass's outline width setting.
     * @return Owned property value in logical pixels; fallback 0.5 when no compatible first-pass
     * value exists.
     * Reads the exact outlineWidth key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] qreal outlineWidth() const;
    /** @brief Read the first symbol pass's label visibility setting.
     * @return Owned property value; fallback false when no compatible first-pass value exists.
     * Reads the exact showLabel key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] bool showLabel() const;
    /** @brief Read the first symbol pass's label font setting.
     * @return Owned property value; fallback QFont() when no compatible first-pass value exists.
     * Reads the exact labelFont key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QFont labelFont() const;
    /** @brief Read the first symbol pass's label color setting.
     * @return Owned property value; fallback QColor(Qt::black) when no compatible first-pass value
     * exists.
     * Reads the exact labelColor key through QVariant conversion. Null rules,
     * non-SingleSymbolRenderer instances, empty stacks, missing keys and nonconvertible types use
     * the fallback. Range, enum and finiteness checks are not applied to converted values. */
    [[nodiscard]] QColor labelColor() const;

    /** @brief Clamp and replace overall symbol opacity for a single-symbol rule.
     * @param v Finite dimensionless opacity, clamped to [0,1]. No finiteness check occurs.
     * Null/other renderer types are ignored. Equal values under qFuzzyCompare after adding one are
     * no-ops; a change copies the style and notifies the rule, refreshing connected adapters. */
    void setOpacity(qreal v);
    /** @brief Write the first symbol pass's fill color property.
     * @param c New fill color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact fillColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setFillColor(const QColor &c);
    /** @brief Write the first symbol pass's fill opacity property.
     * @param v New fill opacity, expressed as a dimensionless opacity. Finite input is clamped to
     * [0,1]; nonfinite input is not validated.
     * Writes the exact fillOpacity key only for SingleSymbolRenderer. A missing pass is created as
     * a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setFillOpacity(qreal v);
    /** @brief Write the first symbol pass's outline color property.
     * @param c New outline color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact outlineColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setOutlineColor(const QColor &c);
    /** @brief Write the first symbol pass's outline width property.
     * @param v New outline width, expressed as logical pixels. Copied verbatim without range,
     * finiteness or semantic validation.
     * Writes the exact outlineWidth key only for SingleSymbolRenderer. A missing pass is created
     * as a default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setOutlineWidth(qreal v);
    /** @brief Write the first symbol pass's label visibility property.
     * @param v New label visibility. True enables the flag and false disables it; no other setting
     * is normalized. Writes the exact showLabel key only for SingleSymbolRenderer. A missing pass
     * is created as a default SymbolLayer; missing keys are inserted. An equal convertible stored
     * value is a no-op. A change replaces the copied style and calls
     * Rule::notifyRendererStateChanged(); other passes and renderer configuration remain. */
    void setShowLabel(bool v);
    /** @brief Write the first symbol pass's label font property.
     * @param f New label font. Copied without checking whether the requested font is installed.
     * Writes the exact labelFont key only for SingleSymbolRenderer. A missing pass is created as a
     * default SymbolLayer; missing keys are inserted. An equal convertible stored value is a
     * no-op. A change replaces the copied style and calls Rule::notifyRendererStateChanged();
     * other passes and renderer configuration remain. */
    void setLabelFont(const QFont &f);
    /** @brief Write the first symbol pass's label color property.
     * @param c New label color. Copied verbatim; invalid QColor values are accepted. Writes the
     * exact labelColor key only for SingleSymbolRenderer. A missing pass is created as a default
     * SymbolLayer; missing keys are inserted. An equal convertible stored value is a no-op. A
     * change replaces the copied style and calls Rule::notifyRendererStateChanged(); other passes
     * and renderer configuration remain. */
    void setLabelColor(const QColor &c);

    /** @brief Inspect the rule bound at construction.
     * @return Borrowed pointer, possibly null; the caller must ensure its lifetime exceeds this
     * adapter. */
    [[nodiscard]] Rule *rule() const { return m_rule; }

signals:
    /** @brief Notify editors to reread all exposed properties after the rule emits rendererReplaced().
     * Effective property writes call notifyRendererStateChanged(), which emits rendererReplaced()
     * and ruleChanged(). Equal writes normally emit nothing; one setter can change multiple keys
     * and produce multiple notifications. */
    void changed();

private slots:
    /** @brief Forward a rule renderer/state replacement to the adapter's changed() signal. */
    void onRendererReplaced();

private:
    Rule *m_rule = nullptr;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SYMBOLSTYLEADAPTER_H
