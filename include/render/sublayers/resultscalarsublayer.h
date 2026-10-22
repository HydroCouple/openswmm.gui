/**
 * @file      resultscalarsublayer.h
 * @brief     Styling of scalar result fields independently of application chrome.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_RESULTSCALARSUBLAYER_H
#define OPENSWMM_RENDER_RESULTSCALARSUBLAYER_H

#include "io/mesh2dscalarframe.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include <memory>

namespace OpenSWMM::Render
{

// Scientific data colors remain independent of the application's chrome theme.
class ResultScalarStyle : public ScalarFillStyle
{
    Q_OBJECT
public:
    enum class Presentation { Fill, Contours, Labels };
    Q_ENUM(Presentation)
private:
    Q_PROPERTY(OpenSWMM::Render::ResultScalarStyle::Presentation presentation READ presentation
                   WRITE setPresentation NOTIFY styleChanged)
    Q_PROPERTY(QColor missingColor READ missingColor WRITE setMissingColor NOTIFY styleChanged)
    Q_PROPERTY(
        QColor waterlessColor READ waterlessColor WRITE setWaterlessColor NOTIFY styleChanged)
    Q_PROPERTY(QColor notApplicableColor READ notApplicableColor WRITE setNotApplicableColor NOTIFY
                   styleChanged)
    Q_CLASSINFO("group:missingColor", "Data availability")
    Q_CLASSINFO("group:waterlessColor", "Data availability")
    Q_CLASSINFO("group:notApplicableColor", "Data availability")
public:
    /** @brief Construct scalar styling with a child contour style and semantic-key binding.
     * @param parent Optional QObject owner. Starts in Fill presentation, clears the
     * base depth attribute and uses PerFrameAutoStretch. Child styleChanged forwards
     * to this style's setDirty(); ordinary access occurs on the owning Qt thread. */
    explicit ResultScalarStyle(QObject *parent = nullptr);
    /** @brief Read the selected rendering presentation.
     * @return Fill initially, or the last accepted Contours/Labels selection. */
    Presentation presentation() const { return m_presentation; }
    /** @brief Select an accepted presentation and notify on a change.
     * @param value Fill, Contours or Labels; unknown enum integers are ignored. */
    void setPresentation(Presentation value);
    /** @brief Borrow the owned contour/label style.
     * @return Child pointer valid until this style is destroyed; do not delete/reparent. */
    IsolineStyle *contourStyle() const { return m_contourStyle; }
    /** @brief Select the scheme associated with the current presentation.
     * @return Borrowed contour scheme only for Contours, otherwise the base fill scheme
     * (including Labels). Replaced by the corresponding style setters/JSON readers. */
    const ClassificationScheme &rangeScheme() const
    {
        return m_presentation == Presentation::Contours ? m_contourStyle->scheme() : scheme();
    }
    /** @brief Generate contour levels from the contour scheme's effective range.
     * @param minimum Automatic lower bound in the selected variable unit.
     * @param maximum Automatic upper bound in the same unit.
     * @param samples Borrowed values for data-dependent classification.
     * @return IsolineStyle::levelsForRange() output after contour custom-range selection.
     * This helper uses the contour scheme regardless of current presentation. */
    std::vector<double> contourLevels(double minimum, double maximum,
                                      const QVector<double> &samples) const
    {
        const auto range = m_contourStyle->scheme().effectiveRange(minimum, maximum);
        return m_contourStyle->levelsForRange(range.first, range.second, samples);
    }
    /** @brief Read the missing/invalid-data fallback color.
     * @return Stored color, initially RGBA(145,145,145,150). */
    QColor missingColor() const { return m_missing; }
    /** @brief Read the explicitly waterless-cell color.
     * @return Stored color, initially RGBA(230,230,230,90). */
    QColor waterlessColor() const { return m_waterless; }
    /** @brief Read the not-applicable-cell color.
     * @return Stored color, initially transparent. */
    QColor notApplicableColor() const { return m_notApplicable; }
    /** @brief Replace the missing/invalid-data color and notify on change.
     * @param color Valid QColor copied into storage; invalid or equal input is ignored. */
    void setMissingColor(const QColor &color);
    /** @brief Replace the waterless-cell color and notify on change.
     * @param color Valid QColor copied into storage; invalid or equal input is ignored. */
    void setWaterlessColor(const QColor &color);
    /** @brief Replace the not-applicable-cell color and notify on change.
     * @param color Valid QColor copied into storage; invalid or equal input is ignored. */
    void setNotApplicableColor(const QColor &color);
    /** @brief Serialize fill classification plus presentation, contour style and availability colors.
     * @return Independent object with ARGB availability colors; no frame/descriptor is stored. */
    QJsonObject toJson() const override;
    /** @brief Load scalar-result styling and emit one final styleChanged from this object.
     * @param json Borrowed object; base partial-load rules apply. Missing/mistyped
     * presentation selects Fill; unknown presentation integers retain the old selection.
     * Only an object contourStyle is loaded. Invalid/absent availability colors retain
     * current values. This object's intermediate signals are blocked; the child contour
     * style can still emit its own signals. Final styleChanged is unconditional. */
    void fromJson(const QJsonObject &json) override;

private:
    Presentation m_presentation = Presentation::Fill;
    IsolineStyle *m_contourStyle = nullptr;
    QColor m_missing{145, 145, 145, 150};
    QColor m_waterless{230, 230, 230, 90};
    QColor m_notApplicable{Qt::transparent};
};

// Additional flat cell-result pass. attribute() stores the source's semantic
// variable key, never a species-array index. Host layer owns frame acquisition,
// persistence and CPU geometry; the QSG renderer uses the same cellColors().
class ResultScalarSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a visible, opacity-one result pass with its owned style.
     * @param id Stable semantic pass ID, moved into storage; empty generates
     * results2d.variable. plus a brace-free UUID. Uniqueness of supplied IDs is unchecked.
     * @param parent Optional QObject owner; destroys this pass and style. Style changes
     * clear the published frame and emit invalidated(). Ordinary access is on the Qt
     * owning thread; only presentedFrame()/publishPresentedFrame() provide atomic publication. */
    explicit ResultScalarSublayer(QString id, QObject *parent = nullptr);
    /** @brief Identify the scene pass kind.
     * @return ColorRampFillKind for every presentation. */
    Kind kind() const override { return ColorRampFillKind; }
    /** @brief Copy the immutable pass identifier.
     * @return Supplied or generated construction-time ID. */
    QString id() const override { return m_id; }
    /** @brief Describe the bound result variable, units and presentation.
     * @return Unavailable result plus the key if the descriptor mismatches/has no label;
     * otherwise descriptor label, known units (or units unknown), and contour/label suffix. */
    QString displayName() const override;
    /** @brief Read local visibility.
     * @return Stored flag, initially true. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace visibility, clearing the presented frame when hidden.
     * @param value New flag; equal values are no-ops. A change emits invalidated(). */
    void setVisible(bool value) override;
    /** @brief Read local opacity.
     * @return Finite stored multiplier in [0,1], initially one. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Set finite clamped opacity and invalidate on exact inequality.
     * @param value Multiplier; nonfinite input is ignored, finite input clamps to [0,1].
     * A change to zero clears the presented frame; positive changes retain it. */
    void setOpacity(qreal value) override;
    /** @brief Report time-dependent result rendering.
     * @return True without acquiring data or starting a job. */
    bool isDynamic() const override { return true; }
    /** @brief Borrow the owned scalar-result style through the base interface.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    SublayerStyle *style() override { return m_style; }
    /** @brief Borrow the concrete owned scalar-result style.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    ResultScalarStyle *fillStyle() const { return m_style; }
    /** @brief Copy the bound semantic variable key.
     * @return Style attribute string, never interpreted here as a species-array index. */
    QString variableKey() const { return m_style->attribute(); }
    /** @brief Bind a semantic result key through the style attribute setter.
     * @param key Exact key copied without validation/resolution; on change, style
     * notification clears the published frame and invalidates the pass. */
    void setVariableKey(const QString &key) { m_style->setAttribute(key); }
    /** @brief Borrow the latest metadata descriptor.
     * @return Stored descriptor, replaced by setDescriptor(); it can differ from variableKey(). */
    const openswmmvis::io::Mesh2DResultVariable &descriptor() const { return m_descriptor; }
    /** @brief Copy source metadata, clear the published frame and invalidate unconditionally.
     * @param descriptor Borrowed metadata copied without checking its key, units or availability. */
    void setDescriptor(const openswmmvis::io::Mesh2DResultVariable &descriptor);
    // Render-thread publication only transfers immutable data: no QObject
    // mutation or signals. GUI legends atomically load the displayed frame.
    /** @brief Atomically acquire shared ownership of the last published immutable frame.
     * @return Shared snapshot, possibly null. Safe to retain after another publication;
     * this does not synchronize descriptor/style/visibility access. */
    std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> presentedFrame() const;
    /** @brief Atomically replace the displayed immutable-frame snapshot without Qt signals.
     * @param frame Shared immutable frame moved into atomic storage, or null to clear.
     * No key/generation/visibility check occurs. Render-thread publication is supported;
     * callers separately coordinate style/descriptor state and reject stale jobs. */
    void publishPresentedFrame(std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> frame);
    /** @brief Build one display color per frame value using status and the fill scheme.
     * @param frame Borrowed immutable frame; descriptor key must match variableKey()
     * and error must be empty. Missing status entries are treated as Missing.
     * @return Empty when hidden, zero-opacity, unbound, mismatched or errored. Otherwise
     * one color per value: waterless/not-applicable colors take priority over numeric
     * validity; missing/nonfinite values or invalid classification use missingColor.
     * Valid values use class or continuous colors, multiplied by local opacity. Range/
     * samples are derived from finite Valid cells only when cached metadata is absent/invalid.
     * This uses the fill scheme even in Contours/Labels mode and publishes nothing.
     * Read on the owning thread or with an externally synchronized immutable style snapshot. */
    QVector<QColor> cellColors(const openswmmvis::io::Mesh2DScalarFrame &frame) const;
    /** @brief Build legend rows using descriptor metadata and an otherwise empty frame.
     * @return Delegated preview without consulting presentedFrame(); automatic numeric
     * bounds are usually unavailable unless the scheme provides a custom range. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Build status and value legend rows for an explicit immutable frame.
     * @param frame Borrowed frame used for range/sample derivation and key/error checks.
     * @return Empty when hidden. Matching Contours/Labels produce one line-style row
     * and return early. Fill mode produces classified or six continuous previews
     * (one for a constant range), or an unavailable/invalid-classification row, followed
     * by Missing, Waterless and Not applicable swatches. Rows carry units, id and local
     * opacity; zero opacity does not suppress the legend. The caller selects the
     * appropriate frame; this method neither loads nor publishes the atomic snapshot. */
    QList<LegendSymbolItem>
    legendSymbolItems(const openswmmvis::io::Mesh2DScalarFrame &frame) const;
    /** @brief Preserve an existing node without generating geometry in this hook.
     * @param existing Borrowed node returned unchanged, including null.
     * @par Context
     * The unnamed SublayerContext reference is unused; host renderers build geometry.
     * @return existing without allocation or ownership transfer. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override
    {
        return existing;
    }

private:
    /** @brief Select descriptor units for display without converting numeric values.
     * @param descriptor Borrowed metadata; units are used only when unitsKnown is
     * true and the string is nonempty.
     * @return Exact units text, otherwise translated units unknown. */
    QString unitsLabel(const openswmmvis::io::Mesh2DResultVariable &descriptor) const;
    std::shared_ptr<const openswmmvis::io::Mesh2DScalarFrame> m_presentedFrame;
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1;
    ResultScalarStyle *m_style;
    openswmmvis::io::Mesh2DResultVariable m_descriptor;
};

} // namespace OpenSWMM::Render
#endif
