/*!
 * \file   rule.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Rule implementation (Slice Z.1).
 */

#include "render/rule.h"

// Gap A1.3 — archetype-seeded renderer construction.
#include "render/rendererfactory.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/renderers/unclassedcolorsrenderer.h"

#include <utility>

namespace OpenSWMM::Render
{

namespace {

/*!
 * \brief Construct a concrete IFeatureRenderer from a JSON object whose
 *        "id" field discriminates the renderer kind.
 *
 *        Mirrors the local helper in `src/render/multikindrenderer.cpp`
 *        and `src/project/projectserializer.cpp`. Kept TU-local on
 *        purpose — promoting a shared factory header is a wider refactor
 *        (CLAUDE.md §3: don't refactor things that aren't broken).
 */
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
    // Slice Z.9 — Unclassed continuous-colour renderer.
    else if (id == QLatin1String("unclassed"))
        r = std::make_unique<UnclassedColorsRenderer>();
    if (r)
        r->fromJson(j);
    return r;
}

} // namespace

Rule::Rule(QObject *parent)
    : QObject(parent),
      m_renderer(std::make_unique<SingleSymbolRenderer>())
{
}

Rule::Rule(QString name,
           std::unique_ptr<IFeatureRenderer> renderer,
           QObject *parent)
    : QObject(parent),
      m_name(std::move(name)),
      m_renderer(renderer ? std::move(renderer)
                          : std::make_unique<SingleSymbolRenderer>())
{
}

Rule::~Rule() = default;

void Rule::setName(const QString &name)
{
    if (m_name == name)
        return;
    m_name = name;
    emit ruleChanged();
}

void Rule::setVisible(bool v)
{
    if (m_isVisible == v)
        return;
    m_isVisible = v;
    emit ruleChanged();
}

void Rule::setFilterExpression(const QString &expr)
{
    if (m_filterExpression == expr)
        return;
    m_filterExpression = expr;
    emit ruleChanged();
}

void Rule::setMinScale(double s)
{
    if (qFuzzyCompare(m_minScale + 1.0, s + 1.0))
        return;
    m_minScale = s;
    emit ruleChanged();
}

void Rule::setMaxScale(double s)
{
    if (qFuzzyCompare(m_maxScale + 1.0, s + 1.0))
        return;
    m_maxScale = s;
    emit ruleChanged();
}

void Rule::setBlendMode(const QString &m)
{
    if (m_blendMode == m)
        return;
    m_blendMode = m;
    emit ruleChanged();
}

void Rule::setRebinPerFrame(bool v)
{
    if (m_rebinPerFrame == v)
        return;
    m_rebinPerFrame = v;
    emit ruleChanged();
}

void Rule::setSymbolLevelsEnabled(bool v)
{
    if (m_symbolLevelsEnabled == v)
        return;
    m_symbolLevelsEnabled = v;
    emit ruleChanged();
}

void Rule::setRenderer(std::unique_ptr<IFeatureRenderer> r)
{
    // Even a null swap should result in a valid renderer — the contract
    // is that renderer() never returns nullptr.
    m_renderer = r ? std::move(r) : std::make_unique<SingleSymbolRenderer>();
    emit rendererReplaced();
    emit ruleChanged();
}

void Rule::notifyRendererStateChanged()
{
    emit rendererReplaced();
    emit ruleChanged();
}

bool Rule::setRendererById(const QString &id)
{
    if (id.isEmpty())
        return false;
    if (m_renderer && m_renderer->rendererId() == id)
        return false;  // no-op — same class

    // Gap A1.3 — construct through the shared factory so the new
    // renderer's base/fallback symbol is seeded from the outgoing one
    // (or an archetype-appropriate skeleton). The Rule holds no category,
    // so the archetype is inferred from the current symbol's layer kind.
    const FeatureSublayer::Archetype archetype =
        RendererFactory::archetypeFromSymbol(
            RendererFactory::baseSymbolOf(m_renderer.get()),
            FeatureSublayer::Archetype::Point);

    auto next = RendererFactory::makeRenderer(id, archetype,
                                              m_renderer.get());
    if (!next)
        return false;
    setRenderer(std::move(next));
    return true;
}

QJsonObject Rule::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("name")]             = m_name;
    j[QStringLiteral("isVisible")]        = m_isVisible;
    if (!m_filterExpression.isEmpty())
        j[QStringLiteral("filterExpression")] = m_filterExpression;
    if (m_minScale != 0.0)
        j[QStringLiteral("minScale")] = m_minScale;
    if (m_maxScale != 0.0)
        j[QStringLiteral("maxScale")] = m_maxScale;
    if (m_blendMode != QLatin1String("Normal"))
        j[QStringLiteral("blendMode")] = m_blendMode;
    // Slice Z.7 — persist only when non-default (off) to keep diffs minimal.
    if (m_rebinPerFrame)
        j[QStringLiteral("rebinPerFrame")] = m_rebinPerFrame;
    // Slice Z.11 — same elision rule.
    if (m_symbolLevelsEnabled)
        j[QStringLiteral("symbolLevelsEnabled")] = m_symbolLevelsEnabled;
    if (m_renderer)
        j[QStringLiteral("renderer")] = m_renderer->toJson();
    return j;
}

std::unique_ptr<Rule> Rule::fromJson(const QJsonObject &j, QObject *parent)
{
    const QJsonObject rj = j.value(QStringLiteral("renderer")).toObject();
    if (rj.isEmpty())
        return nullptr;

    auto renderer = makeRendererFromJson(rj);
    if (!renderer)
        return nullptr;

    auto rule = std::make_unique<Rule>(
        j.value(QStringLiteral("name")).toString(),
        std::move(renderer),
        parent);

    // isVisible defaults to true if missing.
    rule->m_isVisible = j.value(QStringLiteral("isVisible")).toBool(true);
    rule->m_filterExpression =
        j.value(QStringLiteral("filterExpression")).toString();
    rule->m_minScale = j.value(QStringLiteral("minScale")).toDouble(0.0);
    rule->m_maxScale = j.value(QStringLiteral("maxScale")).toDouble(0.0);

    const QString blend = j.value(QStringLiteral("blendMode")).toString();
    if (!blend.isEmpty())
        rule->m_blendMode = blend;

    // Slice Z.7 — defaults to false when key absent.
    rule->m_rebinPerFrame = j.value(QStringLiteral("rebinPerFrame")).toBool(false);
    // Slice Z.11 — same default.
    rule->m_symbolLevelsEnabled =
        j.value(QStringLiteral("symbolLevelsEnabled")).toBool(false);

    return rule;
}

std::unique_ptr<Rule> Rule::clone(QObject *parent) const
{
    auto copy = std::make_unique<Rule>(
        m_name,
        m_renderer ? m_renderer->clone() : std::make_unique<SingleSymbolRenderer>(),
        parent);
    copy->m_isVisible        = m_isVisible;
    copy->m_filterExpression = m_filterExpression;
    copy->m_minScale         = m_minScale;
    copy->m_maxScale         = m_maxScale;
    copy->m_blendMode            = m_blendMode;
    copy->m_rebinPerFrame        = m_rebinPerFrame;
    copy->m_symbolLevelsEnabled  = m_symbolLevelsEnabled;
    return copy;
}

} // namespace OpenSWMM::Render
