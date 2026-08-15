/*!
 * \file   colorrampcombobox.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/widgets/colorrampcombobox.h"

#include "core/preferencesmanager.h"
#include "ui/dialogs/colorrampeditordialog.h"

#include <QAbstractItemView>
#include <QInputDialog>
#include <QPaintEvent>
#include <QPainter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QStyledItemDelegate>

namespace
{

constexpr int RampDataRole = Qt::UserRole + 1;   // RasterColorRamp payload
constexpr int RampKindRole = Qt::UserRole + 2;   // "builtin" / "custom" / "edit" / "sep"

// Custom delegate that paints each combo row as
//   ┌─────────────┐
//   │ ▒▒▒▒▒▒▒▒▒▒▒ │ <- ramp gradient swatch
//   └─────────────┘ Label
// instead of the default text-only row, so the user can compare ramps
// visually as in QGIS / ArcGIS Pro.
class RampItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setHeight(std::max(s.height(), 22));
        return s;
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const QString kind = idx.data(RampKindRole).toString();

        // Separators just render as a thin horizontal line — no swatch / text.
        if (kind == QStringLiteral("sep"))
        {
            p->save();
            p->setPen(opt.palette.color(QPalette::Mid));
            const int y = opt.rect.center().y();
            p->drawLine(opt.rect.left() + 6, y, opt.rect.right() - 6, y);
            p->restore();
            return;
        }

        // "Edit Custom Ramp…" sentinel — italic text, no swatch.
        if (kind == QStringLiteral("edit"))
        {
            QStyleOptionViewItem o = opt;
            o.font.setItalic(true);
            QStyledItemDelegate::paint(p, o, idx);
            return;
        }

        // Ramp item: paint the gradient swatch on the left, label on the right.
        const QVariant payload = idx.data(RampDataRole);
        if (!payload.canConvert<RasterColorRamp>())
        {
            QStyledItemDelegate::paint(p, opt, idx);
            return;
        }

        const auto ramp = payload.value<RasterColorRamp>();

        p->save();
        p->fillRect(opt.rect,
                    (opt.state & QStyle::State_Selected) ? opt.palette.highlight()
                                                         : opt.palette.base());

        const int pad = 3;
        const int textPad = 6;
        const int swatchW = std::min(opt.rect.width() / 2, 140);
        QRect swatchRect(opt.rect.left() + pad,
                         opt.rect.top() + pad,
                         swatchW,
                         opt.rect.height() - 2 * pad);

        // Sample the ramp every couple of pixels — handles HSV interpolation
        // implicitly (the ramp's colorAt does the work).
        for (int x = swatchRect.left(); x < swatchRect.right(); ++x)
        {
            const double t = (swatchRect.width() > 1)
                ? static_cast<double>(x - swatchRect.left()) / static_cast<double>(swatchRect.width() - 1)
                : 0.0;
            p->setPen(ramp.colorAt(t));
            p->drawLine(x, swatchRect.top(), x, swatchRect.bottom());
        }
        p->setPen(opt.palette.color(QPalette::Mid));
        p->drawRect(swatchRect);

        QRect textRect = opt.rect;
        textRect.setLeft(swatchRect.right() + textPad);
        p->setPen(((opt.state & QStyle::State_Selected) ? opt.palette.highlightedText()
                                                        : opt.palette.text()).color());
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, idx.data(Qt::DisplayRole).toString());
        p->restore();
    }
};

} // namespace

ColorRampComboBox::ColorRampComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setItemDelegate(new RampItemDelegate(this));
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
    view()->setMinimumWidth(280);
    rebuildItems();

    connect(this, QOverload<int>::of(&QComboBox::activated),
            this, &ColorRampComboBox::onActivated);
}

ColorRampComboBox::~ColorRampComboBox() = default;

void ColorRampComboBox::rebuildItems()
{
    blockSignals(true);
    clear();
    appendBuiltins();
    appendCustoms();
    appendEditSentinel();
    blockSignals(false);
}

void ColorRampComboBox::appendBuiltins()
{
    const QStringList names = RasterColorRamp::builtinNames();
    for (const QString &name : names)
    {
        addItem(name);
        const int row = count() - 1;
        setItemData(row, QVariant::fromValue(RasterColorRamp::builtin(name)), RampDataRole);
        setItemData(row, QStringLiteral("builtin"), RampKindRole);
    }
}

void ColorRampComboBox::appendCustoms()
{
    const auto customs = PreferencesManager::instance()->customColorRamps();
    if (customs.isEmpty())
        return;

    // Non-selectable separator row.
    addItem(QString());
    int row = count() - 1;
    setItemData(row, QStringLiteral("sep"), RampKindRole);
    auto *model = qobject_cast<QStandardItemModel *>(this->model());
    if (model && model->item(row))
        model->item(row)->setFlags(Qt::NoItemFlags);

    for (auto it = customs.constBegin(); it != customs.constEnd(); ++it)
    {
        addItem(it.key());
        row = count() - 1;
        setItemData(row, QVariant::fromValue(it.value()), RampDataRole);
        setItemData(row, QStringLiteral("custom"), RampKindRole);
    }
}

void ColorRampComboBox::appendEditSentinel()
{
    // Non-selectable separator row before the sentinel.
    addItem(QString());
    int row = count() - 1;
    setItemData(row, QStringLiteral("sep"), RampKindRole);
    auto *model = qobject_cast<QStandardItemModel *>(this->model());
    if (model && model->item(row))
        model->item(row)->setFlags(Qt::NoItemFlags);

    addItem(tr("Edit Custom Ramp…"));
    row = count() - 1;
    setItemData(row, QStringLiteral("edit"), RampKindRole);
}

RasterColorRamp ColorRampComboBox::currentRamp() const
{
    const QVariant v = itemData(currentIndex(), RampDataRole);
    if (v.canConvert<RasterColorRamp>())
        return v.value<RasterColorRamp>();
    return RasterColorRamp::grayscale();
}

void ColorRampComboBox::setCurrentRampByName(const QString &name)
{
    for (int i = 0; i < count(); ++i)
    {
        if (itemText(i).compare(name, Qt::CaseInsensitive) == 0)
        {
            setCurrentIndex(i);
            return;
        }
    }
}

void ColorRampComboBox::onActivated(int index)
{
    if (index < 0) return;
    const QString kind = itemData(index, RampKindRole).toString();

    if (kind == QStringLiteral("edit"))
    {
        openEditor();
        return;
    }
    if (kind == QStringLiteral("sep"))
        return;

    emit rampChanged(currentRamp());
}

void ColorRampComboBox::paintEvent(QPaintEvent *)
{
    // Slice S4 — paint the standard combo box chrome (border + arrow) via
    // QStylePainter, then overlay the current ramp's gradient swatch + the
    // ramp's display name on top, mirroring the dropdown row layout. No
    // text-only fallback after selection.
    QStylePainter sp(this);
    sp.setPen(palette().color(QPalette::Text));

    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    // Suppress the default text — we paint label + swatch ourselves below.
    opt.currentText.clear();
    sp.drawComplexControl(QStyle::CC_ComboBox, opt);

    // Field rect (the inner area excluding the arrow button).
    const QRect field = style()->subControlRect(QStyle::CC_ComboBox, &opt,
                                                 QStyle::SC_ComboBoxEditField, this);
    if (!field.isValid()) {
        sp.drawControl(QStyle::CE_ComboBoxLabel, opt);
        return;
    }

    const QString kind = itemData(currentIndex(), RampKindRole).toString();
    const QVariant payload = itemData(currentIndex(), RampDataRole);

    QPainter p(this);
    p.setClipRect(field);
    p.setRenderHint(QPainter::Antialiasing, false);

    if (kind == QStringLiteral("builtin") || kind == QStringLiteral("custom")) {
        if (payload.canConvert<RasterColorRamp>()) {
            const auto ramp = payload.value<RasterColorRamp>();
            const int pad = 2;
            const int swatchW = std::min(field.width() / 2, 120);
            QRect swatch(field.left() + pad,
                         field.top() + pad,
                         swatchW,
                         field.height() - 2 * pad);
            for (int x = swatch.left(); x < swatch.right(); ++x) {
                const double t = (swatch.width() > 1)
                    ? double(x - swatch.left()) / double(swatch.width() - 1)
                    : 0.0;
                p.setPen(ramp.colorAt(t));
                p.drawLine(x, swatch.top(), x, swatch.bottom());
            }
            p.setPen(palette().color(QPalette::Mid));
            p.drawRect(swatch);

            QRect textRect = field;
            textRect.setLeft(swatch.right() + 6);
            p.setPen(palette().color(QPalette::Text));
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                       currentText());
            return;
        }
    }

    // Sentinel / separator / unknown — fall back to the default label.
    p.setPen(palette().color(QPalette::Text));
    p.drawText(field.adjusted(4, 0, -4, 0),
               Qt::AlignVCenter | Qt::AlignLeft, currentText());
}

void ColorRampComboBox::openEditor()
{
    // Pre-seed the dialog with the previous non-"edit" selection so the
    // user starts from a sensible baseline rather than an empty ramp.
    RasterColorRamp seed = RasterColorRamp::viridis();
    for (int i = 0; i < count(); ++i)
    {
        const QString kind = itemData(i, RampKindRole).toString();
        if (kind == QStringLiteral("builtin") || kind == QStringLiteral("custom"))
        {
            seed = itemData(i, RampDataRole).value<RasterColorRamp>();
            break;
        }
    }
    // The WIP dialog (Slice BB-α extension) accepts a starting ramp via
    // its (RasterColorRamp, QWidget*) overload — see colorrampeditordialog.h.
    openswmmvis::ui::ColorRampEditorDialog dlg(seed, this);
    if (dlg.exec() != QDialog::Accepted)
    {
        // User cancelled — revert combo back to whatever was selected before
        // the user clicked into the "Edit Custom Ramp…" row.
        if (count() > 0) setCurrentIndex(0);
        return;
    }

    const RasterColorRamp edited = dlg.ramp();
    bool ok = false;
    const int nextIdx = PreferencesManager::instance()->customColorRamps().size() + 1;
    const QString suggested = tr("Custom %1").arg(nextIdx);
    const QString name = QInputDialog::getText(this,
                                               tr("Save Custom Ramp"),
                                               tr("Name for this ramp:"),
                                               QLineEdit::Normal,
                                               suggested,
                                               &ok).trimmed();
    if (!ok || name.isEmpty())
    {
        // User declined to name the ramp — drop the unsaved edit, revert
        // selection to whatever was active before.
        if (count() > 0) setCurrentIndex(0);
        return;
    }

    PreferencesManager::instance()->saveCustomColorRamp(name, edited);
    rebuildItems();
    setCurrentRampByName(name);
    emit customRampSaved(name, edited);
    emit rampChanged(edited);
}
