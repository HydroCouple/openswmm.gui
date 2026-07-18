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
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/widgets/classificationeditor.h"
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

/*! Build a ClassificationEditor bound to a mesh sublayer scheme. The sample
 *  provider feeds the "Auto-classify from data" (resample) button the mesh's
 *  bed-elevation distribution; the range provider gives it zMin/zMax. */
openswmmvis::ui::ClassificationEditor *makeMeshClassEditor(
    SWMM2DMeshLayer *layer,
    openswmmvis::ui::SublayerSchemeBinding::Getter getter,
    openswmmvis::ui::SublayerSchemeBinding::Setter setter,
    bool continuous, QWidget *parent)
{
    auto *binding = new openswmmvis::ui::SublayerSchemeBinding(
        std::move(getter), std::move(setter),
        [layer] { return layer->elevationSamples(); },
        [layer] { return qMakePair(layer->zMin(), layer->zMax()); },
        /*supportsContinuousMode=*/continuous,
        /*supportsRangeModes=*/false);
    return new openswmmvis::ui::ClassificationEditor(
        binding, /*ownBinding=*/true, parent);
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
    m_showNodes = new QCheckBox(tr("Show mesh vertices"), dispBox);
    dispLay->addWidget(m_showEdges);
    dispLay->addWidget(m_showNodes);

    // Zoom thresholds — edges / vertices auto-appear once cells project at
    // least this many pixels across. Keeps the far view clean; lets the user
    // decide how close is "close enough".
    auto *zoomForm = new QFormLayout;
    m_edgeZoomPx = new QDoubleSpinBox(dispBox);
    m_edgeZoomPx->setRange(0.0, 40.0);
    m_edgeZoomPx->setDecimals(1);
    m_edgeZoomPx->setSingleStep(0.5);
    m_edgeZoomPx->setSuffix(tr(" px/cell"));
    m_edgeZoomPx->setToolTip(tr(
        "Show the wireframe once mesh cells project at least this many "
        "pixels across. 0 = always show when enabled."));
    zoomForm->addRow(tr("Show edges at:"), m_edgeZoomPx);

    m_vertexZoomPx = new QDoubleSpinBox(dispBox);
    m_vertexZoomPx->setRange(0.0, 40.0);
    m_vertexZoomPx->setDecimals(1);
    m_vertexZoomPx->setSingleStep(0.5);
    m_vertexZoomPx->setSuffix(tr(" px/cell"));
    m_vertexZoomPx->setToolTip(tr(
        "Show vertex dots once mesh cells project at least this many pixels "
        "across. 0 = always show when enabled."));
    zoomForm->addRow(tr("Show vertices at:"), m_vertexZoomPx);
    dispLay->addLayout(zoomForm);

    root->addWidget(dispBox);

    // ── Terrain fill group ─────────────────────────────────────────────
    // Full classification of the shaded-relief terrain fill by bed
    // elevation: a colour ramp/band (not just high/low), Continuous or
    // Classified mode, a classification method (equal interval / quantile /
    // natural breaks / std-dev / log / exp / manual), class count, an
    // "Auto-classify from data" (resample) button that computes the class
    // bins from the mesh's elevation distribution, and an editable bin
    // table. Terrain always classifies by elevation (no attribute picker).
    auto *tBox  = new QGroupBox(tr("Terrain fill (bed elevation)"), this);
    auto *tLay  = new QVBoxLayout(tBox);
    m_terrainClassEditor = makeMeshClassEditor(
        m_layer,
        [this] {
            auto *f = fillStyleOf(m_layer);
            return f ? f->scheme() : OpenSWMM::Render::ClassificationScheme{};
        },
        [this](const OpenSWMM::Render::ClassificationScheme &s) {
            if (auto *f = fillStyleOf(m_layer)) f->setScheme(s);
        },
        /*continuous=*/true, tBox);
    tLay->addWidget(m_terrainClassEditor);

    auto *tOpRow = new QFormLayout;
    m_fillOpacity = new QDoubleSpinBox(this);
    m_fillOpacity->setRange(0.0, 1.0);
    m_fillOpacity->setSingleStep(0.05);
    m_fillOpacity->setDecimals(2);
    tOpRow->addRow(tr("Fill opacity:"), m_fillOpacity);
    tLay->addLayout(tOpRow);
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

    // ── Elevation contour lines (isolines — single colour) ─────────────
    auto *cBox = new QGroupBox(tr("Elevation contour lines"), this);
    auto *cForm = new QFormLayout(cBox);

    m_showContours = new QCheckBox(tr("Show contour lines"), cBox);
    cForm->addRow(QString(), m_showContours);

    m_intervals = new QSpinBox(this);
    m_intervals->setRange(1, 200);
    m_intervals->setSingleStep(1);
    cForm->addRow(tr("Line count:"), m_intervals);

    m_contourColor = new ColorButton(this);
    cForm->addRow(tr("Line colour:"), m_contourColor);

    m_contourWidth = new QDoubleSpinBox(this);
    m_contourWidth->setRange(0.25, 10.0);
    m_contourWidth->setDecimals(2);
    m_contourWidth->setSingleStep(0.25);
    m_contourWidth->setSuffix(tr(" px"));
    cForm->addRow(tr("Line width:"), m_contourWidth);

    m_showLabels = new QCheckBox(tr("Show elevation labels"), cBox);
    cForm->addRow(QString(), m_showLabels);
    root->addWidget(cBox);

    // ── Filled elevation bands (own classification) ─────────────────────
    // The filled contour bands get the full classification editor: colour
    // ramp/band, method, class count, resample, and an editable bin table.
    auto *bBox = new QGroupBox(tr("Filled elevation bands"), this);
    auto *bLay = new QVBoxLayout(bBox);

    m_filledContours = new QCheckBox(tr("Show filled elevation bands"), bBox);
    bLay->addWidget(m_filledContours);

    m_smoothBands = new QCheckBox(tr("Smooth band boundaries"), bBox);
    m_smoothBands->setToolTip(tr(
        "On: class boundaries are interpolated through cells (marching "
        "triangles). Off: each cell is filled flat with its band colour."));
    bLay->addWidget(m_smoothBands);

    auto *opRow = new QFormLayout;
    m_filledOpacity = new QDoubleSpinBox(this);
    m_filledOpacity->setRange(0.0, 1.0);
    m_filledOpacity->setSingleStep(0.05);
    m_filledOpacity->setDecimals(2);
    opRow->addRow(tr("Fill opacity:"), m_filledOpacity);
    bLay->addLayout(opRow);

    m_bandClassEditor = makeMeshClassEditor(
        m_layer,
        [this] {
            auto *b = bandStyleOf(m_layer);
            return b ? b->scheme() : OpenSWMM::Render::ClassificationScheme{};
        },
        [this](const OpenSWMM::Render::ClassificationScheme &s) {
            if (auto *b = bandStyleOf(m_layer)) b->setScheme(s);
        },
        /*continuous=*/false, bBox);
    bLay->addWidget(m_bandClassEditor);
    root->addWidget(bBox);
    root->addStretch();

    // ── Bindings ───────────────────────────────────────────────────────
    // Terrain-fill + band classification are driven by their embedded
    // ClassificationEditors (the bindings write the fill / band scheme and
    // fire styleChanged → repaint). No extra wiring needed here.

    connect(m_showEdges, &QCheckBox::toggled, this, [this](bool v) { m_layer->setShowEdges(v); });
    connect(m_showNodes, &QCheckBox::toggled, this, [this](bool v) { m_layer->setShowMeshNodes(v); });
    connect(m_edgeZoomPx, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_layer->setEdgeZoomMinCellPx(v); });
    connect(m_vertexZoomPx, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_layer->setVertexZoomMinCellPx(v); });

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
    // Line count drives only the isolines now — the filled bands own their
    // class count through the band ClassificationEditor below.
    connect(m_intervals, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) {
                if (auto *iso = isoStyleOf(m_layer))
                    iso->setIsoValueCount(v);
            });
    connect(m_contourColor, &ColorButton::colorChanged, this, [this](const QColor &c) { m_layer->setContourColor(c); });
    connect(m_contourWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) { m_layer->setContourLineWidth(v); });
    connect(m_showLabels, &QCheckBox::toggled, this, [this](bool v) {
        if (auto *iso = isoStyleOf(m_layer)) iso->setLabels(v);
    });
    connect(m_filledContours, &QCheckBox::toggled, this, [this](bool v) { m_layer->setFilledContours(v); });
    connect(m_filledOpacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) { m_layer->setFilledContoursOpacity(v); });
    connect(m_smoothBands, &QCheckBox::toggled, this, [this](bool v) {
        if (auto *b = bandStyleOf(m_layer)) b->setSmoothBands(v);
    });
    connect(m_fillOpacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (auto *fs = m_layer->meshFillSublayer()) fs->setOpacity(v);
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
        b16(m_showLabels), b17(m_smoothBands), b18(m_fillOpacity),
        b20(m_edgeZoomPx), b21(m_vertexZoomPx);

    m_edgeZoomPx->setValue(m_layer->edgeZoomMinCellPx());
    m_vertexZoomPx->setValue(m_layer->vertexZoomMinCellPx());

    // Classification editors re-read their bound scheme themselves.
    if (m_terrainClassEditor) m_terrainClassEditor->refresh();
    if (m_bandClassEditor)    m_bandClassEditor->refresh();

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
    if (auto *iso = isoStyleOf(m_layer)) {
        m_intervals->setValue(iso->isoValueCount());
        m_showLabels->setChecked(iso->labels());
    }
    m_contourColor->setColor(m_layer->contourColor());
    m_contourWidth->setValue(m_layer->contourLineWidth());
    m_filledContours->setChecked(m_layer->filledContours());
    m_filledOpacity->setValue(m_layer->filledContoursOpacity());
    if (auto *b = bandStyleOf(m_layer))
        m_smoothBands->setChecked(b->smoothBands());
    if (auto *fs = m_layer->meshFillSublayer())
        m_fillOpacity->setValue(fs->opacity());
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
