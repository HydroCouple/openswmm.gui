/*!
 * \file   classificationbindings.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.1 (UNIFIED_STYLING plan S1) — adapter interface that lets
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

namespace OpenSWMM::Render {
class GraduatedRenderer;
class IFeatureRenderer;
}

namespace openswmmvis::ui {

/*!
 * \class IClassificationBinding
 * \brief What ClassificationEditor talks to instead of a concrete model.
 */
class IClassificationBinding
{
public:
    virtual ~IClassificationBinding() = default;

    // ── Core ───────────────────────────────────────────────────────────
    [[nodiscard]] virtual OpenSWMM::Render::ClassificationScheme scheme() const = 0;
    /*! MUST route through the model's normal change channel (renderer
     *  install / bag setter) so every view repaints. */
    virtual void setScheme(const OpenSWMM::Render::ClassificationScheme &) = 0;

    /*! Values feeding Quantile / Jenks / StdDev auto-classification. Large
     *  sources should decimate. Empty is legal (the editor degrades to
     *  equal spacing). */
    [[nodiscard]] virtual QVector<double> sampleValues() const = 0;

    /*! Force a re-derive of class edges from current data, even when the
     *  scheme config is unchanged (the data itself may have changed). The
     *  default re-pushes the current scheme through the change channel; the
     *  GraduatedRendererBinding clears the renderer's cached breaks so the
     *  layer re-classifies against fresh samples. */
    virtual void autoClassify() { setScheme(scheme()); }

    /*! Outer data range the scheme classifies over (e.g. [dryDepth,
     *  maxDepth] or a field's min/max). */
    [[nodiscard]] virtual QPair<double, double> dataRange() const = 0;

    /*! The model's authoritative class edges (N+1 ascending, endpoints
     *  included) when it already holds them — e.g. a GraduatedRenderer's
     *  data-derived lastBreaks. Returning empty (the default) tells the
     *  editor to recompute edges itself via ClassificationScheme::levelEdges
     *  over dataRange() + sampleValues(). Lets the 1D table mirror the map's
     *  exact breaks without re-running the layer's sample gathering. */
    [[nodiscard]] virtual QVector<double> computedEdges() const { return {}; }

    // ── Optional capabilities (sensible no-op defaults) ────────────────
    /*! Continuous/Classified toggle is offered only when true. 1D graduated
     *  symbols are always classified → false; depth fill → true. */
    [[nodiscard]] virtual bool supportsContinuousMode() const { return false; }

    /*! Animation range-mode row (Fixed over run / Per-frame / Fixed user)
     *  is offered only when true — the 1D-results idiom, where FixedUser
     *  reveals the min/max sub-row. */
    [[nodiscard]] virtual bool supportsRangeModes() const { return false; }

    /*! "Use custom range" checkbox + min/max is offered only when true —
     *  the static / 2D idiom (no animation range modes). Mutually exclusive
     *  with supportsRangeModes() in the editor (range modes win). */
    [[nodiscard]] virtual bool supportsCustomRange() const { return false; }

    /*! Attribute picker is shown only when this returns a non-empty list.
     *  Pairs of (displayName, canonicalName). */
    [[nodiscard]] virtual QVector<QPair<QString, QString>> availableAttributes() const { return {}; }
    [[nodiscard]] virtual QString attribute() const { return {}; }
    virtual void setAttribute(const QString &) {}
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
    using RendererGetter   = std::function<OpenSWMM::Render::GraduatedRenderer *()>;
    using RendererInstaller = std::function<void(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>)>;
    using AttributeList    = std::function<QVector<QPair<QString, QString>>()>;
    /*! Whether the animation range-mode row applies right now — depends on
     *  host type AND the currently selected attribute's dynamism, so it is a
     *  closure re-queried on each refresh rather than a fixed flag. */
    using RangeModeGate    = std::function<bool()>;

    GraduatedRendererBinding(RendererGetter getRenderer,
                             RendererInstaller installRenderer,
                             AttributeList attributeList,
                             RangeModeGate rangeModesEnabled);

    /*! Re-read the cached scheme from the live renderer. Call after the
     *  host's refreshFromModel / Cancel rollback so the editor reflects the
     *  renderer again. */
    void resync();

    OpenSWMM::Render::ClassificationScheme scheme() const override { return m_scheme; }
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s) override;
    QVector<double> sampleValues() const override { return {}; } // layer samples internally
    void autoClassify() override;
    QPair<double, double> dataRange() const override;
    QVector<double> computedEdges() const override;

    bool supportsContinuousMode() const override { return false; }
    bool supportsRangeModes() const override { return m_rangeGate ? m_rangeGate() : false; }
    bool supportsCustomRange() const override { return false; } // range-mode combo covers it
    QVector<QPair<QString, QString>> availableAttributes() const override;
    QString attribute() const override;
    void setAttribute(const QString &name) override;

private:
    [[nodiscard]] OpenSWMM::Render::ClassificationScheme schemeFromRenderer() const;

    RendererGetter    m_get;
    RendererInstaller m_install;
    AttributeList     m_attributes;
    RangeModeGate     m_rangeGate;
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
    using Getter        = std::function<OpenSWMM::Render::ClassificationScheme()>;
    using Setter        = std::function<void(const OpenSWMM::Render::ClassificationScheme &)>;
    using SampleProvider = std::function<QVector<double>()>;
    using RangeProvider  = std::function<QPair<double, double>()>;

    SublayerSchemeBinding(Getter getter, Setter setter,
                          SampleProvider sampleProvider, RangeProvider rangeProvider,
                          bool supportsContinuousMode, bool supportsRangeModes);

    OpenSWMM::Render::ClassificationScheme scheme() const override { return m_get(); }
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s) override { m_set(s); }
    QVector<double> sampleValues() const override { return m_samples ? m_samples() : QVector<double>{}; }
    QPair<double, double> dataRange() const override { return m_range ? m_range() : QPair<double, double>{ 0.0, 1.0 }; }

    bool supportsContinuousMode() const override { return m_supportsContinuous; }
    bool supportsRangeModes() const override { return m_supportsRangeModes; }
    bool supportsCustomRange() const override { return !m_supportsRangeModes; }

private:
    Getter         m_get;
    Setter         m_set;
    SampleProvider m_samples;
    RangeProvider  m_range;
    bool           m_supportsContinuous = false;
    bool           m_supportsRangeModes = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_CLASSIFICATIONBINDINGS_H
