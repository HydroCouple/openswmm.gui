/*!
 * \file   symbolstyleeditors.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SE.1–SE.3 — dedicated editors for the *SymbolStyleAdapter family.
 */
#include "ui/dialogs/editors/symbolstyleeditors.h"

#include "render/markershape.h"
#include "render/symbolstyleadapter.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/dashstylecombo.h"
#include "ui/widgets/markershapecombo.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using OpenSWMM::Render::LineSymbolStyleAdapter;
using OpenSWMM::Render::MarkerShape;
using OpenSWMM::Render::PointSymbolStyleAdapter;
using OpenSWMM::Render::PolygonSymbolStyleAdapter;

namespace {
// Shared opacity spin (0–100 %), maps to the adapter's 0..1 opacity.
QDoubleSpinBox *makeOpacitySpin(QWidget *parent)
{
    auto *s = new QDoubleSpinBox(parent);
    s->setRange(0.0, 100.0);
    s->setDecimals(0);
    s->setSingleStep(5.0);
    s->setSuffix(QStringLiteral(" %"));
    return s;
}
} // namespace

// ===========================================================================
// SE.1 — Point
// ===========================================================================
PointSymbolStyleEditor::PointSymbolStyleEditor(PointSymbolStyleAdapter *adapter,
                                               QWidget *parent)
    : IStyleEditorWidget(parent), m_a(adapter)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *box  = new QGroupBox(tr("Marker"), this);
    auto *form = new QFormLayout(box);

    m_shape = new MarkerShapeCombo(box);
    m_shape->populateCanonical();     // all 19 canonical MarkerShapes
    form->addRow(tr("&Shape:"), m_shape);

    m_size = new QDoubleSpinBox(box);
    m_size->setRange(0.5, 60.0);
    m_size->setDecimals(1);
    m_size->setSingleStep(0.5);
    m_size->setSuffix(tr(" px"));
    form->addRow(tr("S&ize:"), m_size);

    m_fill = new ColorButton(box);
    m_fill->setShowAlpha(true);
    form->addRow(tr("&Fill:"), m_fill);

    m_outline = new ColorButton(box);
    m_outline->setShowAlpha(true);
    form->addRow(tr("&Outline:"), m_outline);

    m_outlineW = new QDoubleSpinBox(box);
    m_outlineW->setRange(0.0, 20.0);
    m_outlineW->setDecimals(2);
    m_outlineW->setSingleStep(0.25);
    m_outlineW->setSuffix(tr(" px"));
    form->addRow(tr("O&utline width:"), m_outlineW);

    m_opacity = makeOpacitySpin(box);
    form->addRow(tr("Op&acity:"), m_opacity);

    root->addWidget(box);
    root->addStretch();

    connect(m_shape, &MarkerShapeCombo::shapeValueChanged, this,
            [this](int v) { if (m_a) m_a->setMarkerShape(static_cast<MarkerShape>(v)); });
    connect(m_size, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { if (m_a) m_a->setMarkerSize(v); });
    connect(m_fill, &ColorButton::colorChanged, this,
            [this](const QColor &c) { if (m_a) m_a->setFillColor(c); });
    connect(m_outline, &ColorButton::colorChanged, this,
            [this](const QColor &c) { if (m_a) m_a->setOutlineColor(c); });
    connect(m_outlineW, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { if (m_a) m_a->setOutlineWidth(v); });
    connect(m_opacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });

    if (m_a)
        connect(m_a, &PointSymbolStyleAdapter::changed,
                this, &PointSymbolStyleEditor::refreshFromModel,
                Qt::UniqueConnection);
    refreshFromModel();
}

void PointSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b1(m_shape), b2(m_size), b3(m_fill),
                   b4(m_outline), b5(m_outlineW), b6(m_opacity);
    m_shape->setShapeValue(static_cast<int>(m_a->markerShape()));
    m_size->setValue(m_a->markerSize());
    m_fill->setColor(m_a->fillColor());
    m_outline->setColor(m_a->outlineColor());
    m_outlineW->setValue(m_a->outlineWidth());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// SE.2 — Line
// ===========================================================================
LineSymbolStyleEditor::LineSymbolStyleEditor(LineSymbolStyleAdapter *adapter,
                                             QWidget *parent)
    : IStyleEditorWidget(parent), m_a(adapter)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *box  = new QGroupBox(tr("Line"), this);
    auto *form = new QFormLayout(box);

    m_color = new ColorButton(box);
    m_color->setShowAlpha(true);
    form->addRow(tr("Colou&r:"), m_color);

    m_width = new QDoubleSpinBox(box);
    m_width->setRange(0.25, 30.0);
    m_width->setDecimals(2);
    m_width->setSingleStep(0.25);
    m_width->setSuffix(tr(" px"));
    form->addRow(tr("&Width:"), m_width);

    m_dash = new DashStyleCombo(box);
    form->addRow(tr("S&tyle:"), m_dash);

    m_opacity = makeOpacitySpin(box);
    form->addRow(tr("Opacit&y:"), m_opacity);

    root->addWidget(box);
    root->addStretch();

    connect(m_color, &ColorButton::colorChanged, this,
            [this](const QColor &c) { if (m_a) m_a->setLineColor(c); });
    connect(m_width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { if (m_a) m_a->setLineWidth(v); });
    connect(m_dash, &DashStyleCombo::penStyleChanged, this,
            [this](Qt::PenStyle s) { if (m_a) m_a->setDashPattern(s); });
    connect(m_opacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });

    if (m_a)
        connect(m_a, &LineSymbolStyleAdapter::changed,
                this, &LineSymbolStyleEditor::refreshFromModel,
                Qt::UniqueConnection);
    refreshFromModel();
}

void LineSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b1(m_color), b2(m_width), b3(m_dash), b4(m_opacity);
    m_color->setColor(m_a->lineColor());
    m_width->setValue(m_a->lineWidth());
    m_dash->setPenStyle(m_a->dashPattern());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// SE.3 — Polygon
// ===========================================================================
PolygonSymbolStyleEditor::PolygonSymbolStyleEditor(PolygonSymbolStyleAdapter *adapter,
                                                   QWidget *parent)
    : IStyleEditorWidget(parent), m_a(adapter)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *box  = new QGroupBox(tr("Fill"), this);
    auto *form = new QFormLayout(box);

    m_fill = new ColorButton(box);
    m_fill->setShowAlpha(true);
    form->addRow(tr("Fill:"), m_fill);

    m_outline = new ColorButton(box);
    m_outline->setShowAlpha(true);
    form->addRow(tr("Outli&ne:"), m_outline);

    m_outlineW = new QDoubleSpinBox(box);
    m_outlineW->setRange(0.0, 20.0);
    m_outlineW->setDecimals(2);
    m_outlineW->setSingleStep(0.25);
    m_outlineW->setSuffix(tr(" px"));
    form->addRow(tr("Outlin&e width:"), m_outlineW);

    m_opacity = makeOpacitySpin(box);
    form->addRow(tr("Opacity:"), m_opacity);

    root->addWidget(box);
    root->addStretch();

    connect(m_fill, &ColorButton::colorChanged, this,
            [this](const QColor &c) { if (m_a) m_a->setFillColor(c); });
    connect(m_outline, &ColorButton::colorChanged, this,
            [this](const QColor &c) { if (m_a) m_a->setOutlineColor(c); });
    connect(m_outlineW, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { if (m_a) m_a->setOutlineWidth(v); });
    connect(m_opacity, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { if (m_a) m_a->setOpacity(v / 100.0); });

    if (m_a)
        connect(m_a, &PolygonSymbolStyleAdapter::changed,
                this, &PolygonSymbolStyleEditor::refreshFromModel,
                Qt::UniqueConnection);
    refreshFromModel();
}

void PolygonSymbolStyleEditor::refreshFromModel()
{
    if (!m_a) return;
    QSignalBlocker b1(m_fill), b2(m_outline), b3(m_outlineW), b4(m_opacity);
    m_fill->setColor(m_a->fillColor());
    m_outline->setColor(m_a->outlineColor());
    m_outlineW->setValue(m_a->outlineWidth());
    m_opacity->setValue(m_a->opacity() * 100.0);
}

// ===========================================================================
// Registry hookup. Keys are unqualified tails; createEditorFor (SE.1) matches
// them against the namespaced metaObject className.
// ===========================================================================
REGISTER_STYLE_EDITOR(
    PointSymbolStyleAdapter,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *a = qobject_cast<PointSymbolStyleAdapter *>(obj))
            return new PointSymbolStyleEditor(a, parent);
        return nullptr;
    })

REGISTER_STYLE_EDITOR(
    LineSymbolStyleAdapter,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *a = qobject_cast<LineSymbolStyleAdapter *>(obj))
            return new LineSymbolStyleEditor(a, parent);
        return nullptr;
    })

REGISTER_STYLE_EDITOR(
    PolygonSymbolStyleAdapter,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *a = qobject_cast<PolygonSymbolStyleAdapter *>(obj))
            return new PolygonSymbolStyleEditor(a, parent);
        return nullptr;
    })

} // namespace openswmmvis::ui
