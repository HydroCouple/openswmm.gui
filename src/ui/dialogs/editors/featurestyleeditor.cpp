/*!
 * \file   featurestyleeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/featurestyleeditor.h"
#include "ui/theme/themehelpers.h"

#include "layers/openswmmvislayer.h"
#include "render/iattributeprovider.h"   // L-1 — available label fields
#include "render/sublayers/feature/featuresublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"
#include "ui/dialogs/editors/kindrendererpanel.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/labelconfigeditor.h"
#include "ui/widgets/dashstylecombo.h"
#include "ui/widgets/markershapecombo.h"
#include "ui/widgets/stylepreviewswatch.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPen>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using OpenSWMM::Render::FeatureSublayerStyle;
using OpenSWMM::Render::PointFeatureSublayerStyle;
using OpenSWMM::Render::LineFeatureSublayerStyle;
using OpenSWMM::Render::PolygonFeatureSublayerStyle;

// ---------------------------------------------------------------------------
// Base — common rows + preview
// ---------------------------------------------------------------------------

FeatureStyleEditorBase::FeatureStyleEditorBase(FeatureSublayerStyle *style, QWidget *parent)
    : IStyleEditorWidget(parent), m_style(style)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *classifyBox = new QGroupBox(tr("Classification"), this);
    m_form = new QFormLayout(classifyBox);
    root->addWidget(classifyBox);

    // Attribute picker — a combo populated from the host layer's
    // IAttributeProvider (numeric fields) instead of the old free-text
    // line edit. Kept editable so unknown/legacy attribute names survive.
    m_attributeCombo = new QComboBox(this);
    m_attributeCombo->setEditable(true);
    m_attributeCombo->setInsertPolicy(QComboBox::NoInsert);
    if (auto *sub = m_style
            ? qobject_cast<OpenSWMM::Render::FeatureSublayer *>(m_style->parent())
            : nullptr) {
        if (auto *host = qobject_cast<OpenSWMMVisLayer *>(sub->parent())) {
            if (auto *prov = dynamic_cast<OpenSWMM::Render::IAttributeProvider *>(host)) {
                for (const auto &f : prov->availableAttributes(sub->category())) {
                    if (f.type == QMetaType::QString)
                        continue;   // colour ramp / sizing need numerics
                    m_attributeCombo->addItem(f.displayName, f.name);
                }
            }
        }
    }
    m_form->addRow(tr("&Attribute:"), m_attributeCombo);

    m_singleColorBtn = new ColorButton(this);
    m_singleColorBtn->setMinimumWidth(100);
    m_form->addRow(tr("&Single colour:"), m_singleColorBtn);

    m_useRampBox = new QCheckBox(tr("Use colour ramp (per-attribute)"), this);
    m_form->addRow(QString(), m_useRampBox);

    // Subscribe to the style bag's NOTIFY signal so external mutations
    // (Cancel rollback, .oswp load) refresh the UI.
    connect(m_style, &FeatureSublayerStyle::styleChanged,
            this, &FeatureStyleEditorBase::refreshFromModel,
            Qt::UniqueConnection);
}

void FeatureStyleEditorBase::buildCommonRows()
{
    // Bindings — pushed via setters which fire styleChanged.
    connect(m_attributeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int row) {
        m_style->setAttribute(m_attributeCombo->itemData(row).isValid()
                                  ? m_attributeCombo->itemData(row).toString()
                                  : m_attributeCombo->itemText(row));
    });
    connect(m_attributeCombo->lineEdit(), &QLineEdit::editingFinished,
            this, [this]() {
        // Free-typed attribute name (editable combo, no matching entry).
        const int row = m_attributeCombo->findText(m_attributeCombo->currentText());
        m_style->setAttribute(row >= 0 && m_attributeCombo->itemData(row).isValid()
                                  ? m_attributeCombo->itemData(row).toString()
                                  : m_attributeCombo->currentText());
    });
    connect(m_singleColorBtn, &ColorButton::colorChanged, this, [this](const QColor &c) {
        m_style->setColor(c);
    });
    connect(m_useRampBox, &QCheckBox::toggled, this, [this](bool v) {
        m_style->setUseColorRamp(v);
    });

    // ── L-1 — per-sublayer Labels group ─────────────────────────────────
    // LAYER_STYLING_LABELING_PLAN_2026-08-16 — full-fidelity LabelConfig
    // editor (font / halo / placement / scale window / background /
    // priority) instead of the old enabled/expression/colour subset.
    auto *labelBox = new QGroupBox(tr("Labels"), this);
    auto *labelLay = new QVBoxLayout(labelBox);

    m_labelEditor = new LabelConfigEditor(labelBox);
    if (m_style)
        m_labelEditor->setConfig(m_style->labelConfig());

    // Populate the available-field hint from the host layer's attribute
    // provider for this sublayer's category (plus the always-available name).
    if (auto *sub = m_style
            ? qobject_cast<OpenSWMM::Render::FeatureSublayer *>(m_style->parent())
            : nullptr) {
        if (auto *host = qobject_cast<OpenSWMMVisLayer *>(sub->parent())) {
            if (auto *prov = dynamic_cast<OpenSWMM::Render::IAttributeProvider *>(host))
                m_labelEditor->setAvailableFields(
                    prov->availableAttributes(sub->category()));
        }
    }

    labelLay->addWidget(m_labelEditor);
    layout()->addWidget(labelBox);

    connect(m_labelEditor, &LabelConfigEditor::configChanged, this,
            [this](const OpenSWMM::Render::LabelConfig &cfg) {
                if (m_style) m_style->setLabelConfig(cfg);
            });
}

void FeatureStyleEditorBase::addPreviewRow()
{
    auto *box = new QGroupBox(tr("Preview"), this);
    auto *lay = new QVBoxLayout(box);
    m_preview = new StylePreviewSwatch(box);
    lay->addWidget(m_preview);
    layout()->addWidget(box);

    // Slice S2+S5 — add the classified-rendering panel. Walk style→parent
    // to find the owning FeatureSublayer, then up to the host layer.
    if (auto *sub = qobject_cast<OpenSWMM::Render::FeatureSublayer *>(m_style->parent())) {
        auto *host = qobject_cast<OpenSWMMVisLayer *>(sub->parent());
        if (host) {
            auto *panel = new KindRendererPanel(host, sub->category(), this);
            layout()->addWidget(panel);
        }
    }
}

void FeatureStyleEditorBase::refreshFromModel()
{
    if (!m_style) return;
    QSignalBlocker b1(m_attributeCombo), b2(m_singleColorBtn), b3(m_useRampBox);
    const QString attr = m_style->attribute();
    int row = m_attributeCombo->findData(attr);
    if (row < 0)
        row = m_attributeCombo->findText(attr);
    if (row >= 0)
        m_attributeCombo->setCurrentIndex(row);
    else
        m_attributeCombo->setEditText(attr);
    m_singleColorBtn->setColor(m_style->color());
    m_useRampBox->setChecked(m_style->useColorRamp());

    // L-1 — label controls (created lazily in buildCommonRows, so guard).
    // setConfig re-seeds without emitting configChanged.
    if (m_labelEditor)
        m_labelEditor->setConfig(m_style->labelConfig());
    updatePreview();
}

// ---------------------------------------------------------------------------
// Point archetype
// ---------------------------------------------------------------------------

PointFeatureStyleEditor::PointFeatureStyleEditor(PointFeatureSublayerStyle *style,
                                                  QWidget *parent)
    : FeatureStyleEditorBase(style, parent), m_pointStyle(style)
{
    auto *symbolBox = new QGroupBox(tr("Marker"), this);
    auto *form = new QFormLayout(symbolBox);

    m_sizeSpin = new QDoubleSpinBox(this);
    m_sizeSpin->setRange(0.5, 60.0);
    m_sizeSpin->setDecimals(1);
    m_sizeSpin->setSingleStep(0.5);
    m_sizeSpin->setSuffix(tr(" px"));
    form->addRow(tr("Si&ze:"), m_sizeSpin);

    m_shapeCombo = new MarkerShapeCombo(this);
    m_shapeCombo->populateDefault(false);  // No Cross — matches the 5-shape enum.
    form->addRow(tr("S&hape:"), m_shapeCombo);

    layout()->addWidget(symbolBox);
    buildCommonRows();
    addPreviewRow();

    if (m_preview) m_preview->setKind(StylePreviewSwatch::PointKind);

    connect(m_sizeSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_pointStyle->setMarkerSizePx(v); });
    connect(m_shapeCombo, &MarkerShapeCombo::shapeValueChanged,
            this, [this](int v) {
                m_pointStyle->setShape(static_cast<PointFeatureSublayerStyle::MarkerShape>(v));
            });

    refreshFromModel();
}

void PointFeatureStyleEditor::refreshFromModel()
{
    FeatureStyleEditorBase::refreshFromModel();
    if (!m_pointStyle) return;
    QSignalBlocker bs(m_sizeSpin), bc(m_shapeCombo);
    m_sizeSpin->setValue(m_pointStyle->markerSizePx());
    m_shapeCombo->setShapeValue(int(m_pointStyle->shape()));
    updatePreview();
}

void PointFeatureStyleEditor::updatePreview()
{
    if (!m_preview || !m_pointStyle) return;
    m_preview->setColor(m_pointStyle->color());
    m_preview->setMarkerSizePx(m_pointStyle->markerSizePx());
    m_preview->setMarkerShape(int(m_pointStyle->shape()));
}

// ---------------------------------------------------------------------------
// Line archetype
// ---------------------------------------------------------------------------

LineFeatureStyleEditor::LineFeatureStyleEditor(LineFeatureSublayerStyle *style,
                                                QWidget *parent)
    : FeatureStyleEditorBase(style, parent), m_lineStyle(style)
{
    auto *lineBox = new QGroupBox(tr("Line"), this);
    auto *lineForm = new QFormLayout(lineBox);

    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(0.25, 30.0);
    m_widthSpin->setDecimals(2);
    m_widthSpin->setSingleStep(0.25);
    m_widthSpin->setSuffix(tr(" px"));
    lineForm->addRow(tr("&Width:"), m_widthSpin);

    m_dashCombo = new DashStyleCombo(this);
    lineForm->addRow(tr("S&tyle:"), m_dashCombo);

    m_renderAsLineBox = new QCheckBox(tr("Render as polyline (else midpoint glyph)"), this);
    lineForm->addRow(QString(), m_renderAsLineBox);
    layout()->addWidget(lineBox);

    auto *arrowBox = new QGroupBox(tr("Flow arrows"), this);
    auto *arrowForm = new QFormLayout(arrowBox);

    m_showArrowsBox = new QCheckBox(tr("Show flow direction arrows"), this);
    arrowForm->addRow(QString(), m_showArrowsBox);

    m_arrowLenSpin = new QDoubleSpinBox(this);
    m_arrowLenSpin->setRange(2.0, 80.0);
    m_arrowLenSpin->setDecimals(1);
    m_arrowLenSpin->setSingleStep(1.0);
    m_arrowLenSpin->setSuffix(tr(" px"));
    arrowForm->addRow(tr("Le&ngth:"), m_arrowLenSpin);

    m_arrowWidSpin = new QDoubleSpinBox(this);
    m_arrowWidSpin->setRange(1.0, 40.0);
    m_arrowWidSpin->setDecimals(1);
    m_arrowWidSpin->setSingleStep(1.0);
    m_arrowWidSpin->setSuffix(tr(" px"));
    arrowForm->addRow(tr("Half-width:"), m_arrowWidSpin);

    m_arrowColorBtn = new ColorButton(this);
    arrowForm->addRow(tr("Colo&ur:"), m_arrowColorBtn);

    layout()->addWidget(arrowBox);
    buildCommonRows();
    addPreviewRow();

    if (m_preview) m_preview->setKind(StylePreviewSwatch::LineKind);

    connect(m_widthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_lineStyle->setLineWidthPx(v); });
    connect(m_dashCombo, &DashStyleCombo::penStyleChanged,
            this, [this](Qt::PenStyle s) { m_lineStyle->setDashPattern(s); });
    connect(m_renderAsLineBox, &QCheckBox::toggled,
            this, [this](bool v) { m_lineStyle->setRenderAsLine(v); });
    connect(m_showArrowsBox, &QCheckBox::toggled,
            this, [this](bool v) { m_lineStyle->setShowFlowArrows(v); });
    connect(m_arrowLenSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_lineStyle->setArrowLengthPx(v); });
    connect(m_arrowWidSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_lineStyle->setArrowWidthPx(v); });
    connect(m_arrowColorBtn, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_lineStyle->setArrowColor(c); });

    refreshFromModel();
}

void LineFeatureStyleEditor::refreshFromModel()
{
    FeatureStyleEditorBase::refreshFromModel();
    if (!m_lineStyle) return;
    QSignalBlocker bw(m_widthSpin), bd(m_dashCombo), bra(m_renderAsLineBox),
                   bsa(m_showArrowsBox), bal(m_arrowLenSpin), baw(m_arrowWidSpin),
                   bac(m_arrowColorBtn);
    m_widthSpin->setValue(m_lineStyle->lineWidthPx());
    m_dashCombo->setPenStyle(m_lineStyle->dashPattern());
    m_renderAsLineBox->setChecked(m_lineStyle->renderAsLine());
    m_showArrowsBox->setChecked(m_lineStyle->showFlowArrows());
    m_arrowLenSpin->setValue(m_lineStyle->arrowLengthPx());
    m_arrowWidSpin->setValue(m_lineStyle->arrowWidthPx());
    m_arrowColorBtn->setColor(m_lineStyle->arrowColor());
    updatePreview();
}

void LineFeatureStyleEditor::updatePreview()
{
    if (!m_preview || !m_lineStyle) return;
    m_preview->setColor(m_lineStyle->color());
    QPen pen;
    pen.setStyle(m_lineStyle->dashPattern());
    m_preview->setStrokePen(pen);
    m_preview->setLineWidthPx(m_lineStyle->lineWidthPx());
    m_preview->setShowArrows(m_lineStyle->showFlowArrows());
}

// ---------------------------------------------------------------------------
// Polygon archetype
// ---------------------------------------------------------------------------

PolygonFeatureStyleEditor::PolygonFeatureStyleEditor(PolygonFeatureSublayerStyle *style,
                                                      QWidget *parent)
    : FeatureStyleEditorBase(style, parent), m_polyStyle(style)
{
    auto *polyBox = new QGroupBox(tr("Polygon"), this);
    auto *form = new QFormLayout(polyBox);

    m_outlineColorBtn = new ColorButton(this);
    form->addRow(tr("Outline colou&r:"), m_outlineColorBtn);

    m_outlineWidthSpin = new QDoubleSpinBox(this);
    m_outlineWidthSpin->setRange(0.0, 20.0);
    m_outlineWidthSpin->setDecimals(2);
    m_outlineWidthSpin->setSingleStep(0.25);
    m_outlineWidthSpin->setSuffix(tr(" px"));
    form->addRow(tr("Outline width:"), m_outlineWidthSpin);

    m_fillOpacitySpin = new QDoubleSpinBox(this);
    m_fillOpacitySpin->setRange(0.0, 1.0);
    m_fillOpacitySpin->setDecimals(2);
    m_fillOpacitySpin->setSingleStep(0.05);
    form->addRow(tr("Fill o&pacity:"), m_fillOpacitySpin);

    layout()->addWidget(polyBox);
    buildCommonRows();
    addPreviewRow();

    if (m_preview) m_preview->setKind(StylePreviewSwatch::PolygonKind);

    connect(m_outlineColorBtn, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_polyStyle->setOutlineColor(c); });
    connect(m_outlineWidthSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_polyStyle->setOutlineWidthPx(v); });
    connect(m_fillOpacitySpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_polyStyle->setFillOpacity(v); });

    refreshFromModel();
}

void PolygonFeatureStyleEditor::refreshFromModel()
{
    FeatureStyleEditorBase::refreshFromModel();
    if (!m_polyStyle) return;
    QSignalBlocker boc(m_outlineColorBtn), bow(m_outlineWidthSpin),
                   bfo(m_fillOpacitySpin);
    m_outlineColorBtn->setColor(m_polyStyle->outlineColor());
    m_outlineWidthSpin->setValue(m_polyStyle->outlineWidthPx());
    m_fillOpacitySpin->setValue(m_polyStyle->fillOpacity());
    updatePreview();
}

void PolygonFeatureStyleEditor::updatePreview()
{
    if (!m_preview || !m_polyStyle) return;
    m_preview->setColor(m_polyStyle->color());
    QPen outline(m_polyStyle->outlineColor());
    outline.setWidthF(m_polyStyle->outlineWidthPx());
    m_preview->setStrokePen(outline);
    m_preview->setFillOpacity(m_polyStyle->fillOpacity());
}

// ---------------------------------------------------------------------------
// Registry hookup — static at the bottom of this TU.
// ---------------------------------------------------------------------------

REGISTER_STYLE_EDITOR(
    PointFeatureSublayerStyle,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *s = qobject_cast<PointFeatureSublayerStyle *>(obj))
            return new PointFeatureStyleEditor(s, parent);
        return nullptr;
    })

REGISTER_STYLE_EDITOR(
    LineFeatureSublayerStyle,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *s = qobject_cast<LineFeatureSublayerStyle *>(obj))
            return new LineFeatureStyleEditor(s, parent);
        return nullptr;
    })

REGISTER_STYLE_EDITOR(
    PolygonFeatureSublayerStyle,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *s = qobject_cast<PolygonFeatureSublayerStyle *>(obj))
            return new PolygonFeatureStyleEditor(s, parent);
        return nullptr;
    })

} // namespace openswmmvis::ui
