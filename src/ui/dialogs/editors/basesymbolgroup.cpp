/*!
 * \file   basesymbolgroup.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/basesymbolgroup.h"

#include "layers/swmmmodellayer.h"
#include "layers/swmmelementsymboladapter.h"
#include "layers/swmmresultslayer.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "ui/dialogs/editors/kindrendererpanel.h"
#include "ui/dialogs/irendererpanel.h"
#include "ui/dialogs/istyleeditorwidget.h"

#include <QToolButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

QObject *baseSymbolStyleFor(OpenSWMMVisLayer *host, OpenSWMMVis::SwmmCategory category)
{
    if (auto *res = qobject_cast<SWMMResultsLayer *>(host)) {
        if (auto *sub = res->featureSublayer(category))
            return sub->style();
        return nullptr;
    }
    // For SWMMModelLayer the per-kind subject is the layer's PERSISTENT
    // SwmmElementSymbolAdapter — same instance the dialog's subjects wrap,
    // so edits made here are covered by the dialog's Cancel snapshot and
    // nothing leaks.
    if (auto *swmm = qobject_cast<SWMMModelLayer *>(host)) {
        const QString suffix = [&]() -> QString {
            switch (category) {
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
        if (!suffix.isEmpty())
            return swmm->elementSymbolAdapter(QStringLiteral("model.") + suffix);
    }
    return nullptr;
}

QWidget *createBaseSymbolGroup(const RendererPanelContext &ctx, QWidget *parent)
{
    if (!ctx.hostLayer || !ctx.category.has_value()) return nullptr;
    QObject *style = baseSymbolStyleFor(ctx.hostLayer, *ctx.category);
    if (!style) return nullptr;

    auto *group = new QWidget(parent);
    group->setObjectName(QStringLiteral("baseSymbolGroup"));
    auto *lay = new QVBoxLayout(group);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    auto *toggle = new QToolButton(group);
    toggle->setObjectName(QStringLiteral("baseSymbolToggle"));
    toggle->setText(QObject::tr("Base symbol && labels"));
    toggle->setToolTip(QObject::tr(
        "Size / line width, outline, marker shape and labels for this kind. "
        "Colour comes from the classes below."));
    toggle->setCheckable(true);
    toggle->setChecked(false);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setAutoRaise(true);
    lay->addWidget(toggle);

    auto *content = new QWidget(group);
    auto *contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(12, 0, 0, 0);
    auto *editor = StyleEditorRegistry::instance().createEditorFor(style, content);
    if (!editor) {
        delete group;
        return nullptr;
    }
    // The results-layer feature-style editors embed their own classified-
    // rendering panel; the panel this group sits in already owns
    // classification, so hide the nested copy.
    for (auto *nested : editor->findChildren<KindRendererPanel *>())
        nested->hide();
    contentLay->addWidget(editor);
    content->setVisible(false);
    lay->addWidget(content);

    QObject::connect(toggle, &QToolButton::toggled, content,
                     [toggle, content](bool on) {
                         content->setVisible(on);
                         toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                     });
    return group;
}

} // namespace openswmmvis::ui
