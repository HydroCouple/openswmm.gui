/**
 * @file      singlesymbolrendererpanel.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     IRendererPanel implementation for the "Single Symbol" renderer
 *         class.  Slice X.3.
 *
 *         For SWMM kinds the panel reuses the existing FeatureStyleEditor
 *         family (point/line/polygon archetypes) via the existing
 *         StyleEditorRegistry — no behaviour duplicated, just routed
 *         differently.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/irendererpanel.h"
#include <QDebug>

#include "layers/openswmmvislayer.h"
// Slice B.6c — Rule-aware Single Symbol panel.
#include "render/rule.h"
#include "render/symbolstyleadapter.h"
#include "ui/dialogs/editors/basesymbolgroup.h" // feedback C2 — shared style lookup
#include "ui/dialogs/istyleeditorwidget.h"

#include <QHeaderView>
#include <QLabel>
#include <QTreeView>
#include <QVBoxLayout>

#include <memory> // m_styleAdapter (previously came via ilayerstylesubject.h)

// SE.4 — the QPropertyModel generic-grid fallback was removed from the
// symbology editor. Every *SymbolStyleAdapter archetype now has a dedicated
// IStyleEditorWidget (symbolstyleeditors{,2d}.cpp), so StyleEditorRegistry
// always returns a real editor. QPropertyModel remains in use elsewhere
// (attribute panel, preferences, sublayer style dialog, …).

namespace openswmmvis::ui
{

namespace
{

class SingleSymbolPanel : public IRendererPanel
{
public:
    /**
     * @brief Mount a dedicated symbol editor for the rule or host/category style in a copied
     * context.
     * @param ctx Borrowed context copied into panel storage; raw rule/host and any borrowed
     * persistent style must outlive panel/edit callbacks. Rule takes priority and receives
     * an owned archetype-specific SymbolStyleAdapter wrapper, not ownership of the rule.
     * @param parent Optional Qt owner; caller owns parentless panel.
     * Registry-created editor is parent-owned; missing style/editor shows a diagnostic label,
     * with no generic property-grid fallback. Does not install a renderer, capture a snapshot,
     * create undo commands or apply edits itself. Child editors define live behavior.
     */
    explicit SingleSymbolPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent), m_ctx(ctx)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);

        QObject *style = nullptr;

        // Rule path owns an archetype-specific adapter wrapper around the rule's
        // renderer. The registry supplies a dedicated editor; absent registration
        // shows the diagnostic below. There is no generic property-grid fallback.
        if (m_ctx.rule) {
            m_styleAdapter.reset(OpenSWMM::Render::SymbolStyleAdapter::createFor(m_ctx.rule));
            style = m_styleAdapter.get();
        } else if (m_ctx.category.has_value()) {
            // Results sublayer style, or the model layer's PERSISTENT
            // SwmmElementSymbolAdapter (same instance the dialog's subjects
            // wrap, so the Cancel snapshot covers edits made here). Shared
            // with the Graduated / Categorized panels' "Base symbol &
            // labels" group (feedback C2).
            style = baseSymbolStyleFor(m_ctx.hostLayer, *m_ctx.category);
        }

        if (style) {
            auto *editor = StyleEditorRegistry::instance().createEditorFor(style, this);
            if (editor) {
                lay->addWidget(editor, 1);
                return;
            }
            // SE.4 — no generic-grid fallback. Reaching here means an
            // archetype adapter has no dedicated editor registered, which
            // is a programming error (every archetype is covered in
            // symbolstyleeditors{,2d}.cpp). Show a diagnostic label rather
            // than the old QPropertyModel grid.
        }
        lay->addWidget(
            new QLabel(tr("No single-symbol editor registered for this layer kind."), this));
        lay->addStretch();
    }

    /**
     * @brief Perform no explicit refresh in this wrapper.
     * Relies on mounted editors' own notification subscriptions; does not find/invoke their
     * refresh methods, replace adapters or update a diagnostic-only page.
     */
    void refreshFromModel() override
    {
        // Children refresh themselves through their own NOTIFY hookups.
    }

    /**
     * @brief Return this panel's translated display label.
     * @return Owned Single Symbol string, independent of live renderer state.
     */
    QString displayName() const override { return tr("Single Symbol"); }

private:
    RendererPanelContext m_ctx;
    // Slice SS.1 — the factory returns one of four concrete subclasses
    // (Point / Line / Polygon / generic), so we hold it as the base
    // QObject. The QPropertyModel / StyleEditorRegistry only need a
    // QObject* anyway.
    std::unique_ptr<QObject> m_styleAdapter; // Slice B.6c → SS.1
};

} // namespace

// Static registration factory borrows ctx targets and passes widget ownership
// through parent. Registry insertion order across translation units is unspecified.
REGISTER_RENDERER_PANEL("single", "Single Symbol",
                        [](const RendererPanelContext &ctx, QWidget *parent) -> IRendererPanel * {
                            return new SingleSymbolPanel(ctx, parent);
                        })

} // namespace openswmmvis::ui
