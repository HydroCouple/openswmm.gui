/*!
 * \file   gisvectorsymboleditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/editors/gisvectorsymboleditor.h"

#include "layers/gisvectorsymboladapter.h"
#include "ui/widgets/colorbutton.h"
#include "ui/widgets/dashstylecombo.h"
#include "ui/widgets/markershapecombo.h"
#include "ui/widgets/stylepreviewswatch.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

GisVectorSymbolEditor::GisVectorSymbolEditor(GisVectorSymbolAdapter *adapter, QWidget *parent)
    : IStyleEditorWidget(parent), m_adapter(adapter)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto *tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    // ── Marker tab ─────────────────────────────────────────────────────
    {
        auto *page = new QWidget(tabs);
        auto *vlay = new QVBoxLayout(page);
        auto *form = new QFormLayout;

        m_shapeCombo = new MarkerShapeCombo(page);
        m_shapeCombo->populateCanonical();   // G-1 — full canonical 19-shape set
        form->addRow(tr("&Shape:"), m_shapeCombo);

        m_markerSize = new QDoubleSpinBox(page);
        m_markerSize->setRange(0.5, 60.0);
        m_markerSize->setSuffix(tr(" px"));
        m_markerSize->setSingleStep(0.5);
        form->addRow(tr("S&ize:"), m_markerSize);

        m_markerFill = new ColorButton(page);
        m_markerFill->setShowAlpha(true);   // allow per-colour transparency
        form->addRow(tr("Fill:"), m_markerFill);

        m_markerStroke = new ColorButton(page);
        form->addRow(tr("S&troke:"), m_markerStroke);

        m_markerStrokeW = new QDoubleSpinBox(page);
        m_markerStrokeW->setRange(0.0, 20.0);
        m_markerStrokeW->setDecimals(2);
        m_markerStrokeW->setSuffix(tr(" px"));
        m_markerStrokeW->setSingleStep(0.25);
        form->addRow(tr("St&roke width:"), m_markerStrokeW);
        vlay->addLayout(form);

        auto *previewBox = new QGroupBox(tr("Preview"), page);
        auto *previewLay = new QVBoxLayout(previewBox);
        m_pointPreview = new StylePreviewSwatch(previewBox);
        m_pointPreview->setKind(StylePreviewSwatch::PointKind);
        previewLay->addWidget(m_pointPreview);
        vlay->addWidget(previewBox);
        vlay->addStretch();
        tabs->addTab(page, tr("M&arker (points)"));
    }

    // ── Line tab ───────────────────────────────────────────────────────
    {
        auto *page = new QWidget(tabs);
        auto *vlay = new QVBoxLayout(page);
        auto *form = new QFormLayout;

        m_lineColor = new ColorButton(page);
        form->addRow(tr("C&olour:"), m_lineColor);

        m_lineWidth = new QDoubleSpinBox(page);
        m_lineWidth->setRange(0.25, 30.0);
        m_lineWidth->setDecimals(2);
        m_lineWidth->setSingleStep(0.25);
        m_lineWidth->setSuffix(tr(" px"));
        form->addRow(tr("&Width:"), m_lineWidth);

        m_lineDash = new DashStyleCombo(page);
        form->addRow(tr("St&yle:"), m_lineDash);
        vlay->addLayout(form);

        auto *previewBox = new QGroupBox(tr("Preview"), page);
        auto *previewLay = new QVBoxLayout(previewBox);
        m_linePreview = new StylePreviewSwatch(previewBox);
        m_linePreview->setKind(StylePreviewSwatch::LineKind);
        previewLay->addWidget(m_linePreview);
        vlay->addWidget(previewBox);
        vlay->addStretch();
        tabs->addTab(page, tr("&Line"));
    }

    // ── Polygon tab ────────────────────────────────────────────────────
    {
        auto *page = new QWidget(tabs);
        auto *vlay = new QVBoxLayout(page);
        auto *form = new QFormLayout;

        m_polyFill = new ColorButton(page);
        m_polyFill->setShowAlpha(true);   // polygon fill transparency
        form->addRow(tr("Fill colo&ur:"), m_polyFill);

        m_polyOutline = new ColorButton(page);
        form->addRow(tr("Outli&ne colour:"), m_polyOutline);

        m_polyOutlineW = new QDoubleSpinBox(page);
        m_polyOutlineW->setRange(0.0, 20.0);
        m_polyOutlineW->setDecimals(2);
        m_polyOutlineW->setSuffix(tr(" px"));
        m_polyOutlineW->setSingleStep(0.25);
        form->addRow(tr("Outlin&e width:"), m_polyOutlineW);
        vlay->addLayout(form);

        auto *previewBox = new QGroupBox(tr("Preview"), page);
        auto *previewLay = new QVBoxLayout(previewBox);
        m_polygonPreview = new StylePreviewSwatch(previewBox);
        m_polygonPreview->setKind(StylePreviewSwatch::PolygonKind);
        previewLay->addWidget(m_polygonPreview);
        vlay->addWidget(previewBox);
        vlay->addStretch();
        tabs->addTab(page, tr("&Polygon"));
    }

    // ── Labels tab ─────────────────────────────────────────────────────
    {
        auto *page = new QWidget(tabs);
        auto *vlay = new QVBoxLayout(page);
        auto *form = new QFormLayout;

        m_showLabels = new QCheckBox(tr("Show labels"), page);
        form->addRow(QString(), m_showLabels);

        m_labelField = new QLineEdit(page);
        m_labelField->setPlaceholderText(tr("OGR field name (e.g. NAME, ID)"));
        form->addRow(tr("Field:"), m_labelField);

        m_labelFont = new QFontComboBox(page);
        form->addRow(tr("Font:"), m_labelFont);

        m_labelColor = new ColorButton(page);
        form->addRow(tr("Colour:"), m_labelColor);
        vlay->addLayout(form);
        vlay->addStretch();
        tabs->addTab(page, tr("La&bels"));
    }

    // ── Bindings ───────────────────────────────────────────────────────
    connect(m_shapeCombo, &MarkerShapeCombo::shapeValueChanged,
            this, [this](int v) { m_adapter->setMarkerShape(static_cast<GisVectorSymbolAdapter::MarkerShape>(v)); });
    connect(m_markerSize, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setMarkerSize(v); });
    connect(m_markerFill, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setMarkerFill(c); });
    connect(m_markerStroke, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setMarkerOutline(c); });
    connect(m_markerStrokeW, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setMarkerOutlineW(v); });

    connect(m_lineColor, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setLineColor(c); });
    connect(m_lineWidth, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setLineWidth(v); });
    connect(m_lineDash, &DashStyleCombo::penStyleChanged,
            this, [this](Qt::PenStyle s) { m_adapter->setLineDash(s); });

    connect(m_polyFill, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setPolygonFill(c); });
    connect(m_polyOutline, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setPolygonOutlineColor(c); });
    connect(m_polyOutlineW, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { m_adapter->setPolygonOutlineWidth(v); });

    connect(m_showLabels, &QCheckBox::toggled,
            this, [this](bool v) { m_adapter->setShowLabels(v); });
    connect(m_labelField, &QLineEdit::editingFinished,
            this, [this]() { m_adapter->setLabelField(m_labelField->text()); });
    connect(m_labelFont, &QFontComboBox::currentFontChanged,
            this, [this](const QFont &f) { m_adapter->setLabelFont(f); });
    connect(m_labelColor, &ColorButton::colorChanged,
            this, [this](const QColor &c) { m_adapter->setLabelColor(c); });

    connect(m_adapter, &GisVectorSymbolAdapter::symbolChanged,
            this, &GisVectorSymbolEditor::refreshFromModel,
            Qt::UniqueConnection);

    refreshFromModel();
}

void GisVectorSymbolEditor::refreshFromModel()
{
    if (!m_adapter) return;
    QSignalBlocker bs[] = {
        QSignalBlocker(m_shapeCombo), QSignalBlocker(m_markerSize),
        QSignalBlocker(m_markerFill), QSignalBlocker(m_markerStroke),
        QSignalBlocker(m_markerStrokeW),
        QSignalBlocker(m_lineColor), QSignalBlocker(m_lineWidth),
        QSignalBlocker(m_lineDash),
        QSignalBlocker(m_polyFill), QSignalBlocker(m_polyOutline),
        QSignalBlocker(m_polyOutlineW),
        QSignalBlocker(m_showLabels), QSignalBlocker(m_labelField),
        QSignalBlocker(m_labelFont), QSignalBlocker(m_labelColor),
    };
    Q_UNUSED(bs);

    m_shapeCombo->setShapeValue(int(m_adapter->markerShape()));
    m_markerSize->setValue(m_adapter->markerSize());
    m_markerFill->setColor(m_adapter->markerFill());
    m_markerStroke->setColor(m_adapter->markerOutline());
    m_markerStrokeW->setValue(m_adapter->markerOutlineW());

    m_lineColor->setColor(m_adapter->lineColor());
    m_lineWidth->setValue(m_adapter->lineWidth());
    m_lineDash->setPenStyle(m_adapter->lineDash());

    m_polyFill->setColor(m_adapter->polygonFill());
    m_polyOutline->setColor(m_adapter->polygonOutlineColor());
    m_polyOutlineW->setValue(m_adapter->polygonOutlineWidth());

    m_showLabels->setChecked(m_adapter->showLabels());
    m_labelField->setText(m_adapter->labelField());
    m_labelFont->setCurrentFont(m_adapter->labelFont());
    m_labelColor->setColor(m_adapter->labelColor());

    updatePreview();
}

void GisVectorSymbolEditor::updatePreview()
{
    if (m_pointPreview) {
        m_pointPreview->setColor(m_adapter->markerFill());
        QPen stroke(m_adapter->markerOutline());
        stroke.setWidthF(m_adapter->markerOutlineW());
        m_pointPreview->setStrokePen(stroke);
        m_pointPreview->setMarkerSizePx(m_adapter->markerSize());
        m_pointPreview->setMarkerShape(int(m_adapter->markerShape()));
    }
    if (m_linePreview) {
        m_linePreview->setColor(m_adapter->lineColor());
        QPen pen;
        pen.setStyle(m_adapter->lineDash());
        m_linePreview->setStrokePen(pen);
        m_linePreview->setLineWidthPx(m_adapter->lineWidth());
    }
    if (m_polygonPreview) {
        m_polygonPreview->setColor(m_adapter->polygonFill());
        QPen outline(m_adapter->polygonOutlineColor());
        outline.setWidthF(m_adapter->polygonOutlineWidth());
        m_polygonPreview->setStrokePen(outline);
        m_polygonPreview->setFillOpacity(m_adapter->polygonFill().alphaF());
    }
}

REGISTER_STYLE_EDITOR(
    GisVectorSymbolAdapter,
    [](QObject *obj, QWidget *parent) -> IStyleEditorWidget * {
        if (auto *a = qobject_cast<GisVectorSymbolAdapter *>(obj))
            return new GisVectorSymbolEditor(a, parent);
        return nullptr;
    })

} // namespace openswmmvis::ui
