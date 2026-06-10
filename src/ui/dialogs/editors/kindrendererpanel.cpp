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
#include "render/attributesource.h"   // O1-3 — RangeMode enum
#include "render/intervalbinner.h"
// Gap A1.3 — archetype-seeded renderer construction.
#include "render/rendererfactory.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/categorizedrenderer.h"
// Slice B.6a — Rule-aware mode.
#include "render/rule.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/colorrampcombobox.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

using OpenSWMM::Render::BinMethod;
using OpenSWMM::Render::IntervalBinner;

namespace openswmmvis::ui {

namespace {

constexpr int kModeNone       = 0;
constexpr int kModeGraduated  = 1;
constexpr int kModeCategorized = 2;

/*! Delegate that renders the Colour cell as a swatch and pops a colour
 *  picker on double-click. Reads the colour from BackgroundRole; writes
 *  back via setData. */
class ColorCellDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const QColor c = idx.data(Qt::BackgroundRole).value<QColor>();
        p->save();
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, opt.palette.highlight());
        QRect inner = opt.rect.adjusted(4, 4, -4, -4);
        if (c.isValid()) {
            p->setBrush(c);
            p->setPen(QPen(QColor(60, 60, 60), 0.8));
            p->drawRoundedRect(inner, 3, 3);
        }
        p->restore();
    }

    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        return nullptr;  // open dialog directly via editorEvent
    }

    bool editorEvent(QEvent *e, QAbstractItemModel *m,
                     const QStyleOptionViewItem &, const QModelIndex &idx) override
    {
        if (e->type() != QEvent::MouseButtonDblClick) return false;
        QColor cur = idx.data(Qt::BackgroundRole).value<QColor>();
        const QColor picked = QColorDialog::getColor(
            cur.isValid() ? cur : QColor(Qt::white),
            nullptr, QObject::tr("Class colour"),
            QColorDialog::ShowAlphaChannel);
        if (!picked.isValid()) return true;
        m->setData(idx, picked, Qt::BackgroundRole);
        return true;
    }
};

} // namespace

// ---------------------------------------------------------------------------

KindRendererPanel::KindRendererPanel(OpenSWMMVisLayer *hostLayer,
                                      OpenSWMMVis::SwmmCategory category,
                                      QWidget *parent)
    : QWidget(parent), m_hostLayer(hostLayer), m_category(category)
{
    m_modelLayer   = qobject_cast<SWMMModelLayer *>(hostLayer);
    m_resultsLayer = qobject_cast<SWMMResultsLayer *>(hostLayer);

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
    modeForm->addRow(tr("Mode:"), m_modeCombo);

    // Slice DM.2 — attribute picker. The row is added to the same form so
    // it sits directly under "Mode:". Hidden when the host layer (or the
    // Rule's containing layer) does not implement IAttributeProvider —
    // preserves the existing free-text path for Rule-based / GIS layers
    // that haven't been ported.
    m_attrCombo = new QComboBox(this);
    m_attrCombo->setMinimumContentsLength(20);
    m_attrCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    // Wrap the row label + combo in a single QWidget so we can hide both
    // together. QFormLayout has no per-row hide helper.
    m_attrRow = new QWidget(this);
    {
        auto *rowLay = new QHBoxLayout(m_attrRow);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->addWidget(new QLabel(tr("Attribute:"), m_attrRow));
        rowLay->addWidget(m_attrCombo, 1);
    }
    modeForm->addRow(m_attrRow);
    vlay->addLayout(modeForm);

    // Graduated controls — hidden until Graduated mode is selected.
    m_graduatedBox = new QWidget(this);
    auto *gForm = new QFormLayout(m_graduatedBox);

    m_rampCombo = new ColorRampComboBox(m_graduatedBox);
    gForm->addRow(tr("Colour ramp:"), m_rampCombo);

    m_methodCombo = new QComboBox(m_graduatedBox);
    // Data-sampling classification methods (compute breaks from the layer's
    // values when Auto-classify runs) + Manual (user types the breaks in the
    // table below). VS.3 added Natural breaks / Std dev / Log / Exponential.
    m_methodCombo->addItem(tr("Equal interval"),       int(BinMethod::EqualInterval));
    m_methodCombo->addItem(tr("Quantile"),             int(BinMethod::Quantile));
    m_methodCombo->addItem(tr("Natural breaks (Jenks)"), int(BinMethod::NaturalBreaks));
    m_methodCombo->addItem(tr("Standard deviation"),   int(BinMethod::StdDev));
    m_methodCombo->addItem(tr("Logarithmic"),          int(BinMethod::Logarithmic));
    m_methodCombo->addItem(tr("Exponential"),          int(BinMethod::Exponential));
    m_methodCombo->addItem(tr("Manual"),               int(BinMethod::Manual));
    gForm->addRow(tr("Method:"), m_methodCombo);

    m_countSpin = new QSpinBox(m_graduatedBox);
    m_countSpin->setRange(2, 32);
    m_countSpin->setValue(5);
    gForm->addRow(tr("Classes:"), m_countSpin);

    // O1-3 — animated range mode. Only meaningful for results (animated)
    // layers, so the row is hidden for static model layers in
    // refreshFromModel(). "Fixed over run" keeps a single classification
    // across the whole animation; "Per-frame auto-stretch" re-classifies
    // each frame against that frame's values.
    using OpenSWMM::Render::RangeMode;
    m_rangeCombo = new QComboBox(m_graduatedBox);
    m_rangeCombo->addItem(tr("Fixed over run"),       int(RangeMode::FixedOverRun));
    m_rangeCombo->addItem(tr("Per-frame auto-stretch"), int(RangeMode::PerFrameAutoStretch));
    m_rangeCombo->addItem(tr("Fixed (user breaks)"),  int(RangeMode::FixedUser));
    m_rangeRow = new QWidget(m_graduatedBox);
    {
        auto *rowLay = new QHBoxLayout(m_rangeRow);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->addWidget(new QLabel(tr("Range:"), m_rangeRow));
        rowLay->addWidget(m_rangeCombo, 1);
    }
    gForm->addRow(m_rangeRow);

    // Gap A2.2 — user-defined min/max, applied as the ramp range. Visible
    // only when "Fixed (user)" is the active range mode; FixedUser skips
    // every auto-classification so the breaks always honour these bounds.
    m_userRangeRow = new QWidget(m_graduatedBox);
    {
        auto *rowLay = new QHBoxLayout(m_userRangeRow);
        rowLay->setContentsMargins(0, 0, 0, 0);
        auto makeSpin = [this]() {
            auto *s = new QDoubleSpinBox(m_userRangeRow);
            s->setRange(-1.0e12, 1.0e12);
            s->setDecimals(4);
            s->setKeyboardTracking(false);
            return s;
        };
        m_userMinSpin = makeSpin();
        m_userMaxSpin = makeSpin();
        m_userMaxSpin->setValue(1.0);
        rowLay->addWidget(new QLabel(tr("Min:"), m_userRangeRow));
        rowLay->addWidget(m_userMinSpin, 1);
        rowLay->addWidget(new QLabel(tr("Max:"), m_userRangeRow));
        rowLay->addWidget(m_userMaxSpin, 1);
    }
    gForm->addRow(m_userRangeRow);
    m_userRangeRow->setVisible(false);

    // Gap A4.5 — output axis, archetype-gated: Point kinds scale marker
    // SIZE by the classified value, Line kinds scale stroke WIDTH; Polygon
    // kinds get neither (the row stays hidden). Model + persistence +
    // paint consumption already exist (Slices 8.13.43-α / VS.4) — this is
    // the missing editor surface.
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
    gForm->addRow(m_axisRow);
    m_axisRow->setVisible(false);

    m_autoBtn = new QToolButton(m_graduatedBox);
    m_autoBtn->setText(tr("Auto-classify from data"));
    m_autoBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    gForm->addRow(QString(), m_autoBtn);

    m_breaksModel = new QStandardItemModel(0, 4, m_graduatedBox);
    m_breaksModel->setHorizontalHeaderLabels({
        tr("Lower"), tr("Upper"), tr("Colour"), tr("Label")
    });
    m_breaksTable = new QTableView(m_graduatedBox);
    m_breaksTable->setModel(m_breaksModel);
    m_breaksTable->verticalHeader()->setVisible(false);
    m_breaksTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_breaksTable->horizontalHeader()->setStretchLastSection(true);
    m_breaksTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    m_breaksTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_breaksTable->setAlternatingRowColors(true);
    m_breaksTable->setItemDelegateForColumn(2, new ColorCellDelegate(m_breaksTable));
    m_breaksTable->setMinimumHeight(150);
    gForm->addRow(m_breaksTable);

    vlay->addWidget(m_graduatedBox);

    // Bindings
    connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &KindRendererPanel::onModeChanged);
    connect(m_attrCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &KindRendererPanel::onAttributeChanged);
    connect(m_rampCombo, &ColorRampComboBox::rampChanged,
            this, [this](const RasterColorRamp &) { onRampChanged(); });
    connect(m_methodCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &KindRendererPanel::onBinMethodChanged);
    connect(m_countSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &KindRendererPanel::onBinCountChanged);
    connect(m_autoBtn, &QToolButton::clicked,
            this, &KindRendererPanel::onAutoClassify);
    connect(m_rangeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &KindRendererPanel::onRangeModeChanged);
    connect(m_userMinSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &KindRendererPanel::onUserRangeChanged);
    connect(m_userMaxSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &KindRendererPanel::onUserRangeChanged);
    connect(m_axisCheck, &QCheckBox::toggled,
            this, &KindRendererPanel::onOutputAxisChanged);
    connect(m_axisMinSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &KindRendererPanel::onOutputAxisChanged);
    connect(m_axisMaxSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &KindRendererPanel::onOutputAxisChanged);
    connect(m_breaksModel, &QStandardItemModel::itemChanged,
            this, &KindRendererPanel::onBreakEdited);

    refreshFromModel();
}

// Slice B.6a — Rule-aware constructor. Delegates UI build to the legacy
// ctor by passing nullptr+CatJunctions sentinels, then overwrites m_rule
// and re-runs refreshFromModel against the Rule's owned renderer.
KindRendererPanel::KindRendererPanel(OpenSWMM::Render::Rule *rule, QWidget *parent)
    : KindRendererPanel(static_cast<OpenSWMMVisLayer *>(nullptr),
                        OpenSWMMVis::CatJunctions, parent)
{
    m_rule = rule;
    refreshFromModel();
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

namespace {
/*! Install a fresh renderer through whichever target the panel is bound
 *  to (Rule, model layer kind, or results layer kind). Encapsulates the
 *  three-way dispatch so the per-edit slots stay small. Slice B.6a. */
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

/*! Reset to defaults — Rule has no "default" notion, so it falls back
 *  to a fresh SingleSymbolRenderer. Layer paths use their existing
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

void KindRendererPanel::refreshFromModel()
{
    auto *r = currentRenderer();
    QSignalBlocker bm(m_modeCombo);
    QSignalBlocker ba(m_attrCombo);
    QSignalBlocker br(m_rampCombo);
    QSignalBlocker bb(m_methodCombo);
    QSignalBlocker bc(m_countSpin);
    QSignalBlocker brm(m_rangeCombo);

    int modeRow = kModeNone;
    QString currentAttribute;
    if (auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(r)) {
        modeRow = kModeGraduated;
        // findData (not setCurrentIndex(enum)) — combo order no longer
        // matches the BinMethod enum values now that Jenks/StdDev/Log/Exp
        // sit between Quantile and Manual.
        m_methodCombo->setCurrentIndex(
            m_methodCombo->findData(int(g->binner().method())));
        m_countSpin->setValue(g->binner().binCount());
        m_rangeCombo->setCurrentIndex(m_rangeCombo->findData(int(g->rangeMode())));
        currentAttribute = g->classifyAttribute();
    } else if (dynamic_cast<OpenSWMM::Render::CategorizedRenderer *>(r)) {
        modeRow = kModeCategorized;
    }
    m_modeCombo->setCurrentIndex(modeRow);

    // Slice DM.2 — populate the attribute combo from the host's
    // IAttributeProvider. Hide the row when no provider, or when the
    // mode is None / Categorized (the Categorized panel owns its own
    // attribute combo). Preserve the renderer's current attribute as
    // the selected item even when the canonical name isn't in the
    // provider list (display it verbatim).
    OpenSWMMVisLayer *layerForProvider = m_hostLayer;
    if (!layerForProvider && m_rule) {
        // Rule's QObject parent is its RuleList; the RuleList's parent
        // is the owning layer. Walk two parents up to find the
        // IAttributeProvider host.
        if (auto *p = m_rule->parent())
            layerForProvider = qobject_cast<OpenSWMMVisLayer *>(p->parent());
    }
    auto *provider = layerForProvider
        ? qobject_cast<OpenSWMM::Render::IAttributeProvider *>(layerForProvider)
        : nullptr;
    const bool showAttrRow = provider && modeRow == kModeGraduated;
    m_attrRow->setVisible(showAttrRow);
    m_attrCombo->clear();
    if (provider) {
        const auto fields = provider->availableAttributes(m_category);
        for (const auto &f : fields) {
            // Gap A4.3 — Graduated classifies numerics; string fields
            // (tag / status / group) belong to the Categorized panel.
            if (f.type == QMetaType::QString)
                continue;
            m_attrCombo->addItem(f.displayName, f.name);
        }
        // Restore selection by canonical name. When the renderer's
        // current attribute isn't in the list, prepend it so the
        // existing state isn't silently lost.
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

    // O1-3 — the animated range mode only applies to results (animated)
    // layers. Hide the row for static model / GIS layers so it doesn't
    // imply a behaviour that never fires.
    //
    // Gap A4.4 — gate on the SELECTED attribute's dynamism, not just the
    // host type: a static field on an animated layer cannot vary per frame,
    // so per-frame range modes would be a no-op lie for it.
    const bool isAnimated =
        qobject_cast<SWMMResultsLayer *>(layerForProvider) != nullptr;
    bool attrIsDynamic = isAnimated;
    if (provider && !currentAttribute.isEmpty()) {
        const auto fields = provider->availableAttributes(m_category);
        for (const auto &f : fields) {
            if (f.name == currentAttribute) {
                attrIsDynamic = f.isDynamic;
                break;
            }
        }
    }
    m_rangeRow->setVisible(isAnimated && attrIsDynamic
                           && modeRow == kModeGraduated);

    // Gap A2.2 — user range row tracks the FixedUser mode; values mirror
    // the renderer's ramp range (FixedUser stores the user range there).
    {
        bool userMode = false;
        if (auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(r)) {
            userMode = (g->rangeMode() == OpenSWMM::Render::RangeMode::FixedUser);
            QSignalBlocker bmin(m_userMinSpin), bmax(m_userMaxSpin);
            m_userMinSpin->setValue(g->ramp().minValue);
            m_userMaxSpin->setValue(g->ramp().maxValue);
        }
        m_userRangeRow->setVisible(isAnimated && modeRow == kModeGraduated
                                   && userMode);
    }

    // Gap A4.5 — output-axis row, archetype-gated. Point kinds expose the
    // size axis, Line kinds the width axis, Polygon kinds neither.
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
    }

    rebuildBreaksTable();
}

void KindRendererPanel::onOutputAxisChanged()
{
    // Gap A4.5 — push the axis toggle + pixel range onto the renderer.
    // The axis is archetype-selected: SIZE for point kinds, WIDTH for line
    // kinds (the row is hidden for polygons, so no third case).
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
}

void KindRendererPanel::onUserRangeChanged()
{
    // Gap A2.2 — push the user min/max into the renderer as the ramp range
    // and drop derived breaks; FixedUser mode never auto-classifies, so the
    // painter's inline equal-interval over the ramp range takes effect.
    if (m_suppressEdits) return;
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g || g->rangeMode() != OpenSWMM::Render::RangeMode::FixedUser) return;

    double mn = m_userMinSpin->value();
    double mx = m_userMaxSpin->value();
    if (mn > mx) std::swap(mn, mx);
    if (mn == mx) mx = mn + 1.0;   // degenerate range guard

    auto fresh = g->clone();
    if (auto *gf = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get())) {
        gf->setRange(mn, mx);
        gf->clearBreaks();
    }
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                    std::move(fresh));
    rebuildBreaksTable();
}

void KindRendererPanel::onAttributeChanged(int comboRow)
{
    // Slice DM.2 — push the picked attribute back onto the current
    // GraduatedRenderer and reinstall so rebuildKindFeatureOverrides
    // picks up the new attribute on the next paint. No-op when the
    // current renderer isn't Graduated.
    if (m_suppressEdits) return;
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    const QString name = m_attrCombo->itemData(comboRow).toString();
    if (name == g->classifyAttribute()) return;
    auto fresh = g->clone();
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get())) {
        gfresh->setClassifyAttribute(name);
        // New attribute → the old breaks/range are meaningless. Clear them so
        // the layer rebuild re-samples this attribute's values.
        gfresh->clearBreaks();
    }
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                    std::move(fresh));
    rebuildBreaksTable();
}

void KindRendererPanel::onRangeModeChanged(int comboRow)
{
    // O1-3 — push the chosen range mode onto the GraduatedRenderer and
    // reinstall. PerFrameAutoStretch is consumed by
    // SWMMResultsLayer::rebinDynamicRulesIfNeeded() on each animation
    // tick; the other modes leave the breaks fixed.
    if (m_suppressEdits) return;
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    const auto mode = static_cast<OpenSWMM::Render::RangeMode>(
        m_rangeCombo->itemData(comboRow).toInt());
    if (mode == g->rangeMode()) return;
    auto fresh = g->clone();
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get())) {
        gfresh->setRangeMode(mode);
        // Gap A2.2 — mode changes invalidate breaks derived under the old
        // mode: FixedUser takes the spin-box range; the auto modes
        // re-classify on the next override rebuild.
        if (mode == OpenSWMM::Render::RangeMode::FixedUser) {
            double mn = m_userMinSpin->value();
            double mx = m_userMaxSpin->value();
            if (mn > mx) std::swap(mn, mx);
            if (mn == mx) mx = mn + 1.0;
            gfresh->setRange(mn, mx);
        }
        gfresh->clearBreaks();
    }
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                    std::move(fresh));
    refreshFromModel();   // toggles the user-range row visibility
}

void KindRendererPanel::onModeChanged(int comboRow)
{
    using namespace OpenSWMM::Render;
    const int mode = m_modeCombo->itemData(comboRow).toInt();

    std::unique_ptr<IFeatureRenderer> next;
    if (mode == kModeNone) {
        // Reset to defaults (SingleSymbol).
        resetToDefaults(m_rule, m_modelLayer, m_resultsLayer, m_category);
        m_graduatedBox->setVisible(false);
        refreshFromModel();
        return;
    }

    // Gap A1.3 — construct through the shared factory: it seeds the new
    // renderer's base/fallback symbol from the outgoing renderer (or the
    // archetype skeleton — a default-constructed renderer's EMPTY base
    // symbol made paint silently fall back to the legacy ramp) and the
    // classify attribute from the host's IAttributeProvider (DM.2).
    QVector<AttributeField> fields;
    {
        OpenSWMMVisLayer *layerForProvider = m_hostLayer;
        if (!layerForProvider && m_rule) {
            // A Rule's QObject parent is its RuleList; the RuleList's
            // parent is the owning layer. Walk two parents up to find
            // the IAttributeProvider host.
            if (auto *p = m_rule->parent())
                layerForProvider = qobject_cast<OpenSWMMVisLayer *>(p->parent());
        }
        if (auto *provider = layerForProvider
            ? qobject_cast<OpenSWMM::Render::IAttributeProvider *>(layerForProvider)
            : nullptr)
            fields = provider->availableAttributes(m_category);
    }
    const auto archetype = FeatureSublayer::archetypeFor(m_category);

    if (mode == kModeGraduated) {
        next = RendererFactory::makeRenderer(
            QStringLiteral("graduated"), archetype, currentRenderer(),
            fields.isEmpty() ? nullptr : &fields);
        if (auto *g = dynamic_cast<GraduatedRenderer *>(next.get())) {
            // Panel-local knobs override the factory defaults.
            g->setRamp(m_rampCombo->currentRamp());
            IntervalBinner b;
            b.setMethod(BinMethod::EqualInterval);
            b.setBinCount(m_countSpin->value());
            g->setBinner(b);
        }
    } else if (mode == kModeCategorized) {
        next = RendererFactory::makeRenderer(
            QStringLiteral("categorized"), archetype, currentRenderer(),
            fields.isEmpty() ? nullptr : &fields);
    }

    if (next)
        installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                        std::move(next));
    // Slice DM.2 — full refresh so the attribute row populates and the
    // visible state matches the just-installed renderer.
    refreshFromModel();
}

void KindRendererPanel::onRampChanged()
{
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    g->setRamp(m_rampCombo->currentRamp());
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category, g->clone());
    // installRenderer triggers rebuildKindFeatureOverrides → repaint
    // (via setKindRenderer) or Rule::rendererReplaced (Rule path).
    rebuildBreaksTable();
}

void KindRendererPanel::onBinMethodChanged(int comboRow)
{
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    const auto method = static_cast<BinMethod>(m_methodCombo->itemData(comboRow).toInt());
    IntervalBinner b = g->binner();
    b.setMethod(method);

    if (method == BinMethod::Manual) {
        // Switching to Manual: seed the editable breaks from the current
        // (data-derived) classification so the user tweaks from a sensible
        // starting point rather than a collapsed/empty set. The user then
        // edits the "Upper" cells in the table to specify bins by hand.
        if (b.manualBreaks().isEmpty() && !g->lastBreaks().isEmpty())
            b.setManualBreaks(g->lastBreaks());
        g->setBinner(b);
        installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category, g->clone());
        rebuildBreaksTable();
        return;
    }

    // Data-sampling methods (Equal interval / Quantile / Jenks / StdDev /
    // Log / Exponential): re-classify against the layer's actual values.
    g->setBinner(b);
    onAutoClassify();
}

void KindRendererPanel::onBinCountChanged(int n)
{
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    IntervalBinner b = g->binner();
    b.setBinCount(n);
    g->setBinner(b);
    onAutoClassify();
}

void KindRendererPanel::onAutoClassify()
{
    // Push back through setKindRenderer to retrigger rebuildKindFeatureOverrides
    // which will autoClassify against real data + repaint.
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;
    // Clone then clear lastBreaks so the next layer rebuild re-runs
    // classifyIfNeeded against real data (re-samples + re-derives the range).
    auto fresh = g->clone();
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get()))
        gfresh->clearBreaks();
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category, std::move(fresh));
    rebuildBreaksTable();
}

void KindRendererPanel::onBreakEdited(QStandardItem *item)
{
    if (m_suppressEdits) return;
    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) return;

    // Pull breaks back out of the table. Manual mode lets users override.
    QVector<double> manualBreaks;
    const int rows = m_breaksModel->rowCount();
    // Interior breaks are between bin i and bin i+1 — that's the "Lower"
    // of row i+1 (== "Upper" of row i). Use the "Upper" column.
    for (int i = 0; i < rows - 1; ++i) {
        bool ok = false;
        const double v = m_breaksModel->item(i, 1)->text().toDouble(&ok);
        if (ok) manualBreaks.append(v);
    }
    if (!manualBreaks.isEmpty()) {
        IntervalBinner b = g->binner();
        b.setMethod(BinMethod::Manual);
        b.setManualBreaks(manualBreaks);
        g->setBinner(b);
        // Reflect Manual in the method combo without re-triggering it.
        QSignalBlocker bm(m_methodCombo);
        m_methodCombo->setCurrentIndex(
            m_methodCombo->findData(int(BinMethod::Manual)));
    }

    // Per-bin colour override — the swatch cell (column 2). The bin index is
    // the row; GraduatedRenderer keys per-bin colour overrides by bin-index
    // string. This hooks the graduated colours up to the breaks table so the
    // user can recolour an individual class directly here.
    if (item && item->column() == 2) {
        const QColor c = item->data(Qt::BackgroundRole).value<QColor>();
        if (c.isValid())
            g->setColorForClass(QString::number(item->row()), c);
    }

    auto fresh = g->clone();
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category, std::move(fresh));
}

void KindRendererPanel::rebuildBreaksTable()
{
    m_suppressEdits = true;
    m_breaksModel->setRowCount(0);

    auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(currentRenderer());
    if (!g) {
        m_suppressEdits = false;
        return;
    }

    QVector<double> breaks = g->lastBreaks();
    const int n = g->binner().binCount();
    const double rampMin = g->ramp().minValue;
    const double rampMax = g->ramp().maxValue;

    // Build the per-bin (lower, upper) ranges. lastBreaks holds interior
    // breaks only (n-1 entries). Bin 0 starts at rampMin; bin n-1 ends at
    // rampMax.
    QVector<double> lowers(n), uppers(n);
    for (int i = 0; i < n; ++i) {
        lowers[i] = (i == 0)         ? rampMin : breaks.value(i - 1, rampMin);
        uppers[i] = (i == n - 1)     ? rampMax : breaks.value(i,     rampMax);
    }

    for (int i = 0; i < n; ++i) {
        auto *lowerItem = new QStandardItem(QString::number(lowers[i], 'g', 6));
        lowerItem->setEditable(false);                  // edit "upper" of prev row instead
        auto *upperItem = new QStandardItem(QString::number(uppers[i], 'g', 6));
        upperItem->setEditable(i < n - 1);              // last bin's upper is rampMax (fixed)
        auto *colorItem = new QStandardItem;
        // Show the per-bin override colour when the user has set one
        // (g->colorForClass returns invalid when there's no override), else
        // the ramp colour for the bin's midpoint.
        const double t = (n > 1) ? (double(i) + 0.5) / double(n) : 0.5;
        const QColor ov = g->colorForClass(QString::number(i));
        const QColor c  = ov.isValid() ? ov : g->ramp().colorAt(t);
        colorItem->setData(c, Qt::BackgroundRole);
        colorItem->setEditable(true);
        auto *labelItem = new QStandardItem(tr("Class %1").arg(i + 1));
        labelItem->setEditable(true);
        m_breaksModel->appendRow({lowerItem, upperItem, colorItem, labelItem});
    }
    m_suppressEdits = false;
}

} // namespace openswmmvis::ui
