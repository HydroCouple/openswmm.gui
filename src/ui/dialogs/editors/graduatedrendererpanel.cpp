/*!
 * \file   graduatedrendererpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  IRendererPanel implementation for the "Graduated" renderer
 *         class.  Slice X.4.
 *
 *         For now this is a thin wrapper around KindRendererPanel (which
 *         already has the colour ramp + bin method + class-breaks table
 *         the user asked for in §S2+S5).  A future iteration can promote
 *         the panel content here once the SWMM-specific bits have been
 *         pulled out.
 */
#include "ui/dialogs/irendererpanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/editors/kindrendererpanel.h"

#include <QLabel>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

class GraduatedPanel : public IRendererPanel
{
public:
    explicit GraduatedPanel(const RendererPanelContext &ctx, QWidget *parent)
        : IRendererPanel(parent)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        // Slice B.6a — Rule path takes priority.
        if (ctx.rule) {
            auto *panel = new KindRendererPanel(ctx.rule, this);
            lay->addWidget(panel, 1);
        } else if (ctx.hostLayer && ctx.category.has_value()) {
            auto *panel = new KindRendererPanel(
                ctx.hostLayer, *ctx.category, this);
            lay->addWidget(panel, 1);
        } else {
            lay->addWidget(new QLabel(
                tr("Graduated rendering at the layer level is not yet "
                   "supported — pick a kind from the tree."), this));
            lay->addStretch();
        }
    }

    void refreshFromModel() override { /* delegated to the embedded panel */ }
    QString displayName() const override { return tr("Graduated"); }
};

} // namespace

// Gap A4.2 — Graduated classifies a numeric attribute; grey the dropdown
// row out for kinds whose provider exposes none (e.g. Rain gages).
REGISTER_RENDERER_PANEL_GATED(
    "graduated", "Graduated",
    ([](const RendererPanelContext &ctx, QWidget *parent) -> IRendererPanel * {
        return new GraduatedPanel(ctx, parent);
    }),
    ([](const RendererPanelContext &ctx) { return ctx.hasNumeric(); }),
    "No numeric attributes available for this kind")

} // namespace openswmmvis::ui
