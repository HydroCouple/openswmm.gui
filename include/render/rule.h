/**
 * @file      rule.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Layer-scope Rule — one styled aspect of a layer.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
 *        rendererReplaced() signal so editors can rebind. Explicit renderer-state
 *        notifications emit that signal too, even when the pointer is unchanged.
 */
class Rule : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY ruleChanged)
    Q_PROPERTY(bool isVisible READ isVisible WRITE setVisible NOTIFY ruleChanged)
    Q_PROPERTY(
        QString filterExpression READ filterExpression WRITE setFilterExpression NOTIFY ruleChanged)
    Q_PROPERTY(double minScale READ minScale WRITE setMinScale NOTIFY ruleChanged)
    Q_PROPERTY(double maxScale READ maxScale WRITE setMaxScale NOTIFY ruleChanged)
    Q_PROPERTY(QString blendMode READ blendMode WRITE setBlendMode NOTIFY ruleChanged)
    /*! Slice Z.7 — when true, the Rule's binner re-computes breaks every
     *  animation frame from that frame's data alone. Off by default; the
     *  plan-spec lifecycle (RENDERING_RULE_MODEL_PLAN.md §14.1) is to
     *  sample once at Rule creation and freeze the breaks. The opt-in is
     *  for users who want continuous color rescaling. */
    Q_PROPERTY(bool rebinPerFrame READ rebinPerFrame WRITE setRebinPerFrame NOTIFY ruleChanged)
    /*! Slice Z.11 — when true, the paint host walks features in
     *  level-major order (every feature's level-0 symbol layers first,
     *  then every feature's level-1, …). See RENDERING_RULE_MODEL_PLAN.md
     *  §8 + render/symbollevels.h. Off by default; intersection
     *  seams in line networks come out clean when enabled. */
    Q_PROPERTY(bool symbolLevelsEnabled READ symbolLevelsEnabled WRITE setSymbolLevelsEnabled NOTIFY
                   ruleChanged)

public:
    /** @brief Construct a visible unnamed rule with an empty single-symbol renderer.
     * @param parent Optional QObject owner. Filters/scales are empty/unbounded,
     * blend mode Normal, and per-frame rebinning/symbol levels disabled. */
    explicit Rule(QObject *parent = nullptr);
    /** @brief Construct a named rule and take ownership of its renderer.
     * @param name Text moved verbatim, including empty or duplicate names.
     * @param renderer Ownership transferred; null creates an empty SingleSymbolRenderer.
     * @param parent Optional QObject owner, which owns the Rule through Qt parenting.
     * No change signals are emitted during construction. */
    Rule(QString name, std::unique_ptr<IFeatureRenderer> renderer, QObject *parent = nullptr);
    /** @brief Destroy the owned renderer and QObject-owned children. */
    ~Rule() override;

    // Non-copyable / non-movable: Rule has identity (signals, owned renderer).
    /** @brief Prohibit copying a QObject identity and its owned renderer. */
    Rule(const Rule &) = delete;
    /** @brief Prohibit copy assignment of an established QObject identity. */
    Rule &operator=(const Rule &) = delete;
    /** @brief Prohibit moving a QObject whose signal connections have stable identity. */
    Rule(Rule &&) = delete;
    /** @brief Prohibit move assignment of an established QObject identity. */
    Rule &operator=(Rule &&) = delete;

    /** @brief Read the rule's display/persistence name.
     * @return Owned text, without normalization or uniqueness checks. */
    [[nodiscard]] QString name() const { return m_name; }
    /** @brief Replace the name and emit ruleChanged() on an exact text change.
     * @param name Text copied verbatim; empty, whitespace and duplicates are accepted. */
    void setName(const QString &name);

    /** @brief Read whether the paint host should consider this rule enabled.
     * @return Stored visibility; scale/filter checks are separate host responsibilities. */
    [[nodiscard]] bool isVisible() const { return m_isVisible; }
    /** @brief Toggle rule visibility and emit ruleChanged() on change.
     * @param v True enables consideration by the paint host; false disables it. */
    void setVisible(bool v);

    /*!
     * \brief Optional first-match filter expression — same DSL as
     *        RuleBasedRenderer (see ExpressionEvaluator).
     *
     *        Empty (default) = match every feature. When non-empty the
     *        Rule's renderer is consulted only for features that satisfy
     *        the expression; non-matching features fall through to the
     *        next Rule in the stack.
     * @return Stored expression text; this getter does not parse or evaluate it.
     */
    [[nodiscard]] QString filterExpression() const { return m_filterExpression; }
    /** @brief Replace filter text without compiling or validating it.
     * @param expr Expression copied verbatim, including empty text. An exact text
     * change emits ruleChanged(); host evaluation defines error/nonmatch behavior. */
    void setFilterExpression(const QString &expr);

    /** @brief Read the lower visibility-scale metadata; zero means unbounded.
     * @return Stored map-scale value, interpreted by the paint host. */
    [[nodiscard]] double minScale() const { return m_minScale; }
    /** @brief Store a lower visibility scale and notify when it changes fuzzily.
     * @param s Scale value, with zero for unbounded; no finite, nonnegative or
     * min/max ordering check occurs. Uses qFuzzyCompare after adding one and
     * emits ruleChanged() only when that comparison differs. */
    void setMinScale(double s);

    /** @brief Read the upper visibility-scale metadata; zero means unbounded.
     * @return Stored map-scale value, interpreted by the paint host. */
    [[nodiscard]] double maxScale() const { return m_maxScale; }
    /** @brief Store an upper visibility scale and notify when it changes fuzzily.
     * @param s Scale value, with zero for unbounded; no finite, nonnegative or
     * ordering check occurs. Uses the same shifted comparison as setMinScale(). */
    void setMaxScale(double s);

    /*! \brief Composition / blend mode for this Rule's paint pass.
     *         "Normal" (default), "Multiply", "Screen", "Overlay",
     *         "Darken", "Lighten", "ColorDodge", "ColorBurn",
     *         "HardLight", "SoftLight", "Difference", "Exclusion".
     *         Paint hosts interpret the stored token.
     * @return Owned token text without normalization or validation. */
    [[nodiscard]] QString blendMode() const { return m_blendMode; }
    /** @brief Store a blend-mode token and emit ruleChanged() on exact change.
     * @param m Text copied verbatim; unknown/empty tokens are accepted and left
     * for paint-host interpretation. This setter performs no compositing. */
    void setBlendMode(const QString &m);

    /** @brief Read the owning layer's per-frame classification preference.
     * @return Stored flag; the Rule does not itself sample or reclassify data. */
    [[nodiscard]] bool rebinPerFrame() const { return m_rebinPerFrame; }
    /** @brief Store the per-frame rebinning preference and notify on change.
     * @param v True requests fresh frame-specific breaks from the owning layer. */
    void setRebinPerFrame(bool v);

    /** @brief Read the preferred symbol-pass ordering mode.
     * @return True requests level-major painting; the Rule does not paint itself. */
    [[nodiscard]] bool symbolLevelsEnabled() const { return m_symbolLevelsEnabled; }
    /** @brief Store level-major painting preference and emit ruleChanged() on change.
     * @param v True requests symbol levels; false requests ordinary host ordering. */
    void setSymbolLevelsEnabled(bool v);

    /*! \brief The owned renderer. Never null in a default-constructed Rule
     *         — the constructor seeds it with a SingleSymbolRenderer if
     *         none is supplied.
     * @return Borrowed non-null renderer. Replacement or Rule destruction
     * invalidates it; notifyRendererStateChanged() alone need not change the pointer. */
    [[nodiscard]] IFeatureRenderer *renderer() const { return m_renderer.get(); }

    /*! \brief Replace the renderer. Takes ownership; the old renderer is
     *         destroyed. Emits rendererReplaced() then ruleChanged().
     * @param r Ownership transferred by value. Null creates a fresh empty
     * SingleSymbolRenderer; signals are emitted for every call without an equality check. */
    void setRenderer(std::unique_ptr<IFeatureRenderer> r);

    /*! \brief Slice Z.3b — swap the renderer to a freshly-constructed
     *         instance of the renderer class identified by \p id.
     *
     *         Supported ids: "single", "graduated", "categorized",
     *         "rule", "unclassed". Returns true on swap, false when
     *         \p id is unknown, empty, or equals the current renderer's
     *         id (no-op). On true, fires rendererReplaced() then
     *         ruleChanged() per setRenderer.
     * @param id Exact case-sensitive class key, without trimming.
     * @return True after replacing the renderer; false for no-op/unsupported keys.
     * The shared factory carries the prior base symbol and compatible attribute;
     * it does not preserve bins, categories, rules or other renderer-specific state.
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
     *         .swmm-rule.json and .oswp persistence.
     * @return Owned snapshot, omitting default optional metadata and delegating
     * renderer configuration to its toJson(). No filter/scale validation occurs. */
    [[nodiscard]] QJsonObject toJson() const;

    /*! \brief Construct a Rule from JSON. Returns nullptr if the JSON is
     *         malformed (no "renderer" object or an unknown renderer id).
     *         Tolerant of missing optional keys — they fall back to
     *         defaults.
     * @param j Rule object with a renderer object whose exact id is single,
     * graduated, categorized, rule or unclassed. Unknown/missing renderer IDs fail.
     * @param parent Optional QObject parent, which must outlive the returned
     * unique_ptr owner or have ownership released before it deletes the child.
     * @return Owning rule snapshot, or nullptr for unsupported/missing renderer
     * metadata. Other malformed fields use Qt defaults; filter/scale validity is
     * not checked. Fields are assigned without emitting change signals. */
    [[nodiscard]] static std::unique_ptr<Rule> fromJson(const QJsonObject &j,
                                                        QObject *parent = nullptr);

    /*! \brief Deep copy. Owned renderer is cloned via
     *         IFeatureRenderer::clone(). The copy carries the same
     *         property values as the original.
     * @param parent Optional QObject parent with the same lifetime requirement as fromJson().
     * @return Sole ownership of a new Rule identity and independently cloned
     * renderer. Existing signal connections are not copied and construction emits no changes. */
    [[nodiscard]] std::unique_ptr<Rule> clone(QObject *parent = nullptr) const;

signals:
    /** @brief Notify a metadata edit, renderer replacement, or explicit renderer-state change. */
    void ruleChanged();
    /** @brief Notify renderer replacement or an explicit internal state update.
     * Rebind/read the renderer again; the pointer may be unchanged. Emitted
     * before ruleChanged() by setRenderer() and notifyRendererStateChanged(). */
    void rendererReplaced();

private:
    QString m_name;
    bool m_isVisible = true;
    QString m_filterExpression;
    double m_minScale = 0.0;
    double m_maxScale = 0.0;
    QString m_blendMode = QStringLiteral("Normal");
    bool m_rebinPerFrame = false;
    bool m_symbolLevelsEnabled = false;
    std::unique_ptr<IFeatureRenderer> m_renderer;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RULE_H
