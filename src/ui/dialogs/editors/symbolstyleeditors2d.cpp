/*!
 * \file   symbolstyleeditors2d.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SE.4 — dedicated editors for the raster / mesh / 2D-results
 *         *SymbolStyleAdapter archetypes.
 */
#include "ui/dialogs/editors/symbolstyleeditors2d.h"

#include "render/markershape.h"
#include "render/symbolstyleadapter.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/dashstylecombo.h"
#include "ui/widgets/markershapecombo.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using namespace OpenSWMM::Render;

namespace {

QDoubleSpinBox *opacitySpin(QWidget *p)
{
    auto *s = new QDoubleSpinBox(p);
    s->setRange(0.0, 100.0); s->setDecimals(0); s->setSingleStep(5.0);
    s->setSuffix(QStringLiteral(" %"));
    return s;
}
QDoubleSpinBox *dspin(QWidget *p, double lo, double hi, int dec, double step,
                      const QString &suffix = QString())
{
    auto *s = new QDoubleSpinBox(p);
    s->setRange(lo, hi); s->setDecimals(dec); s->setSingleStep(step);
    if (!suffix.isEmpty()) s->setSuffix(suffix);
    return s;
}
ColorButton *colorBtn(QWidget *p)
{
    auto *c = new ColorButton(p); c->setShowAlpha(true); return c;
}
QGroupBox *group(QWidget *parent, const QString &title, QFormLayout *&form)
{
    auto *box = new QGroupBox(title, parent);
    form = new QFormLayout(box);
    return box;
}

template <class SpinT, class Fn>
void onValue(SpinT *s, QObject *ctx, Fn fn)
{
    QObject::connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), ctx, fn);
}

} // namespace

// ===========================================================================
// Raster color ramp
// ===========================================================================
RasterColorRampSymbolStyleEditor::RasterColorRampSymbolStyleEditor(
    RasterColorRampSymbolStyleAdapter *a, QWidget *parent)
    : IStyleEditorWidget(parent), m_a(a)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    QFormLayout *f = nullptr;
    auto *box = group(this, tr("Colour ramp"), f);

    m_attr = new QLineEdit(box);
    m_attr->setPlaceholderText(tr("e.g. depth, head, velocity"));
    f->addRow(tr("&Attribute:"), m_attr);
    m_min = dspin(box, -1e9, 1e9, 3, 0.1); f->addRow(tr("Mi&n value:"), m_min);
    m_max = dspin(box, -1e9, 1e9, 3, 0.1); f->addRow(tr("Ma&x value:"), m_max);
    m_log = new QCheckBox(tr("Logarithmic scale"), box); f->addRow(QString(), m_log);
    m_low = colorBtn(box);  f->addRow(tr("&Low colour:"),  m_low);
    m_high = colorBtn(box); f->addRow(tr("Hi&gh colour:"), m_high);
    m_below = colorBtn(box); f->addRow(tr("&Below min:"),  m_below);
    m_above = colorBtn(box); f->addRow(tr("Ab&ove max:"),  m_above);
    m_opacity = opacitySpin(box); f->addRow(tr("O&pacity:"), m_opacity);
    root->addWidget(box); root->addStretch();

    connect(m_attr, &QLineEdit::editingFinished, this,
            [this] { if (m_a) m_a->setAttribute(m_attr->text()); });
    onValue(m_min, this, [this](double v) { if (m_a) m_a->setMinValue(v); });
    onValue(m_max, this, [this](double v) { if (m_a) m_a->setMaxValue(v); });
    connect(m_log, &QCheckBox::toggled, this, [this](bool v) { if (m_a) m_a->setUseLogScale(v); });
    connect(m_low,  &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setLowColor(c); });
    connect(m_high, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setHighColor(c); });
    connect(m_below,&ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setBelowMinColor(c); });
    connect(m_above,&ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setAboveMaxColor(c); });
    onValue(m_opacity, this, [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });

    if (m_a) connect(m_a, &RasterColorRampSymbolStyleAdapter::changed,
                     this, &RasterColorRampSymbolStyleEditor::refreshFromModel, Qt::UniqueConnection);
    refreshFromModel();
}
void RasterColorRampSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b0(m_attr), b1(m_min), b2(m_max), b3(m_log), b4(m_low),
                   b5(m_high), b6(m_below), b7(m_above), b8(m_opacity);
    m_attr->setText(m_a->attribute());
    m_min->setValue(m_a->minValue()); m_max->setValue(m_a->maxValue());
    m_log->setChecked(m_a->useLogScale());
    m_low->setColor(m_a->lowColor());   m_high->setColor(m_a->highColor());
    m_below->setColor(m_a->belowMinColor()); m_above->setColor(m_a->aboveMaxColor());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// Hillshade / mesh fill
// ===========================================================================
HillshadeSymbolStyleEditor::HillshadeSymbolStyleEditor(
    HillshadeSymbolStyleAdapter *a, QWidget *parent)
    : IStyleEditorWidget(parent), m_a(a)
{
    auto *root = new QVBoxLayout(this); root->setContentsMargins(8, 8, 8, 8);
    QFormLayout *f = nullptr;
    auto *box = group(this, tr("Relief"), f);
    m_useRamp = new QCheckBox(tr("Colour by elevation ramp"), box); f->addRow(QString(), m_useRamp);
    m_fill = colorBtn(box); f->addRow(tr("Flat fill:"), m_fill);
    m_strength = dspin(box, 0.0, 1.0, 2, 0.05); f->addRow(tr("Hillshad&e strength:"), m_strength);
    m_opacity = opacitySpin(box); f->addRow(tr("Opacit&y:"), m_opacity);
    root->addWidget(box); root->addStretch();

    connect(m_useRamp, &QCheckBox::toggled, this, [this](bool v) { if (m_a) m_a->setUseElevationRamp(v); });
    connect(m_fill, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setFillColor(c); });
    onValue(m_strength, this, [this](double v) { if (m_a) m_a->setHillshadeStrength(v); });
    onValue(m_opacity, this, [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });
    if (m_a) connect(m_a, &HillshadeSymbolStyleAdapter::changed,
                     this, &HillshadeSymbolStyleEditor::refreshFromModel, Qt::UniqueConnection);
    refreshFromModel();
}
void HillshadeSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b1(m_useRamp), b2(m_fill), b3(m_strength), b4(m_opacity);
    m_useRamp->setChecked(m_a->useElevationRamp());
    m_fill->setColor(m_a->fillColor());
    m_strength->setValue(m_a->hillshadeStrength());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// Contour bands
// ===========================================================================
ContourBandSymbolStyleEditor::ContourBandSymbolStyleEditor(
    ContourBandSymbolStyleAdapter *a, QWidget *parent)
    : IStyleEditorWidget(parent), m_a(a)
{
    auto *root = new QVBoxLayout(this); root->setContentsMargins(8, 8, 8, 8);
    QFormLayout *f = nullptr;
    // Title tracks the classified attribute (terrain = bed elevation,
    // results = water depth) so the terrain editor isn't mislabelled "Depth".
    const bool elev = m_a && m_a->attribute().compare(
        QLatin1String("elevation"), Qt::CaseInsensitive) == 0;
    auto *box = group(this, elev ? tr("Elevation Contours")
                                 : tr("Depth Contours"), f);
    m_attr = new QLineEdit(box); f->addRow(tr("Attrib&ute:"), m_attr);
    m_bands = new QSpinBox(box); m_bands->setRange(2, 64); f->addRow(tr("Bands:"), m_bands);
    m_low = colorBtn(box);  f->addRow(tr("Lo&w colour:"),  m_low);
    m_high = colorBtn(box); f->addRow(tr("High colour:"), m_high);
    m_below = colorBtn(box); f->addRow(tr("Below min:"),  m_below);
    m_above = colorBtn(box); f->addRow(tr("Above max:"),  m_above);
    m_smooth = new QCheckBox(tr("Smooth band boundaries"), box); f->addRow(QString(), m_smooth);
    m_opacity = opacitySpin(box); f->addRow(tr("Opacity:"), m_opacity);
    root->addWidget(box); root->addStretch();

    connect(m_attr, &QLineEdit::editingFinished, this, [this] { if (m_a) m_a->setAttribute(m_attr->text()); });
    connect(m_bands, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) { if (m_a) m_a->setBandCount(v); });
    connect(m_low,  &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setLowColor(c); });
    connect(m_high, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setHighColor(c); });
    connect(m_below,&ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setBelowMinColor(c); });
    connect(m_above,&ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setAboveMaxColor(c); });
    connect(m_smooth, &QCheckBox::toggled, this, [this](bool v) { if (m_a) m_a->setSmoothBands(v); });
    onValue(m_opacity, this, [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });
    if (m_a) connect(m_a, &ContourBandSymbolStyleAdapter::changed,
                     this, &ContourBandSymbolStyleEditor::refreshFromModel, Qt::UniqueConnection);
    refreshFromModel();
}
void ContourBandSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b0(m_attr), b1(m_bands), b2(m_low), b3(m_high),
                   b4(m_below), b5(m_above), b6(m_smooth), b7(m_opacity);
    m_attr->setText(m_a->attribute());
    m_bands->setValue(m_a->bandCount());
    m_low->setColor(m_a->lowColor());   m_high->setColor(m_a->highColor());
    m_below->setColor(m_a->belowMinColor()); m_above->setColor(m_a->aboveMaxColor());
    m_smooth->setChecked(m_a->smoothBands());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// Isolines
// ===========================================================================
IsolineSymbolStyleEditor::IsolineSymbolStyleEditor(
    IsolineSymbolStyleAdapter *a, QWidget *parent)
    : IStyleEditorWidget(parent), m_a(a)
{
    auto *root = new QVBoxLayout(this); root->setContentsMargins(8, 8, 8, 8);
    QFormLayout *f = nullptr;
    const bool elev = m_a && m_a->attribute().compare(
        QLatin1String("elevation"), Qt::CaseInsensitive) == 0;
    auto *box = group(this, elev ? tr("Elevation Isolines")
                                 : tr("Depth Isolines"), f);
    m_attr = new QLineEdit(box); f->addRow(tr("Attribute:"), m_attr);
    m_count = new QSpinBox(box); m_count->setRange(1, 64); f->addRow(tr("Iso count:"), m_count);
    m_color = colorBtn(box); f->addRow(tr("Colour:"), m_color);
    m_width = dspin(box, 0.25, 30.0, 2, 0.25, tr(" px")); f->addRow(tr("Width:"), m_width);
    m_dash = new DashStyleCombo(box); f->addRow(tr("Style:"), m_dash);
    m_labels = new QCheckBox(tr("Show value labels"), box); f->addRow(QString(), m_labels);
    m_opacity = opacitySpin(box); f->addRow(tr("Opacity:"), m_opacity);
    root->addWidget(box); root->addStretch();

    connect(m_attr, &QLineEdit::editingFinished, this, [this] { if (m_a) m_a->setAttribute(m_attr->text()); });
    connect(m_count, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) { if (m_a) m_a->setIsoValueCount(v); });
    connect(m_color, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setColor(c); });
    onValue(m_width, this, [this](double v) { if (m_a) m_a->setLineWidthPx(v); });
    connect(m_dash, &DashStyleCombo::penStyleChanged, this, [this](Qt::PenStyle s) { if (m_a) m_a->setDashPattern(s); });
    connect(m_labels, &QCheckBox::toggled, this, [this](bool v) { if (m_a) m_a->setLabels(v); });
    onValue(m_opacity, this, [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });
    if (m_a) connect(m_a, &IsolineSymbolStyleAdapter::changed,
                     this, &IsolineSymbolStyleEditor::refreshFromModel, Qt::UniqueConnection);
    refreshFromModel();
}
void IsolineSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b0(m_attr), b1(m_count), b2(m_color), b3(m_width), b4(m_dash), b5(m_labels), b6(m_opacity);
    m_attr->setText(m_a->attribute());
    m_count->setValue(m_a->isoValueCount());
    m_color->setColor(m_a->color());
    m_width->setValue(m_a->lineWidthPx());
    m_dash->setPenStyle(m_a->dashPattern());
    m_labels->setChecked(m_a->labels());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// Mesh edge
// ===========================================================================
MeshEdgeSymbolStyleEditor::MeshEdgeSymbolStyleEditor(
    MeshEdgeSymbolStyleAdapter *a, QWidget *parent)
    : IStyleEditorWidget(parent), m_a(a)
{
    auto *root = new QVBoxLayout(this); root->setContentsMargins(8, 8, 8, 8);
    QFormLayout *f = nullptr;
    auto *box = group(this, tr("Mesh wireframe"), f);
    m_color = colorBtn(box); f->addRow(tr("Colour:"), m_color);
    m_width = dspin(box, 0.1, 20.0, 2, 0.1, tr(" px")); f->addRow(tr("Width:"), m_width);
    m_dash = new DashStyleCombo(box); f->addRow(tr("Style:"), m_dash);
    m_opacity = opacitySpin(box); f->addRow(tr("Opacity:"), m_opacity);
    root->addWidget(box);

    QFormLayout *sf = nullptr;
    auto *slope = group(this, tr("Slope emphasis"), sf);
    m_slopeDriven = new QCheckBox(tr("Widen steep edges"), slope); sf->addRow(QString(), m_slopeDriven);
    m_slopeBreak = dspin(slope, 0.0, 10.0, 3, 0.01); sf->addRow(tr("Slope brea&k:"), m_slopeBreak);
    m_wideWidth = dspin(slope, 0.1, 20.0, 2, 0.1, tr(" px")); sf->addRow(tr("Wide width:"), m_wideWidth);
    m_wideColor = colorBtn(slope); sf->addRow(tr("Wide colour:"), m_wideColor);
    root->addWidget(slope); root->addStretch();

    connect(m_color, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setColor(c); });
    onValue(m_width, this, [this](double v) { if (m_a) m_a->setLineWidthPx(v); });
    connect(m_dash, &DashStyleCombo::penStyleChanged, this, [this](Qt::PenStyle s) { if (m_a) m_a->setDashPattern(s); });
    onValue(m_opacity, this, [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });
    connect(m_slopeDriven, &QCheckBox::toggled, this, [this](bool v) { if (m_a) m_a->setUseSlopeDrivenWidth(v); });
    onValue(m_slopeBreak, this, [this](double v) { if (m_a) m_a->setSlopeBreak(v); });
    onValue(m_wideWidth, this, [this](double v) { if (m_a) m_a->setWideWidthPx(v); });
    connect(m_wideColor, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setWideColor(c); });
    if (m_a) connect(m_a, &MeshEdgeSymbolStyleAdapter::changed,
                     this, &MeshEdgeSymbolStyleEditor::refreshFromModel, Qt::UniqueConnection);
    refreshFromModel();
}
void MeshEdgeSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b0(m_color), b1(m_width), b2(m_dash), b3(m_opacity),
                   b4(m_slopeDriven), b5(m_slopeBreak), b6(m_wideWidth), b7(m_wideColor);
    m_color->setColor(m_a->color());
    m_width->setValue(m_a->lineWidthPx());
    m_dash->setPenStyle(m_a->dashPattern());
    m_opacity->setValue(m_a->opacity() * 100.0);
    m_slopeDriven->setChecked(m_a->useSlopeDrivenWidth());
    m_slopeBreak->setValue(m_a->slopeBreak());
    m_wideWidth->setValue(m_a->wideWidthPx());
    m_wideColor->setColor(m_a->wideColor());
}

// ===========================================================================
// Mesh node
// ===========================================================================
MeshNodeSymbolStyleEditor::MeshNodeSymbolStyleEditor(
    MeshNodeSymbolStyleAdapter *a, QWidget *parent)
    : IStyleEditorWidget(parent), m_a(a)
{
    auto *root = new QVBoxLayout(this); root->setContentsMargins(8, 8, 8, 8);
    QFormLayout *f = nullptr;
    auto *box = group(this, tr("Mesh Vertices"), f);
    m_shape = new MarkerShapeCombo(box); m_shape->populateCanonical(); f->addRow(tr("Shape:"), m_shape);
    m_size = dspin(box, 0.5, 60.0, 1, 0.5, tr(" px")); f->addRow(tr("Si&ze:"), m_size);
    m_color = colorBtn(box); f->addRow(tr("Fill:"), m_color);
    m_outline = colorBtn(box); f->addRow(tr("Outline:"), m_outline);
    m_outlineW = dspin(box, 0.0, 20.0, 2, 0.25, tr(" px")); f->addRow(tr("Outline width:"), m_outlineW);
    m_opacity = opacitySpin(box); f->addRow(tr("Opacity:"), m_opacity);
    root->addWidget(box);

    QFormLayout *tf = nullptr;
    auto *tag = group(this, tr("Tagged vertices"), tf);
    m_highlightTagged = new QCheckBox(tr("Highlight tagged"), tag); tf->addRow(QString(), m_highlightTagged);
    m_taggedColor = colorBtn(tag); tf->addRow(tr("Tagged colour:"), m_taggedColor);
    m_taggedSize = dspin(tag, 0.5, 60.0, 1, 0.5, tr(" px")); tf->addRow(tr("Tagged size:"), m_taggedSize);
    root->addWidget(tag); root->addStretch();

    connect(m_shape, &MarkerShapeCombo::shapeValueChanged, this,
            [this](int v) { if (m_a) m_a->setShape(static_cast<MarkerShape>(v)); });
    onValue(m_size, this, [this](double v) { if (m_a) m_a->setMarkerSizePx(v); });
    connect(m_color, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setColor(c); });
    connect(m_outline, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setOutlineColor(c); });
    onValue(m_outlineW, this, [this](double v) { if (m_a) m_a->setOutlineWidthPx(v); });
    onValue(m_opacity, this, [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });
    connect(m_highlightTagged, &QCheckBox::toggled, this, [this](bool v) { if (m_a) m_a->setHighlightTagged(v); });
    connect(m_taggedColor, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setTaggedColor(c); });
    onValue(m_taggedSize, this, [this](double v) { if (m_a) m_a->setTaggedSizePx(v); });
    if (m_a) connect(m_a, &MeshNodeSymbolStyleAdapter::changed,
                     this, &MeshNodeSymbolStyleEditor::refreshFromModel, Qt::UniqueConnection);
    refreshFromModel();
}
void MeshNodeSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b0(m_shape), b1(m_size), b2(m_color), b3(m_outline), b4(m_outlineW),
                   b5(m_opacity), b6(m_highlightTagged), b7(m_taggedColor), b8(m_taggedSize);
    m_shape->setShapeValue(static_cast<int>(m_a->shape()));
    m_size->setValue(m_a->markerSizePx());
    m_color->setColor(m_a->color());
    m_outline->setColor(m_a->outlineColor());
    m_outlineW->setValue(m_a->outlineWidthPx());
    m_opacity->setValue(m_a->opacity() * 100.0);
    m_highlightTagged->setChecked(m_a->highlightTagged());
    m_taggedColor->setColor(m_a->taggedColor());
    m_taggedSize->setValue(m_a->taggedSizePx());
}

// ===========================================================================
// Velocity glyph
// ===========================================================================
VelocityVectorSymbolStyleEditor::VelocityVectorSymbolStyleEditor(
    VelocityVectorSymbolStyleAdapter *a, QWidget *parent)
    : IStyleEditorWidget(parent), m_a(a)
{
    auto *root = new QVBoxLayout(this); root->setContentsMargins(8, 8, 8, 8);
    QFormLayout *f = nullptr;
    auto *box = group(this, tr("Flow Velocity"), f);
    m_color = colorBtn(box); f->addRow(tr("Colour:"), m_color);
    m_scale = dspin(box, 0.0, 500.0, 1, 1.0, tr(" px/(m/s)")); f->addRow(tr("Length scale:"), m_scale);
    m_minLen = dspin(box, 0.0, 200.0, 1, 1.0, tr(" px")); f->addRow(tr("Min length:"), m_minLen);
    m_maxLen = dspin(box, 0.0, 400.0, 1, 1.0, tr(" px")); f->addRow(tr("Max length:"), m_maxLen);
    m_spacing = dspin(box, 1.0, 400.0, 1, 1.0, tr(" px")); f->addRow(tr("Spacing:"), m_spacing);
    m_head = dspin(box, 0.0, 64.0, 1, 0.5, tr(" px")); f->addRow(tr("Head size:"), m_head);
    m_dry = dspin(box, 0.0, 100.0, 3, 0.01); f->addRow(tr("Dry-depth cutoff:"), m_dry);
    m_opacity = opacitySpin(box); f->addRow(tr("Opacity:"), m_opacity);
    root->addWidget(box); root->addStretch();

    connect(m_color, &ColorButton::colorChanged, this, [this](const QColor &c) { if (m_a) m_a->setColor(c); });
    onValue(m_scale, this, [this](double v) { if (m_a) m_a->setGlyphLengthScalePxPerMps(v); });
    onValue(m_minLen, this, [this](double v) { if (m_a) m_a->setGlyphLengthMinPx(v); });
    onValue(m_maxLen, this, [this](double v) { if (m_a) m_a->setGlyphLengthMaxPx(v); });
    onValue(m_spacing, this, [this](double v) { if (m_a) m_a->setGlyphSpacingPx(v); });
    onValue(m_head, this, [this](double v) { if (m_a) m_a->setHeadSizePx(v); });
    onValue(m_dry, this, [this](double v) { if (m_a) m_a->setDryDepthCutoff(v); });
    onValue(m_opacity, this, [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });
    if (m_a) connect(m_a, &VelocityVectorSymbolStyleAdapter::changed,
                     this, &VelocityVectorSymbolStyleEditor::refreshFromModel, Qt::UniqueConnection);
    refreshFromModel();
}
void VelocityVectorSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b0(m_color), b1(m_scale), b2(m_minLen), b3(m_maxLen),
                   b4(m_spacing), b5(m_head), b6(m_dry), b7(m_opacity);
    m_color->setColor(m_a->color());
    m_scale->setValue(m_a->glyphLengthScalePxPerMps());
    m_minLen->setValue(m_a->glyphLengthMinPx());
    m_maxLen->setValue(m_a->glyphLengthMaxPx());
    m_spacing->setValue(m_a->glyphSpacingPx());
    m_head->setValue(m_a->headSizePx());
    m_dry->setValue(m_a->dryDepthCutoff());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// Registry hookup. Unqualified tails; createEditorFor (SE.1) matches them
// against the namespaced metaObject className.
// ===========================================================================
#define REG2D(ADAPTER, EDITOR) \
    REGISTER_STYLE_EDITOR(ADAPTER, \
        [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * { \
            if (auto *a = qobject_cast<OpenSWMM::Render::ADAPTER *>(obj)) \
                return new EDITOR(a, parent); \
            return nullptr; \
        })

REG2D(RasterColorRampSymbolStyleAdapter, RasterColorRampSymbolStyleEditor)
REG2D(HillshadeSymbolStyleAdapter,       HillshadeSymbolStyleEditor)
REG2D(ContourBandSymbolStyleAdapter,     ContourBandSymbolStyleEditor)
REG2D(IsolineSymbolStyleAdapter,         IsolineSymbolStyleEditor)
REG2D(MeshEdgeSymbolStyleAdapter,        MeshEdgeSymbolStyleEditor)
REG2D(MeshNodeSymbolStyleAdapter,        MeshNodeSymbolStyleEditor)
REG2D(VelocityVectorSymbolStyleAdapter,  VelocityVectorSymbolStyleEditor)

#undef REG2D

} // namespace openswmmvis::ui
