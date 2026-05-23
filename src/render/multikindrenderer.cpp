/*!
 * \file   multikindrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/multikindrenderer.h"

#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"

#include <QJsonObject>

#include <algorithm>

namespace OpenSWMM::Render
{

namespace {

// Factory: construct a concrete IFeatureRenderer from a JSON object whose
// "id" field discriminates the renderer kind. Local to this TU so it
// doesn't pollute the public header — MultiKindRenderer is the only
// caller. Returns nullptr if "id" is missing or unknown; the caller
// decides whether to fall back to a default.
std::unique_ptr<IFeatureRenderer> makeRendererFromJson(const QJsonObject &j)
{
    const QString id = j.value(QStringLiteral("id")).toString();
    std::unique_ptr<IFeatureRenderer> r;
    if (id == QLatin1String("single"))
        r = std::make_unique<SingleSymbolRenderer>();
    else if (id == QLatin1String("graduated"))
        r = std::make_unique<GraduatedRenderer>();
    else if (id == QLatin1String("categorized"))
        r = std::make_unique<CategorizedRenderer>();
    else if (id == QLatin1String("rule"))
        r = std::make_unique<RuleBasedRenderer>();
    if (r) r->fromJson(j);
    return r;
}

} // namespace

MultiKindRenderer::MultiKindRenderer()
    : m_fallback(std::make_unique<SingleSymbolRenderer>())
{
}

const IFeatureRenderer *MultiKindRenderer::rendererFor(const QString &kind) const
{
    const auto it = m_perKind.find(kind);
    return (it == m_perKind.end()) ? nullptr : it->second.get();
}

IFeatureRenderer *MultiKindRenderer::rendererFor(const QString &kind)
{
    const auto it = m_perKind.find(kind);
    return (it == m_perKind.end()) ? nullptr : it->second.get();
}

void MultiKindRenderer::setRendererFor(const QString &kind,
                                       std::unique_ptr<IFeatureRenderer> renderer)
{
    if (kind.isEmpty()) return;
    if (!renderer) {
        m_perKind.erase(kind);
        return;
    }
    // operator[] default-constructs the unique_ptr slot then move-assigns —
    // works around the lack of an insert-or-assign overload that takes a
    // move-only mapped type cleanly across Qt / libc++ versions.
    m_perKind[kind] = std::move(renderer);
}

void MultiKindRenderer::clearRendererFor(const QString &kind)
{
    m_perKind.erase(kind);
}

void MultiKindRenderer::setFallback(std::unique_ptr<IFeatureRenderer> r)
{
    // Never allow a null fallback — keeps symbolFor() total. If the caller
    // passes nullptr we silently keep the current fallback rather than
    // crashing on the next paint.
    if (r) m_fallback = std::move(r);
}

std::vector<QString> MultiKindRenderer::kinds() const
{
    std::vector<QString> out;
    out.reserve(m_perKind.size());
    for (const auto &kv : m_perKind)
        out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

SymbolStyle MultiKindRenderer::symbolFor(const FeatureRef &f,
                                         const QVariantMap &attrs) const
{
    // Dispatch on categoryHint; empty hint or missing per-kind entry both
    // fall through to the fallback so the renderer never returns garbage.
    if (!f.categoryHint.isEmpty()) {
        const auto it = m_perKind.find(f.categoryHint);
        if (it != m_perKind.end() && it->second)
            return it->second->symbolFor(f, attrs);
    }
    return m_fallback->symbolFor(f, attrs);
}

QList<LegendSymbolItem> MultiKindRenderer::legendSymbolItems() const
{
    // Aggregate per-kind legend items in deterministic order. Each kind's
    // items inherit a `userLabel` prefix carrying the kind name so the
    // legend dock can group visually. Sort order from kinds() (alphabetic)
    // keeps the legend stable across save/load.
    QList<LegendSymbolItem> out;
    for (const QString &kind : kinds()) {
        const auto it = m_perKind.find(kind);
        if (it == m_perKind.end() || !it->second) continue;
        const IFeatureRenderer *r = it->second.get();
        const auto items = r->legendSymbolItems();
        for (LegendSymbolItem item : items) {
            // Combine kind prefix with whichever-label-is-set on the inner
            // item. Precedence: user override (BB Phase 8.6.10) wins over
            // the renderer's auto label; empty inner label collapses to
            // just the kind.
            const QString innerLabel = !item.userLabel.isEmpty()
                                           ? item.userLabel
                                           : item.label;
            item.userLabel = innerLabel.isEmpty()
                                 ? kind
                                 : kind + QStringLiteral(" / ") + innerLabel;
            out.append(item);
        }
    }
    return out;
}

QJsonObject MultiKindRenderer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), rendererId());

    // Per-kind dict — keys are kind strings, values are inner renderer JSON.
    QJsonObject kindsObj;
    for (const QString &kind : kinds()) {
        const auto it = m_perKind.find(kind);
        if (it != m_perKind.end() && it->second)
            kindsObj.insert(kind, it->second->toJson());
    }
    obj.insert(QStringLiteral("kinds"), kindsObj);

    if (m_fallback)
        obj.insert(QStringLiteral("fallback"), m_fallback->toJson());

    return obj;
}

void MultiKindRenderer::fromJson(const QJsonObject &j)
{
    m_perKind.clear();

    const QJsonObject kindsObj = j.value(QStringLiteral("kinds")).toObject();
    for (auto it = kindsObj.constBegin(); it != kindsObj.constEnd(); ++it) {
        auto inner = makeRendererFromJson(it.value().toObject());
        if (inner)
            m_perKind[it.key()] = std::move(inner);
    }

    if (j.contains(QStringLiteral("fallback"))) {
        if (auto fb = makeRendererFromJson(j.value(QStringLiteral("fallback")).toObject()))
            m_fallback = std::move(fb);
        // else: keep current fallback (per setFallback's contract).
    }
    // No fallback in JSON → ctor default already created a SingleSymbolRenderer.
}

std::unique_ptr<IFeatureRenderer> MultiKindRenderer::clone() const
{
    auto out = std::make_unique<MultiKindRenderer>();
    for (const auto &kv : m_perKind)
        out->m_perKind[kv.first] = kv.second->clone();
    if (m_fallback)
        out->m_fallback = m_fallback->clone();
    return out;
}

} // namespace OpenSWMM::Render
