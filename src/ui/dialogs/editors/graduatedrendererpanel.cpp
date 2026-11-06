/**
 * @file      graduatedrendererpanel.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     IRendererPanel implementation for the "Graduated" renderer
 *         class.  Slice X.4.
 *
 *         For now this is a thin wrapper around KindRendererPanel (which
 *         already has the colour ramp + bin method + class-breaks table
 *         the user asked for in §S2+S5).  A future iteration can promote
 *         the panel content here once the SWMM-specific bits have been
 *         pulled out.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/irendererpanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/editors/basesymbolgroup.h" // feedback C2
#include "ui/dialogs/editors/kindrendererpanel.h"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QVBoxLayout>

namespace openswmmvis::ui
{

namespace
{

class GraduatedPanel : public IRendererPanel
{
public:
    /**
     * @brief Compose optional base-symbol controls and a contextual per-kind classification
     * panel.
     * @param ctx Borrowed context consumed during construction; raw host/rule targets must
     * outlive created child editors. Rule+host+category chooses contextual rule constructor,
     * rule-only uses its sentinel context, otherwise host+category uses legacy kind dispatch.
     * @param parent Optional Qt owner; caller owns parentless panel.
     * Unsupported layer-only context shows a label. When both base and classification panels
     * exist, a callback enables named baseSymbolSize spins iff output size-by-value is off;
     * it borrows base-owned controls and is initialized immediately. Embedded construction/
     * refresh can seed a missing classification attribute. No separate undo/Cancel snapshot.
     */
    explicit GraduatedPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        // Feedback C2 — size / outline / labels stay editable under a
        // classified renderer (colour still comes from the classes).
        QWidget *base = createBaseSymbolGroup(ctx, this);
        if (base)
            lay->addWidget(base);
        KindRendererPanel *panel = nullptr;
        // Slice B.6a — Rule path takes priority. Hand it the kind context
        // when we have it: the renderer still goes through the Rule, but the
        // panel needs the layer + category to reach the per-kind flow-arrow
        // channel, which is not renderer state and so has no Rule mirror.
        if (ctx.rule && ctx.hostLayer && ctx.category.has_value()) {
            panel = new KindRendererPanel(ctx.rule, ctx.hostLayer, *ctx.category, this);
            lay->addWidget(panel, 1);
        } else if (ctx.rule) {
            panel = new KindRendererPanel(ctx.rule, this);
            lay->addWidget(panel, 1);
        } else if (ctx.hostLayer && ctx.category.has_value()) {
            panel = new KindRendererPanel(ctx.hostLayer, *ctx.category, this);
            lay->addWidget(panel, 1);
        } else {
            lay->addWidget(new QLabel(tr("Graduated rendering at the layer level is not yet "
                                         "supported — pick a kind from the tree."),
                                      this));
            lay->addStretch();
        }
        // "Size by value" owns the size axis while it is on: grey the static
        // size / line-width spin so the two don't fight.
        if (base && panel) {
            auto greyStaticSize = [base](bool sizeByValue) {
                for (auto *spin :
                     base->findChildren<QDoubleSpinBox *>(QStringLiteral("baseSymbolSize")))
                    spin->setEnabled(!sizeByValue);
            };
            connect(panel, &KindRendererPanel::outputAxisToggled, base, greyStaticSize);
            greyStaticSize(panel->sizeByValueEnabled());
        }
    }

    /**
     * @brief Perform no explicit refresh in this wrapper despite its embedded classification
     * panel.
     * Does not call the child's refresh or subscribe to host changes here; callers needing
     * fresh child state must use normal child paths/remounting. No model mutation.
     */
    void refreshFromModel() override { /* delegated to the embedded panel */ }
    /**
     * @brief Return this panel's translated display label.
     * @return Owned Graduated string, independent of renderer availability.
     */
    QString displayName() const override { return tr("Graduated"); }
};

} // namespace

// Gap A4.2 — Graduated classifies a numeric attribute; grey the dropdown
// row out for kinds whose provider exposes none (e.g. Rain gages).
REGISTER_RENDERER_PANEL_GATED("graduated", "Graduated",
                              ([](const RendererPanelContext &ctx, QWidget *parent)
                                   -> IRendererPanel * { return new GraduatedPanel(ctx, parent); }),
                              ([](const RendererPanelContext &ctx) { return ctx.hasNumeric(); }),
                              "No numeric attributes available for this kind")

} // namespace openswmmvis::ui
