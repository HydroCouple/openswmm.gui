/*!
 * \file   singlesymbolrendererpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  IRendererPanel implementation for the "Single Symbol" renderer
 *         class.  Slice X.3.
 *
 *         For SWMM kinds the panel reuses the existing FeatureStyleEditor
 *         family (point/line/polygon archetypes) via the existing
 *         StyleEditorRegistry — no behaviour duplicated, just routed
 *         differently.
 */
#include "ui/dialogs/irendererpanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"
// Slice B.6c — Rule-aware Single Symbol panel.
#include "render/rule.h"
#include "render/symbolstyleadapter.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include "ui/dialogs/istyleeditorwidget.h"

#include <QHeaderView>
#include <QLabel>
#include <QTreeView>
#include <QVBoxLayout>

// SE.4 — the QPropertyModel generic-grid fallback was removed from the
// symbology editor. Every *SymbolStyleAdapter archetype now has a dedicated
// IStyleEditorWidget (symbolstyleeditors{,2d}.cpp), so StyleEditorRegistry
// always returns a real editor. QPropertyModel remains in use elsewhere
// (attribute panel, preferences, sublayer style dialog, …).

namespace openswmmvis::ui {

namespace {

class SingleSymbolPanel : public IRendererPanel
{
public:
    explicit SingleSymbolPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent), m_ctx(ctx)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);

        QObject *style = nullptr;

        // Slice B.6c — Rule path takes priority. Build a SymbolStyleAdapter
        // around the Rule's SingleSymbolRenderer and surface its
        // Q_PROPERTYs through the StyleEditorRegistry (falls through to
        // the QPropertyModel generic tree when no specific editor is
        // registered for SymbolStyleAdapter yet — that's the user-visible
        // form for now; a dedicated FeatureStyleEditor variant is the
        // named follow-up).
        //
        // Slice SS.1 — pick an archetype-aware adapter (Point / Line /
        // Polygon) via the factory so the QPropertyModel-driven editor
        // only surfaces properties that apply to the underlying
        // geometry. Falls back to the generic SymbolStyleAdapter when
        // the first SymbolLayer's kind doesn't map to one of the three
        // archetypes (raster / hillshade / contour passes).
        if (m_ctx.rule) {
            m_styleAdapter.reset(
                OpenSWMM::Render::SymbolStyleAdapter::createFor(m_ctx.rule));
            style = m_styleAdapter.get();
        }
        else if (m_ctx.category.has_value()) {
            if (auto *res = qobject_cast<SWMMResultsLayer *>(m_ctx.hostLayer)) {
                if (auto *sub = res->featureSublayer(*m_ctx.category))
                    style = sub->style();
            }
            // For SWMMModelLayer the per-kind subject is the existing
            // SwmmElementSymbolAdapter; surface it via styleSubjects().
            else if (qobject_cast<SWMMModelLayer *>(m_ctx.hostLayer)) {
                // Walk the layer's subjects to find one matching the
                // current category's routing id.
                auto subjects = m_ctx.hostLayer->styleSubjects();
                for (auto &sub : subjects) {
                    QString r = sub->routingId();
                    // routingId convention: "model.<kindName>" — extract
                    // the suffix and match against the category.
                    if (r.startsWith(QStringLiteral("model."))) {
                        const QString catSuffix = r.mid(6);
                        const QString want = [&]() -> QString {
                            switch (*m_ctx.category) {
                                case OpenSWMMVis::CatJunctions:     return QStringLiteral("junctions");
                                case OpenSWMMVis::CatOutfalls:      return QStringLiteral("outfalls");
                                case OpenSWMMVis::CatStorage:       return QStringLiteral("storage");
                                case OpenSWMMVis::CatDividers:      return QStringLiteral("dividers");
                                case OpenSWMMVis::CatConduits:      return QStringLiteral("conduits");
                                case OpenSWMMVis::CatPumps:         return QStringLiteral("pumps");
                                case OpenSWMMVis::CatOrifices:      return QStringLiteral("orifices");
                                case OpenSWMMVis::CatWeirs:         return QStringLiteral("weirs");
                                case OpenSWMMVis::CatOutlets:       return QStringLiteral("outlets");
                                case OpenSWMMVis::CatSubcatchments: return QStringLiteral("subcatchments");
                                case OpenSWMMVis::CatRainGages:     return QStringLiteral("raingages");
                                default:                            return QString();
                            }
                        }();
                        if (catSuffix == want) {
                            // Move the unique_ptr into our member so the
                            // subject (and the heap-allocated adapter it
                            // owns) outlive this panel.
                            m_modelSubject = std::move(sub);
                            style = m_modelSubject->propertyObject();
                            break;
                        }
                    }
                }
            }
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
        lay->addWidget(new QLabel(
            tr("No single-symbol editor registered for this layer kind."),
            this));
        lay->addStretch();
    }

    void refreshFromModel() override
    {
        // Children refresh themselves through their own NOTIFY hookups.
    }

    QString displayName() const override { return tr("Single Symbol"); }

private:
    RendererPanelContext  m_ctx;
    std::unique_ptr<ILayerStyleSubject>                  m_modelSubject;
    // Slice SS.1 — the factory returns one of four concrete subclasses
    // (Point / Line / Polygon / generic), so we hold it as the base
    // QObject. The QPropertyModel / StyleEditorRegistry only need a
    // QObject* anyway.
    std::unique_ptr<QObject>                              m_styleAdapter;  // Slice B.6c → SS.1
};

} // namespace

// Register the factory at static-init time.  RendererPanelRegistry walks
// in insertion order, so this entry appears first in the dropdown.
REGISTER_RENDERER_PANEL(
    "single", "Single Symbol",
    [](const RendererPanelContext &ctx, QWidget *parent) -> IRendererPanel * {
        return new SingleSymbolPanel(ctx, parent);
    })

} // namespace openswmmvis::ui
