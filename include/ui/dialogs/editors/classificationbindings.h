/**
 * @file      classificationbindings.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice US.1 (UNIFIED_STYLING plan S1) — adapter interface that lets
 *         one ClassificationEditor drive any classified visual.
 *
 *         The editor knows nothing about renderers or style bags. It reads a
 *         ClassificationScheme via scheme(), writes edits via setScheme(),
 *         and asks the binding for the data range + a value sample to run
 *         auto-classification. Concrete bindings translate that onto:
 *
 *           - GraduatedRendererBinding — the 1D per-kind GraduatedRenderer
 *             (maps scheme fields onto the renderer's existing accessors,
 *             pushing only what changed so unrelated state — e.g. the ramp
 *             when only the class count changed — is never clobbered).
 *           - SublayerSchemeBinding — a SublayerStyle bag that stores a
 *             ClassificationScheme by value (std::function getter/setter +
 *             sample provider; the same lambda-adapter idiom as
 *             ColorSourceBindings in swmm2dresultsstylepanel.cpp).
 *           - RasterSchemeBinding — a GISRasterLayer's live
 *             GraduatedRasterRenderer (scheme + band statistics + cached
 *             edges; every push reclassifies against the band sample and
 *             invalidates the tile cache).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_CLASSIFICATIONBINDINGS_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_CLASSIFICATIONBINDINGS_H

#include "render/classificationscheme.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

class GISRasterLayer;

namespace OpenSWMM::Render
{
class GraduatedRasterRenderer;
class GraduatedRenderer;
class IFeatureRenderer;
}

namespace openswmmvis::ui
{

/*!
 * \class IClassificationBinding
 * \brief What ClassificationEditor talks to instead of a concrete model.
 */
class IClassificationBinding
{
public:
    /**
     * @brief Destroy a binding polymorphically and release its owned callback/value storage.
     * Does not imply ownership of referenced layer/renderer targets or rollback prior edits.
     */
    virtual ~IClassificationBinding() = default;

    // ── Core ───────────────────────────────────────────────────────────
    /**
     * @brief Read the editable classification configuration by value.
     * @return Detached scheme, potentially a cached mirror according to the concrete binding;
     * no borrowed renderer reference. Caller follows that binding's resynchronization policy.
     */
    [[nodiscard]] virtual OpenSWMM::Render::ClassificationScheme scheme() const = 0;
    /**
     * @brief Submit a classification edit through the concrete model's normal change/notification
     * channel.
     * @param s Borrowed desired configuration consumed synchronously; implementations must
     * copy retained state rather than keep this reference. Concrete bindings define ignored
     * fields, normalization, unavailable-target behavior and notification side effects.
     * No common success result, undo, snapshot, rollback or thread synchronization.
     */
    virtual void setScheme(const OpenSWMM::Render::ClassificationScheme &s) = 0;

    /**
     * @brief Provide a detached numeric sample for data-dependent auto-classification.
     * @return Owned sample vector; empty is permitted and the editor can fall back to equal
     * spacing. Concrete providers define sampling/decimation and no-data filtering; caller
     * must not assume every source value is present or a universal finite-value guarantee.
     */
    [[nodiscard]] virtual QVector<double> sampleValues() const = 0;

    /**
     * @brief Request reclassification by repushing the current scheme through setScheme().
     * Default implementation provides no explicit cache clearing; concrete bindings may
     * refresh ranges/samples or reinstall a renderer. No uniform completion/error result.
     */
    virtual void autoClassify() { setScheme(scheme()); }

    /**
     * @brief Read the outer scalar domain in the selected attribute's native units.
     * @return Owned lower/upper pair, with concrete fallback/cache policy. Does not imply a
     * fresh scan, finite endpoints or automatic normalization across every implementation.
     */
    [[nodiscard]] virtual QPair<double, double> dataRange() const = 0;

    /**
     * @brief Offer authoritative cached class edges when the target supplies them.
     * @return Empty by default, instructing the editor to derive edges from scheme/range/
     * samples. Nonempty overrides should describe endpoint-inclusive edges in class order;
     * this interface neither sorts nor validates values.
     */
    [[nodiscard]] virtual QVector<double> computedEdges() const { return {}; }

    // ── Optional capabilities (sensible no-op defaults) ────────────────
    /**
     * @brief Advertise whether the editor should expose continuous/classified mode choice.
     * @return False by default; capability only, without changing classification mode.
     */
    [[nodiscard]] virtual bool supportsContinuousMode() const { return false; }

    /**
     * @brief Advertise animated fixed-run/per-frame/user-range controls.
     * @return False by default; editor gives this capability priority over custom-range
     * checkbox support. Does not compute or update any range.
     */
    [[nodiscard]] virtual bool supportsRangeModes() const { return false; }

    /**
     * @brief Advertise a custom-range checkbox for targets without animated range modes.
     * @return False by default; capability only, without changing bounds or validating them.
     */
    [[nodiscard]] virtual bool supportsCustomRange() const { return false; }

    /**
     * @brief Offer display/canonical-name pairs for an optional attribute picker.
     * @return Owned empty list by default, hiding the picker; no target or field references
     * are retained in the returned values.
     */
    [[nodiscard]] virtual QVector<QPair<QString, QString>> availableAttributes() const
    {
        return {};
    }
    /**
     * @brief Read the current canonical classification attribute if supported.
     * @return Owned empty string by default; not a guarantee that any named field exists.
     */
    [[nodiscard]] virtual QString attribute() const { return {}; }
    /**
     * @brief Ignore attribute changes in bindings that do not implement attribute selection.
     * @param name Borrowed requested name, ignored and not retained by this default method.
     * No state change, notification or success result.
     */
    virtual void setAttribute(const QString &name) {}
};

// ───────────────────────────────────────────────────────────────────────
/*!
 * \class GraduatedRendererBinding
 * \brief Bridges the editor's ClassificationScheme onto a 1D per-kind
 *        GraduatedRenderer without changing the renderer's JSON.
 *
 *        The host (KindRendererPanel) supplies three closures: fetch the
 *        live renderer, install a replacement (its existing three-way
 *        Rule/model/results dispatch), and sample the layer's values. The
 *        binding caches a scheme mirror so setScheme can push only the
 *        deltas — preserving today's behaviour where, e.g., changing the
 *        class count never resets the ramp.
 */
class GraduatedRendererBinding final : public IClassificationBinding
{
public:
    using RendererGetter = std::function<OpenSWMM::Render::GraduatedRenderer *()>;
    using RendererInstaller =
        std::function<void(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>)>;
    using AttributeList = std::function<QVector<QPair<QString, QString>>()>;
    /*! Whether the animation range-mode row applies right now — depends on
     *  host type AND the currently selected attribute's dynamism, so it is a
     *  closure re-queried on each refresh rather than a fixed flag. */
    using RangeModeGate = std::function<bool()>;

    /**
     * @brief Store callbacks and seed a cached scheme mirror from the current graduated renderer.
     * @param getRenderer Optional callable returning a borrowed live renderer or null each
     * lookup. Its captured host must outlive the binding/use; renderer is not owned/cached.
     * @param installRenderer Optional callable consuming ownership of a cloned replacement
     * IFeatureRenderer and performing the host's normal install/repaint path. Empty means
     * clones are discarded, yet setScheme can still advance the mirror.
     * @param attributeList Optional callable returning owned display/canonical-name pairs.
     * @param rangeModesEnabled Optional dynamically queried capability gate; captures must
     * remain valid and must not destroy this binding during a call.
     * Moves callbacks into owned storage and invokes resync immediately. All callbacks run
     * synchronously, without exception handling, locks, undo or rollback; caller serializes
     * GUI/model access and resyncs after external renderer changes/Cancel restoration.
     */
    GraduatedRendererBinding(RendererGetter getRenderer, RendererInstaller installRenderer,
                             AttributeList attributeList, RangeModeGate rangeModesEnabled);

    /**
     * @brief Replace the cached scheme with a fresh translation of the currently fetched
     * renderer.
     * Missing getter/renderer yields default configuration forced to Classified. Does not
     * install a renderer, gather samples or notify views.
     */
    void resync();

    /**
     * @brief Return the cached scheme mirror without querying the live renderer.
     * @return Detached copy; may differ from live state until resync or after an edit with
     * no installer. No target mutation.
     */
    OpenSWMM::Render::ClassificationScheme scheme() const override { return m_scheme; }
    /**
     * @brief Clone the current graduated renderer and apply differences against the cached
     * mirror.
     * @param s Borrowed desired scheme, copied into the mirror after a handled call.
     * Pushes binner/method/count/manual breaks, range mode/bounds, ramp payload/inversion/
     * endpoint colors and class-color overrides only when changed from the mirror. FixedUser
     * bounds are sorted and equal values widened by1; finiteness is not validated here.
     * Changed classification/range clears cached breaks; ramp replacement preserves current
     * renderer range. Installs even a no-difference clone when installer exists. Missing
     * renderer/incompatible clone still updates mirror without installation; mode and unrelated
     * scheme fields are not independently applied. No automatic resync, success/undo result
     * or rollback if caller callbacks fail; stale mirrors require explicit resync.
     */
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s) override;
    /**
     * @brief Leave data sampling to the host layer's renderer-rebuild path.
     * @return Owned empty vector unconditionally; no scan or layer callback is performed.
     */
    QVector<double> sampleValues() const override { return {}; } // layer samples internally
    /**
     * @brief Clone a live renderer, clear graduated cached breaks, install if possible and resync
     * the mirror.
     * No-op with missing renderer. Installer receives clone ownership; if absent, clone is
     * discarded and resync sees unchanged live state. Layer installation performs any actual
     * sampling/rebuild; no synchronous sample gather or error result here.
     */
    void autoClassify() override;
    /**
     * @brief Read the freshly fetched renderer's current ramp endpoints.
     * @return Owned min/max pair or0/1 fallback if unavailable, without scanning data,
     * sorting endpoints or consulting the cached desired scheme.
     */
    QPair<double, double> dataRange() const override;
    /**
     * @brief Copy current cached interior breaks between live ramp endpoints.
     * @return Owned endpoint-inclusive vector, or empty if renderer absent/lastBreaks empty,
     * including the single-bin case. Preserves stored order/duplicates without validation;
     * does not classify or gather samples.
     */
    QVector<double> computedEdges() const override;

    /**
     * @brief Keep1D graduated rendering classified-only in the shared editor.
     * @return False independent of renderer availability; no mode change.
     */
    bool supportsContinuousMode() const override { return false; }
    /**
     * @brief Reevaluate the captured dynamic range-mode capability on every query.
     * @return Gate result, false if no gate; no cached capability or range mutation.
     * Caller ensures callback captures remain alive; exceptions propagate.
     */
    bool supportsRangeModes() const override { return m_rangeGate ? m_rangeGate() : false; }
    /**
     * @brief Suppress the separate custom-range checkbox for this animated-range binding.
     * @return Always false; FixedUser is represented through the range-mode controls.
     */
    bool supportsCustomRange() const override { return false; } // range-mode combo covers it
    /**
     * @brief Query the optional field-list callback without retaining its returned container.
     * @return Owned display/canonical-name pairs, empty for no callback. No automatic field
     * validation, sorting or numeric-type filtering is performed by this binding.
     */
    QVector<QPair<QString, QString>> availableAttributes() const override;
    /**
     * @brief Read the current live renderer's canonical classification field.
     * @return Owned field name or empty if getter/renderer unavailable; no mirror lookup.
     */
    QString attribute() const override;
    /**
     * @brief Clone/install a changed classification attribute, clear cached breaks and resync.
     * @param name Borrowed canonical field name copied by the renderer, without validating
     * provider membership, emptiness or type. Missing renderer or identical name is a no-op.
     * A compatible clone receives the field; installer is optional. No error result, undo
     * or retained caller string reference.
     */
    void setAttribute(const QString &name) override;

private:
    /**
     * @brief Translate the current live graduated renderer into the editor scheme.
     * @return Owned Classified scheme with binner, named/exact-stop custom ramp, range mode/
     * bounds and class overrides; default Classified scheme when absent. First builtin
     * whose stops exactly match wins, ignoring range metadata. No independent inversion
     * reconstruction, sample gathering, installation or target mutation.
     */
    [[nodiscard]] OpenSWMM::Render::ClassificationScheme schemeFromRenderer() const;

    RendererGetter m_get;
    RendererInstaller m_install;
    AttributeList m_attributes;
    RangeModeGate m_rangeGate;
    OpenSWMM::Render::ClassificationScheme m_scheme;
};

// ───────────────────────────────────────────────────────────────────────
/*!
 * \class SublayerSchemeBinding
 * \brief Drives a ClassificationScheme stored by value on a style bag.
 *
 *        Pure std::function adapter — the bag supplies a getter, a setter
 *        (which must call its setDirty()/styleChanged so the renderer +
 *        legend refresh), a sample provider, and a data-range provider.
 */
class SublayerSchemeBinding final : public IClassificationBinding
{
public:
    using Getter = std::function<OpenSWMM::Render::ClassificationScheme()>;
    using Setter = std::function<void(const OpenSWMM::Render::ClassificationScheme &)>;
    using SampleProvider = std::function<QVector<double>()>;
    using RangeProvider = std::function<QPair<double, double>()>;

    /**
     * @brief Store a style-bag classification adapter and fixed editor capability flags.
     * @param getter Required callable returning a detached scheme; empty throws bad_function_call
     * when scheme() invokes it. Captured target remains caller-owned and must outlive use.
     * @param setter Required callable consuming a borrowed scheme synchronously and issuing
     * normal model dirty/style notifications; it must copy anything retained. Empty throws
     * when setScheme() invokes it.
     * @param sampleProvider Optional synchronous callable returning owned numeric samples;
     * empty yields no samples.
     * @param rangeProvider Optional synchronous callable returning owned scalar bounds;
     * empty yields0/1.
     * @param supportsContinuousMode Fixed continuous/classified control capability.
     * @param supportsRangeModes Fixed animation-range capability; custom-range checkbox is
     * its inverse. Callbacks move into binding storage, with no target ownership, locking,
     * exception handling, validation, snapshot or automatic destruction rollback.
     */
    SublayerSchemeBinding(Getter getter, Setter setter, SampleProvider sampleProvider,
                          RangeProvider rangeProvider, bool supportsContinuousMode,
                          bool supportsRangeModes);

    /**
     * @brief Invoke the required getter for the current style-bag scheme.
     * @return Owned scheme; no cache. Empty callable or callback exception propagates.
     */
    OpenSWMM::Render::ClassificationScheme scheme() const override { return m_get(); }
    /**
     * @brief Forward a configuration directly through the required setter callback.
     * @param s Borrowed desired scheme consumed synchronously; callback defines retention,
     * validation/notification semantics. No mirror, normalization, undo or error result;
     * empty callable/callback exceptions propagate.
     */
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s) override { m_set(s); }
    /**
     * @brief Request current samples from the optional provider.
     * @return Owned vector or empty when provider absent. No filtering/decimation here;
     * provider exceptions propagate.
     */
    QVector<double> sampleValues() const override
    {
        return m_samples ? m_samples() : QVector<double>{};
    }
    /**
     * @brief Request current outer bounds from the optional provider.
     * @return Owned pair or0/1 when absent; no sorting/finite validation or scan here.
     */
    QPair<double, double> dataRange() const override
    {
        return m_range ? m_range() : QPair<double, double>{0.0, 1.0};
    }

    /**
     * @brief Read the fixed continuous-mode capability supplied at construction.
     * @return Stored flag, independent of live model state; no mutation.
     */
    bool supportsContinuousMode() const override { return m_supportsContinuous; }
    /**
     * @brief Read the fixed animated-range capability supplied at construction.
     * @return Stored flag, without querying callback targets.
     */
    bool supportsRangeModes() const override { return m_supportsRangeModes; }
    /**
     * @brief Offer the static custom-range checkbox exactly when animated range modes are
     * disabled.
     * @return Negation of the stored range-mode capability; no bound validation or mutation.
     */
    bool supportsCustomRange() const override { return !m_supportsRangeModes; }

private:
    Getter m_get;
    Setter m_set;
    SampleProvider m_samples;
    RangeProvider m_range;
    bool m_supportsContinuous = false;
    bool m_supportsRangeModes = false;
};

// ───────────────────────────────────────────────────────────────────────
/*!
 * \class RasterSchemeBinding
 * \brief Drives the GraduatedRasterRenderer live on a GISRasterLayer.
 *
 *        The renderer is fetched from the layer on every call (never
 *        cached — the dialog's Cancel / undo path swaps it). dataRange() is
 *        the RENDERER's dataMin/dataMax (not fresh band statistics) so the
 *        editor's table agrees with what the map paints after a restore
 *        from JSON; computedEdges() mirrors the renderer's cached edges so
 *        the table never re-samples the raster. setScheme() reclassifies
 *        against the layer's (cached) band sample and notifies the layer,
 *        which drops its tiles and repaints. When the live renderer is not
 *        a GraduatedRasterRenderer (Paletted / RGB active) the binding is
 *        inert: scheme() is a default and setScheme() no-ops.
 */
class RasterSchemeBinding final : public IClassificationBinding
{
public:
    /**
     * @brief Borrow a raster layer without caching its replaceable renderer.
     * @param layer Optional raw layer pointer; nonnull layer must outlive the binding and
     * all calls, since it is not QPointer-tracked. Null makes data access use fallbacks.
     * No layer/renderer ownership, callbacks, snapshot or automatic invalidation wiring.
     */
    explicit RasterSchemeBinding(GISRasterLayer *layer) : m_layer(layer) {}

    /**
     * @brief Read the currently installed graduated raster scheme by value.
     * @return Owned scheme or default for null layer/non-graduated renderer; never a cached
     * renderer reference or fresh band-statistics result.
     */
    OpenSWMM::Render::ClassificationScheme scheme() const override;
    /**
     * @brief Mutate a live graduated raster renderer, reclassify from the current band's sample
     * and notify its layer.
     * @param s Borrowed desired configuration copied by the renderer; no caller ref retained.
     * No-op for unavailable/non-graduated renderer. Notification invalidates layer tiles/
     * requests repaint; sampling may use the layer cache and can be costly. No undo, snapshot,
     * transaction, success result or exception handling; serialize GUI/layer access.
     */
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s) override;
    /**
     * @brief Request samples of the layer's current render band even if its renderer is
     * nongraduated.
     * @return Owned vector from the layer, empty if no layer. Layer defines cache/decimation/
     * no-data policy; binding does not scan/filter separately.
     */
    QVector<double> sampleValues() const override;
    /**
     * @brief Refresh graduated renderer data bounds from finite band statistics, reclassify and
     * notify the layer.
     * No-op for unavailable/nongraduated renderer. Uses current render-band sample; explicit
     * custom scheme range still wins within renderer policy. Nonfinite statistics leave
     * old data bounds. No range sorting, undo, snapshot or success result here.
     */
    void autoClassify() override;
    /**
     * @brief Read current graduated renderer dataMin/dataMax rather than fresh band statistics.
     * @return Owned bounds or0/1 fallback; no normalization or cache rebuild.
     */
    QPair<double, double> dataRange() const override;
    /**
     * @brief Copy the current graduated raster renderer's cached edges without resampling.
     * @return Owned vector or empty for unavailable/nongraduated renderer; preserves stored
     * order and does not classify or validate edge count.
     */
    QVector<double> computedEdges() const override;

    /**
     * @brief Advertise raster continuous/classified controls even while another renderer is
     * active.
     * @return Always true; no live renderer/type check or mode mutation.
     */
    bool supportsContinuousMode() const override { return true; }
    /**
     * @brief Hide animation-range controls for this raster binding.
     * @return Always false, independent of layer/renderer availability.
     */
    bool supportsRangeModes() const override { return false; }
    /**
     * @brief Advertise raster custom-range controls independently of renderer availability.
     * @return Always true; no bounds mutation or validation.
     */
    bool supportsCustomRange() const override { return true; }

private:
    /**
     * @brief Resolve the current raster renderer dynamically on each call.
     * @return Borrowed graduated renderer pointer or null for missing layer/type mismatch;
     * expires on layer destruction or renderer replacement. No ownership transfer/cache.
     */
    [[nodiscard]] OpenSWMM::Render::GraduatedRasterRenderer *renderer() const;

    GISRasterLayer *m_layer = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_CLASSIFICATIONBINDINGS_H
