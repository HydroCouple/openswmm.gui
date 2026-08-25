/*!
 * \file   swmm2dmeshstylepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Per-sublayer styling panel for SWMM2DMeshLayer.
 *
 *         Pattern (same as Swmm2DResultsStylePanel): each control is
 *         initialised from the style bag BEFORE its change-signal is
 *         connected, so construction never fires spurious writes;
 *         afterwards every edit applies live.
 */
#include "ui/dialogs/swmm2dmeshstylepanel.h"

#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshbctype.h"
#include "mesh/meshcellparams.h"
#include "render/categoricalpalette.h"
#include "render/sublayers/contourbandsublayer.h"
#include "render/sublayers/couplednodesublayer.h"
#include "render/sublayers/isolinesublayer.h"
#include "render/sublayers/meshbcsublayer.h"
#include "render/sublayers/meshedgesublayer.h"
#include "render/sublayers/meshfillsublayer.h"
#include "render/sublayers/meshnodesublayer.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/dialogs/sublayertabhelpers.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/dashstylecombo.h"
#include "ui/widgets/sunpositionthumb.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaEnum>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

namespace openswmmvis::ui {

namespace {

using OpenSWMM::Render::ContourBandStyle;
using OpenSWMM::Render::CoupledNodeStyle;
using OpenSWMM::Render::IsolineStyle;
using OpenSWMM::Render::MeshBcStyle;
using OpenSWMM::Render::MeshEdgeStyle;
using OpenSWMM::Render::MeshFillStyle;
using OpenSWMM::Render::MeshNodeStyle;

/*! ClassificationEditor bound to a mesh sublayer scheme: the sample
 *  provider feeds "Auto-classify from data" the mesh's bed-elevation
 *  distribution; the range provider gives it zMin/zMax. */
ClassificationEditor *makeMeshClassEditor(
    SWMM2DMeshLayer *layer,
    SublayerSchemeBinding::Getter getter,
    SublayerSchemeBinding::Setter setter,
    bool continuous, QWidget *parent)
{
    auto *binding = new SublayerSchemeBinding(
        std::move(getter), std::move(setter),
        [layer] { return layer->elevationSamples(); },
        [layer] { return qMakePair(layer->zMin(), layer->zMax()); },
        /*supportsContinuousMode=*/continuous,
        /*supportsRangeModes=*/false);
    return new ClassificationEditor(binding, /*ownBinding=*/true, parent);
}

/*! Display label for a CellAttribute — the cell-parameter registry's own
 *  label where one exists; Elevation is the one non-registry source. */
QString cellAttributeLabel(MeshFillStyle::CellAttribute a)
{
    if (a == MeshFillStyle::CellAttribute::Elevation)
        return QObject::tr("Bed elevation");
    const QByteArray key = MeshFillStyle::attributeKey(a);
    for (const auto &spec : mesh::cellParamSpecs())
        if (spec.key == key)
            return spec.label;
    return QString::fromLatin1(key);
}

} // namespace

Swmm2DMeshStylePanel::Swmm2DMeshStylePanel(SWMM2DMeshLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    if (!m_layer) return;

    auto wrapScroll = [tabs](QWidget *page) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        return scroll;
    };
    // Routing contract: LayerStyleDialog::focusInitialSubject() matches
    // tabToolTip == the sublayer id.
    auto addTab = [tabs, &wrapScroll](QWidget *page, const QString &label,
                                      OpenSWMM::Render::ISublayer *sub) {
        const int idx = tabs->addTab(wrapScroll(page), label);
        if (sub) tabs->setTabToolTip(idx, sub->id());
    };

    addTab(buildTerrainFillTab(tabs), tr("&Terrain Fill"),       m_layer->meshFillSublayer());
    addTab(buildBandTab(tabs),        tr("Elevation &Bands"),    m_layer->contourBandSublayer());
    addTab(buildIsolineTab(tabs),     tr("Elevation &Isolines"), m_layer->isolineSublayer());
    addTab(buildMeshEdgeTab(tabs),    tr("&Mesh Edges"),         m_layer->meshEdgeSublayer());
    addTab(buildMeshNodeTab(tabs),    tr("Mes&h Vertices"),      m_layer->meshNodeSublayer());
    addTab(buildBcTab(tabs),          tr("Boundary &Conditions"), m_layer->meshBcSublayer());
    addTab(buildCoupledNodeTab(tabs), tr("Couple&d Nodes"),      m_layer->coupledNodeSublayer());
}

// ─── Terrain fill (ramp + attribute colouring + hillshade) ──────────────────

QWidget *Swmm2DMeshStylePanel::buildTerrainFillTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshFillSublayer();
    MeshFillStyle *st = sub ? sub->fillStyle() : nullptr;
    auto *L = m_layer.data();

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show terrain fill")));

    if (st) {
        // ── Colour by attribute ─────────────────────────────────────────
        // Bed elevation is the default; any registry cell parameter
        // (Manning's n, init depth, infiltration …) can drive the fill.
        // Previously colorByAttribute / noDataColor / categoryPalette had
        // no UI at all.
        auto *attrBox  = new QGroupBox(tr("Colour by"), page);
        auto *attrForm = new QFormLayout(attrBox);

        auto *attr = new QComboBox(attrBox);
        const QMetaEnum me = QMetaEnum::fromType<MeshFillStyle::CellAttribute>();
        for (int i = 0; i < me.keyCount(); ++i) {
            const auto a = MeshFillStyle::CellAttribute(me.value(i));
            attr->addItem(cellAttributeLabel(a), me.value(i));
        }
        attr->setCurrentIndex(attr->findData(int(st->colorByAttribute())));
        attr->setMinimumWidth(kComboMinWidthPx);
        attrForm->addRow(tr("&Attribute:"), attr);

        auto *noData = new ColorButton(attrBox);
        noData->setShowAlpha(true);
        noData->setColor(st->noDataColor());
        noData->setToolTip(tr("Colour for cells whose selected attribute is "
                              "unset (no data)."));
        attrForm->addRow(tr("No-data colour:"), noData);

        auto *palette = new QComboBox(attrBox);
        palette->addItems(CategoricalPalette::builtinNames());
        {
            const int pi = palette->findText(st->categoryPalette(), Qt::MatchFixedString);
            palette->setCurrentIndex(pi >= 0 ? pi : 0);
        }
        palette->setMinimumWidth(kComboMinWidthPx);
        palette->setToolTip(tr("Palette for categorical attributes (the "
                               "infiltration method)."));
        attrForm->addRow(tr("Category palette:"), palette);
        lay->addWidget(attrBox);

        auto applyCategorical = [st, palette]() {
            palette->setEnabled(st->colorsByCategory());
        };
        applyCategorical();

        connect(attr, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [st, attr, applyCategorical](int idx) {
                    st->setColorByAttribute(
                        MeshFillStyle::CellAttribute(attr->itemData(idx).toInt()));
                    applyCategorical();
                });
        connect(noData, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setNoDataColor(c); });
        connect(palette, &QComboBox::currentTextChanged, this,
                [st](const QString &name) { st->setCategoryPalette(name); });

        // ── Classification (ramp / method / class table) ────────────────
        lay->addWidget(makeMeshClassEditor(
            L,
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            /*continuous=*/true, page));

        // ── Hillshade (sun lighting) ────────────────────────────────────
        // The sun parameters live on the LAYER's Q_PROPERTYs (the renderer
        // reads them there; hillshadeStrength maps into MeshFillStyle) —
        // snapshot/undo coverage comes from the "mesh.layer" style subject.
        auto *hsBox = new QGroupBox(tr("Hillshade (sun lighting)"), page);
        auto *hsLay = new QHBoxLayout(hsBox);
        auto *hsForm = new QFormLayout;

        auto *sunThumb = new SunPositionThumb(hsBox);
        sunThumb->setAzimuth(L->hillshadeAzimuth());
        sunThumb->setAltitude(L->hillshadeAltitude());

        auto *azRow = new QWidget(hsBox);
        auto *azLay = new QHBoxLayout(azRow);
        azLay->setContentsMargins(0, 0, 0, 0);
        auto *azimuth = makeDSpin(azRow, 0.0, 360.0, 5.0, 1,
                                  L->hillshadeAzimuth(), tr("°"));
        azimuth->setWrapping(true);
        auto *azSlider = new QSlider(Qt::Horizontal, azRow);
        azSlider->setRange(0, 360);
        azSlider->setValue(int(std::lround(L->hillshadeAzimuth())));
        azLay->addWidget(azSlider, 1);
        azLay->addWidget(azimuth);
        hsForm->addRow(tr("A&zimuth (light from):"), azRow);

        auto *altRow = new QWidget(hsBox);
        auto *altLay = new QHBoxLayout(altRow);
        altLay->setContentsMargins(0, 0, 0, 0);
        auto *altitude = makeDSpin(altRow, 0.0, 90.0, 5.0, 1,
                                   L->hillshadeAltitude(), tr("°"));
        auto *altSlider = new QSlider(Qt::Horizontal, altRow);
        altSlider->setRange(0, 90);
        altSlider->setValue(int(std::lround(L->hillshadeAltitude())));
        altLay->addWidget(altSlider, 1);
        altLay->addWidget(altitude);
        hsForm->addRow(tr("A&ltitude (sun angle):"), altRow);

        auto *zExag = makeDSpin(hsBox, 0.1, 100.0, 0.5, 2,
                                L->hillshadeZExag(), tr(" ×"));
        hsForm->addRow(tr("&Vertical exaggeration:"), zExag);

        auto *minLit = makeDSpin(hsBox, 0.0, 1.0, 0.05, 2, L->hillshadeMinLit());
        minLit->setToolTip(tr("Minimum lit value so deep shadows don't go "
                              "fully black."));
        hsForm->addRow(tr("Sha&dow floor:"), minLit);

        hsLay->addLayout(hsForm, 2);
        hsLay->addWidget(sunThumb, 1);
        lay->addWidget(hsBox);

        auto pushAz = [L, azSlider, sunThumb](double v) {
            L->setHillshadeAzimuth(v);
            QSignalBlocker bs(azSlider);
            azSlider->setValue(int(std::lround(v)));
            sunThumb->setAzimuth(v);
        };
        connect(azimuth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, pushAz);
        connect(azSlider, &QSlider::valueChanged, this,
                [azimuth, pushAz](int v) {
                    QSignalBlocker bs(azimuth);
                    azimuth->setValue(double(v));
                    pushAz(double(v));
                });
        auto pushAlt = [L, altSlider, sunThumb](double v) {
            L->setHillshadeAltitude(v);
            QSignalBlocker bs(altSlider);
            altSlider->setValue(int(std::lround(v)));
            sunThumb->setAltitude(v);
        };
        connect(altitude, qOverload<double>(&QDoubleSpinBox::valueChanged), this, pushAlt);
        connect(altSlider, &QSlider::valueChanged, this,
                [altitude, pushAlt](int v) {
                    QSignalBlocker bs(altitude);
                    altitude->setValue(double(v));
                    pushAlt(double(v));
                });
        connect(zExag, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [L](double v) { L->setHillshadeZExag(v); });
        connect(minLit, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [L](double v) { L->setHillshadeMinLit(v); });
    }
    lay->addStretch();
    return page;
}

// ─── Filled elevation bands ─────────────────────────────────────────────────

QWidget *Swmm2DMeshStylePanel::buildBandTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->contourBandSublayer();
    ContourBandStyle *st = sub ? sub->bandStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show filled elevation bands")));

    if (st) {
        lay->addWidget(makeMeshClassEditor(
            m_layer.data(),
            [st] { return st->scheme(); },
            [st](const OpenSWMM::Render::ClassificationScheme &s) { st->setScheme(s); },
            /*continuous=*/false, page));

        auto *renderBox  = new QGroupBox(tr("Rendering"), page);
        auto *renderForm = new QFormLayout(renderBox);
        auto *smooth = new QCheckBox(
            tr("Interpolate band boundaries (marching triangles)"), renderBox);
        smooth->setChecked(st->smoothBands());
        smooth->setToolTip(tr("On: class boundaries are interpolated through "
                              "cells (marching triangles). Off: each cell is "
                              "filled flat with its band colour."));
        renderForm->addRow(QString(), smooth);
        lay->addWidget(renderBox);

        connect(smooth, &QCheckBox::toggled, this,
                [st](bool on) { st->setSmoothBands(on); });
    }
    lay->addStretch();
    return page;
}

// ─── Elevation isolines ─────────────────────────────────────────────────────

QWidget *Swmm2DMeshStylePanel::buildIsolineTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->isolineSublayer();
    IsolineStyle *st = sub ? sub->isolineStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show elevation contour lines")));

    if (st) {
        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *count = makeSpin(symBox, 1, 200, st->isoValueCount());
        symForm->addRow(tr("L&ine count:"), count);

        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        symForm->addRow(tr("Li&ne colour:"), color);

        auto *width = makeDSpin(symBox, 0.25, 10.0, 0.25, 2,
                                st->lineWidthPx(), tr(" px"));
        symForm->addRow(tr("Lin&e width:"), width);

        auto *labels = new QCheckBox(tr("Show elevation labels"), symBox);
        labels->setChecked(st->labels());
        symForm->addRow(QString(), labels);
        lay->addWidget(symBox);

        connect(count, qOverload<int>(&QSpinBox::valueChanged), this,
                [st](int v) { st->setIsoValueCount(v); });
        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLineWidthPx(v); });
        connect(labels, &QCheckBox::toggled, this,
                [st](bool v) { st->setLabels(v); });
    }
    lay->addStretch();
    return page;
}

// ─── Mesh edges (wireframe + slope emphasis) ────────────────────────────────

QWidget *Swmm2DMeshStylePanel::buildMeshEdgeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshEdgeSublayer();
    MeshEdgeStyle *st = sub ? sub->edgeStyle() : nullptr;
    auto *L = m_layer.data();

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show mesh edges (wireframe)")));

    if (st) {
        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        symForm->addRow(tr("Colour:"), color);

        auto *width = makeDSpin(symBox, 0.1, 10.0, 0.05, 2,
                                st->lineWidthPx(), tr(" px"));
        symForm->addRow(tr("Width:"), width);

        auto *dash = new DashStyleCombo(symBox);
        dash->setPenStyle(st->dashPattern());
        dash->setMinimumWidth(kComboMinWidthPx);
        symForm->addRow(tr("Stro&ke style:"), dash);

        auto *edgeZoom = makeDSpin(symBox, 0.0, 40.0, 0.5, 1,
                                   L->edgeZoomMinCellPx(), tr(" px/cell"));
        edgeZoom->setToolTip(tr(
            "Show the wireframe once mesh cells project at least this many "
            "pixels across. 0 = always show when enabled."));
        symForm->addRow(tr("S&how edges at:"), edgeZoom);
        lay->addWidget(symBox);

        auto *slopeBox  = new QGroupBox(tr("Slope emphasis"), page);
        auto *slopeForm = new QFormLayout(slopeBox);
        auto *useSlope = new QCheckBox(tr("Widen steep edges"), slopeBox);
        useSlope->setChecked(st->useSlopeDrivenWidth());
        slopeForm->addRow(QString(), useSlope);
        auto *slopeBreak = makeDSpin(slopeBox, 0.0, 1.0, 0.05, 2, st->slopeBreak());
        slopeBreak->setToolTip(tr("Fraction of the maximum slope above which "
                                  "edges use the wide width/colour."));
        slopeForm->addRow(tr("Slope brea&k:"), slopeBreak);
        auto *wideWidth = makeDSpin(slopeBox, 0.1, 10.0, 0.05, 2,
                                    st->wideWidthPx(), tr(" px"));
        slopeForm->addRow(tr("Wide width:"), wideWidth);
        auto *wideColor = new ColorButton(slopeBox);
        wideColor->setShowAlpha(true);
        wideColor->setColor(st->wideColor());
        slopeForm->addRow(tr("Wide colour:"), wideColor);
        lay->addWidget(slopeBox);

        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setLineWidthPx(v); });
        connect(dash, &DashStyleCombo::penStyleChanged, this,
                [st](Qt::PenStyle s) { st->setDashPattern(s); });
        connect(edgeZoom, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [L](double v) { L->setEdgeZoomMinCellPx(v); });
        connect(useSlope, &QCheckBox::toggled, this,
                [st](bool v) { st->setUseSlopeDrivenWidth(v); });
        connect(slopeBreak, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setSlopeBreak(v); });
        connect(wideWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setWideWidthPx(v); });
        connect(wideColor, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setWideColor(c); });
    }
    lay->addStretch();
    return page;
}

// ─── Mesh vertices ──────────────────────────────────────────────────────────

QWidget *Swmm2DMeshStylePanel::buildMeshNodeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshNodeSublayer();
    MeshNodeStyle *st = sub ? sub->nodeStyle() : nullptr;
    auto *L = m_layer.data();

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show mesh vertices")));

    if (st) {
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

        auto *vertexZoom = makeDSpin(symBox, 0.0, 40.0, 0.5, 1,
                                     L->vertexZoomMinCellPx(), tr(" px/cell"));
        vertexZoom->setToolTip(tr(
            "Show vertex dots once mesh cells project at least this many "
            "pixels across. 0 = always show when enabled."));
        symForm->addRow(tr("Sh&ow vertices at:"), vertexZoom);
        lay->addWidget(symBox);

        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(size, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setMarkerSizePx(v); });
        connect(outlineColor, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setOutlineColor(c); });
        connect(outlineWidth, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setOutlineWidthPx(v); });
        connect(vertexZoom, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [L](double v) { L->setVertexZoomMinCellPx(v); });
    }
    lay->addStretch();
    return page;
}

// ─── Boundary conditions ────────────────────────────────────────────────────

QWidget *Swmm2DMeshStylePanel::buildBcTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->meshBcSublayer();
    MeshBcStyle *st = sub ? sub->bcStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub,
                                      tr("Show boundary-condition indicators")));

    if (st && sub) {
        auto *grid = new QGroupBox(tr("Per-type styling"), page);
        auto *gl   = new QGridLayout(grid);
        gl->addWidget(new QLabel(tr("Type"), grid),   0, 0);
        gl->addWidget(new QLabel(tr("Colour"), grid), 0, 1);
        gl->addWidget(new QLabel(tr("Width"), grid),  0, 2);

        const QSet<int> present = sub->bcTypesPresent();
        for (int t = 0; t < MeshBcStyle::kBcTypeCount; ++t) {
            const int row = t + 1;

            QString label = mesh::MeshBCTypes::label(mesh::MeshBCTypes::Type(t));
            if (t == 0)
                label = tr("%1 / interior edges").arg(label);
            auto *vis = new QCheckBox(label, grid);
            vis->setChecked(st->bcTypeVisible(t));
            if (t == 0)
                vis->setToolTip(tr("Recolours the interior wireframe with the "
                                   "Wall colour while this sublayer is shown."));
            // Types not present in the mesh stay editable but are greyed so
            // the user can see which rows currently draw nothing.
            if (!present.contains(t))
                vis->setText(vis->text() + tr(" (none in mesh)"));
            gl->addWidget(vis, row, 0);

            auto *color = new ColorButton(grid);
            color->setShowAlpha(true);
            color->setColor(st->bcColorForType(t));
            gl->addWidget(color, row, 1);

            QDoubleSpinBox *width = nullptr;
            if (t > 0) {
                width = makeDSpin(grid, 0.1, 20.0, 0.2, 2,
                                  st->bcWidthForType(t), tr(" px"));
                gl->addWidget(width, row, 2);
            } else {
                // Wall edges ARE the wireframe — width comes from Mesh Edges.
                gl->addWidget(new QLabel(tr("(wireframe width)"), grid), row, 2);
            }

            // setCustomized: a direct panel edit locks this layer's BC style
            // against live re-seeding from the Preferences defaults.
            connect(vis, &QCheckBox::toggled, this,
                    [st, t](bool on) {
                        st->setCustomized(true);
                        st->setBcTypeVisible(t, on);
                    });
            connect(color, &ColorButton::colorChanged, this,
                    [st, t](const QColor &c) {
                        st->setCustomized(true);
                        st->setBcColor(t, c);
                    });
            if (width)
                connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged),
                        this, [st, t](double v) {
                            st->setCustomized(true);
                            st->setBcWidth(t, v);
                        });
        }
        gl->setColumnStretch(0, 1);
        lay->addWidget(grid);

        auto *note = new QLabel(
            tr("The boundary ring stays visible at every zoom level; the "
               "interior wireframe follows the Mesh Edges zoom threshold."),
            page);
        note->setWordWrap(true);
        lay->addWidget(note);
    }
    lay->addStretch();
    return page;
}

// ─── Coupled nodes ──────────────────────────────────────────────────────────

QWidget *Swmm2DMeshStylePanel::buildCoupledNodeTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay  = new QVBoxLayout(page);

    auto *sub = m_layer->coupledNodeSublayer();
    CoupledNodeStyle *st = sub ? sub->coupledStyle() : nullptr;

    lay->addWidget(makeSublayerHeader(page, sub, tr("Show SWMM-coupled vertices")));

    if (st) {
        auto *symBox  = new QGroupBox(tr("Symbology"), page);
        auto *symForm = new QFormLayout(symBox);

        auto *color = new ColorButton(symBox);
        color->setShowAlpha(true);
        color->setColor(st->color());
        symForm->addRow(tr("Colour:"), color);

        auto *size = makeDSpin(symBox, 0.5, 40.0, 0.5, 1,
                               st->markerSizePx(), tr(" px"));
        symForm->addRow(tr("Marker size:"), size);
        lay->addWidget(symBox);

        auto *note = new QLabel(
            tr("Marks 2D mesh vertices coupled to 1D SWMM nodes "
               "([2D_VERTEX_NODE_MAP]). Markers appear at the Mesh Vertices "
               "zoom threshold."),
            page);
        note->setWordWrap(true);
        lay->addWidget(note);

        connect(color, &ColorButton::colorChanged, this,
                [st](const QColor &c) { st->setColor(c); });
        connect(size, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [st](double v) { st->setMarkerSizePx(v); });
    }
    lay->addStretch();
    return page;
}

} // namespace openswmmvis::ui
