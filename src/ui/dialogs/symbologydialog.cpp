/*!
 * \file   symbologydialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/symbologydialog.h"

#include "layers/gisvectorlayer.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace openswmmvis::ui {

SymbologyDialog::SymbologyDialog(OpenSWMMVisLayer *layer, QWidget *parent)
    : QDialog(parent), m_layer(layer)
{
    setWindowTitle(tr("Layer Symbology"));
    resize(700, 540);
    m_singleColor = QColor(64, 128, 240);
    buildUi();
}

SymbologyDialog::~SymbologyDialog() = default;

void SymbologyDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    buildSingleTab(m_tabs);
    buildGraduatedTab(m_tabs);
    buildCategorizedTab(m_tabs);
    buildLabelsTab(m_tabs);
    buildArrowsTab(m_tabs);
    root->addWidget(m_tabs, 1);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SymbologyDialog::onApplyClicked);
    root->addWidget(bb);
}

void SymbologyDialog::buildSingleTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_singleColorBtn = new QPushButton(tr("Pick…"), w);
    m_singleColorBtn->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_singleColor.name()));
    connect(m_singleColorBtn, &QPushButton::clicked, this, &SymbologyDialog::onColorClicked);
    form->addRow(tr("Colour:"), m_singleColorBtn);

    m_singleSize = new QDoubleSpinBox(w);
    m_singleSize->setRange(0.5, 50.0);
    m_singleSize->setValue(6.0);
    m_singleSize->setSuffix(tr(" px"));
    form->addRow(tr("Symbol size:"), m_singleSize);

    m_singleWidth = new QDoubleSpinBox(w);
    m_singleWidth->setRange(0.1, 10.0);
    m_singleWidth->setValue(1.2);
    m_singleWidth->setSuffix(tr(" px"));
    form->addRow(tr("Stroke width:"), m_singleWidth);

    tabs->addTab(w, tr("Single symbol"));
}

void SymbologyDialog::buildGraduatedTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_gradAttr = new QComboBox(w);
    m_gradAttr->addItems({tr("Depth"), tr("Head"), tr("Flow"), tr("Velocity"),
                          tr("Runoff"), tr("Volume"), tr("Diameter")});
    form->addRow(tr("Attribute:"), m_gradAttr);

    m_gradRamp = new QComboBox(w);
    m_gradRamp->addItems({tr("Viridis"), tr("Plasma"), tr("Turbo"),
                          tr("Blue-Red (diverging)"), tr("Grayscale")});
    form->addRow(tr("Ramp:"), m_gradRamp);

    m_gradClasses = new QSpinBox(w);
    m_gradClasses->setRange(2, 12);
    m_gradClasses->setValue(5);
    form->addRow(tr("Classes:"), m_gradClasses);

    auto *sizeRow = new QHBoxLayout;
    m_gradMinSize = new QDoubleSpinBox(w);
    m_gradMinSize->setRange(0.5, 50.0);
    m_gradMinSize->setValue(3.0);
    m_gradMaxSize = new QDoubleSpinBox(w);
    m_gradMaxSize->setRange(0.5, 50.0);
    m_gradMaxSize->setValue(12.0);
    sizeRow->addWidget(m_gradMinSize);
    sizeRow->addWidget(new QLabel(tr("→"), w));
    sizeRow->addWidget(m_gradMaxSize);
    sizeRow->addStretch(1);
    form->addRow(tr("Size range:"), sizeRow);

    tabs->addTab(w, tr("Graduated"));
}

void SymbologyDialog::buildCategorizedTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_catAttr = new QComboBox(w);
    m_catAttr->addItems({tr("Type"), tr("Status"), tr("User flag"), tr("Group")});
    form->addRow(tr("Attribute:"), m_catAttr);

    m_catScheme = new QComboBox(w);
    m_catScheme->addItems({tr("Tab10"), tr("Pastel"), tr("Set3"), tr("Distinct")});
    form->addRow(tr("Colour scheme:"), m_catScheme);

    tabs->addTab(w, tr("Categorized"));
}

void SymbologyDialog::buildLabelsTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_labelEnabled = new QCheckBox(tr("Show labels"), w);
    form->addRow(m_labelEnabled);

    m_labelExpr = new QLineEdit(w);
    m_labelExpr->setPlaceholderText(QStringLiteral("$name"));
    form->addRow(tr("Expression:"), m_labelExpr);

    m_labelSize = new QDoubleSpinBox(w);
    m_labelSize->setRange(6.0, 36.0);
    m_labelSize->setValue(10.0);
    m_labelSize->setSuffix(tr(" pt"));
    form->addRow(tr("Font size:"), m_labelSize);

    m_labelHalo = new QCheckBox(tr("White halo"), w);
    m_labelHalo->setChecked(true);
    form->addRow(m_labelHalo);

    tabs->addTab(w, tr("Labels"));
}

void SymbologyDialog::buildArrowsTab(QTabWidget *tabs)
{
    auto *w = new QWidget(tabs);
    auto *form = new QFormLayout(w);

    m_arrowEnabled = new QCheckBox(tr("Show flow-direction arrows on links"), w);
    form->addRow(m_arrowEnabled);

    m_arrowSize = new QDoubleSpinBox(w);
    m_arrowSize->setRange(2.0, 30.0);
    m_arrowSize->setValue(6.0);
    m_arrowSize->setSuffix(tr(" px"));
    form->addRow(tr("Arrow size:"), m_arrowSize);

    tabs->addTab(w, tr("Arrows"));
}

void SymbologyDialog::onColorClicked()
{
    const QColor c = QColorDialog::getColor(m_singleColor, this, tr("Symbol colour"));
    if (!c.isValid()) return;
    m_singleColor = c;
    m_singleColorBtn->setStyleSheet(QStringLiteral("background-color: %1;").arg(c.name()));
}

void SymbologyDialog::onApplyClicked()
{
    applyToLayer();
}

void SymbologyDialog::accept()
{
    applyToLayer();
    QDialog::accept();
}

namespace {

using namespace OpenSWMM::Render;

// One SimpleMarker SymbolLayer carrying colour + size + a thin stroke
// width. v1 default — any layer kind (markers, lines, fills) accepts a
// "color" / "size" / "width" lookup, so a single shape works as a
// neutral starting point. Editor for marker/line/fill stack composition
// lands in Slice BI.3.
SymbolStyle makeSimpleMarkerStyle(const QColor &c, double size, double strokeWidth)
{
    SymbolStyle style;
    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleMarker;
    sl.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
    sl.props.insert(QStringLiteral("color"), c.name(QColor::HexArgb));
    sl.props.insert(QStringLiteral("size"),  size);
    sl.props.insert(QStringLiteral("width"), strokeWidth);
    style.layers.append(sl);
    return style;
}

// Interpolates between two QColors in RGB space at t in [0,1].
QColor lerpColor(const QColor &a, const QColor &b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(
        a.redF()   + t * (b.redF()   - a.redF()),
        a.greenF() + t * (b.greenF() - a.greenF()),
        a.blueF()  + t * (b.blueF()  - a.blueF()));
}

// Samples `count` evenly-spaced colours from a named ramp. The ramp
// catalogue is intentionally inline — the user-facing combo strings in
// buildGraduatedTab() are the ground truth; keep both in sync.
QList<QColor> sampleRamp(const QString &name, int count)
{
    // Stops as (position, QColor) — viridis from gisrasterlayer.cpp;
    // plasma / turbo / blue-red / grayscale chosen for visual distinction.
    QList<QPair<double, QColor>> stops;
    if (name == QLatin1String("Viridis")) {
        stops = {
            {0.000, QColor( 68,   1,  84)}, {0.250, QColor( 62,  83, 137)},
            {0.500, QColor( 53, 153, 122)}, {0.750, QColor(163, 214,  63)},
            {1.000, QColor(253, 231,  37)},
        };
    } else if (name == QLatin1String("Plasma")) {
        stops = {
            {0.000, QColor( 13,   8, 135)}, {0.250, QColor(126,   3, 167)},
            {0.500, QColor(204,  71, 120)}, {0.750, QColor(248, 149,  64)},
            {1.000, QColor(240, 249,  33)},
        };
    } else if (name == QLatin1String("Turbo")) {
        stops = {
            {0.000, QColor( 48,  18,  59)}, {0.250, QColor( 40, 142, 232)},
            {0.500, QColor( 42, 218, 168)}, {0.750, QColor(241, 215,  56)},
            {1.000, QColor(122,   4,   3)},
        };
    } else if (name.startsWith(QLatin1String("Blue-Red"))) {
        stops = {
            {0.0, QColor( 33, 102, 172)}, {0.5, QColor(247, 247, 247)},
            {1.0, QColor(178,  24,  43)},
        };
    } else { // Grayscale and unknowns
        stops = { {0.0, Qt::black}, {1.0, Qt::white} };
    }

    QList<QColor> out;
    out.reserve(count);
    if (count <= 0) return out;
    if (count == 1) { out.append(stops.first().second); return out; }
    for (int i = 0; i < count; ++i) {
        const double t = double(i) / double(count - 1);
        // Find bracketing stops.
        int hi = 1;
        while (hi < stops.size() - 1 && stops[hi].first < t) ++hi;
        const auto &s0 = stops[hi - 1];
        const auto &s1 = stops[hi];
        const double span = (s1.first - s0.first);
        const double local = span > 0.0 ? (t - s0.first) / span : 0.0;
        out.append(lerpColor(s0.second, s1.second, local));
    }
    return out;
}

// Canonical attribute key used by the renderer's classifyAttribute /
// the QVariantMap passed to symbolFor(). Match the user-visible combo
// labels so the renderer JSON round-trip stays human-readable.
QString attributeKey(const QString &uiLabel)
{
    return uiLabel.toLower();
}

} // namespace

void SymbologyDialog::applyToLayer()
{
    if (!m_layer || !m_tabs) return;

    std::unique_ptr<OpenSWMM::Render::IFeatureRenderer> renderer;

    switch (m_tabs->currentIndex())
    {
    case 0: { // Single
        auto r = std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>();
        r->setSymbol(makeSimpleMarkerStyle(m_singleColor,
                                           m_singleSize->value(),
                                           m_singleWidth->value()));
        renderer = std::move(r);
        break;
    }
    case 1: { // Graduated
        auto r = std::make_unique<OpenSWMM::Render::GraduatedRenderer>();
        r->setClassifyAttribute(attributeKey(m_gradAttr->currentText()));
        r->setBinColors(sampleRamp(m_gradRamp->currentText(),
                                   m_gradClasses->value()));
        // Use the larger of the two as the base symbol's size; the
        // renderer overrides "color" per bin and leaves "size" as the
        // template default.
        r->setBaseSymbol(makeSimpleMarkerStyle(m_singleColor,
                                               m_gradMaxSize->value(),
                                               m_singleWidth->value()));
        // Leave the numeric range at its renderer-default [0,1]; the
        // caller (animation / data load) will call autoClassify() with
        // real samples when results arrive. Users who want to lock the
        // range manually do it from the layer's Properties dialog.
        renderer = std::move(r);
        break;
    }
    case 2: { // Categorized
        // v1: store attribute + fallback symbol. The per-value Category
        // list comes from a deferred editor (Slice BI.3); features that
        // don't match any (still-empty) category get the fallback symbol,
        // so the renderer is functional today.
        auto r = std::make_unique<OpenSWMM::Render::CategorizedRenderer>();
        r->setClassifyAttribute(attributeKey(m_catAttr->currentText()));
        const QList<QColor> palette = sampleRamp(QStringLiteral("Viridis"), 1);
        r->setFallbackSymbol(makeSimpleMarkerStyle(
            palette.isEmpty() ? m_singleColor : palette.first(),
            m_singleSize->value(),
            m_singleWidth->value()));
        renderer = std::move(r);
        break;
    }
    default:
        // Labels / Arrows tabs don't construct a renderer — they're
        // layer-state toggles whose home is the Properties dialog.
        // Silently ignore Apply for those tabs so the user isn't
        // surprised by a no-op that also clears their renderer.
        return;
    }

    if (!renderer) return;

    // Each concrete layer owns its own typed setRenderer(unique_ptr<...>).
    // Raster / basemap layers don't carry an IFeatureRenderer, so they
    // fall through unchanged — the dialog's tabs aren't meaningful for
    // pixel-based layers.
    if (auto *l = qobject_cast<GISVectorLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMMResultsLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMM2DResultsLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMM2DMeshLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    } else if (auto *l = qobject_cast<SWMMModelLayer *>(m_layer.data())) {
        l->setRenderer(std::move(renderer));
    }
}

} // namespace openswmmvis::ui
