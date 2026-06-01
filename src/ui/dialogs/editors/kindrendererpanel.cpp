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
#include "render/renderers/singlesymbolrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/categorizedrenderer.h"
// Slice B.6a — Rule-aware mode.
#include "render/rule.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/colorrampcombobox.h"

#include <QColor>
#include <QColorDialog>
#include <QComboBox>
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
    m_methodCombo->addItem(tr("Equal interval"), int(BinMethod::EqualInterval));
    m_methodCombo->addItem(tr("Quantile"),        int(BinMethod::Quantile));
    m_methodCombo->addItem(tr("Manual"),          int(BinMethod::Manual));
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
        m_methodCombo->setCurrentIndex(int(g->binner().method()));
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
        for (const auto &f : fields)
            m_attrCombo->addItem(f.displayName, f.name);
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
    const bool isAnimated =
        qobject_cast<SWMMResultsLayer *>(layerForProvider) != nullptr;
    m_rangeRow->setVisible(isAnimated && modeRow == kModeGraduated);

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
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get()))
        gfresh->setClassifyAttribute(name);
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
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get()))
        gfresh->setRangeMode(mode);
    installRenderer(m_rule, m_modelLayer, m_resultsLayer, m_category,
                    std::move(fresh));
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
    if (mode == kModeGraduated) {
        auto g = std::make_unique<GraduatedRenderer>();
        // Slice DM.2 — seed the new Graduated renderer with the first
        // attribute the host's IAttributeProvider exposes (when any).
        // Avoids the "Graduated mode picked but no attribute selected"
        // dead state that used to require the user to know to type a
        // field name into a separate row that didn't exist yet.
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
            : nullptr) {
            const auto fields = provider->availableAttributes(m_category);
            if (!fields.isEmpty())
                g->setClassifyAttribute(fields.first().name);
        }
        g->setRamp(m_rampCombo->currentRamp());
        IntervalBinner b;
        b.setMethod(BinMethod::EqualInterval);
        b.setBinCount(m_countSpin->value());
        g->setBinner(b);
        next = std::move(g);
    } else if (mode == kModeCategorized) {
        next = std::make_unique<CategorizedRenderer>();
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
    IntervalBinner b = g->binner();
    b.setMethod(static_cast<BinMethod>(m_methodCombo->itemData(comboRow).toInt()));
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
    // Clone then strip lastBreaks via a fresh binner so the next
    // rebuildKindFeatureOverrides re-runs autoClassify against real data.
    auto fresh = g->clone();
    if (auto *gfresh = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(fresh.get()))
        gfresh->setBinner(g->binner());
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
        m_methodCombo->setCurrentIndex(int(BinMethod::Manual));
    }

    Q_UNUSED(item);

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
        const double t = (n > 1) ? (double(i) + 0.5) / double(n) : 0.5;
        const QColor c = g->ramp().colorAt(t);
        colorItem->setData(c, Qt::BackgroundRole);
        colorItem->setEditable(true);
        auto *labelItem = new QStandardItem(tr("Class %1").arg(i + 1));
        labelItem->setEditable(true);
        m_breaksModel->appendRow({lowerItem, upperItem, colorItem, labelItem});
    }
    m_suppressEdits = false;
}

} // namespace openswmmvis::ui
