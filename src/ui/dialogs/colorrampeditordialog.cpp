/*!
 * \file   colorrampeditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/colorrampeditordialog.h"

#include "core/preferencesmanager.h"
#include "ui/widgets/colorbutton.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace openswmmvis::ui {

namespace {

/*! Live gradient strip. Paints in paintEvent by sampling the ramp per
 *  pixel column (so HSV interpolation renders truthfully) — inherently
 *  resize-aware, unlike the previous fixed QPixmap-in-QLabel preview.
 *  Stop positions are marked with small triangles under the strip. */
class GradientPreviewWidget : public QWidget
{
public:
    explicit GradientPreviewWidget(const RasterColorRamp *ramp, QWidget *parent = nullptr)
        : QWidget(parent), m_ramp(ramp)
    {
        setMinimumHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int markerH = 8;
        QRect strip = rect().adjusted(0, 0, -1, -markerH - 1);

        // Checkerboard underlay so per-stop alpha is visible.
        const int sq = 6;
        for (int y = strip.top(); y < strip.bottom(); y += sq)
            for (int x = strip.left(); x < strip.right(); x += sq)
                p.fillRect(QRect(x, y, sq, sq).intersected(strip),
                           (((x / sq) + (y / sq)) % 2) ? QColor(200, 200, 200)
                                                       : QColor(255, 255, 255));

        for (int x = strip.left(); x < strip.right(); ++x)
        {
            const double t = (strip.width() > 1)
                ? double(x - strip.left()) / double(strip.width() - 1)
                : 0.0;
            p.setPen(m_ramp->colorAt(t));
            p.drawLine(x, strip.top(), x, strip.bottom());
        }
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(strip);

        // Stop markers.
        p.setRenderHint(QPainter::Antialiasing, true);
        for (const QGradientStop &s : m_ramp->stops)
        {
            const int x = strip.left() + int(s.first * (strip.width() - 1));
            QPolygonF tri;
            tri << QPointF(x, strip.bottom() + 1)
                << QPointF(x - 4, strip.bottom() + markerH)
                << QPointF(x + 4, strip.bottom() + markerH);
            p.setPen(palette().color(QPalette::Text));
            p.setBrush(s.second);
            p.drawPolygon(tri);
        }
    }

private:
    const RasterColorRamp *m_ramp;
};

constexpr int kColPos   = 0;
constexpr int kColColor = 1;

} // namespace

ColorRampEditorDialog::ColorRampEditorDialog(const RasterColorRamp &initial,
                                             QWidget *parent)
    : QDialog(parent)
    , m_ramp(initial)
{
    setWindowTitle(tr("Edit Custom Color Ramp"));
    if (m_ramp.stops.size() < 2)
        m_ramp = RasterColorRamp::viridis();
    buildUi();
    rebuildStopTable();
    refreshSavedCombo();
    refreshOkEnabled();
    resize(560, 560);
}

ColorRampEditorDialog::~ColorRampEditorDialog() = default;

QString ColorRampEditorDialog::rampName() const
{
    return m_nameEdit->text().trimmed();
}

void ColorRampEditorDialog::setRampName(const QString &name)
{
    m_nameEdit->setText(name);
}

void ColorRampEditorDialog::buildUi()
{
    auto *lay = new QVBoxLayout(this);

    // ── Name ────────────────────────────────────────────────────────────
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("&Name:"), this));
        m_nameEdit = new QLineEdit(this);
        m_nameEdit->setPlaceholderText(tr("Name for this ramp"));
        static_cast<QLabel *>(row->itemAt(0)->widget())->setBuddy(m_nameEdit);
        row->addWidget(m_nameEdit, 1);
        lay->addLayout(row);

        m_nameWarning = new QLabel(this);
        m_nameWarning->setStyleSheet(QStringLiteral("color: palette(highlight);"));
        m_nameWarning->setVisible(false);
        lay->addWidget(m_nameWarning);

        connect(m_nameEdit, &QLineEdit::textChanged,
                this, &ColorRampEditorDialog::onNameEdited);
    }

    // ── Preset / interpolation / reverse ────────────────────────────────
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Preset:"), this));
        m_presetCombo = new QComboBox(this);
        m_presetCombo->addItem(tr("(custom)"));
        // Full builtin catalogue — the old dialog hard-coded 7 of them.
        const QStringList names = RasterColorRamp::builtinNames();
        for (const QString &n : names)
            m_presetCombo->addItem(n);
        row->addWidget(m_presetCombo, 1);

        row->addWidget(new QLabel(tr("Interpolation:"), this));
        m_interpCombo = new QComboBox(this);
        m_interpCombo->addItem(tr("RGB"),         int(RampInterp::Rgb));
        m_interpCombo->addItem(tr("HSV (short)"), int(RampInterp::HsvShort));
        m_interpCombo->addItem(tr("HSV (long)"),  int(RampInterp::HsvLong));
        m_interpCombo->setCurrentIndex(
            m_interpCombo->findData(int(m_ramp.interp)));
        row->addWidget(m_interpCombo);

        m_reverseBtn = new QToolButton(this);
        m_reverseBtn->setText(tr("Reverse"));
        m_reverseBtn->setToolTip(tr("Mirror the stop order"));
        row->addWidget(m_reverseBtn);
        lay->addLayout(row);

        connect(m_presetCombo, qOverload<int>(&QComboBox::activated),
                this, &ColorRampEditorDialog::onPresetChanged);
        connect(m_interpCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &ColorRampEditorDialog::onInterpChanged);
        connect(m_reverseBtn, &QToolButton::clicked,
                this, &ColorRampEditorDialog::onReverseClicked);
    }

    // ── Live preview ────────────────────────────────────────────────────
    m_preview = new GradientPreviewWidget(&m_ramp, this);
    lay->addWidget(m_preview);

    // ── Stop table ──────────────────────────────────────────────────────
    {
        m_stopTable = new QTableWidget(0, 2, this);
        m_stopTable->setHorizontalHeaderLabels({tr("Position"), tr("Color")});
        m_stopTable->horizontalHeader()->setSectionResizeMode(kColPos, QHeaderView::Stretch);
        m_stopTable->horizontalHeader()->setSectionResizeMode(kColColor, QHeaderView::Stretch);
        m_stopTable->verticalHeader()->setVisible(false);
        m_stopTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_stopTable->setSelectionMode(QAbstractItemView::SingleSelection);
        lay->addWidget(m_stopTable, 1);

        auto *row = new QHBoxLayout;
        m_addStopBtn = new QPushButton(tr("&Add Stop"), this);
        m_removeStopBtn = new QPushButton(tr("&Remove Stop"), this);
        row->addWidget(m_addStopBtn);
        row->addWidget(m_removeStopBtn);
        row->addStretch(1);
        lay->addLayout(row);

        connect(m_addStopBtn, &QPushButton::clicked,
                this, &ColorRampEditorDialog::onAddStop);
        connect(m_removeStopBtn, &QPushButton::clicked,
                this, &ColorRampEditorDialog::onRemoveStop);
    }

    // ── Saved custom ramps (load / delete) ──────────────────────────────
    {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Saved custom ramps:"), this));
        m_savedCombo = new QComboBox(this);
        row->addWidget(m_savedCombo, 1);
        m_loadSavedBtn = new QPushButton(tr("&Load"), this);
        m_deleteSavedBtn = new QPushButton(tr("&Delete"), this);
        row->addWidget(m_loadSavedBtn);
        row->addWidget(m_deleteSavedBtn);
        lay->addLayout(row);

        connect(m_loadSavedBtn, &QPushButton::clicked,
                this, &ColorRampEditorDialog::onLoadSaved);
        connect(m_deleteSavedBtn, &QPushButton::clicked,
                this, &ColorRampEditorDialog::onDeleteSaved);
    }

    // ── OK / Cancel ─────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okBtn = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
}

void ColorRampEditorDialog::rebuildStopTable()
{
    m_suppress = true;
    m_stopTable->setRowCount(int(m_ramp.stops.size()));
    for (int i = 0; i < m_ramp.stops.size(); ++i)
    {
        const QGradientStop &s = m_ramp.stops.at(i);

        auto *posSpin = new QDoubleSpinBox(m_stopTable);
        posSpin->setRange(0.0, 1.0);
        posSpin->setDecimals(3);
        posSpin->setSingleStep(0.05);
        posSpin->setValue(s.first);
        posSpin->setFrame(false);
        connect(posSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &ColorRampEditorDialog::onStopEdited);
        m_stopTable->setCellWidget(i, kColPos, posSpin);

        auto *colBtn = new ColorButton(s.second, m_stopTable);
        connect(colBtn, &ColorButton::colorChanged,
                this, &ColorRampEditorDialog::onStopEdited);
        m_stopTable->setCellWidget(i, kColColor, colBtn);
    }
    m_suppress = false;
    m_preview->update();
    refreshOkEnabled();
}

void ColorRampEditorDialog::readStopsFromTable()
{
    QGradientStops stops;
    stops.reserve(m_stopTable->rowCount());
    for (int i = 0; i < m_stopTable->rowCount(); ++i)
    {
        const auto *posSpin = qobject_cast<QDoubleSpinBox *>(m_stopTable->cellWidget(i, kColPos));
        const auto *colBtn  = qobject_cast<ColorButton *>(m_stopTable->cellWidget(i, kColColor));
        if (!posSpin || !colBtn) continue;
        stops.append({posSpin->value(), colBtn->color()});
    }
    std::stable_sort(stops.begin(), stops.end(),
                     [](const QGradientStop &a, const QGradientStop &b)
                     { return a.first < b.first; });
    m_ramp.stops = stops;
}

void ColorRampEditorDialog::onPresetChanged(int index)
{
    if (index <= 0) return;   // "(custom)" placeholder
    const QString name = m_presetCombo->itemText(index);
    const RasterColorRamp preset = RasterColorRamp::builtin(name);
    m_ramp.stops = preset.stops;
    m_ramp.interp = preset.interp;
    {
        QSignalBlocker b(m_interpCombo);
        m_interpCombo->setCurrentIndex(m_interpCombo->findData(int(m_ramp.interp)));
    }
    rebuildStopTable();
}

void ColorRampEditorDialog::onInterpChanged(int index)
{
    m_ramp.interp = RampInterp(m_interpCombo->itemData(index).toInt());
    markCustomised();
    m_preview->update();
}

void ColorRampEditorDialog::onReverseClicked()
{
    QGradientStops rev;
    rev.reserve(m_ramp.stops.size());
    for (auto it = m_ramp.stops.crbegin(); it != m_ramp.stops.crend(); ++it)
        rev.append({1.0 - it->first, it->second});
    m_ramp.stops = rev;
    markCustomised();
    rebuildStopTable();
}

void ColorRampEditorDialog::onAddStop()
{
    readStopsFromTable();
    // Insert at the midpoint of the widest gap so the new stop is visible
    // and immediately draggable to where the user wants it.
    double bestPos = 0.5;
    double bestGap = -1.0;
    for (int i = 0; i + 1 < m_ramp.stops.size(); ++i)
    {
        const double gap = m_ramp.stops[i + 1].first - m_ramp.stops[i].first;
        if (gap > bestGap)
        {
            bestGap = gap;
            bestPos = m_ramp.stops[i].first + gap / 2.0;
        }
    }
    m_ramp.stops.append({bestPos, m_ramp.colorAt(bestPos)});
    std::stable_sort(m_ramp.stops.begin(), m_ramp.stops.end(),
                     [](const QGradientStop &a, const QGradientStop &b)
                     { return a.first < b.first; });
    markCustomised();
    rebuildStopTable();
}

void ColorRampEditorDialog::onRemoveStop()
{
    const int row = m_stopTable->currentRow();
    if (row < 0 || m_stopTable->rowCount() <= 2)
        return;   // a ramp needs at least two stops
    readStopsFromTable();
    if (row < m_ramp.stops.size())
        m_ramp.stops.removeAt(row);
    markCustomised();
    rebuildStopTable();
}

void ColorRampEditorDialog::onStopEdited()
{
    if (m_suppress) return;
    readStopsFromTable();
    markCustomised();
    m_preview->update();
}

void ColorRampEditorDialog::onNameEdited(const QString &text)
{
    const QString name = text.trimmed();
    const bool clash = !name.isEmpty()
        && PreferencesManager::instance()->customColorRamps().contains(name);
    m_nameWarning->setText(clash
        ? tr("A custom ramp named \"%1\" already exists and will be overwritten.").arg(name)
        : QString());
    m_nameWarning->setVisible(clash);
    refreshOkEnabled();
}

void ColorRampEditorDialog::onLoadSaved()
{
    const QString name = m_savedCombo->currentText();
    if (name.isEmpty()) return;
    const auto customs = PreferencesManager::instance()->customColorRamps();
    const auto it = customs.constFind(name);
    if (it == customs.constEnd()) return;
    m_ramp.stops = it.value().stops;
    m_ramp.interp = it.value().interp;
    {
        QSignalBlocker b(m_interpCombo);
        m_interpCombo->setCurrentIndex(m_interpCombo->findData(int(m_ramp.interp)));
    }
    m_nameEdit->setText(name);
    rebuildStopTable();
}

void ColorRampEditorDialog::onDeleteSaved()
{
    const QString name = m_savedCombo->currentText();
    if (name.isEmpty()) return;
    const auto ret = QMessageBox::question(
        this, tr("Delete Custom Ramp"),
        tr("Delete the custom ramp \"%1\"? Styles already using it keep "
           "their colors (the ramp is embedded in the style), but it will "
           "no longer be offered in ramp lists.").arg(name));
    if (ret != QMessageBox::Yes) return;
    PreferencesManager::instance()->removeCustomColorRamp(name);
    refreshSavedCombo();
    onNameEdited(m_nameEdit->text());
}

void ColorRampEditorDialog::refreshSavedCombo()
{
    const QString prev = m_savedCombo->currentText();
    QSignalBlocker b(m_savedCombo);
    m_savedCombo->clear();
    const auto customs = PreferencesManager::instance()->customColorRamps();
    for (auto it = customs.constBegin(); it != customs.constEnd(); ++it)
        m_savedCombo->addItem(it.key());
    const int idx = m_savedCombo->findText(prev);
    if (idx >= 0) m_savedCombo->setCurrentIndex(idx);
    const bool any = m_savedCombo->count() > 0;
    m_loadSavedBtn->setEnabled(any);
    m_deleteSavedBtn->setEnabled(any);
}

void ColorRampEditorDialog::refreshOkEnabled()
{
    m_okBtn->setEnabled(!rampName().isEmpty() && m_ramp.stops.size() >= 2);
}

void ColorRampEditorDialog::markCustomised()
{
    QSignalBlocker b(m_presetCombo);
    m_presetCombo->setCurrentIndex(0);   // "(custom)"
}

} // namespace openswmmvis::ui
