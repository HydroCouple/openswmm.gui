/*!
 * \file   kindrendererpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/kindrendererpanel.h"

#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
// Slice DM.2 — IAttributeProvider for the attribute picker.
#include "render/iattributeprovider.h"
#include "render/ifeaturerenderer.h"
// Gap A1.3 — archetype-seeded renderer construction.
#include "render/rendererfactory.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/categorizedrenderer.h"
// Slice B.6a — Rule-aware mode.
#include "render/rule.h"
// Slice US.1 — shared classification editor + binding.
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/widgets/colorbutton.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace openswmmvis::ui {

namespace {

constexpr int kModeNone        = 0;
constexpr int kModeGraduated   = 1;
constexpr int kModeCategorized = 2;

/*! Install a fresh renderer through whichever target the panel is bound to
 *  (Rule, model layer kind, or results layer kind). Slice B.6a. */
void installRenderer(OpenSWMM::Render::Rule *rule,
                     SWMMModelLayer *modelLayer,
                     SWMMResultsLayer *resultsLayer,
                     OpenSWMMVis::SwmmCategory category,
                     std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> next)
{
    if (rule)              rule->setRenderer(std::move(next));
    else if (modelLayer)   modelLayer->setKindRenderer(category, std::move(next));
    else if (resultsLayer) resultsLayer->setKindRenderer(category, std::move(next));
}

/*! Reset to defaults — Rule has no "default" notion, so it falls back to a
 *  fresh SingleSymbolRenderer. Layer paths use their existing
 *  resetKindRendererToDefaults which picks per-kind glyph defaults. */
void resetToDefaults(OpenSWMM::Render::Rule *rule,
                     SWMMModelLayer *modelLayer,
                     SWMMResultsLayer *resultsLayer,
                     OpenSWMMVis::SwmmCategory category)
{
    using OpenSWMM::Render::SingleSymbolRenderer;
    if (rule)              rule->setRenderer(std::make_unique<SingleSymbolRenderer>());
    else if (modelLayer)   modelLayer->resetKindRendererToDefaults(category);
    else if (resultsLayer) resultsLayer->resetKindRendererToDefaults(category);
}

} // namespace

// ---------------------------------------------------------------------------

KindRendererPanel::KindRendererPanel(OpenSWMMVisLayer *hostLayer,
                                      OpenSWMMVis::SwmmCategory category,
                                      QWidget *parent)
    : QWidget(parent), m_hostLayer(hostLayer), m_category(category)
{
    m_modelLayer   = qobject_cast<SWMMModelLayer *>(hostLayer);
    m_resultsLayer = qobject_cast<SWMMResultsLayer *>(hostLayer);
    buildUi();
    refreshFromModel();
}

// Slice B.6a — Rule-aware constructor. Delegates UI build to the legacy ctor
// by passing nullptr+CatJunctions sentinels, then overwrites m_rule and
// re-runs refreshFromModel against the Rule's owned renderer.
KindRendererPanel::KindRendererPanel(OpenSWMM::Render::Rule *rule, QWidget *parent)
    : KindRendererPanel(static_cast<OpenSWMMVisLayer *>(nullptr),
                        OpenSWMMVis::CatJunctions, parent)
{
    m_rule = rule;
    refreshFromModel();
}

// Rule + kind context. Delegates to the layer ctor so m_hostLayer /
// m_modelLayer / m_resultsLayer / m_category are all resolved, then installs
// the Rule — which takes priority in currentRenderer(), installRenderer() and
// resetToDefaults(), so the renderer path behaves exactly as the rule-only
// ctor did. The layer context is what lets syncArrowsFromHost() find the
// per-kind flow-arrow channel (see the header note).
KindRendererPanel::KindRendererPanel(OpenSWMM::Render::Rule *rule,
                                      OpenSWMMVisLayer *hostLayer,
                                      OpenSWMMVis::SwmmCategory category,
                                      QWidget *parent)
    : KindRendererPanel(hostLayer, category, parent)
{
    m_rule = rule;
    refreshFromModel();
}

KindRendererPanel::~KindRendererPanel() = default;

void KindRendererPanel::buildUi()
{
    auto *box = new QGroupBox(tr("Classified rendering"), this);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);

    auto *vlay = new QVBoxLayout(box);
    auto *modeForm = new QFormLayout;
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("None (single symbol)"), kModeNone);
    m_modeCombo->addItem(tr("Graduated (numeric ramp)"), kModeGraduated);
    m_modeCombo->addItem(tr("Categorized (coming soon)"), kModeCategorized);
    modeForm->addRow(tr("&Mode:"), m_modeCombo);

    // Slice DM.2 — attribute picker. Hidden when the host layer (or the Rule's
    // containing layer) does not implement IAttributeProvider.
    m_attrCombo = new QComboBox(this);
    m_attrCombo->setMinimumContentsLength(20);
    m_attrCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_attrRow = new QWidget(this);
    {
        auto *rowLay = new QHBoxLayout(m_attrRow);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->addWidget(new QLabel(tr("Color by:"), m_attrRow));
        rowLay->addWidget(m_attrCombo, 1);
    }
    modeForm->addRow(m_attrRow);
    vlay->addLayout(modeForm);

    // Graduated controls — hidden until Graduated mode is selected. The
    // classification block is the shared ClassificationEditor; the output-axis
    // row below it is 1D-specific (size/width by value).
    m_graduatedBox = new QWidget(this);
    auto *gLay = new QVBoxLayout(m_graduatedBox);
    gLay->setContentsMargins(0, 0, 0, 0);

    // Slice US.1 — GraduatedRendererBinding bridges the shared editor onto
    // this panel's per-kind GraduatedRenderer.
    m_binding = std::make_unique<GraduatedRendererBinding>(
        // getRenderer
        [this]() -> OpenSWMM::Render::GraduatedRenderer * {
            return dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
        },
        // installRenderer
        [this](std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> next) {
            installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                            std::move(next));
        },
        // attributeList — empty; this panel owns the attribute picker.
        []() { return QVector<QPair<QString, QString>>{}; },
        // rangeModesEnabled — animated host AND a dynamic selected attribute.
        [this]() -> bool {
            auto *host = attributeProviderHost();
            const bool isAnimated = qobject_cast<SWMMResultsLayer *>(host) != nullptr;
            if (!isAnimated) return false;
            auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
            if (!g) return false;
            auto *provider = host
                ? qobject_cast<OpenSWMM::Render::IAttributeProvider *>(host)
                : nullptr;
            if (!provider) return true;     // animated, no provider → allow
            const QString attr = g->classifyAttribute();
            if (attr.isEmpty()) return true;
            for (const auto &f : provider->availableAttributes(m_category))
                if (f.name == attr) return f.isDynamic;
            return true;
        });

    m_classEditor = new ClassificationEditor(m_binding.get(), /*ownBinding=*/false,
                                             m_graduatedBox);
    gLay->addWidget(m_classEditor);

    // Gap A4.5 — output axis, archetype-gated: Point kinds scale marker SIZE,
    // Line kinds scale stroke WIDTH; Polygon kinds get neither.
    m_axisRow = new QWidget(m_graduatedBox);
    {
        auto *rowLay = new QHBoxLayout(m_axisRow);
        rowLay->setContentsMargins(0, 0, 0, 0);
        m_axisCheck = new QCheckBox(tr("Size by value"), m_axisRow);
        auto makeSpin = [this]() {
            auto *s = new QDoubleSpinBox(m_axisRow);
            s->setRange(0.1, 256.0);
            s->setDecimals(1);
            s->setSuffix(QStringLiteral(" px"));
            s->setKeyboardTracking(false);
            return s;
        };
        m_axisMinSpin = makeSpin();
        m_axisMinSpin->setValue(4.0);
        m_axisMaxSpin = makeSpin();
        m_axisMaxSpin->setValue(18.0);
        rowLay->addWidget(m_axisCheck, 1);
        rowLay->addWidget(new QLabel(tr("Min:"), m_axisRow));
        rowLay->addWidget(m_axisMinSpin);
        rowLay->addWidget(new QLabel(tr("Max:"), m_axisRow));
        rowLay->addWidget(m_axisMaxSpin);
    }
    gLay->addWidget(m_axisRow);
    m_axisRow->setVisible(false);

    // "Size by:" — independent attribute for the size/width axis. The
    // "(same as color)" entry (empty data) keeps the historical bin-mapped
    // behaviour; picking a field scales sizes over that field's own range.
    m_sizeAttrRow = new QWidget(m_graduatedBox);
    {
        auto *rowLay = new QHBoxLayout(m_sizeAttrRow);
        rowLay->setContentsMargins(0, 0, 0, 0);
        m_sizeAttrCombo = new QComboBox(m_sizeAttrRow);
        m_sizeAttrCombo->setMinimumContentsLength(20);
        m_sizeAttrCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        rowLay->addWidget(new QLabel(tr("Size by:"), m_sizeAttrRow));
        rowLay->addWidget(m_sizeAttrCombo, 1);
    }
    gLay->addWidget(m_sizeAttrRow);
    m_sizeAttrRow->setVisible(false);

    vlay->addWidget(m_graduatedBox);

    // ── Flow-direction arrows (link kinds) ─────────────────────────────
    // Outside m_graduatedBox on purpose: arrows belong to the kind, so they
    // stay put across every renderer mode rather than disappearing with the
    // classification block.
    m_arrowBox = new QGroupBox(tr("Flow direction arrows"), this);
    {
        auto *aForm = new QFormLayout(m_arrowBox);
        m_arrowShowChk = new QCheckBox(tr("Show flow arrows"), m_arrowBox);
        aForm->addRow(QString(), m_arrowShowChk);
        m_arrowSizeSpin = new QDoubleSpinBox(m_arrowBox);
        m_arrowSizeSpin->setRange(2.0, 60.0);
        m_arrowSizeSpin->setDecimals(1);
        m_arrowSizeSpin->setSingleStep(0.5);
        m_arrowSizeSpin->setSuffix(tr(" px"));
        aForm->addRow(tr("Si&ze:"), m_arrowSizeSpin);
        m_arrowColorBtn = new ColorButton(m_arrowBox);
        aForm->addRow(tr("Colou&r:"), m_arrowColorBtn);
    }
    m_arrowBox->setVisible(false);          // shown by syncArrowsFromHost()
    vlay->addWidget(m_arrowBox);

    connect(m_arrowShowChk, &QCheckBox::toggled,
            this, &KindRendererPanel::onArrowsChanged);
    connect(m_arrowSizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double) { onArrowsChanged(); });
    connect(m_arrowColorBtn, &ColorButton::colorChanged,
            this, [this](const QColor &) { onArrowsChanged(); });

    connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &KindRendererPanel::onModeChanged);
    connect(m_attrCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &KindRendererPanel::onAttributeChanged);
    connect(m_axisCheck, &QCheckBox::toggled,
            this, &KindRendererPanel::onOutputAxisChanged);
    connect(m_axisMinSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &KindRendererPanel::onOutputAxisChanged);
    connect(m_axisMaxSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &KindRendererPanel::onOutputAxisChanged);
    connect(m_sizeAttrCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &KindRendererPanel::onSizeAttributeChanged);
}

// ---------------------------------------------------------------------------

OpenSWMM::Render::IFeatureRenderer *KindRendererPanel::currentRenderer() const
{
    // Slice B.6a — Rule path takes priority.
    if (m_rule)         return m_rule->renderer();
    if (m_modelLayer)   return m_modelLayer  ->kindRenderer(m_category);
    if (m_resultsLayer) return m_resultsLayer->kindRenderer(m_category);
    return nullptr;
}

OpenSWMMVisLayer *KindRendererPanel::attributeProviderHost() const
{
    if (m_hostLayer) return m_hostLayer;
    if (m_rule) {
        // A Rule's QObject parent is its RuleList; the RuleList's parent is
        // the owning layer. Walk two parents up to find the host.
        if (auto *p = m_rule->parent())
            return qobject_cast<OpenSWMMVisLayer *>(p->parent());
    }
    return nullptr;
}

void KindRendererPanel::refreshFromModel()
{
    auto *r = currentRenderer();
    QSignalBlocker bm(m_modeCombo);
    QSignalBlocker ba(m_attrCombo);

    int modeRow = kModeNone;
    QString currentAttribute;
    if (auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(r)) {
        modeRow = kModeGraduated;
        currentAttribute = g->classifyAttribute();
    } else if (dynamic_cast<OpenSWMM::Render::CategorizedRenderer *>(r)) {
        modeRow = kModeCategorized;
    }
    m_modeCombo->setCurrentIndex(modeRow);

    // Slice DM.2 — populate the attribute combo from the host's
    // IAttributeProvider. Hide when no provider or when the mode is None /
    // Categorized (the Categorized panel owns its own attribute combo).
    OpenSWMMVisLayer *layerForProvider = attributeProviderHost();
    auto *provider = layerForProvider
        ? qobject_cast<OpenSWMM::Render::IAttributeProvider *>(layerForProvider)
        : nullptr;
    const bool showAttrRow = provider && modeRow == kModeGraduated;
    m_attrRow->setVisible(showAttrRow);
    m_attrCombo->clear();
    if (provider) {
        const auto fields = provider->availableAttributes(m_category);
        for (const auto &f : fields) {
            // Gap A4.3 — Graduated classifies numerics; string fields belong
            // to the Categorized panel.
            if (f.type == QMetaType::QString)
                continue;
            m_attrCombo->addItem(f.displayName, f.name);
        }
        int idx = -1;
        for (int i = 0; i < m_attrCombo->count(); ++i) {
            if (m_attrCombo->itemData(i).toString() == currentAttribute) {
                idx = i; break;
            }
        }
        if (idx < 0 && !currentAttribute.isEmpty()) {
            m_attrCombo->insertItem(0, currentAttribute, currentAttribute);
            idx = 0;
        }
        if (idx >= 0)
            m_attrCombo->setCurrentIndex(idx);
    }

    m_graduatedBox->setVisible(modeRow == kModeGraduated);

    // Gap A4.5 — output-axis row, archetype-gated. Point kinds expose the size
    // axis, Line kinds the width axis, Polygon kinds neither.
    {
        using OpenSWMM::Render::FeatureSublayer;
        const auto archetype = FeatureSublayer::archetypeFor(m_category);
        const bool axisApplies =
            modeRow == kModeGraduated
            && archetype != FeatureSublayer::Archetype::Polygon;
        m_axisRow->setVisible(axisApplies);
        if (axisApplies) {
            m_axisCheck->setText(archetype == FeatureSublayer::Archetype::Point
                                     ? tr("Size by value")
                                     : tr("Width by value"));
            if (auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(r)) {
                QSignalBlocker bc(m_axisCheck);
                QSignalBlocker bmin(m_axisMinSpin), bmax(m_axisMaxSpin);
                if (archetype == FeatureSublayer::Archetype::Point) {
                    m_axisCheck->setChecked(g->outputSizeEnabled());
                    m_axisMinSpin->setValue(g->outputSizeMin());
                    m_axisMaxSpin->setValue(g->outputSizeMax());
                } else {
                    m_axisCheck->setChecked(g->outputWidthEnabled());
                    m_axisMinSpin->setValue(g->outputWidthMin());
                    m_axisMaxSpin->setValue(g->outputWidthMax());
                }
                m_axisMinSpin->setEnabled(m_axisCheck->isChecked());
                m_axisMaxSpin->setEnabled(m_axisCheck->isChecked());
            }
        }

        // "Size by:" — same visibility gate as the axis row; populated with
        // the numeric fields plus a "(same as color)" default (empty data).
        QSignalBlocker bs(m_sizeAttrCombo);
        m_sizeAttrRow->setVisible(axisApplies && provider);
        m_sizeAttrCombo->clear();
        if (axisApplies && provider) {
            m_sizeAttrCombo->addItem(tr("(same as color)"), QString());
            const auto fields = provider->availableAttributes(m_category);
            for (const auto &f : fields) {
                if (f.type == QMetaType::QString)
                    continue;
                m_sizeAttrCombo->addItem(f.displayName, f.name);
            }
            QString currentSizeAttr;
            if (auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(r))
                currentSizeAttr = g->sizeAttribute();
            int sidx = m_sizeAttrCombo->findData(currentSizeAttr);
            if (sidx < 0 && !currentSizeAttr.isEmpty()) {
                m_sizeAttrCombo->addItem(currentSizeAttr, currentSizeAttr);
                sidx = m_sizeAttrCombo->count() - 1;
            }
            m_sizeAttrCombo->setCurrentIndex(sidx < 0 ? 0 : sidx);
            m_sizeAttrCombo->setEnabled(m_axisCheck->isChecked());
        }
    }

    // Slice US.1 — re-seed the shared editor from the live renderer.
    if (m_binding) m_binding->resync();
    if (m_classEditor) m_classEditor->refresh();

    // Arrows live on the kind, not the renderer, so they re-seed here too —
    // this is also the path the dialog calls after a Cancel rollback.
    syncArrowsFromHost();
}

namespace {

/*! The link kinds that carry a flow direction at all. Nodes and
 *  subcatchments have no upstream/downstream, so the group stays hidden. */
bool isLinkKind(OpenSWMMVis::SwmmCategory c)
{
    using namespace OpenSWMMVis;
    return c == CatConduits || c == CatPumps || c == CatOrifices
        || c == CatWeirs    || c == CatOutlets;
}

} // namespace

void KindRendererPanel::syncArrowsFromHost()
{
    if (!m_arrowBox) return;

    // Two independent channels carry "flow arrows", and which one applies is
    // decided by the host, not by this panel:
    //   - model layers  → SWMMElementSymbol::showArrows/arrowSize/arrowColor,
    //                     reached through the layer's PERSISTENT per-kind
    //                     adapter so edits land on the same object the
    //                     single-symbol editor and Cancel rollback use;
    //   - results layers → LineFeatureSublayerStyle::showFlowArrows /
    //                     arrowLengthPx / arrowColor.
    // Anything else (rule-only mode, GIS vector) has no channel: hide.
    if (!isLinkKind(m_category)) { m_arrowBox->setVisible(false); return; }

    bool   show = false;
    double size = 10.0;
    QColor color(34, 34, 34);
    bool   have = false;

    if (m_modelLayer) {
        show  = m_modelLayer->linkArrowsEnabled(m_category);
        size  = m_modelLayer->linkArrowSize(m_category);
        color = m_modelLayer->linkArrowColor(m_category);
        have  = true;
    } else if (m_resultsLayer) {
        if (auto *sub = m_resultsLayer->featureSublayer(m_category)) {
            if (auto *ls = qobject_cast<OpenSWMM::Render::LineFeatureSublayerStyle *>(
                    sub->featureStyle())) {
                show  = ls->showFlowArrows();
                size  = ls->arrowLengthPx();
                color = ls->arrowColor();
                have  = true;
            }
        }
    }

    m_arrowBox->setVisible(have);
    if (!have) return;

    QSignalBlocker b1(m_arrowShowChk), b2(m_arrowSizeSpin), b3(m_arrowColorBtn);
    m_arrowShowChk->setChecked(show);
    m_arrowSizeSpin->setValue(size);
    m_arrowColorBtn->setColor(color);
    m_arrowSizeSpin->setEnabled(show);
    m_arrowColorBtn->setEnabled(show);
}

void KindRendererPanel::onArrowsChanged()
{
    if (m_suppressEdits) return;

    const bool   show  = m_arrowShowChk->isChecked();
    const double size  = m_arrowSizeSpin->value();
    const QColor color = m_arrowColorBtn->color();
    m_arrowSizeSpin->setEnabled(show);
    m_arrowColorBtn->setEnabled(show);

    if (m_modelLayer) {
        // The per-kind setters resync the persistent symbol adapter and emit
        // repaintRequested, so the single-symbol editor and Cancel rollback
        // see the same edit this panel just made.
        m_modelLayer->setLinkArrowsEnabled(m_category, show);
        m_modelLayer->setLinkArrowSize(m_category, size);
        m_modelLayer->setLinkArrowColor(m_category, color);
    } else if (m_resultsLayer) {
        if (auto *sub = m_resultsLayer->featureSublayer(m_category)) {
            if (auto *ls = qobject_cast<OpenSWMM::Render::LineFeatureSublayerStyle *>(
                    sub->featureStyle())) {
                ls->setShowFlowArrows(show);
                ls->setArrowLengthPx(size);
                ls->setArrowColor(color);
            }
        }
    }
}

void KindRendererPanel::onOutputAxisChanged()
{
    // Gap A4.5 — push the axis toggle + pixel range onto the renderer.
    if (m_suppressEdits) return;
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;

    using OpenSWMM::Render::FeatureSublayer;
    const auto archetype = FeatureSublayer::archetypeFor(m_category);
    if (archetype == FeatureSublayer::Archetype::Polygon) return;

    double mn = m_axisMinSpin->value();
    double mx = m_axisMaxSpin->value();
    if (mn > mx) std::swap(mn, mx);
    const bool on = m_axisCheck->isChecked();
    m_axisMinSpin->setEnabled(on);
    m_axisMaxSpin->setEnabled(on);
    if (m_sizeAttrCombo) m_sizeAttrCombo->setEnabled(on);

    auto fresh = g->clone();
    if (auto *gf = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get())) {
        if (archetype == FeatureSublayer::Archetype::Point) {
            gf->setOutputSizeEnabled(on);
            gf->setOutputSizeRange(mn, mx);
        } else {
            gf->setOutputWidthEnabled(on);
            gf->setOutputWidthRange(mn, mx);
        }
    }
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                    std::move(fresh));
    if (m_binding) m_binding->resync();
}

void KindRendererPanel::onSizeAttributeChanged(int comboRow)
{
    // Push the independent size attribute onto the current GraduatedRenderer.
    // Empty data ("(same as color)") restores the bin-mapped behaviour.
    if (m_suppressEdits) return;
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    const QString name = m_sizeAttrCombo->itemData(comboRow).toString();
    if (name == g->sizeAttribute()) return;
    auto fresh = g->clone();
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get()))
        gfresh->setSizeAttribute(name);   // invalidates the sampled value range
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                    std::move(fresh));
}

void KindRendererPanel::onAttributeChanged(int comboRow)
{
    // Slice DM.2 — push the picked attribute back onto the current
    // GraduatedRenderer. No-op when the current renderer isn't Graduated.
    if (m_suppressEdits) return;
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    const QString name = m_attrCombo->itemData(comboRow).toString();
    if (name == g->classifyAttribute()) return;
    auto fresh = g->clone();
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get())) {
        gfresh->setClassifyAttribute(name);
        // New attribute → old breaks/range are meaningless. Clear so the layer
        // rebuild re-samples this attribute's values.
        gfresh->clearBreaks();
    }
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                    std::move(fresh));
    // The attribute's dynamism may flip the range-mode row; re-seed + refresh.
    if (m_binding) m_binding->resync();
    if (m_classEditor) m_classEditor->refresh();
}

void KindRendererPanel::onModeChanged(int comboRow)
{
    using namespace OpenSWMM::Render;
    const int mode = m_modeCombo->itemData(comboRow).toInt();

    if (mode == kModeNone) {
        resetToDefaults(m_rule, m_modelLayer, m_resultsLayer, m_category);
        m_graduatedBox->setVisible(false);
        refreshFromModel();
        return;
    }

    // Gap A1.3 — construct through the shared factory: it seeds the new
    // renderer's base/fallback symbol from the outgoing renderer (or the
    // archetype skeleton) and the classify attribute from the host's
    // IAttributeProvider (DM.2).
    QVector<AttributeField> fields;
    if (auto *host = attributeProviderHost()) {
        if (auto *provider = qobject_cast<OpenSWMM::Render::IAttributeProvider *>(host))
            fields = provider->availableAttributes(m_category);
    }
    const auto archetype = FeatureSublayer::archetypeFor(m_category);

    std::unique_ptr<IFeatureRenderer> next;
    if (mode == kModeGraduated) {
        next = RendererFactory::makeRenderer(
            QStringLiteral("graduated"), archetype, currentRenderer(),
            fields.isEmpty() ? nullptr : &fields);
    } else if (mode == kModeCategorized) {
        next = RendererFactory::makeRenderer(
            QStringLiteral("categorized"), archetype, currentRenderer(),
            fields.isEmpty() ? nullptr : &fields);
    }

    if (next)
        installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                        std::move(next));
    refreshFromModel();
}

} // namespace openswmmvis::ui
