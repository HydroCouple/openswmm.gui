/**
 * @file      irendererpanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Renderer-class panel interface + factory registry.
 *
 *         Slice X.2.  Mirrors Qt's IStyleEditorWidget seam but at the
 *         renderer-class layer.  The Symbology tab's top dropdown lists
 *         all available renderer classes (Single / Graduated / Categorized
 *         / Rule-based + any future plugins).  Switching the dropdown
 *         mounts a registered panel matching the picked class.
 *
 *         Each renderer panel:
 *           - reads the current per-kind renderer from its host layer,
 *           - writes through to setKindRenderer / setRenderer on each edit,
 *           - listens for the layer's repaintRequested / rendererChanged
 *             signals to refresh on external mutations.
 *
 *         Plugins register a factory via REGISTER_RENDERER_PANEL.  The
 *         dropdown picks up the new class automatically — no edits to the
 *         dialog code.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IRENDERERPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_IRENDERERPANEL_H

#include "layers/swmm_category.h"
// Gap A4.1 — resolved capability snapshot (attribute fields + archetype).
#include "render/iattributeprovider.h"
#include "render/sublayers/feature/featuresublayer.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class OpenSWMMVisLayer;

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class Rule;
}

namespace openswmmvis::ui
{

/*!
 * \brief Context passed to renderer-panel factories at construction time.
 *
 *        Three possible target shapes, in priority order:
 *
 *          1. \c rule != nullptr — Slice Z.3b. The panel edits the
 *             Rule's owned renderer via `rule->renderer()` /
 *             `rule->setRenderer(...)`. \c hostLayer / \c category
 *             are ignored when \c rule is set. The Rule outlives the
 *             panel.
 *          2. \c hostLayer + \c category — the panel edits one of
 *             SWMMModelLayer / SWMMResultsLayer's per-kind renderer
 *             slots (the legacy multi-kind path).
 *          3. \c hostLayer only — the panel edits the layer's single
 *             layer-level renderer (the vector / raster case).
 *
 *        Panel implementations check \c ctx.rule first; if null, they
 *        fall back to the layer + category path. Slice Z.3b only
 *        rewires the outer SymbologyTab's renderer-class picker (read
 *        + class-swap). The individual IRendererPanel implementations
 *        are migrated to consult \c ctx.rule in the named Z.3c
 *        follow-up.
 */
struct RendererPanelContext {
    OpenSWMMVisLayer *hostLayer = nullptr;
    std::optional<OpenSWMMVis::SwmmCategory> category; // empty => layer-level
    OpenSWMM::Render::Rule *rule = nullptr;            // Slice Z.3b

    // ----- Gap A4.1 — resolved capability snapshot -----------------------
    // Filled by resolve(); declarative inputs for contextual gating so the
    // panels and the renderer-class dropdown share one source of truth
    // instead of re-deriving (and drifting on) provider lookups and
    // rule→layer parent walks.

    /*! Attribute fields exposed by the host's IAttributeProvider for the
     *  target category (empty when no provider / no fields). */
    QVector<OpenSWMM::Render::AttributeField> fields;

    /*! Geometry archetype of the target kind (Point when unknown). */
    OpenSWMM::Render::FeatureSublayer::Archetype archetype =
        OpenSWMM::Render::FeatureSublayer::Archetype::Point;

    /*! True when the host is an animated (results) layer — gates
     *  range-mode rows and per-frame rebin options. */
    bool animated = false;

    /**
     * @return True for ANY field whose type is NOT QString, including Bool/unknown/custom types; no
     *   stricter numeric conversion/type/range check. Read snapshot only, no provider
     *   refresh/mutation.
     */
    [[nodiscard]] bool hasNumeric() const
    {
        for (const auto &f : fields)
            if (f.type != QMetaType::QString)
                return true;
        return false;
    }
    /**
     * @return True iff snapshot contains field type EXACT QString; other string-convertible
     *   metatypes do not count. No provider query/mutation.
     */
    [[nodiscard]] bool hasString() const
    {
        for (const auto &f : fields)
            if (f.type == QMetaType::QString)
                return true;
        return false;
    }

    /*! Build a context with the capability snapshot resolved: provider
     *  lookup (including the rule → RuleList → layer parent walk),
     *  archetype from the category, animated from the host layer type. */
    [[nodiscard]] static RendererPanelContext
    /**
     * @param[in] layer Optional borrowed raw host input, retained in result even when null;
     *   provider lookup prefers it when present.
     * @param[in] category Optional category copied; provider queries Junctions when absent,
     *   archetype stays Point absent.
     * @param[in] rule Optional borrowed raw Rule copied; when no explicit layer, walk exactly
     *   rule.parent.parent and accept inherits(OpenSWMMVisLayer).
     * @return Context value with copied provider attribute list, archetype from category, animated
     *   only host QObject inherits SWMMResultsLayer (2D results not automatically included).
     *   Resolved ancestor used for fields/animated only, NOT written back to ctx.hostLayer.
     * @note No renderer creation/write, ownership pin, QObject-parent search beyond two levels or
     *   applicability filtering. Inputs/source getters GUI-thread and must remain alive through
     *   panel session; snapshot does not auto-refresh later field/capability changes.
     */
    resolve(OpenSWMMVisLayer *layer, std::optional<OpenSWMMVis::SwmmCategory> category,
            OpenSWMM::Render::Rule *rule = nullptr);
};

/*! Base interface every renderer panel inherits.  Same QWidget contract
 *  as IStyleEditorWidget so the SymbologyTab can swap panels in/out of a
 *  QStackedWidget without per-class casts. */
class IRendererPanel : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    /**
     * @brief Default QWidget editor destruction, no renderer rollback, ownership transfer or
     *   registry removal. Context host/rule lifetime belongs to caller.
     */
    ~IRendererPanel() override = default;

    /*! Re-read the current renderer state from the host layer / kind and
     *  refresh widget values.  Called on construction and again whenever
     *  the host layer fires rendererChanged.
     * @brief Derived GUI-thread refresh contract: read current context renderer and update controls
     *   without unintended setter feedback.
     * @note No base implementation, signal subscription, rollback or undo provided; each concrete
     *   panel owns its connections and model writes. Host/rule must remain valid.
     */
    virtual void refreshFromModel() = 0;

    /*! User-facing label for the dropdown entry (e.g. "Single Symbol").
     * @return Concrete panel display label by value. No registry lookup, translation/uniqueness
     *   requirement enforced here or source mutation.
     */
    virtual QString displayName() const = 0;
};

/*! Picks the matching panel for a (rendererId, context) pair. */
class RendererPanelRegistry
{
public:
    /*! Factory: (context) → IRendererPanel.  The factory installs the
     *  matching renderer-class on the host before returning so the
     *  current panel always reflects the live renderer state. */
    using Factory =
        std::function<IRendererPanel *(const RendererPanelContext &ctx, QWidget *parent)>;

    /*! Gap A4.2 — declarative applicability: returns true when the
     *  renderer class makes sense for the resolved context (e.g.
     *  Graduated needs at least one numeric attribute). Null predicate =
     *  always applicable. */
    using Applicable = std::function<bool(const RendererPanelContext &ctx)>;

    struct Entry {
        QString rendererId;  // "single" / "graduated" / "categorized" / "rulebased" / ...
        QString displayName; // shown in the dropdown
        Factory factory;
        Applicable applicable;  // Gap A4.2 — null => always applicable
        QString disabledReason; // tooltip shown on the greyed-out row
    };

    /**
     * @return Process-local function-static singleton reference, not caller-owned. Registry vector
     *   operations unsynchronized, intended startup/GUI-thread serialized use; no factory
     *   invocation.
     */
    static RendererPanelRegistry &instance();

    /**
     * @param[in] rendererId Moved exact raw key, duplicates/empty allowed.
     * @param[in] displayName Moved user-facing dropdown caption.
     * @param[in] factory Moved renderer-panel callback, not validated/invoked here; caller
     *   preserves captured context lifetime.
     * @param[in] applicable Moved optional predicate, empty means UI may treat always applicable;
     *   not evaluated here/find.
     * @param[in] disabledReason Moved optional tooltip for unavailable renderer class, no gating by
     *   itself.
     * @note Append in registration order, duplicates never replace. Can invalidate entries/find
     *   pointers/references on reallocation; no thread safety, renderer creation/model write or
     *   unregister support.
     */
    void registerRenderer(QString rendererId, QString displayName, Factory factory,
                          Applicable applicable = {}, QString disabledReason = QString());

    /*! All registered renderer IDs in registration order — the
     *  SymbologyTab walks this to populate the dropdown.
     * @return Borrowed registry vector reference in registration order, including duplicates/null
     *   factories. Element pointers/iterators invalidated by later registration/reallocation; copy
     *   for independent lifetime. No callback/source query.
     */
    [[nodiscard]] const std::vector<Entry> &entries() const { return m_entries; }

    /*! Resolve a registered factory by rendererId.  Returns nullptr when
     *  the id has no factory yet — caller falls back to the dropdown's
     *  first entry.
     * @param[in] rendererId Exact case-sensitive key, no trim.
     * @return Borrowed FIRST matching entry even if factory empty/applicability false, null absent.
     *   Valid until registry mutation/destruction; no factory/predicate invocation or ownership
     *   transfer.
     */
    [[nodiscard]] const Entry *find(const QString &rendererId) const;

private:
    /**
     * @brief Private empty registry construction, no default renderer/editor installation. instance
     *   owns lifetime.
     */
    RendererPanelRegistry() = default;
    std::vector<Entry> m_entries;
};

/*! RAII registration helper.  Use the macros below. */
class RendererPanelRegistrar
{
public:
    /**
     * @param[in] rendererId Moved raw key.
     * @param[in] displayName Moved dropdown label.
     * @param[in] factory Moved callable retained by global registry.
     * @param[in] applicable Moved optional predicate, not evaluated.
     * @param[in] disabledReason Moved optional disabled tooltip.
     * @brief Append renderer registration immediately, normally static initialization. No
     *   auto-unregister on helper destruction, renderer/model write or widget allocation until
     *   caller invokes factory. Startup/GUI operations serialized by host.
     */
    RendererPanelRegistrar(QString rendererId, QString displayName,
                           RendererPanelRegistry::Factory factory,
                           RendererPanelRegistry::Applicable applicable = {},
                           QString disabledReason = QString())
    {
        RendererPanelRegistry::instance().registerRenderer(
            std::move(rendererId), std::move(displayName), std::move(factory),
            std::move(applicable), std::move(disabledReason));
    }
};

#define REGISTER_RENDERER_PANEL(ID_STRING, DISPLAY_STRING, FACTORY)                                \
    static const openswmmvis::ui::RendererPanelRegistrar s_register_renderer_##__LINE__(           \
        QStringLiteral(ID_STRING), QStringLiteral(DISPLAY_STRING), FACTORY);

/*! Gap A4.2 — registration with a declarative applicability predicate +
 *  tooltip reason for the greyed-out dropdown row. */
#define REGISTER_RENDERER_PANEL_GATED(ID_STRING, DISPLAY_STRING, FACTORY, APPLICABLE,              \
                                      REASON_STRING)                                               \
    static const openswmmvis::ui::RendererPanelRegistrar s_register_renderer_##__LINE__(           \
        QStringLiteral(ID_STRING), QStringLiteral(DISPLAY_STRING), FACTORY, APPLICABLE,            \
        QStringLiteral(REASON_STRING));

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_IRENDERERPANEL_H
