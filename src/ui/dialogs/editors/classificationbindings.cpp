/*!
 * \file   classificationbindings.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.1 — concrete IClassificationBinding implementations.
 */
#include "ui/dialogs/editors/classificationbindings.h"

#include "render/colorramp.h"
#include "render/renderers/graduatedrenderer.h"

#include <algorithm>

using OpenSWMM::Render::BinMethod;
using OpenSWMM::Render::ClassificationScheme;
using OpenSWMM::Render::GraduatedRenderer;
using OpenSWMM::Render::IFeatureRenderer;
using OpenSWMM::Render::IntervalBinner;
using OpenSWMM::Render::RangeMode;
// RasterColorRamp lives at global scope (render/colorramp.h).

namespace openswmmvis::ui {

namespace {

/*! Best-effort reverse match of a renderer's RasterColorRamp to a built-in
 *  name, so the editor's ramp combo reflects the renderer instead of always
 *  showing the default. Returns "" when no built-in matches (custom ramp) —
 *  the scheme then keeps its default name (today's behaviour). */
QString builtinNameFor(const RasterColorRamp &ramp)
{
    for (const QString &name : RasterColorRamp::builtinNames()) {
        if (RasterColorRamp::builtin(name).stops == ramp.stops)
            return name;
    }
    return QString();
}

/*! Translate the editor's scheme into the renderer's ramp: stops from the
 *  resolved ramp (inverted in place when requested), value range preserved
 *  from the renderer so FixedUser bounds aren't clobbered. */
RasterColorRamp rendererRampFromScheme(const ClassificationScheme &s,
                                       double preserveMin, double preserveMax)
{
    RasterColorRamp ramp = s.resolvedRamp();
    if (s.invertRamp()) {
        QGradientStops inv;
        inv.reserve(ramp.stops.size());
        for (const auto &stop : ramp.stops)
            inv.append({ 1.0 - stop.first, stop.second });
        std::sort(inv.begin(), inv.end(),
                  [](const auto &a, const auto &b) { return a.first < b.first; });
        ramp.stops = inv;
    }
    ramp.minValue = preserveMin;
    ramp.maxValue = preserveMax;
    return ramp;
}

} // namespace

// ── GraduatedRendererBinding ────────────────────────────────────────────

GraduatedRendererBinding::GraduatedRendererBinding(RendererGetter getRenderer,
                                                   RendererInstaller installRenderer,
                                                   AttributeList attributeList,
                                                   RangeModeGate rangeModesEnabled)
    : m_get(std::move(getRenderer)),
      m_install(std::move(installRenderer)),
      m_attributes(std::move(attributeList)),
      m_rangeGate(std::move(rangeModesEnabled))
{
    resync();
}

ClassificationScheme GraduatedRendererBinding::schemeFromRenderer() const
{
    ClassificationScheme s;
    s.setMode(ClassificationScheme::ClassMode::Classified);
    GraduatedRenderer *g = m_get ? m_get() : nullptr;
    if (!g)
        return s;

    s.setBinner(g->binner());

    const QString name = builtinNameFor(g->ramp());
    if (!name.isEmpty())
        s.setRampName(name);

    s.setRangeMode(g->rangeMode());
    const bool fixedUser = (g->rangeMode() == RangeMode::FixedUser);
    s.setUseCustomRange(fixedUser);
    s.setRangeMin(g->ramp().minValue);
    s.setRangeMax(g->ramp().maxValue);

    const auto &overrides = g->binColorOverrides();
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        s.setColorOverride(it.key(), it.value());

    return s;
}

void GraduatedRendererBinding::resync()
{
    m_scheme = schemeFromRenderer();
}

void GraduatedRendererBinding::setScheme(const ClassificationScheme &s)
{
    GraduatedRenderer *g = m_get ? m_get() : nullptr;
    if (!g) { m_scheme = s; return; }

    const ClassificationScheme prev = m_scheme;
    auto cloneAsGraduated = [&]() -> std::pair<std::unique_ptr<IFeatureRenderer>, GraduatedRenderer *> {
        auto fresh = g->clone();
        auto *gf = dynamic_cast<GraduatedRenderer *>(fresh.get());
        return { std::move(fresh), gf };
    };

    auto [fresh, gf] = cloneAsGraduated();
    if (!gf) { m_scheme = s; return; }

    // Binner (method / count / manual breaks). setBinner clears the cached
    // breaks so the layer rebuild re-classifies from data — exactly the old
    // onBinCountChanged / onBinMethodChanged / onAutoClassify behaviour.
    if (s.binner().method() != prev.binner().method()
        || s.binner().binCount() != prev.binner().binCount()
        || s.binner().manualBreaks() != prev.binner().manualBreaks()) {
        gf->setBinner(s.binner());
    }

    // Range mode + FixedUser bounds.
    if (s.rangeMode() != prev.rangeMode()) {
        gf->setRangeMode(s.rangeMode());
        if (s.rangeMode() == RangeMode::FixedUser) {
            double mn = s.rangeMin(), mx = s.rangeMax();
            if (mn > mx) std::swap(mn, mx);
            if (mn == mx) mx = mn + 1.0;
            gf->setRange(mn, mx);
        }
        gf->clearBreaks();
    } else if (s.rangeMode() == RangeMode::FixedUser
               && (s.rangeMin() != prev.rangeMin() || s.rangeMax() != prev.rangeMax())) {
        double mn = s.rangeMin(), mx = s.rangeMax();
        if (mn > mx) std::swap(mn, mx);
        if (mn == mx) mx = mn + 1.0;
        gf->setRange(mn, mx);
        gf->clearBreaks();
    }

    // Ramp (name / invert / two-colour). Preserve the renderer's value range.
    if (s.rampName() != prev.rampName() || s.invertRamp() != prev.invertRamp()
        || s.lowColor() != prev.lowColor() || s.highColor() != prev.highColor()) {
        gf->setRamp(rendererRampFromScheme(s, gf->ramp().minValue, gf->ramp().maxValue));
    }

    // Per-class colour overrides — push diffs (set / clear).
    {
        const auto &prevOv = prev.colorOverrides();
        const auto &nextOv = s.colorOverrides();
        for (auto it = nextOv.cbegin(); it != nextOv.cend(); ++it)
            if (prevOv.value(it.key()) != it.value())
                gf->setColorForClass(QString::number(it.key()), it.value());
        for (auto it = prevOv.cbegin(); it != prevOv.cend(); ++it)
            if (!nextOv.contains(it.key()))
                gf->setColorForClass(QString::number(it.key()), QColor()); // clears
    }

    if (m_install)
        m_install(std::move(fresh));
    m_scheme = s;
}

QPair<double, double> GraduatedRendererBinding::dataRange() const
{
    GraduatedRenderer *g = m_get ? m_get() : nullptr;
    if (g)
        return { g->ramp().minValue, g->ramp().maxValue };
    return { 0.0, 1.0 };
}

void GraduatedRendererBinding::autoClassify()
{
    // Re-derive from data regardless of whether the config changed: clone,
    // drop the cached breaks, reinstall. The layer's rebuild re-classifies
    // against fresh samples (the same path the old "Auto-classify" button
    // drove).
    GraduatedRenderer *g = m_get ? m_get() : nullptr;
    if (!g) return;
    auto fresh = g->clone();
    if (auto *gf = dynamic_cast<GraduatedRenderer *>(fresh.get()))
        gf->clearBreaks();
    if (m_install)
        m_install(std::move(fresh));
    resync();
}

QVector<double> GraduatedRendererBinding::computedEdges() const
{
    // Mirror the renderer's data-derived breaks so the editor's table shows
    // the exact edges the map paints. lastBreaks holds the (n-1) interior
    // breaks; wrap with the ramp's value range as the endpoints. Empty when
    // the renderer hasn't classified yet → editor falls back to levelEdges.
    GraduatedRenderer *g = m_get ? m_get() : nullptr;
    if (!g || g->lastBreaks().isEmpty())
        return {};
    QVector<double> edges;
    edges.reserve(g->lastBreaks().size() + 2);
    edges.append(g->ramp().minValue);
    for (double b : g->lastBreaks())
        edges.append(b);
    edges.append(g->ramp().maxValue);
    return edges;
}

QVector<QPair<QString, QString>> GraduatedRendererBinding::availableAttributes() const
{
    return m_attributes ? m_attributes() : QVector<QPair<QString, QString>>{};
}

QString GraduatedRendererBinding::attribute() const
{
    GraduatedRenderer *g = m_get ? m_get() : nullptr;
    return g ? g->classifyAttribute() : QString();
}

void GraduatedRendererBinding::setAttribute(const QString &name)
{
    GraduatedRenderer *g = m_get ? m_get() : nullptr;
    if (!g || name == g->classifyAttribute())
        return;
    auto fresh = g->clone();
    if (auto *gf = dynamic_cast<GraduatedRenderer *>(fresh.get())) {
        gf->setClassifyAttribute(name);
        gf->clearBreaks();
    }
    if (m_install)
        m_install(std::move(fresh));
    resync();
}

// ── SublayerSchemeBinding ───────────────────────────────────────────────

SublayerSchemeBinding::SublayerSchemeBinding(Getter getter, Setter setter,
                                             SampleProvider sampleProvider,
                                             RangeProvider rangeProvider,
                                             bool supportsContinuousMode,
                                             bool supportsRangeModes)
    : m_get(std::move(getter)),
      m_set(std::move(setter)),
      m_samples(std::move(sampleProvider)),
      m_range(std::move(rangeProvider)),
      m_supportsContinuous(supportsContinuousMode),
      m_supportsRangeModes(supportsRangeModes)
{
}

} // namespace openswmmvis::ui
