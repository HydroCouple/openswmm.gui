/*!
 * \file   rastersymbologypanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/rastersymbologypanel.h"

#include "layers/gisrasterlayer.h"
#include "render/categoricalpalette.h"
#include "render/renderers/graduatedrasterrenderer.h"
#include "render/renderers/multibandcolorrenderer.h"
#include "render/renderers/palettedrasterrenderer.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/widgets/classificationeditor.h"
#include "ui/widgets/colorcelldelegate.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using OpenSWMM::Render::GraduatedRasterRenderer;
using OpenSWMM::Render::MultiBandColorRenderer;
using OpenSWMM::Render::PalettedRasterRenderer;

namespace openswmmvis::ui {

namespace {
constexpr int kColValue = 0;
constexpr int kColLabel = 1;
constexpr int kColColor = 2;
} // namespace

RasterSymbologyPanel::RasterSymbologyPanel(GISRasterLayer *layer, QWidget *parent)
    : QWidget(parent), m_layer(layer)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Renderer chooser ────────────────────────────────────────────────
    auto *kindRow = new QHBoxLayout;
    auto *kindLabel = new QLabel(tr("&Renderer:"), this);
    m_kindCombo = new QComboBox(this);
    m_kindCombo->setObjectName(QStringLiteral("rasterRendererKind"));
    m_kindCombo->addItem(tr("Singleband pseudocolor"), int(Kind::Graduated));
    m_kindCombo->addItem(tr("Paletted / unique values"), int(Kind::Paletted));
    // The RGB composite warps bytes straight into the tile, so it is only
    // offered where it renders faithfully (≥3 Byte bands).
    if (m_layer && m_layer->bandCount() >= 3 && m_layer->isByteRaster())
        m_kindCombo->addItem(tr("Multiband colour"), int(Kind::MultiBand));
    kindLabel->setBuddy(m_kindCombo);
    kindRow->addWidget(kindLabel);
    kindRow->addWidget(m_kindCombo, 1);
    root->addLayout(kindRow);

    m_sourceBox = buildSourceGroup();
    root->addWidget(m_sourceBox);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildGraduatedPage());   // Kind::Graduated
    m_stack->addWidget(buildPalettedPage());    // Kind::Paletted
    m_stack->addWidget(buildMultiBandPage());   // Kind::MultiBand
    root->addWidget(m_stack);

    m_hsBox = buildHillshadeGroup();
    root->addWidget(m_hsBox);
    root->addStretch();

    connect(m_kindCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &RasterSymbologyPanel::onKindComboChanged);

    if (m_layer) {
        // Queued: a push from one of our own editors (or the dialog's
        // Cancel / undo restore) re-syncs the panel after the current
        // handler returns, never inside it.
        connect(m_layer, &GISRasterLayer::rasterRendererChanged,
                this, &RasterSymbologyPanel::refreshFromModel, Qt::QueuedConnection);
        connect(m_layer, &GISRasterLayer::renderBandChanged,
                this, &RasterSymbologyPanel::refreshFromModel, Qt::QueuedConnection);
    }

    refreshFromModel();
}

// ── Builders ────────────────────────────────────────────────────────────

QGroupBox *RasterSymbologyPanel::buildSourceGroup()
{
    auto *box  = new QGroupBox(tr("Source"), this);
    auto *form = new QFormLayout(box);

    m_bandSpin = new QSpinBox(box);
    m_bandSpin->setObjectName(QStringLiteral("rasterRenderBand"));
    m_bandSpin->setRange(1, std::max(1, m_layer ? m_layer->bandCount() : 1));
    form->addRow(tr("R&ender band:"), m_bandSpin);

    m_nodataLabel = new QLabel(box);
    m_nodataLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("&NoData value:"), m_nodataLabel);

    connect(m_bandSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_suppress && m_layer)
            m_layer->setRenderBand(v);
    });
    return box;
}

QWidget *RasterSymbologyPanel::buildGraduatedPage()
{
    auto *page = new QWidget(this);
    auto *lay  = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    // The shared classification editor drives the renderer's scheme through
    // RasterSchemeBinding (data range = renderer stats, samples = the
    // layer's cached band sample, edges mirrored from the renderer).
    m_classEditor = new ClassificationEditor(new RasterSchemeBinding(m_layer.data()),
                                             /*ownBinding=*/true, page);
    lay->addWidget(m_classEditor);

    m_clipCheck = new QCheckBox(tr("Clip out-of-range values (render transparent)"), page);
    m_clipCheck->setObjectName(QStringLiteral("rasterClipOutOfRange"));
    m_clipCheck->setToolTip(tr("Values below / above the classified range render "
                               "transparent instead of taking the end colours."));
    lay->addWidget(m_clipCheck);
    connect(m_clipCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_suppress || !m_layer) return;
        if (auto *g = graduated()) {
            g->setClipOutOfRange(on);
            m_layer->notifyRasterRendererEdited();
        }
    });
    return page;
}

QWidget *RasterSymbologyPanel::buildPalettedPage()
{
    auto *page = new QWidget(this);
    auto *lay  = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *form = new QFormLayout;
    m_paletteCombo = new QComboBox(page);
    m_paletteCombo->addItems(CategoricalPalette::builtinNames());
    m_paletteCombo->setToolTip(tr("Colours assigned to classes (and to values not "
                                  "listed in the table)."));
    form->addRow(tr("&Palette:"), m_paletteCombo);
    lay->addLayout(form);

    m_palModel = new QStandardItemModel(0, 3, page);
    m_palModel->setHorizontalHeaderLabels({ tr("Value"), tr("Label"), tr("Colour") });
    m_palTable = new QTableView(page);
    m_palTable->setObjectName(QStringLiteral("rasterPalettedTable"));
    m_palTable->setModel(m_palModel);
    m_palTable->setItemDelegateForColumn(kColColor, new ColorCellDelegate(m_palTable));
    m_palTable->horizontalHeader()->setStretchLastSection(true);
    m_palTable->verticalHeader()->setVisible(false);
    m_palTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_palTable->setMinimumHeight(160);
    lay->addWidget(m_palTable, 1);

    auto *btnRow = new QHBoxLayout;
    m_classifyBtn = new QPushButton(tr("Classify unique values"), page);
    m_classifyBtn->setToolTip(tr("Rebuild the classes from the values found in a "
                                 "decimated sample of the band (rare classes may be "
                                 "missed — add them with +)."));
    m_loadTableBtn = new QPushButton(tr("Load colour table"), page);
    m_loadTableBtn->setToolTip(tr("Use the colour table embedded in the raster file."));
    auto *addBtn    = new QPushButton(tr("+"), page);
    auto *removeBtn = new QPushButton(tr("−"), page);
    addBtn->setToolTip(tr("Add a class"));
    removeBtn->setToolTip(tr("Remove the selected classes"));
    addBtn->setFixedWidth(28);
    removeBtn->setFixedWidth(28);
    btnRow->addWidget(m_classifyBtn);
    btnRow->addWidget(m_loadTableBtn);
    btnRow->addStretch();
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    lay->addLayout(btnRow);

    connect(m_paletteCombo, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        if (m_suppress || !m_layer) return;
        auto *p = paletted();
        if (!p) return;
        // Re-colour every class from the new palette, keeping values/labels.
        p->setPaletteName(name);
        const QList<QColor> pal = CategoricalPalette::byName(name);
        QList<PalettedRasterRenderer::Class> classes = p->classes();
        for (int i = 0; i < classes.size() && !pal.isEmpty(); ++i)
            classes[i].color = pal.at(i % pal.size());
        p->setClasses(classes);
        m_layer->notifyRasterRendererEdited();
    });
    connect(m_palModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem *) {
        if (!m_suppress) pushPalettedTable();
    });
    connect(m_classifyBtn, &QPushButton::clicked, this, [this] {
        if (!m_layer) return;
        if (auto *p = paletted()) {
            p->buildClassesFromValues(m_layer->uniqueValues(m_layer->renderBand()));
            m_layer->notifyRasterRendererEdited();
        }
    });
    connect(m_loadTableBtn, &QPushButton::clicked, this, [this] {
        if (!m_layer) return;
        if (auto *p = paletted()) {
            p->setClasses(m_layer->colorTableClasses());
            m_layer->notifyRasterRendererEdited();
        }
    });
    connect(addBtn, &QPushButton::clicked, this, [this] {
        if (!m_layer) return;
        auto *p = paletted();
        if (!p) return;
        QList<PalettedRasterRenderer::Class> classes = p->classes();
        int next = 0;
        for (const auto &c : classes) next = std::max(next, c.value + 1);
        const QList<QColor> pal = CategoricalPalette::byName(p->paletteName());
        PalettedRasterRenderer::Class c;
        c.value = next;
        c.label = QString::number(next);
        c.color = pal.isEmpty() ? QColor(150, 150, 150) : pal.at(classes.size() % pal.size());
        classes.append(c);
        p->setClasses(classes);
        m_layer->notifyRasterRendererEdited();
    });
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        if (!m_layer) return;
        auto *p = paletted();
        if (!p) return;
        QList<int> rows;
        for (const QModelIndex &idx : m_palTable->selectionModel()->selectedRows())
            rows.append(idx.row());
        if (rows.isEmpty()) return;
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        QList<PalettedRasterRenderer::Class> classes = p->classes();
        for (int r : rows)
            if (r >= 0 && r < classes.size()) classes.removeAt(r);
        p->setClasses(classes);
        m_layer->notifyRasterRendererEdited();
    });
    return page;
}

QWidget *RasterSymbologyPanel::buildMultiBandPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);

    const int n = std::max(1, m_layer ? m_layer->bandCount() : 1);
    auto makeSpin = [&](const char *name, int lo) {
        auto *s = new QSpinBox(page);
        s->setObjectName(QString::fromLatin1(name));
        s->setRange(lo, n);
        return s;
    };
    m_redSpin   = makeSpin("rasterRedBand", 1);
    m_greenSpin = makeSpin("rasterGreenBand", 1);
    m_blueSpin  = makeSpin("rasterBlueBand", 1);
    m_alphaSpin = makeSpin("rasterAlphaBand", 0);
    m_alphaSpin->setSpecialValueText(tr("(none)"));
    form->addRow(tr("&Red band:"),   m_redSpin);
    form->addRow(tr("&Green band:"), m_greenSpin);
    form->addRow(tr("&Blue band:"),  m_blueSpin);
    form->addRow(tr("&Alpha band:"), m_alphaSpin);

    for (QSpinBox *s : { m_redSpin, m_greenSpin, m_blueSpin, m_alphaSpin })
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
            if (!m_suppress) pushMultiBand();
        });
    return page;
}

QGroupBox *RasterSymbologyPanel::buildHillshadeGroup()
{
    auto *box  = new QGroupBox(tr("Hillshade relief"), this);
    auto *form = new QFormLayout(box);

    m_hsEnable = new QCheckBox(tr("Enable relief shading"), box);
    form->addRow(QString(), m_hsEnable);

    m_hsAzimuth = new QDoubleSpinBox(box);
    m_hsAzimuth->setRange(0.0, 360.0); m_hsAzimuth->setSuffix(tr("°"));
    form->addRow(tr("A&zimuth:"), m_hsAzimuth);

    m_hsAltitude = new QDoubleSpinBox(box);
    m_hsAltitude->setRange(0.0, 90.0); m_hsAltitude->setSuffix(tr("°"));
    form->addRow(tr("A&ltitude:"), m_hsAltitude);

    m_hsZFactor = new QDoubleSpinBox(box);
    m_hsZFactor->setRange(0.0, 100.0); m_hsZFactor->setDecimals(2);
    m_hsZFactor->setSingleStep(0.1);
    form->addRow(tr("Z &factor:"), m_hsZFactor);

    m_hsStrength = new QDoubleSpinBox(box);
    m_hsStrength->setRange(0.0, 1.0); m_hsStrength->setDecimals(2);
    m_hsStrength->setSingleStep(0.05);
    form->addRow(tr("&Strength:"), m_hsStrength);

    connect(m_hsEnable, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_suppress && m_layer) m_layer->setHillshadeEnabled(on);
    });
    for (QDoubleSpinBox *sb : { m_hsAzimuth, m_hsAltitude, m_hsZFactor, m_hsStrength })
        connect(sb, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double) { if (!m_suppress) pushHillshade(); });
    return box;
}

// ── Model access ────────────────────────────────────────────────────────

GraduatedRasterRenderer *RasterSymbologyPanel::graduated() const
{
    return m_layer ? dynamic_cast<GraduatedRasterRenderer *>(m_layer->rasterRenderer()) : nullptr;
}

PalettedRasterRenderer *RasterSymbologyPanel::paletted() const
{
    return m_layer ? dynamic_cast<PalettedRasterRenderer *>(m_layer->rasterRenderer()) : nullptr;
}

MultiBandColorRenderer *RasterSymbologyPanel::multiband() const
{
    return m_layer ? dynamic_cast<MultiBandColorRenderer *>(m_layer->rasterRenderer()) : nullptr;
}

RasterSymbologyPanel::Kind RasterSymbologyPanel::currentKind() const
{
    if (paletted())  return Kind::Paletted;
    if (multiband()) return Kind::MultiBand;
    return Kind::Graduated;
}

// ── Renderer switching ──────────────────────────────────────────────────

void RasterSymbologyPanel::onKindComboChanged(int index)
{
    if (m_suppress || !m_layer || index < 0) return;
    const auto kind = Kind(m_kindCombo->itemData(index).toInt());
    if (kind == currentKind()) return;
    installKind(kind);
}

void RasterSymbologyPanel::installKind(Kind kind)
{
    if (!m_layer) return;
    const int band = m_layer->renderBand();
    std::unique_ptr<OpenSWMM::Render::IRasterRenderer> fresh;
    switch (kind) {
    case Kind::Graduated: {
        auto g = std::make_unique<GraduatedRasterRenderer>();
        const auto [lo, hi] = m_layer->bandRange(band);
        if (std::isfinite(lo) && std::isfinite(hi))
            g->setDataRange(lo, hi);
        g->reclassify(m_layer->sampleValues(band));
        fresh = std::move(g);
        break;
    }
    case Kind::Paletted: {
        auto p = std::make_unique<PalettedRasterRenderer>();
        if (m_layer->hasColorTable())
            p->setClasses(m_layer->colorTableClasses());
        else
            p->buildClassesFromValues(m_layer->uniqueValues(band));
        fresh = std::move(p);
        break;
    }
    case Kind::MultiBand: {
        const int n = m_layer->bandCount();
        fresh = std::make_unique<MultiBandColorRenderer>(1, 2, 3, n >= 4 ? 4 : 0);
        break;
    }
    }
    m_layer->setRasterRenderer(std::move(fresh));   // invalidates + repaints
    refreshFromModel();
}

// ── Pushes ──────────────────────────────────────────────────────────────

void RasterSymbologyPanel::pushPalettedTable()
{
    if (!m_layer) return;
    auto *p = paletted();
    if (!p) return;
    QList<PalettedRasterRenderer::Class> classes;
    classes.reserve(m_palModel->rowCount());
    for (int r = 0; r < m_palModel->rowCount(); ++r) {
        PalettedRasterRenderer::Class c;
        c.value = m_palModel->item(r, kColValue)->text().toInt();
        c.label = m_palModel->item(r, kColLabel)->text();
        c.color = m_palModel->item(r, kColColor)->data(Qt::BackgroundRole).value<QColor>();
        classes.append(c);
    }
    p->setClasses(classes);
    m_layer->notifyRasterRendererEdited();
}

void RasterSymbologyPanel::pushMultiBand()
{
    if (!m_layer) return;
    if (auto *mb = multiband()) {
        mb->setBands(m_redSpin->value(), m_greenSpin->value(),
                     m_blueSpin->value(), m_alphaSpin->value());
        m_layer->notifyRasterRendererEdited();
    }
}

void RasterSymbologyPanel::pushHillshade()
{
    if (!m_layer) return;
    m_layer->setHillshadeParams(m_hsAzimuth->value(), m_hsAltitude->value(),
                                m_hsZFactor->value(), m_hsStrength->value());
}

// ── Sync from model ─────────────────────────────────────────────────────

void RasterSymbologyPanel::rebuildPalettedTable()
{
    // Only ever called under m_suppress (refreshFromModel), which is what
    // keeps the itemChanged handler quiet — do NOT block the model's signals
    // here or the view never learns about the inserted rows.
    m_palModel->removeRows(0, m_palModel->rowCount());
    auto *p = paletted();
    if (!p) return;
    for (const auto &c : p->classes()) {
        auto *value = new QStandardItem(QString::number(c.value));
        auto *label = new QStandardItem(c.label);
        auto *color = new QStandardItem;
        color->setData(c.color, Qt::BackgroundRole);
        color->setEditable(false);
        m_palModel->appendRow({ value, label, color });
    }
    m_palTable->resizeColumnToContents(kColValue);
}

void RasterSymbologyPanel::refreshFromModel()
{
    if (!m_layer) return;
    m_suppress = true;

    const Kind kind = currentKind();
    // A project may restore an RGB renderer the combo did not offer (e.g.
    // the file was re-typed); keep the combo truthful.
    int idx = m_kindCombo->findData(int(kind));
    if (idx < 0) {
        m_kindCombo->addItem(tr("Multiband colour"), int(Kind::MultiBand));
        idx = m_kindCombo->findData(int(kind));
    }
    {
        const QSignalBlocker b(m_kindCombo);
        m_kindCombo->setCurrentIndex(idx);
    }
    m_stack->setCurrentIndex(int(kind));
    m_sourceBox->setVisible(kind != Kind::MultiBand);
    m_hsBox->setVisible(kind == Kind::Graduated);

    {
        const QSignalBlocker b(m_bandSpin);
        m_bandSpin->setRange(1, std::max(1, m_layer->bandCount()));
        m_bandSpin->setValue(m_layer->renderBand());
    }
    m_nodataLabel->setText(m_layer->hasNoDataValue()
                               ? QString::number(m_layer->noDataValue(), 'g', 8)
                               : tr("(none)"));

    switch (kind) {
    case Kind::Graduated: {
        m_classEditor->refresh();
        const QSignalBlocker b(m_clipCheck);
        if (auto *g = graduated())
            m_clipCheck->setChecked(g->clipOutOfRange());
        break;
    }
    case Kind::Paletted: {
        if (auto *p = paletted()) {
            const QSignalBlocker b(m_paletteCombo);
            const int pi = m_paletteCombo->findText(p->paletteName(), Qt::MatchFixedString);
            if (pi >= 0) m_paletteCombo->setCurrentIndex(pi);
        }
        rebuildPalettedTable();
        m_loadTableBtn->setEnabled(m_layer->hasColorTable());
        break;
    }
    case Kind::MultiBand: {
        const int n = std::max(1, m_layer->bandCount());
        for (QSpinBox *s : { m_redSpin, m_greenSpin, m_blueSpin })
            s->setRange(1, n);
        m_alphaSpin->setRange(0, n);
        if (auto *mb = multiband()) {
            const QSignalBlocker b1(m_redSpin), b2(m_greenSpin), b3(m_blueSpin), b4(m_alphaSpin);
            m_redSpin->setValue(mb->redBand());
            m_greenSpin->setValue(mb->greenBand());
            m_blueSpin->setValue(mb->blueBand());
            m_alphaSpin->setValue(mb->alphaBand());
        }
        break;
    }
    }

    {
        const QSignalBlocker b5(m_hsEnable), b6(m_hsAzimuth), b7(m_hsAltitude),
            b8(m_hsZFactor), b9(m_hsStrength);
        m_hsEnable->setChecked(m_layer->hillshadeEnabled());
        m_hsAzimuth->setValue(m_layer->hillshadeAzimuthDeg());
        m_hsAltitude->setValue(m_layer->hillshadeAltitudeDeg());
        m_hsZFactor->setValue(m_layer->hillshadeZFactor());
        m_hsStrength->setValue(m_layer->hillshadeStrength());
    }

    m_suppress = false;
}

} // namespace openswmmvis::ui
