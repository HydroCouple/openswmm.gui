/*!
 * \file   classificationeditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.1 — shared classification editor.
 */
#include "ui/widgets/classificationeditor.h"

#include "render/attributesource.h"
#include "render/intervalbinner.h"
#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/widgets/colorrampcombobox.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using OpenSWMM::Render::BinMethod;
using OpenSWMM::Render::ClassificationScheme;
using OpenSWMM::Render::RangeMode;

namespace openswmmvis::ui {

namespace {

constexpr int kModeContinuous = 0;
constexpr int kModeClassified = 1;

/*! Colour-swatch cell with a double-click colour picker. Shared from the
 *  original KindRendererPanel implementation. */
class ColorCellDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const QColor c = idx.data(Qt::BackgroundRole).value<QColor>();
        p->save();
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, opt.palette.highlight());
        QRect inner = opt.rect.adjusted(4, 4, -4, -4);
        if (c.isValid()) {
            p->setBrush(c);
            p->setPen(QPen(QColor(60, 60, 60), 0.8));
            p->drawRoundedRect(inner, 3, 3);
        }
        p->restore();
    }

    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        return nullptr;
    }

    bool editorEvent(QEvent *e, QAbstractItemModel *m,
                     const QStyleOptionViewItem &, const QModelIndex &idx) override
    {
        if (e->type() != QEvent::MouseButtonDblClick) return false;
        QColor cur = idx.data(Qt::BackgroundRole).value<QColor>();
        const QColor picked = QColorDialog::getColor(
            cur.isValid() ? cur : QColor(Qt::white),
            nullptr, QObject::tr("Class colour"),
            QColorDialog::ShowAlphaChannel);
        if (!picked.isValid()) return true;
        m->setData(idx, picked, Qt::BackgroundRole);
        return true;
    }
};

} // namespace

ClassificationEditor::ClassificationEditor(IClassificationBinding *binding,
                                           bool ownBinding, QWidget *parent)
    : QWidget(parent), m_binding(binding), m_ownBinding(ownBinding)
{
    buildUi();
    refresh();
}

ClassificationEditor::~ClassificationEditor()
{
    if (m_ownBinding)
        delete m_binding;
}

void ClassificationEditor::setBinding(IClassificationBinding *binding, bool ownBinding)
{
    if (m_ownBinding && m_binding != binding)
        delete m_binding;
    m_binding = binding;
    m_ownBinding = ownBinding;
    refresh();
}

void ClassificationEditor::buildUi()
{
    auto *box = new QGroupBox(tr("Classification"), this);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);

    auto *form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Mode (Continuous / Classified) — shown only for bindings that allow it.
    m_modeCombo = new QComboBox(box);
    m_modeCombo->addItem(tr("Continuous (smooth ramp)"), kModeContinuous);
    m_modeCombo->addItem(tr("Classified (discrete bands)"), kModeClassified);
    m_modeRow = new QWidget(box);
    {
        auto *l = new QHBoxLayout(m_modeRow);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(tr("Display:"), m_modeRow));
        l->addWidget(m_modeCombo, 1);
    }
    form->addRow(m_modeRow);

    // Attribute picker — shown only when the binding lists attributes.
    m_attrCombo = new QComboBox(box);
    m_attrCombo->setMinimumContentsLength(18);
    m_attrRow = new QWidget(box);
    {
        auto *l = new QHBoxLayout(m_attrRow);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(tr("Attribute:"), m_attrRow));
        l->addWidget(m_attrCombo, 1);
    }
    form->addRow(m_attrRow);

    // Ramp + invert.
    m_rampCombo = new ColorRampComboBox(box);
    m_rampCombo->setMinimumWidth(140);
    m_invertCheck = new QCheckBox(tr("Invert"), box);
    {
        auto *rampRow = new QWidget(box);
        auto *l = new QHBoxLayout(rampRow);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(m_rampCombo, 1);
        l->addWidget(m_invertCheck);
        form->addRow(tr("Colour ramp:"), rampRow);
    }

    // Method + class count.
    m_methodCombo = new QComboBox(box);
    m_methodCombo->addItem(tr("Equal interval"),         int(BinMethod::EqualInterval));
    m_methodCombo->addItem(tr("Quantile"),               int(BinMethod::Quantile));
    m_methodCombo->addItem(tr("Natural breaks (Jenks)"), int(BinMethod::NaturalBreaks));
    m_methodCombo->addItem(tr("Standard deviation"),     int(BinMethod::StdDev));
    m_methodCombo->addItem(tr("Logarithmic"),            int(BinMethod::Logarithmic));
    m_methodCombo->addItem(tr("Exponential"),            int(BinMethod::Exponential));
    m_methodCombo->addItem(tr("Manual"),                 int(BinMethod::Manual));
    m_methodCombo->setMinimumWidth(140);
    m_methodLabel = new QLabel(tr("Method:"), box);
    form->addRow(m_methodLabel, m_methodCombo);

    m_countSpin = new QSpinBox(box);
    m_countSpin->setRange(1, 64);
    m_countSpin->setValue(5);
    m_countSpin->setMinimumWidth(110);
    m_countLabel = new QLabel(tr("Classes:"), box);
    form->addRow(m_countLabel, m_countSpin);

    // Range mode (1D-results idiom).
    using OpenSWMM::Render::RangeMode;
    m_rangeModeCombo = new QComboBox(box);
    m_rangeModeCombo->addItem(tr("Fixed over run"),         int(RangeMode::FixedOverRun));
    m_rangeModeCombo->addItem(tr("Per-frame auto-stretch"), int(RangeMode::PerFrameAutoStretch));
    m_rangeModeCombo->addItem(tr("Fixed (user range)"),     int(RangeMode::FixedUser));
    m_rangeModeRow = new QWidget(box);
    {
        auto *l = new QHBoxLayout(m_rangeModeRow);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(new QLabel(tr("Range:"), m_rangeModeRow));
        l->addWidget(m_rangeModeCombo, 1);
    }
    form->addRow(m_rangeModeRow);

    // Custom range checkbox + min/max — reused for both range idioms.
    m_customRangeRow = new QWidget(box);
    {
        auto *l = new QHBoxLayout(m_customRangeRow);
        l->setContentsMargins(0, 0, 0, 0);
        m_customRangeCheck = new QCheckBox(tr("Custom range"), m_customRangeRow);
        auto makeSpin = [&]() {
            auto *s = new QDoubleSpinBox(m_customRangeRow);
            s->setRange(-1.0e12, 1.0e12);
            s->setDecimals(4);
            s->setKeyboardTracking(false);
            s->setMinimumWidth(90);
            return s;
        };
        m_rangeMinSpin = makeSpin();
        m_rangeMaxSpin = makeSpin();
        m_rangeMaxSpin->setValue(1.0);
        l->addWidget(m_customRangeCheck);
        l->addWidget(new QLabel(tr("Min:"), m_customRangeRow));
        l->addWidget(m_rangeMinSpin, 1);
        l->addWidget(new QLabel(tr("Max:"), m_customRangeRow));
        l->addWidget(m_rangeMaxSpin, 1);
    }
    form->addRow(m_customRangeRow);

    // Label number format (decimal places vs significant figures) — GIS-style
    // control over how class-edge values are rendered in the table + legend.
    m_labelFormatRow = new QWidget(box);
    {
        auto *l = new QHBoxLayout(m_labelFormatRow);
        l->setContentsMargins(0, 0, 0, 0);
        m_labelFormatCombo = new QComboBox(m_labelFormatRow);
        m_labelFormatCombo->addItem(tr("Decimal places"),
                                    int(ClassificationScheme::LabelFormat::Decimals));
        m_labelFormatCombo->addItem(tr("Significant figures"),
                                    int(ClassificationScheme::LabelFormat::SignificantFigures));
        m_labelPrecisionSpin = new QSpinBox(m_labelFormatRow);
        m_labelPrecisionSpin->setRange(0, 9);
        m_labelPrecisionSpin->setValue(3);
        l->addWidget(new QLabel(tr("Label format:"), m_labelFormatRow));
        l->addWidget(m_labelFormatCombo, 1);
        l->addWidget(new QLabel(tr("Digits:"), m_labelFormatRow));
        l->addWidget(m_labelPrecisionSpin);
    }
    form->addRow(m_labelFormatRow);

    // Auto-classify.
    m_autoBtn = new QToolButton(box);
    m_autoBtn->setText(tr("Auto-classify from data"));
    m_autoBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    form->addRow(QString(), m_autoBtn);

    // Class table.
    m_tableModel = new QStandardItemModel(0, 4, box);
    m_tableModel->setHorizontalHeaderLabels({ tr("Lower"), tr("Upper"), tr("Colour"), tr("Label") });
    m_table = new QTableView(box);
    m_table->setModel(m_tableModel);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setItemDelegateForColumn(2, new ColorCellDelegate(m_table));
    m_table->setMinimumHeight(150);
    form->addRow(m_table);

    connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClassificationEditor::onModeChanged);
    connect(m_attrCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClassificationEditor::onAttributeChanged);
    connect(m_rampCombo, &ColorRampComboBox::rampChanged,
            this, [this](const RasterColorRamp &) { onRampChanged(); });
    connect(m_invertCheck, &QCheckBox::toggled, this, &ClassificationEditor::onInvertToggled);
    connect(m_methodCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClassificationEditor::onMethodChanged);
    connect(m_countSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &ClassificationEditor::onClassCountChanged);
    connect(m_rangeModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClassificationEditor::onRangeModeChanged);
    connect(m_customRangeCheck, &QCheckBox::toggled,
            this, &ClassificationEditor::onCustomRangeToggled);
    connect(m_rangeMinSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &ClassificationEditor::onCustomRangeEdited);
    connect(m_rangeMaxSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &ClassificationEditor::onCustomRangeEdited);
    connect(m_labelFormatCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ClassificationEditor::onLabelFormatChanged);
    connect(m_labelPrecisionSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &ClassificationEditor::onLabelPrecisionChanged);
    connect(m_autoBtn, &QToolButton::clicked, this, &ClassificationEditor::onAutoClassify);
    connect(m_tableModel, &QStandardItemModel::itemChanged,
            this, &ClassificationEditor::onTableItemChanged);
}

template <class Fn>
void ClassificationEditor::mutateScheme(Fn fn)
{
    if (m_suppress || !m_binding) return;
    ClassificationScheme s = m_binding->scheme();
    fn(s);
    m_binding->setScheme(s);
    rebuildTable();
    emit edited();
}

void ClassificationEditor::refresh()
{
    if (!m_binding) return;
    m_suppress = true;

    const ClassificationScheme s = m_binding->scheme();

    {
        QSignalBlocker b(m_modeCombo);
        m_modeCombo->setCurrentIndex(
            s.mode() == ClassificationScheme::ClassMode::Continuous ? kModeContinuous
                                                                    : kModeClassified);
    }

    // Attribute combo.
    {
        QSignalBlocker b(m_attrCombo);
        m_attrCombo->clear();
        const auto attrs = m_binding->availableAttributes();
        for (const auto &a : attrs)
            m_attrCombo->addItem(a.first, a.second);
        const QString cur = m_binding->attribute();
        int idx = -1;
        for (int i = 0; i < m_attrCombo->count(); ++i)
            if (m_attrCombo->itemData(i).toString() == cur) { idx = i; break; }
        if (idx < 0 && !cur.isEmpty()) {
            m_attrCombo->insertItem(0, cur, cur);
            idx = 0;
        }
        if (idx >= 0)
            m_attrCombo->setCurrentIndex(idx);
    }

    {
        QSignalBlocker b(m_rampCombo);
        if (!s.rampName().isEmpty())
            m_rampCombo->setCurrentRampByName(s.rampName());
    }
    {
        QSignalBlocker b(m_invertCheck);
        m_invertCheck->setChecked(s.invertRamp());
    }
    {
        QSignalBlocker b(m_methodCombo);
        m_methodCombo->setCurrentIndex(m_methodCombo->findData(int(s.method())));
    }
    {
        QSignalBlocker b(m_countSpin);
        m_countSpin->setValue(s.classCount());
    }
    {
        QSignalBlocker b(m_rangeModeCombo);
        m_rangeModeCombo->setCurrentIndex(m_rangeModeCombo->findData(int(s.rangeMode())));
    }
    {
        QSignalBlocker bc(m_customRangeCheck);
        QSignalBlocker bmin(m_rangeMinSpin);
        QSignalBlocker bmax(m_rangeMaxSpin);
        m_customRangeCheck->setChecked(s.useCustomRange());
        const auto [lo, hi] = m_binding->dataRange();
        m_rangeMinSpin->setValue(s.useCustomRange() ? s.rangeMin() : lo);
        m_rangeMaxSpin->setValue(s.useCustomRange() ? s.rangeMax() : hi);
    }
    {
        QSignalBlocker bf(m_labelFormatCombo);
        QSignalBlocker bp(m_labelPrecisionSpin);
        m_labelFormatCombo->setCurrentIndex(
            m_labelFormatCombo->findData(int(s.labelFormat())));
        m_labelPrecisionSpin->setValue(s.labelPrecision());
    }

    applyVisibility();
    rebuildTable();
    m_suppress = false;
}

void ClassificationEditor::applyVisibility()
{
    if (!m_binding) return;
    const ClassificationScheme s = m_binding->scheme();
    const bool classified = (s.mode() == ClassificationScheme::ClassMode::Classified);

    m_modeRow->setVisible(m_binding->supportsContinuousMode());
    m_attrRow->setVisible(!m_binding->availableAttributes().isEmpty());

    // Discrete-class controls hide in Continuous mode.
    m_methodLabel->setVisible(classified);
    m_methodCombo->setVisible(classified);
    m_countLabel->setVisible(classified);
    m_countSpin->setVisible(classified);
    m_autoBtn->setVisible(classified);
    m_table->setVisible(classified);
    // Label number format applies to the (classified) table + legend rows.
    if (m_labelFormatRow) m_labelFormatRow->setVisible(classified);

    // Range idioms are mutually exclusive: range-mode combo wins.
    const bool rangeModes = m_binding->supportsRangeModes();
    const bool customRange = m_binding->supportsCustomRange();
    m_rangeModeRow->setVisible(rangeModes);

    if (rangeModes) {
        // FixedUser reveals the min/max row (no checkbox).
        const bool fixedUser = (s.rangeMode() == RangeMode::FixedUser);
        m_customRangeRow->setVisible(fixedUser);
        m_customRangeCheck->setVisible(false);
        m_rangeMinSpin->setEnabled(true);
        m_rangeMaxSpin->setEnabled(true);
    } else if (customRange) {
        m_customRangeRow->setVisible(true);
        m_customRangeCheck->setVisible(true);
        // Min/Max are always directly editable; editing them engages the
        // custom range (see onCustomRangeEdited). The checkbox reflects /
        // toggles whether the custom range is in effect.
        m_rangeMinSpin->setEnabled(true);
        m_rangeMaxSpin->setEnabled(true);
    } else {
        m_customRangeRow->setVisible(false);
    }
}

void ClassificationEditor::rebuildTable()
{
    if (!m_binding) return;
    const bool prevSuppress = m_suppress;
    m_suppress = true;
    m_tableModel->setRowCount(0);

    const ClassificationScheme s = m_binding->scheme();
    if (s.mode() == ClassificationScheme::ClassMode::Continuous) {
        m_suppress = prevSuppress;
        return;
    }

    const auto [lo, hi] = m_binding->dataRange();
    // Prefer the model's authoritative edges (e.g. a GraduatedRenderer's
    // data-derived breaks) so the table mirrors the map exactly; otherwise
    // recompute from the scheme + samples.
    QVector<double> edges = m_binding->computedEdges();
    if (edges.size() < 2)
        edges = s.levelEdges(lo, hi, m_binding->sampleValues());
    const int n = edges.size() >= 2 ? edges.size() - 1 : s.classCount();

    for (int i = 0; i < n; ++i) {
        const double lower = edges.size() >= 2 ? edges[i]     : lo;
        const double upper = edges.size() >= 2 ? edges[i + 1] : hi;

        auto *lowerItem = new QStandardItem(s.formatValue(lower));
        lowerItem->setEditable(false);
        auto *upperItem = new QStandardItem(s.formatValue(upper));
        upperItem->setEditable(i < n - 1); // last upper is the range max (fixed)
        auto *colorItem = new QStandardItem;
        colorItem->setData(s.colorForClass(i, n), Qt::BackgroundRole);
        colorItem->setEditable(true);
        const QString lbl = s.labelOverride(i);
        auto *labelItem = new QStandardItem(lbl.isEmpty() ? tr("Class %1").arg(i + 1) : lbl);
        labelItem->setEditable(true);
        m_tableModel->appendRow({ lowerItem, upperItem, colorItem, labelItem });
    }
    m_suppress = prevSuppress;
}

// ── Slots ───────────────────────────────────────────────────────────────

void ClassificationEditor::onModeChanged()
{
    const auto mode = m_modeCombo->currentData().toInt() == kModeContinuous
                          ? ClassificationScheme::ClassMode::Continuous
                          : ClassificationScheme::ClassMode::Classified;
    mutateScheme([&](ClassificationScheme &s) { s.setMode(mode); });
    applyVisibility();
}

void ClassificationEditor::onAttributeChanged(int row)
{
    if (m_suppress || !m_binding) return;
    const QString name = m_attrCombo->itemData(row).toString();
    m_binding->setAttribute(name);
    refresh();
    emit edited();
}

void ClassificationEditor::onRampChanged()
{
    const QString name = m_rampCombo->currentText();
    mutateScheme([&](ClassificationScheme &s) { s.setRampName(name); });
}

void ClassificationEditor::onInvertToggled(bool on)
{
    mutateScheme([&](ClassificationScheme &s) { s.setInvertRamp(on); });
}

void ClassificationEditor::onMethodChanged(int row)
{
    const auto method = static_cast<BinMethod>(m_methodCombo->itemData(row).toInt());
    mutateScheme([&](ClassificationScheme &s) {
        // Switching to Manual seeds editable breaks from the current edges so
        // the user tweaks a sensible starting point rather than an empty set.
        if (method == BinMethod::Manual && s.manualBreaks().isEmpty()) {
            const auto [lo, hi] = m_binding->dataRange();
            s.setManualBreaks(s.interiorLevels(lo, hi, m_binding->sampleValues()));
        }
        s.setMethod(method);
    });
}

void ClassificationEditor::onClassCountChanged(int n)
{
    mutateScheme([&](ClassificationScheme &s) { s.setClassCount(n); });
}

void ClassificationEditor::onRangeModeChanged(int row)
{
    const auto mode = static_cast<RangeMode>(m_rangeModeCombo->itemData(row).toInt());
    mutateScheme([&](ClassificationScheme &s) {
        s.setRangeMode(mode);
        s.setUseCustomRange(mode == RangeMode::FixedUser);
        if (mode == RangeMode::FixedUser) {
            s.setRangeMin(m_rangeMinSpin->value());
            s.setRangeMax(m_rangeMaxSpin->value());
        }
    });
    applyVisibility();
}

void ClassificationEditor::onCustomRangeToggled(bool on)
{
    mutateScheme([&](ClassificationScheme &s) {
        s.setUseCustomRange(on);
        if (on) {
            s.setRangeMin(m_rangeMinSpin->value());
            s.setRangeMax(m_rangeMaxSpin->value());
        }
    });
    applyVisibility();
}

void ClassificationEditor::onCustomRangeEdited()
{
    if (m_suppress) return;
    double mn = m_rangeMinSpin->value();
    double mx = m_rangeMaxSpin->value();
    mutateScheme([&](ClassificationScheme &s) {
        s.setRangeMin(mn);
        s.setRangeMax(mx);
        if (m_binding->supportsRangeModes())
            s.setUseCustomRange(s.rangeMode() == RangeMode::FixedUser);
        else
            s.setUseCustomRange(true);
    });
}

void ClassificationEditor::onLabelFormatChanged(int row)
{
    const auto fmt = static_cast<ClassificationScheme::LabelFormat>(
        m_labelFormatCombo->itemData(row).toInt());
    mutateScheme([&](ClassificationScheme &s) { s.setLabelFormat(fmt); });
}

void ClassificationEditor::onLabelPrecisionChanged(int digits)
{
    mutateScheme([&](ClassificationScheme &s) { s.setLabelPrecision(digits); });
}

void ClassificationEditor::onAutoClassify()
{
    if (m_suppress || !m_binding) return;
    m_binding->autoClassify();
    rebuildTable();
    emit edited();
}

void ClassificationEditor::onTableItemChanged(QStandardItem *item)
{
    if (m_suppress || !item || !m_binding) return;

    if (item->column() == 1) {
        // Upper-bound edited → switch to Manual with the table's interior
        // breaks (each row's upper, except the last).
        QVector<double> breaks;
        const int rows = m_tableModel->rowCount();
        for (int i = 0; i < rows - 1; ++i) {
            bool ok = false;
            const double v = m_tableModel->item(i, 1)->text().toDouble(&ok);
            if (ok) breaks.append(v);
        }
        mutateScheme([&](ClassificationScheme &s) {
            s.setMethod(BinMethod::Manual);
            s.setManualBreaks(breaks);
        });
        QSignalBlocker b(m_methodCombo);
        m_methodCombo->setCurrentIndex(m_methodCombo->findData(int(BinMethod::Manual)));
    } else if (item->column() == 2) {
        const QColor c = item->data(Qt::BackgroundRole).value<QColor>();
        const int idx = item->row();
        mutateScheme([&](ClassificationScheme &s) { s.setColorOverride(idx, c); });
    } else if (item->column() == 3) {
        const QString text = item->text();
        const int idx = item->row();
        mutateScheme([&](ClassificationScheme &s) { s.setLabelOverride(idx, text); });
    }
}

} // namespace openswmmvis::ui
