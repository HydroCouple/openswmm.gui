/*!
 * \file   rule.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Layer-scope Rule — one styled aspect of a layer.
 *
 *         A Rule is the user-facing styling unit introduced by Slice Z.1
 *         (RENDERING_RULE_MODEL_PLAN.md §3.1). It wraps an IFeatureRenderer
 *         plus a small set of layer-scope properties: name, visibility, an
 *         optional filter expression, a scale-visibility window, and a
 *         blend mode. A layer owns an ordered RuleList; the active Rule is
 *         what the Symbology tab edits.
 *
 *         "Rule" follows QGIS's Rule-based renderer terminology — extended
 *         one level up from sub-renderer scope to layer scope. See
 *         RENDERING_RULE_MODEL_PLAN.md §2 for the vocabulary mapping and
 *         §2.1 for the rationale.
 *
 *         JSON round-trip is mandatory: .swmm-rule.json export, .oswp
 *         project persistence, and undo / map-preset snapshots all rely on
 *         it. The renderer is serialised via its IFeatureRenderer::toJson
 *         contract; the renderer id (`"single"` / `"graduated"` / ...) is
 *         what fromJson dispatches on.
 */

#ifndef OPENSWMM_RENDER_RULE_H
#define OPENSWMM_RENDER_RULE_H

#include "render/ifeaturerenderer.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>

namespace OpenSWMM::Render
{

/*!
 * \class Rule
 * \brief One layer-scope styled aspect — name + renderer + filter + scale.
 *
 *        A Rule owns its IFeatureRenderer via std::unique_ptr. Property
 *        edits emit ruleChanged(); replacing the renderer emits the
 *        distinct rendererReplaced() signal so editors can rebind to the
 *        new renderer instance without confusing it with a routine edit.
 */
class Rule : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY ruleChanged)
    Q_PROPERTY(bool isVisible READ isVisible WRITE setVisible NOTIFY ruleChanged)
    Q_PROPERTY(QString filterExpression READ filterExpression WRITE setFilterExpression
                   NOTIFY ruleChanged)
    Q_PROPERTY(double minScale READ minScale WRITE setMinScale NOTIFY ruleChanged)
    Q_PROPERTY(double maxScale READ maxScale WRITE setMaxScale NOTIFY ruleChanged)
    Q_PROPERTY(QString blendMode READ blendMode WRITE setBlendMode NOTIFY ruleChanged)
    /*! Slice Z.7 — when true, the Rule's binner re-computes breaks every
     *  animation frame from that frame's data alone. Off by default; the
     *  plan-spec lifecycle (RENDERING_RULE_MODEL_PLAN.md §14.1) is to
     *  sample once at Rule creation and freeze the breaks. The opt-in is
     *  for users who want continuous color rescaling. */
    Q_PROPERTY(bool rebinPerFrame READ rebinPerFrame WRITE setRebinPerFrame
                   NOTIFY ruleChanged)
    /*! Slice Z.11 — when true, the paint host walks features in
     *  level-major order (every feature's level-0 symbol layers first,
     *  then every feature's level-1, …). See RENDERING_RULE_MODEL_PLAN.md
     *  §8 + render/symbollevels.h. Off by default; intersection
     *  seams in line networks come out clean when enabled. */
    Q_PROPERTY(bool symbolLevelsEnabled READ symbolLevelsEnabled
                   WRITE setSymbolLevelsEnabled NOTIFY ruleChanged)

public:
    explicit Rule(QObject *parent = nullptr);
    Rule(QString name,
         std::unique_ptr<IFeatureRenderer> renderer,
         QObject *parent = nullptr);
    ~Rule() override;

    // Non-copyable / non-movable: Rule has identity (signals, owned renderer).
    Rule(const Rule &) = delete;
    Rule &operator=(const Rule &) = delete;
    Rule(Rule &&) = delete;
    Rule &operator=(Rule &&) = delete;

    [[nodiscard]] QString name() const { return m_name; }
    void setName(const QString &name);

    [[nodiscard]] bool isVisible() const { return m_isVisible; }
    void setVisible(bool v);

    /*!
     * \brief Optional first-match filter expression — same DSL as
     *        RuleBasedRenderer (see ExpressionEvaluator).
     *
     *        Empty (default) = match every feature. When non-empty the
     *        Rule's renderer is consulted only for features that satisfy
     *        the expression; non-matching features fall through to the
     *        next Rule in the stack.
     */
    [[nodiscard]] QString filterExpression() const { return m_filterExpression; }
    void setFilterExpression(const QString &expr);

    /*! \brief Minimum map scale at which this Rule paints. 0 = unbounded. */
    [[nodiscard]] double minScale() const { return m_minScale; }
    void setMinScale(double s);

    /*! \brief Maximum map scale at which this Rule paints. 0 = unbounded. */
    [[nodiscard]] double maxScale() const { return m_maxScale; }
    void setMaxScale(double s);

    /*! \brief Composition / blend mode for this Rule's paint pass.
     *         "Normal" (default), "Multiply", "Screen", "Overlay",
     *         "Darken", "Lighten", "ColorDodge", "ColorBurn",
     *         "HardLight", "SoftLight", "Difference", "Exclusion".
     *         Unknown values fall back to "Normal" at paint time. */
    [[nodiscard]] QString blendMode() const { return m_blendMode; }
    void setBlendMode(const QString &m);

    /*! Slice Z.7 — see Q_PROPERTY doc above. */
    [[nodiscard]] bool rebinPerFrame() const { return m_rebinPerFrame; }
    void setRebinPerFrame(bool v);

    /*! Slice Z.11 — see Q_PROPERTY doc above. */
    [[nodiscard]] bool symbolLevelsEnabled() const { return m_symbolLevelsEnabled; }
    void setSymbolLevelsEnabled(bool v);

    /*! \brief The owned renderer. Never null in a default-constructed Rule
     *         — the constructor seeds it with a SingleSymbolRenderer if
     *         none is supplied. */
    [[nodiscard]] IFeatureRenderer *renderer() const { return m_renderer.get(); }

    /*! \brief Replace the renderer. Takes ownership; the old renderer is
     *         destroyed. Emits rendererReplaced() then ruleChanged(). */
    void setRenderer(std::unique_ptr<IFeatureRenderer> r);

    /*! \brief Slice Z.3b — swap the renderer to a freshly-constructed
     *         instance of the renderer class identified by \p id.
     *
     *         Supported ids: "single", "graduated", "categorized",
     *         "rule", "unclassed". Returns true on swap, false when
     *         \p id is unknown, empty, or equals the current renderer's
     *         id (no-op). On true, fires rendererReplaced() then
     *         ruleChanged() per setRenderer.
     */
    bool setRendererById(const QString &id);

    /*! \brief Slice B.6c-fix — notify listeners that the owned renderer's
     *         internal state has changed without the pointer being
     *         replaced. Use after editing the renderer's prop maps (e.g.
     *         through SymbolStyleAdapter). Emits rendererReplaced()
     *         then ruleChanged(); semantically equivalent to a no-op
     *         setRenderer call for signal purposes so layer-side
     *         connect handlers (B.3 / B.4 / B.5) refresh paint. */
    void notifyRendererStateChanged();

    /*! \brief Serialise this Rule to a JSON object suitable for
     *         .swmm-rule.json and .oswp persistence. */
    [[nodiscard]] QJsonObject toJson() const;

    /*! \brief Construct a Rule from JSON. Returns nullptr if the JSON is
     *         malformed (no "renderer" object or an unknown renderer id).
     *         Tolerant of missing optional keys — they fall back to
     *         defaults. */
    [[nodiscard]] static std::unique_ptr<Rule> fromJson(const QJsonObject &j,
                                                        QObject *parent = nullptr);

    /*! \brief Deep copy. Owned renderer is cloned via
     *         IFeatureRenderer::clone(). The copy carries the same
     *         property values as the original. */
    [[nodiscard]] std::unique_ptr<Rule> clone(QObject *parent = nullptr) const;

signals:
    void ruleChanged();          /*!< Any Q_PROPERTY edit. */
    void rendererReplaced();     /*!< setRenderer() called; renderer pointer changed. */

private:
    QString                           m_name;
    bool                              m_isVisible = true;
    QString                           m_filterExpression;
    double                            m_minScale = 0.0;
    double                            m_maxScale = 0.0;
    QString                           m_blendMode = QStringLiteral("Normal");
    bool                              m_rebinPerFrame = false;
    bool                              m_symbolLevelsEnabled = false;
    std::unique_ptr<IFeatureRenderer> m_renderer;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULE_H
