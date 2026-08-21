/*!
 * \file   swmm2dresultsstylepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  O2-1 / VS.8 — multi-tab styling panel for SWMM2DResultsLayer.
 *
 *         Pattern: each control is initialised from the style bag BEFORE
 *         its change-signal is connected, so construction never fires
 *         spurious writes; afterwards every edit applies live (the same
 *         apply-on-edit model the rest of the styling UI uses).
 */
#include "ui/dialogs/swmm2dresultsstylepanel.h"

#include "layers/swmm2dresultslayer.h"
#include "render/sublayers/scalarfillsublayer.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/dialogs/sublayertabhelpers.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/colorrampcombobox.h"
#include "ui/widgets/dashstylecombo.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <functional>

namespace openswmmvis::ui {

namespace {

using OpenSWMM::Render::ContourBandStyle;
using OpenSWMM::Render::ISublayer;
using OpenSWMM::Render::IsolineStyle;
using OpenSWMM::Render::MeshEdgeStyle;
using OpenSWMM::Render::MeshNodeStyle;
using OpenSWMM::Render::VelocityVectorStyle;

// Spin factories, minimum widths, and the "Show <name>" + opacity header
// row now live in ui/dialogs/sublayertabhelpers.h, shared with the mesh
// styling panel.

/*! Colour-source block shared by the Depth and Contour-band tabs:
 *  [Two-colour gradient | Colour ramp] selector, ramp combo + invert,
 *  low/high colour buttons. `rampName`/`setRampName` etc. adapt the two
 *  style bags without a common base.
 *  NOTE: name-only binding — user CUSTOM ramps resolve through
 *  RasterColorRamp::builtin() and degrade to grayscale. If this block is
 *  revived, bind the full RasterColorRamp payload like
 *  ClassificationEditor::onRampChanged does (ClassificationScheme::
 *  setCustomRamp), not just the name. */
struct ColorSourceBindings {
    std::function<QString()>            rampName;
    std::function<void(const QString &)> setRampName;
    std::function<bool()>               invert;
    std::function<void(bool)>           setInvert;
    std::function<QColor()>             lowColor;
    std::function<void(const QColor &)> setLowColor;
    std::function<QColor()>             highColor;
    std::function<void(const QColor &)> setHighColor;
};

QGroupBox *makeColorSourceGroup(QWidget *parent, const ColorSourceBindings &b)
{
    auto *box  = new QGroupBox(QObject::tr("Colour"), parent);
    auto *form = new QFormLayout(box);

    const bool usingRamp = !b.rampName().isEmpty();

    auto *source = new QComboBox(box);
    source->addItem(QObject::tr("Colour ramp"));
    source->addItem(QObject::tr("Two-colour gradient"));
    source->setCurrentIndex(usingRamp ? 0 : 1);
    source->setMinimumWidth(kComboMinWidthPx);
    form->addRow(QObject::tr("Source:"), source);

    auto *ramp = new ColorRampComboBox(box);
    if (usingRamp) ramp->setCurrentRampByName(b.rampName());
    ramp->setMinimumWidth(kComboMinWidthPx);
    form->addRow(QObject::tr("Ramp:"), ramp);

    auto *invert = new QCheckBox(QObject::tr("Invert ramp"), box);
    invert->setChecked(b.invert());
    form->addRow(QString(), invert);

    auto *low = new ColorButton(box);
    low->setShowAlpha(true);
    low->setColor(b.lowColor());
    form->addRow(QObject::tr("Low colour:"), low);

    auto *high = new ColorButton(box);
    high->setShowAlpha(true);
    high->setColor(b.highColor());
    form->addRow(QObject::tr("High colour:"), high);

    auto applyEnabled = [source, ramp, low, high]() {
        const bool useRamp = (source->currentIndex() == 0);
        ramp->setEnabled(useRamp);
        low->setEnabled(!useRamp);
        high->setEnabled(!useRamp);
    };
    applyEnabled();

    QObject::connect(source, qOverload<int>(&QComboBox::currentIndexChanged), box,
        [b, ramp, applyEnabled](int idx) {
            applyEnabled();
            b.setRampName(idx == 0 ? ramp->currentText() : QString());
        });
    QObject::connect(ramp, &ColorRampComboBox::rampChanged, box,
        [b, source, ramp](const RasterColorRamp &) {
            if (source->currentIndex() == 0)
                b.setRampName(ramp->currentText());
        });
    QObject::connect(invert, &QCheckBox::toggled, box,
                     [b](bool on) { b.setInvert(on); });
    QObject::connect(low, &ColorButton::colorChanged, box,
                     [b](const QColor &c) { b.setLowColor(c); });
    QObject::connect(high, &ColorButton::colorChanged, box,
                     [b](const QColor &c) { b.setHighColor(c); });
    return box;
}

} // namespace

Swmm2DResultsStylePanel::Swmm2DResultsStylePanel(SWMM2DResultsLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    if (!m_layer) return;

    // Each page sits in a scroll area so a narrow/short dialog scrolls
    // instead of compressing the editors below their minimum sizes.
    auto wrapScroll = [tabs](QWidget *page) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        return scroll;
    };
    // Routing contract: LayerStyleDialog::focusInitialSubject() matches
    // tabToolTip == the sublayer id, so right-clicking a sublayer row in the
    // layer tree (or a legend entry) lands on the matching tab instead of
    // always the first one.
    auto addTab = [tabs, &wrapScroll](QWidget *page, const QString &label,
                                      OpenSWMM::Render::ISublayer *sub) {
        const int idx = tabs->addTab(wrapScroll(page), label);
        if (sub) tabs->setTabToolTip(idx, sub->id());
    };

    // The two direct-mesh depth fills (flat per-cell / Gouraud per-vertex).
    // Both sublayers pre-date this panel but had no styling UI at all — the
    // only "smooth" control users could find was the contour-band boundary
    // interpolation checkbox, which is a different knob entirely.
    auto *cellSub   = m_layer->cellDepthFillSublayer();
    auto *smoothSub = m_layer->smoothDepthFillSublayer();
    addTab(buildScalarFillTab(cellSub, cellSub ? cellSub->fillStyle() : nullptr,
                              tr("Show cell depth fill"), tabs),
           tr("&Cell Depth Fill"), cellSub);
    addTab(buildScalarFillTab(smoothSub, smoothSub ? smoothSub->fillStyle() : nullptr,
                              tr("Show smooth depth fill"), tabs),
           tr("&Smooth Depth Fill"), smoothSub);
    // 2026-06-21 — the "Depth Fill" (depth color ramp) and "Flow Arrows" tabs
    // were removed: contour bands now provide the depth fill, and velocity
    // vectors already convey flow direction.
    addTab(buildContourBandTab(tabs), tr("D&epth Contours"),  m_layer->contourBandSublayer());
    addTab(buildIsolineTab(tabs),     tr("De&pth Isolines"),  m_layer->isolineSublayer());
    addTab(buildVelocityTab(tabs),    tr("&Flow Velocity"),   m_layer->velocityVectorSublayer());
    // Issue 6 — Edges/Vertices were previously only editable via the generic
    // property grid; give them dedicated tabs so the edge colour/width (the
    // knobs behind the dark-edge artifact) and the vertex markers are tunable
    // here. The slope-emphasis and tagged-vertex groups are omitted because the
    // results renderer does not honour them (no dead knobs).
    addTab(buildMeshEdgeTab(tabs),     tr("&Mesh Edges"),    m_layer->meshEdgeSublayer());
    addTab(buildMeshNodeTab(tabs),     tr("Mes&h Vertices"), m_layer->meshNodeSublayer());
}

// ─── Scalar depth fills (cell / smooth) ─────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildScalarFillTab(
    OpenSWMM::Render::ISublayer *sub, OpenSWMM::Render::ScalarFillStyle *st,
    const QString &showLabel, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    lay->addWidget(makeSublayerHeader(page, sub, showLabel));

    if (st) {
        auto *attrBox  = new QGroupBox(tr("Attribute"), page);
        auto *attrForm = new QFormLayout(attrBox);
        auto *attr = new QComboBox(attrBox);
        attr->addItem(tr("Depth"),     QStringLiteral("depth"));
        attr->addItem(tr("Elevation"), QStringLiteral("elevation"));
        const int cur = attr->findData(st->attribute());
        attr->setCurrentIndex(cur >= 0 ? cur : 0);
        attr->setMinimumWidth(kComboMinWidthPx);
        attrForm->addRow(tr("&Attribute:"), attr);
        lay->addWidget(attrBox);
        connect(attr, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, attr](int idx) { st->setAttribute(attr->itemData(idx).toString()); });

        // Continuous mode samples the ramp per value (seam-free gradient);
        // Classified bins into discrete classes — the scheme's ClassMode is
        // the toggle, surfaced by the shared editor.
        auto *L = m_layer.data();
        auto *binding = new SublayerSchemeBinding(
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            [] { return QVector<double>{}; },
            [L] { return qMakePair(L->dryDepth(), L->maxDepth()); },
            /*supportsContinuousMode=*/true,
            /*supportsRangeModes=*/true);
        lay->addWidget(new ClassificationEditor(binding, /*ownBinding=*/true, page));
    }
    lay->addStretch();
    return page;
}

// ─── Contour bands ──────────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildContourBandTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->contourBandSublayer();
    ContourBandStyle *st = sub ? sub->bandStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show filled contour bands")));

    if (st) {
        // Slice US.2 — the shared classification editor: method (equal
        // interval / quantile / Jenks / …), class count, colour ramp + invert,
        // custom range, and a per-class colour/label table. The renderer reads
        // st->scheme() to march the bands.
        auto *L = m_layer.data();
        auto *binding = new SublayerSchemeBinding(
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            [] { return QVector<double>{}; },            // table preview only; map samples per frame
            [L] { return qMakePair(L->dryDepth(), L->maxDepth()); },
            /*supportsContinuousMode=*/false,
            /*supportsRangeModes=*/false);
        lay->addWidget(new ClassificationEditor(binding, /*ownBinding=*/true, page));

        auto *renderBox  = new QGroupBox(tr("Rendering"), page);
        auto *renderForm = new QFormLayout(renderBox);
        auto *smooth = new QCheckBox(tr("Interpolate band boundaries (marching triangles)"),
                                     renderBox);
        smooth->setChecked(st->smoothBands());
        smooth->setToolTip(tr("On: class boundaries are interpolated through "
                              "cells (marching triangles). Off: each cell is "
                              "filled flat with its band colour. For a "
                              "seam-free continuous gradient use the Smooth "
                              "Depth Fill tab instead."));
        renderForm->addRow(QString(), smooth);
        lay->addWidget(renderBox);

        connect(smooth, &QCheckBox::toggled, this,
                [st](bool on) { st->setSmoothBands(on); });
    }
    lay->addStretch();
    return page;
}

// ─── Isolines ───────────────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildIsolineTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->isolineSublayer();
    IsolineStyle *st = sub ? sub->isolineStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show isolines")));

    if (st) {
        using LM = IsolineStyle::LevelMode;

        auto *levelBox  = new QGroupBox(tr("Levels"), page);
        auto *levelForm = new QFormLayout(levelBox);
        auto *mode = new QComboBox(levelBox);
        mode->addItem(tr("Fixed count"),             int(LM::Count));
        mode->addItem(tr("Fixed interval + base"),   int(LM::FixedInterval));
        mode->setCurrentIndex(mode->findData(int(st->levelMode())));
        mode->setMinimumWidth(kComboMinWidthPx);
        levelForm->addRow(tr("M&ode:"), mode);
        auto *count = makeSpin(levelBox, 1, 64, st->isoValueCount());
        levelForm->addRow(tr("Co&unt:"), count);
        // Slice US.2 — classification method for the Count idiom (equal
        // interval reproduces the legacy even spacing; quantile / Jenks /
        // std-dev bin the wet-cell depths). FixedInterval ignores it.
        using BM = OpenSWMM::Render::BinMethod;
        auto *method = new QComboBox(levelBox);
        method->addItem(tr("Equal interval"),         int(BM::EqualInterval));
        method->addItem(tr("Quantile"),               int(BM::Quantile));
        method->addItem(tr("Natural breaks (Jenks)"), int(BM::NaturalBreaks));
        method->addItem(tr("Standard deviation"),     int(BM::StdDev));
        method->addItem(tr("Logarithmic"),            int(BM::Logarithmic));
        method->addItem(tr("Exponential"),            int(BM::Exponential));
        method->setCurrentIndex(method->findData(int(st->scheme().method())));
        method->setMinimumWidth(kComboMinWidthPx);
        levelForm->addRow(tr("Me&thod:"), method);
        auto *interval = makeDSpin(levelBox, 1e-6, 1e6, 0.1, 3,
                                   st->levelInterval(), tr(" m"));
        levelForm->addRow(tr("&Interval:"), interval);
        auto *base = makeDSpin(levelBox, -1e6, 1e6, 0.1, 3,
                               st->baseLevel(), tr(" m"));
        base->setToolTip(tr("Contours fall at base + k × interval"));
        levelForm->addRow(tr("B&ase level:"), base);
        lay->addWidget(levelBox);

        auto applyMode = [count, method, interval, base](LM m) {
            count->setEnabled(m == LM::Count);
            method->setEnabled(m == LM::Count);
            interval->setEnabled(m == LM::FixedInterval);
            base->setEnabled(m == LM::FixedInterval);
        };
        applyMode(st->levelMode());

        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);
        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        symForm->addRow(tr("Co&lour:"), color);
        auto *width = makeDSpin(symBox, 0.25, 20.0, 0.25, 2,
                                st->lineWidthPx(), tr(" px"));
        symForm->addRow(tr("&Width:"), width);
        auto *dash = new DashStyleCombo(symBox);
        dash->setPenStyle(st->dashPattern());
        dash->setMinimumWidth(kComboMinWidthPx);
        symForm->addRow(tr("St&roke style:"), dash);
        auto *idxEvery = makeSpin(symBox, 0, 50, st->indexEvery());
        idxEvery->setSpecialValueText(tr("Off"));
        idxEvery->setToolTip(tr("Emphasise every Nth contour (topographic "
                                "index contours); 0 disables"));
        symForm->addRow(tr("I&ndex contour every:"), idxEvery);
        auto *idxWidth = makeDSpin(symBox, 0.25, 20.0, 0.25, 2,
                                   st->indexWidthPx(), tr(" px"));
        idxWidth->setEnabled(st->indexEvery() >= 2);
        symForm->addRow(tr("Inde&x width:"), idxWidth);
        lay->addWidget(symBox);

        auto *labelBox  = new QGroupBox(tr("Labels"), page);
        auto *labelForm = new QFormLayout(labelBox);
        auto *labelsOn = new QCheckBox(tr("Label contour values along lines"),
                                       labelBox);
        labelsOn->setChecked(st->labels());
        labelForm->addRow(QString(), labelsOn);
        auto *decimals = makeSpin(labelBox, 0, 9, st->labelDecimals());
        decimals->setEnabled(st->labels());
        labelForm->addRow(tr("Decimals:"), decimals);
        auto *fontPt = makeDSpin(labelBox, 4.0, 72.0, 0.5, 1,
                                 st->labelFontPt(), tr(" pt"));
        fontPt->setEnabled(st->labels());
        labelForm->addRow(tr("Font si&ze:"), fontPt);
        auto *halo = new QCheckBox(tr("White halo"), labelBox);
        halo->setChecked(st->labelHalo());
        halo->setEnabled(st->labels());
        labelForm->addRow(QString(), halo);
        lay->addWidget(labelBox);

        connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, mode, applyMode](int i) {
                    const auto m = static_cast<LM>(mode->itemData(i).toInt());
                    applyMode(m);
                    st->setLevelMode(m);
                });
        connect(count, qOverload<int>(&QSpinBox::valueChanged), this,
                [st](int n) { st->setIsoValueCount(n); });
        connect(method, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, method](int i) {
                    auto s = st->scheme();
                    s.setMethod(static_cast<OpenSWMM::Render::BinMethod>(
                        method->itemData(i).toInt()));
                    st->setScheme(s);
                });
        connect(interval, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLevelInterval(v); });
        connect(base, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setBaseLevel(v); });
        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double w) { st->setLineWidthPx(w); });
        connect(dash, &DashStyleCombo::penStyleChanged, this,
                [st](Qt::PenStyle s) { st->setDashPattern(s); });
        connect(idxEvery, qOverload<int>(&QSpinBox::valueChanged), this,
                [st, idxWidth](int n) {
                    idxWidth->setEnabled(n >= 2);
                    st->setIndexEvery(n);
                });
        connect(idxWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double w) { st->setIndexWidthPx(w); });
        connect(labelsOn, &QCheckBox::toggled, this,
                [st, decimals, fontPt, halo](bool on) {
                    decimals->setEnabled(on);
                    fontPt->setEnabled(on);
                    halo->setEnabled(on);
                    st->setLabels(on);
                });
        connect(decimals, qOverload<int>(&QSpinBox::valueChanged), this,
                [st](int n) { st->setLabelDecimals(n); });
        connect(fontPt, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLabelFontPt(v); });
        connect(halo, &QCheckBox::toggled, this,
                [st](bool on) { st->setLabelHalo(on); });
    }
    lay->addStretch();
    return page;
}

// ─── Velocity vectors ───────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildVelocityTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->velocityVectorSublayer();
    VelocityVectorStyle *st = sub ? sub->vectorStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show velocity vectors")));

    if (st) {
        using LS = VelocityVectorStyle::LengthScaling;

        auto *sizeBox  = new QGroupBox(tr("Sizing"), page);
        auto *sizeForm = new QFormLayout(sizeBox);
        auto *scaling = new QComboBox(sizeBox);
        scaling->addItem(tr("Linear"),      int(LS::Linear));
        scaling->addItem(tr("Square root"), int(LS::SquareRoot));
        scaling->addItem(tr("Logarithmic"), int(LS::Log));
        scaling->setCurrentIndex(scaling->findData(int(st->lengthScaling())));
        scaling->setToolTip(tr("How |v| maps to arrow length before the "
                               "min/max clamps"));
        scaling->setMinimumWidth(kComboMinWidthPx);
        sizeForm->addRow(tr("Len&gth scaling:"), scaling);
        auto *scale = makeDSpin(sizeBox, 0.1, 500.0, 1.0, 1,
                                st->glyphLengthScalePxPerMps(),
                                tr(" px per m/s"));
        sizeForm->addRow(tr("Scale:"), scale);
        auto *minLen = makeDSpin(sizeBox, 0.0, 200.0, 1.0, 1,
                                 st->glyphLengthMinPx(), tr(" px"));
        sizeForm->addRow(tr("Min length:"), minLen);
        auto *maxLen = makeDSpin(sizeBox, 1.0, 500.0, 1.0, 1,
                                 st->glyphLengthMaxPx(), tr(" px"));
        sizeForm->addRow(tr("Max length:"), maxLen);
        auto *head = makeDSpin(sizeBox, 0.0, 50.0, 0.5, 1,
                               st->headSizePx(), tr(" px"));
        sizeForm->addRow(tr("Head size:"), head);
        auto *shaft = makeDSpin(sizeBox, 0.1, 10.0, 0.1, 1,
                                st->shaftWidthPx(), tr(" px"));
        sizeForm->addRow(tr("Shaft width:"), shaft);
        lay->addWidget(sizeBox);

        auto *colorBox  = new QGroupBox(tr("Colour"), page);
        auto *colorForm = new QFormLayout(colorBox);
        auto *byMag = new QCheckBox(tr("Colour by magnitude"), colorBox);
        byMag->setChecked(st->colorByMagnitude());
        colorForm->addRow(QString(), byMag);
        auto *flat = new ColorButton(colorBox);
        flat->setShowAlpha(true);
        flat->setColor(st->color());
        flat->setEnabled(!st->colorByMagnitude());
        colorForm->addRow(tr("Single colour:"), flat);
        lay->addWidget(colorBox);

        // Slice US.2 — the shared classification editor owns the ramp + invert,
        // method (equal interval / quantile / Jenks / …), class count, and the
        // speed range. Range provider yields the style's speed min/max; map
        // samples per frame so the table preview just degrades to equal spacing.
        auto *binding = new SublayerSchemeBinding(
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            [] { return QVector<double>{}; },
            [st] {
                const double lo = st->speedMinMps();
                const double hi = (st->speedMaxMps() > lo) ? st->speedMaxMps()
                                                           : lo + 1.0;
                return qMakePair(lo, hi);
            },
            /*supportsContinuousMode=*/true,
            /*supportsRangeModes=*/false);
        auto *classEditor = new ClassificationEditor(binding, /*ownBinding=*/true, page);
        classEditor->setEnabled(st->colorByMagnitude());
        lay->addWidget(classEditor);

        auto *placeBox  = new QGroupBox(tr("Placement && filtering"), page);
        auto *placeForm = new QFormLayout(placeBox);
        auto *spacing = makeDSpin(placeBox, 1.0, 500.0, 5.0, 0,
                                  st->glyphSpacingPx(), tr(" px"));
        spacing->setToolTip(tr("Minimum on-screen spacing between arrows "
                               "(strongest cell per grid slot wins); 1 px "
                               "draws every wet cell"));
        placeForm->addRow(tr("Spacing:"), spacing);
        auto *dryCut = makeDSpin(placeBox, 0.0, 1000.0, 0.01, 3,
                                 st->dryDepthCutoff(), tr(" m"));
        dryCut->setToolTip(tr("Suppress arrows where depth is below this"));
        placeForm->addRow(tr("Dr&y depth cutoff:"), dryCut);
        lay->addWidget(placeBox);

        connect(scaling, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, scaling](int i) {
                    st->setLengthScaling(
                        static_cast<LS>(scaling->itemData(i).toInt()));
                });
        connect(scale, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphLengthScalePxPerMps(v); });
        connect(minLen, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphLengthMinPx(v); });
        connect(maxLen, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphLengthMaxPx(v); });
        connect(head, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setHeadSizePx(v); });
        connect(shaft, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setShaftWidthPx(v); });
        connect(byMag, &QCheckBox::toggled, this,
                [st, flat, classEditor](bool on) {
                    flat->setEnabled(!on);
                    classEditor->setEnabled(on);
                    st->setColorByMagnitude(on);
                });
        connect(flat, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(spacing, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setGlyphSpacingPx(v); });
        connect(dryCut, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setDryDepthCutoff(v); });
    }
    lay->addStretch();
    return page;
}

// ─── Mesh edges (Issue 6) ────────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildMeshEdgeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshEdgeSublayer();
    MeshEdgeStyle *st = sub ? sub->edgeStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show mesh edges")));

    if (st) {
        // Symbology only — the slope-emphasis (thin/wide) group is intentionally
        // omitted: the 2D results edge pass uses a single uniform width + flat
        // colour (the wide-colour split needs per-edge colour the flat edge node
        // can't carry), so surfacing those knobs would be misleading.
        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        color->setToolTip(tr("Edge colour. The wireframe is now drawn once per "
                             "unique edge, so a translucent colour no longer "
                             "darkens where cells meet."));
        symForm->addRow(tr("Colour:"), color);

        auto *width = makeDSpin(symBox, 0.1, 10.0, 0.05, 2,
                                st->lineWidthPx(), tr(" px"));
        symForm->addRow(tr("Width:"), width);

        auto *dash = new DashStyleCombo(symBox);
        dash->setPenStyle(st->dashPattern());
        dash->setMinimumWidth(kComboMinWidthPx);
        symForm->addRow(tr("Stro&ke style:"), dash);

        lay->addWidget(symBox);

        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLineWidthPx(v); });
        connect(dash, &DashStyleCombo::penStyleChanged, this,
                [st](Qt::PenStyle s) { st->setDashPattern(s); });
    }
    lay->addStretch();
    return page;
}

// ─── Mesh vertices (Issue 6) ─────────────────────────────────────────────────

QWidget *Swmm2DResultsStylePanel::buildMeshNodeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshNodeSublayer();
    MeshNodeStyle *st = sub ? sub->nodeStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show mesh vertices")));

    if (st) {
        // Symbology + outline only — the shape and tagged-vertex groups are
        // omitted: the results renderer draws a fixed marker and carries no
        // tagged-vertex data, so those knobs would be no-ops here.
        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        symForm->addRow(tr("Colour:"), color);

        auto *size = makeDSpin(symBox, 0.5, 20.0, 0.5, 1,
                               st->markerSizePx(), tr(" px"));
        symForm->addRow(tr("Marker size:"), size);

        auto *outlineColor = new ColorButton(symBox);
        outlineColor->setShowAlpha(true);
        outlineColor->setColor(st->outlineColor());
        symForm->addRow(tr("Outline colour:"), outlineColor);

        auto *outlineWidth = makeDSpin(symBox, 0.0, 5.0, 0.25, 2,
                                       st->outlineWidthPx(), tr(" px"));
        symForm->addRow(tr("Outline width:"), outlineWidth);

        lay->addWidget(symBox);

        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(size, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setMarkerSizePx(v); });
        connect(outlineColor, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setOutlineColor(c); });
        connect(outlineWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setOutlineWidthPx(v); });
    }
    lay->addStretch();
    return page;
}

} // namespace openswmmvis::ui
