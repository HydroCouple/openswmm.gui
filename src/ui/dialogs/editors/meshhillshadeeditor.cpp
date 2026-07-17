/*!
 * \file   meshhillshadeeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/meshhillshadeeditor.h"

#include "layers/swmm2dmeshlayer.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/colorrampcombobox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace {

OpenSWMM::Render::ContourBandStyle *bandStyleOf(SWMM2DMeshLayer *layer)
{
    auto *sub = layer ? layer->contourBandSublayer() : nullptr;
    return sub ? sub->bandStyle() : nullptr;
}
OpenSWMM::Render::IsolineStyle *isoStyleOf(SWMM2DMeshLayer *layer)
{
    auto *sub = layer ? layer->isolineSublayer() : nullptr;
    return sub ? sub->isolineStyle() : nullptr;
}
OpenSWMM::Render::MeshFillStyle *fillStyleOf(SWMM2DMeshLayer *layer)
{
    auto *sub = layer ? layer->meshFillSublayer() : nullptr;
    return sub ? sub->fillStyle() : nullptr;
}

} // namespace

namespace openswmmvis::ui {

// ---------------------------------------------------------------------------
// SunPositionThumb
// ---------------------------------------------------------------------------

SunPositionThumb::SunPositionThumb(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
}

void SunPositionThumb::setAzimuth(double degrees)  { m_azimuth = degrees;  update(); }
void SunPositionThumb::setAltitude(double degrees) { m_altitude = degrees; update(); }

void SunPositionThumb::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Compass dial — a circle with N/E/S/W ticks.
    const QPointF c = QPointF(width(), height()) * 0.5;
    const double r = std::min(width(), height()) * 0.42;
    p.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    p.setBrush(QColor(245, 245, 245));
    p.drawEllipse(c, r, r);

    // Cardinal direction labels.
    const QFont prev = p.font();
    QFont labelFont = prev; labelFont.setBold(true); labelFont.setPointSizeF(prev.pointSizeF() - 1);
    p.setFont(labelFont);
    p.setPen(palette().color(QPalette::Text));
    auto label = [&](const QString &txt, const QPointF &pos) {
        const QRectF box(pos.x() - 12, pos.y() - 10, 24, 20);
        p.drawText(box, Qt::AlignCenter, txt);
    };
    label("N", QPointF(c.x(),          c.y() - r - 8));
    label("S", QPointF(c.x(),          c.y() + r + 8));
    label("E", QPointF(c.x() + r + 10, c.y()));
    label("W", QPointF(c.x() - r - 10, c.y()));
    p.setFont(prev);

    // Sun position: compass bearing measured clockwise from north.
    constexpr double kPi = 3.14159265358979323846;
    const double azRad = m_azimuth * kPi / 180.0;
    // Altitude collapses the vector toward centre at high sun angles.
    const double altT = std::clamp(m_altitude / 90.0, 0.0, 1.0);
    const double rr = r * (1.0 - altT);  // sun overhead → vector length 0
    const double dx =  std::sin(azRad) * rr;
    const double dy = -std::cos(azRad) * rr;
    const QPointF sun = c + QPointF(dx, dy);

    // Arrow from centre to sun, plus a sun glyph.
    p.setPen(QPen(QColor(220, 160, 0), 2.0));
    p.setBrush(QColor(255, 200, 60));
    p.drawLine(c, sun);
    p.drawEllipse(sun, 7.0, 7.0);

    // Altitude readout at the bottom.
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(rect().adjusted(0, 0, 0, -2),
               Qt::AlignBottom | Qt::AlignHCenter,
               QString::number(m_azimuth, 'f', 0) + "° / " +
               QString::number(m_altitude, 'f', 0) + "°");
}

// ---------------------------------------------------------------------------
// MeshHillshadeEditor
// ---------------------------------------------------------------------------

MeshHillshadeEditor::MeshHillshadeEditor(SWMM2DMeshLayer *layer, QWidget *parent)
    : IStyleEditorWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Display group ──────────────────────────────────────────────────
    auto *dispBox = new QGroupBox(tr("Display"), this);
    auto *dispLay = new QVBoxLayout(dispBox);
    m_showEdges = new QCheckBox(tr("Show mesh edges (wireframe)"), dispBox);
    m_showNodes = new QCheckBox(tr("Show mesh nodes"), dispBox);
    dispLay->addWidget(m_showEdges);
    dispLay->addWidget(m_showNodes);
    root->addWidget(dispBox);

    // ── Terrain fill group ─────────────────────────────────────────────
    // The colour scale of the shaded-relief terrain fill itself. Terrain
    // meshes always classify by bed elevation (no attribute picker — unlike
    // 2D results layers there is nothing else to colour by).
    auto *tBox  = new QGroupBox(tr("Terrain fill (bed elevation)"), this);
    auto *tForm = new QFormLayout(tBox);
    m_terrainRamp = new ColorRampComboBox(this);
    m_terrainRamp->setToolTip(tr(
        "Colour scale for the shaded-relief terrain fill. Elevation is "
        "always the classified value for a terrain mesh."));
    tForm->addRow(tr("Colour ramp:"), m_terrainRamp);
    m_terrainInvert = new QCheckBox(tr("Invert ramp"), tBox);
    tForm->addRow(QString(), m_terrainInvert);
    root->addWidget(tBox);

    // ── Hillshade group ────────────────────────────────────────────────
    auto *hsBox = new QGroupBox(tr("Hillshade (sun lighting)"), this);
    auto *hsLay = new QHBoxLayout(hsBox);

    auto *hsForm = new QFormLayout;

    // Azimuth: slider + spinbox (wraps 0..360)
    auto *azRow = new QWidget(this);
    auto *azLay = new QHBoxLayout(azRow);
    azLay->setContentsMargins(0, 0, 0, 0);
    m_azimuth = new QDoubleSpinBox(this);
    m_azimuth->setRange(0.0, 360.0);
    m_azimuth->setSingleStep(5.0);
    m_azimuth->setWrapping(true);
    m_azimuth->setSuffix(tr("°"));
    m_azSlider = new QSlider(Qt::Horizontal, this);
    m_azSlider->setRange(0, 360);
    azLay->addWidget(m_azSlider, 1);
    azLay->addWidget(m_azimuth);
    hsForm->addRow(tr("Azimuth (light from):"), azRow);

    auto *altRow = new QWidget(this);
    auto *altLay = new QHBoxLayout(altRow);
    altLay->setContentsMargins(0, 0, 0, 0);
    m_altitude = new QDoubleSpinBox(this);
    m_altitude->setRange(0.0, 90.0);
    m_altitude->setSingleStep(5.0);
    m_altitude->setSuffix(tr("°"));
    m_altSlider = new QSlider(Qt::Horizontal, this);
    m_altSlider->setRange(0, 90);
    altLay->addWidget(m_altSlider, 1);
    altLay->addWidget(m_altitude);
    hsForm->addRow(tr("Altitude (sun angle):"), altRow);

    m_zExag = new QDoubleSpinBox(this);
    m_zExag->setRange(0.1, 100.0);
    m_zExag->setSingleStep(0.5);
    m_zExag->setDecimals(2);
    m_zExag->setSuffix(tr(" ×"));
    hsForm->addRow(tr("Vertical exaggeration:"), m_zExag);

    auto *minLitRow = new QWidget(this);
    auto *minLitLay = new QHBoxLayout(minLitRow);
    minLitLay->setContentsMargins(0, 0, 0, 0);
    m_minLit = new QDoubleSpinBox(this);
    m_minLit->setRange(0.0, 1.0);
    m_minLit->setSingleStep(0.05);
    m_minLit->setDecimals(2);
    m_minLitSlider = new QSlider(Qt::Horizontal, this);
    m_minLitSlider->setRange(0, 100);
    minLitLay->addWidget(m_minLitSlider, 1);
    minLitLay->addWidget(m_minLit);
    hsForm->addRow(tr("Shadow floor:"), minLitRow);

    hsLay->addLayout(hsForm, 2);

    m_sunThumb = new SunPositionThumb(this);
    hsLay->addWidget(m_sunThumb, 1);

    root->addWidget(hsBox);

    // ── Contours group ─────────────────────────────────────────────────
    auto *cBox = new QGroupBox(tr("Contours (bed elevation)"), this);
    auto *cForm = new QFormLayout(cBox);

    m_showContours = new QCheckBox(tr("Show contour lines"), cBox);
    cForm->addRow(QString(), m_showContours);

    m_intervals = new QSpinBox(this);
    m_intervals->setRange(1, 200);
    m_intervals->setSingleStep(1);
    cForm->addRow(tr("Intervals:"), m_intervals);

    // Slice US.3 — classification method for the filled bands / isolines.
    // EqualInterval reproduces the legacy even spacing; quantile / Jenks /
    // std-dev bin the bed elevations.
    using BM = OpenSWMM::Render::BinMethod;
    m_contourMethod = new QComboBox(this);
    m_contourMethod->addItem(tr("Equal interval"),         int(BM::EqualInterval));
    m_contourMethod->addItem(tr("Quantile"),               int(BM::Quantile));
    m_contourMethod->addItem(tr("Natural breaks (Jenks)"), int(BM::NaturalBreaks));
    m_contourMethod->addItem(tr("Standard deviation"),     int(BM::StdDev));
    m_contourMethod->addItem(tr("Logarithmic"),            int(BM::Logarithmic));
    m_contourMethod->addItem(tr("Exponential"),            int(BM::Exponential));
    cForm->addRow(tr("Method:"), m_contourMethod);

    m_contourColor = new ColorButton(this);
    cForm->addRow(tr("Line colour:"), m_contourColor);

    m_contourWidth = new QDoubleSpinBox(this);
    m_contourWidth->setRange(0.25, 10.0);
    m_contourWidth->setDecimals(2);
    m_contourWidth->setSingleStep(0.25);
    m_contourWidth->setSuffix(tr(" px"));
    cForm->addRow(tr("Line width:"), m_contourWidth);

    m_filledContours = new QCheckBox(tr("Filled elevation bands"), cBox);
    cForm->addRow(QString(), m_filledContours);

    // Slice US.3 — colour scale for the filled bands.
    m_contourRamp = new ColorRampComboBox(this);
    cForm->addRow(tr("Band colour scale:"), m_contourRamp);

    m_filledOpacity = new QDoubleSpinBox(this);
    m_filledOpacity->setRange(0.0, 1.0);
    m_filledOpacity->setSingleStep(0.05);
    m_filledOpacity->setDecimals(2);
    cForm->addRow(tr("Fill opacity:"), m_filledOpacity);

    root->addWidget(cBox);
    root->addStretch();

    // ── Bindings ───────────────────────────────────────────────────────
    // Terrain fill ramp — writes the FILL sublayer's classification scheme
    // (distinct from the contour-band scale below). Both renderers consume
    // it through the schemeDrivesColor branch each repaint.
    connect(m_terrainRamp, &ColorRampComboBox::rampChanged, this,
            [this](const RasterColorRamp &) {
                if (auto *fill = fillStyleOf(m_layer)) {
                    auto s = fill->scheme();
                    s.setRampName(m_terrainRamp->currentText());
                    fill->setScheme(s);
                }
            });
    connect(m_terrainInvert, &QCheckBox::toggled, this, [this](bool v) {
        if (auto *fill = fillStyleOf(m_layer)) {
            auto s = fill->scheme();
            s.setInvertRamp(v);
            fill->setScheme(s);
        }
    });

    connect(m_showEdges, &QCheckBox::toggled, this, [this](bool v) { m_layer->setShowEdges(v); });
    connect(m_showNodes, &QCheckBox::toggled, this, [this](bool v) { m_layer->setShowMeshNodes(v); });

    auto pushAz = [this](double v) {
        m_layer->setHillshadeAzimuth(v);
        QSignalBlocker bs(m_azSlider); m_azSlider->setValue(int(std::lround(v)));
        m_sunThumb->setAzimuth(v);
    };
    connect(m_azimuth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, pushAz);
    connect(m_azSlider, &QSlider::valueChanged, this, [this, pushAz](int v) {
        QSignalBlocker bs(m_azimuth); m_azimuth->setValue(double(v)); pushAz(double(v));
    });

    auto pushAlt = [this](double v) {
        m_layer->setHillshadeAltitude(v);
        QSignalBlocker bs(m_altSlider); m_altSlider->setValue(int(std::lround(v)));
        m_sunThumb->setAltitude(v);
    };
    connect(m_altitude, qOverload<double>(&QDoubleSpinBox::valueChanged), this, pushAlt);
    connect(m_altSlider, &QSlider::valueChanged, this, [this, pushAlt](int v) {
        QSignalBlocker bs(m_altitude); m_altitude->setValue(double(v)); pushAlt(double(v));
    });

    connect(m_zExag, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_layer->setHillshadeZExag(v); });

    auto pushMinLit = [this](double v) {
        m_layer->setHillshadeMinLit(v);
        QSignalBlocker bs(m_minLitSlider); m_minLitSlider->setValue(int(std::lround(v * 100.0)));
    };
    connect(m_minLit, qOverload<double>(&QDoubleSpinBox::valueChanged), this, pushMinLit);
    connect(m_minLitSlider, &QSlider::valueChanged, this, [this, pushMinLit](int v) {
        const double f = double(v) / 100.0;
        QSignalBlocker bs(m_minLit); m_minLit->setValue(f); pushMinLit(f);
    });

    connect(m_showContours, &QCheckBox::toggled, this, [this](bool v) { m_layer->setShowContours(v); });
    connect(m_intervals, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) { m_layer->setContourIntervalCount(v); });
    connect(m_contourColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_layer->setContourColor(c); });
    connect(m_contourWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) { m_layer->setContourLineWidth(v); });
    connect(m_filledContours, &QCheckBox::toggled, this, [this](bool v) { m_layer->setFilledContours(v); });
    connect(m_filledOpacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) { m_layer->setFilledContoursOpacity(v); });

    // Slice US.3 — push the band classification method + colour scale onto the
    // band sublayer's scheme (the isoline scheme shares the method so lines and
    // bands stay coherent). Both renderers read the scheme each paint.
    connect(m_contourMethod, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) {
                const auto m = static_cast<OpenSWMM::Render::BinMethod>(
                    m_contourMethod->itemData(i).toInt());
                if (auto *band = bandStyleOf(m_layer)) {
                    auto s = band->scheme(); s.setMethod(m); band->setScheme(s);
                }
                if (auto *iso = isoStyleOf(m_layer)) {
                    auto s = iso->scheme(); s.setMethod(m); iso->setScheme(s);
                }
            });
    connect(m_contourRamp, &ColorRampComboBox::rampChanged, this,
            [this](const RasterColorRamp &) {
                if (auto *band = bandStyleOf(m_layer)) {
                    auto s = band->scheme();
                    s.setRampName(m_contourRamp->currentText());
                    band->setScheme(s);
                }
            });

    refreshFromModel();
}

void MeshHillshadeEditor::refreshFromModel()
{
    if (!m_layer) return;

    QSignalBlocker b1(m_showEdges), b2(m_showNodes),
        b3(m_azimuth), b4(m_azSlider),
        b5(m_altitude), b6(m_altSlider),
        b7(m_zExag), b8(m_minLit), b9(m_minLitSlider),
        b10(m_showContours), b11(m_intervals),
        b12(m_contourColor), b13(m_contourWidth),
        b14(m_filledContours), b15(m_filledOpacity),
        b16(m_contourMethod), b17(m_contourRamp),
        b18(m_terrainRamp), b19(m_terrainInvert);

    if (auto *fill = fillStyleOf(m_layer)) {
        const QString ramp = fill->scheme().rampName();
        if (!ramp.isEmpty())
            m_terrainRamp->setCurrentRampByName(ramp);
        m_terrainInvert->setChecked(fill->scheme().invertRamp());
    }

    m_showEdges->setChecked(m_layer->showEdges());
    m_showNodes->setChecked(m_layer->showMeshNodes());

    const double az = m_layer->hillshadeAzimuth();
    m_azimuth->setValue(az);
    m_azSlider->setValue(int(std::lround(az)));

    const double alt = m_layer->hillshadeAltitude();
    m_altitude->setValue(alt);
    m_altSlider->setValue(int(std::lround(alt)));

    m_zExag->setValue(m_layer->hillshadeZExag());

    const double ml = m_layer->hillshadeMinLit();
    m_minLit->setValue(ml);
    m_minLitSlider->setValue(int(std::lround(ml * 100.0)));

    m_sunThumb->setAzimuth(az);
    m_sunThumb->setAltitude(alt);

    m_showContours->setChecked(m_layer->showContours());
    m_intervals->setValue(m_layer->contourIntervalCount());
    m_contourColor->setColor(m_layer->contourColor());
    m_contourWidth->setValue(m_layer->contourLineWidth());
    m_filledContours->setChecked(m_layer->filledContours());
    m_filledOpacity->setValue(m_layer->filledContoursOpacity());

    // Slice US.3 — sync the classification method + colour scale from the band
    // sublayer scheme.
    if (auto *band = bandStyleOf(m_layer)) {
        m_contourMethod->setCurrentIndex(
            m_contourMethod->findData(int(band->scheme().method())));
        const QString ramp = band->scheme().rampName();
        if (!ramp.isEmpty())
            m_contourRamp->setCurrentRampByName(ramp);
    }
}

// Registry — MeshHillshadeEditor displaces the QPropertyModel fallback for
// SWMM2DMeshLayer (its styleSubjects() points propertyObject at the layer).
REGISTER_STYLE_EDITOR(
    SWMM2DMeshLayer,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *l = qobject_cast<SWMM2DMeshLayer *>(obj))
            return new MeshHillshadeEditor(l, parent);
        return nullptr;
    })

} // namespace openswmmvis::ui
