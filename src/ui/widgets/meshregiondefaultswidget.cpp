/*!
 * \file   meshregiondefaultswidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/meshregiondefaultswidget.h"

#include "ui/theme/themehelpers.h"

#include <QComboBox>
#include <QDoubleValidator>
#include <QFont>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>
#include <initializer_list>

namespace {

/*! Column layout. P0…P4 are the five positional slots of mesh::InfilRow; the
 *  headers above them are retitled from mesh::infilParamLabel() for whichever
 *  row is current, because the labels are method-dependent. */
enum Column {
    ColRegion = 0,
    ColMannings,
    ColInitDepth,
    ColMethod,
    ColP0, ColP1, ColP2, ColP3, ColP4,
    ColDest,
    ColCount
};

/*! Row 0 is the '*' mesh-wide fallback and is never removed. */
constexpr int kStarRow = 0;

/*! The canonical value of a cell, kept apart from the rendered text.
 *  QTableWidgetItem aliases Qt::EditRole onto Qt::DisplayRole, so a separate
 *  role is the only way to hold a double / enum next to its label. Invalid
 *  means "blank" — NaN for a parameter, "inherit the '*' row" for the two
 *  hydraulic columns. */
constexpr int kRoleValue = Qt::UserRole + 1;

/*! Rendered in a cell the row's method does not use. */
QString emDash() { return QStringLiteral("—"); }

/*! mesh::InfilMethod::None is -1, so the enum value is the combo index minus
 *  one — infilMethodLabels() runs None…Constant in enum order. */
int methodToIndex(mesh::InfilMethod m) { return int(m) + 1; }
mesh::InfilMethod methodFromIndex(int i)
{
    return static_cast<mesh::InfilMethod>(i - 1);
}

struct NumericRange { double lo; double hi; int decimals; };

/*! Manning's n and initial depth reuse the ranges of the dialog's two spin
 *  boxes verbatim so the '*' row and a region row cannot disagree on what is
 *  an acceptable value. */
NumericRange rangeFor(int column)
{
    switch (column) {
    case ColMannings:  return {0.001, 1.0,    4};
    case ColInitDepth: return {0.0,   1000.0, 4};
    default:           return {0.0,   1.0e9,  6};   // infiltration parameters
    }
}

QString formatValue(double v) { return QString::number(v, 'g', 10); }

/*! Editors: a combo for the two enum columns, a validated line edit for every
 *  numeric one. A line edit (rather than a spin box) because "blank" is a
 *  meaningful state in both numeric flavours — an unset parameter is NaN, and
 *  an unset region hydraulic inherits the '*' row. */
class RegionDefaultsDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &idx) const override
    {
        const int col = idx.column();

        if (col == ColMethod || col == ColDest)
        {
            auto *combo = new QComboBox(parent);
            combo->addItems(col == ColMethod ? mesh::infilMethodLabels()
                                             : mesh::infilDestLabels());
            if (col == ColDest)
            {
                // Engine D-I4 — only LOST is accepted in this release. The
                // other two stay visible, disabled, so the grammar is
                // discoverable instead of looking unimplemented.
                if (auto *m = qobject_cast<QStandardItemModel *>(combo->model()))
                    for (int i = 0; i < combo->count(); ++i)
                    {
                        if (mesh::infilDestSupported(static_cast<mesh::InfilDest>(i)))
                            continue;
                        if (QStandardItem *si = m->item(i))
                        {
                            si->setEnabled(false);
                            si->setToolTip(MeshRegionDefaultsWidget::tr(
                                "Arrives with the groundwater release."));
                        }
                    }
            }
            return combo;
        }

        const NumericRange r = rangeFor(col);
        auto *edit = new QLineEdit(parent);
        auto *val  = new QDoubleValidator(r.lo, r.hi, r.decimals, edit);
        val->setNotation(QDoubleValidator::StandardNotation);
        edit->setValidator(val);
        // An inheriting hydraulic cell already displays the '*' value; showing
        // it as the placeholder makes clear that clearing the field is how you
        // get back to inheriting.
        if (col == ColMannings || col == ColInitDepth)
            edit->setPlaceholderText(idx.data(Qt::DisplayRole).toString());
        return edit;
    }

    void setEditorData(QWidget *editor, const QModelIndex &idx) const override
    {
        const QVariant v = idx.data(kRoleValue);

        if (auto *combo = qobject_cast<QComboBox *>(editor))
        {
            const int stored = v.toInt();
            combo->setCurrentIndex(
                idx.column() == ColMethod
                    ? methodToIndex(static_cast<mesh::InfilMethod>(stored))
                    : stored);
            return;
        }
        if (auto *edit = qobject_cast<QLineEdit *>(editor))
            edit->setText(v.isValid() ? formatValue(v.toDouble()) : QString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(editor))
        {
            const int i = combo->currentIndex();
            if (idx.column() == ColMethod)
                model->setData(idx, int(methodFromIndex(i)), kRoleValue);
            else if (mesh::infilDestSupported(static_cast<mesh::InfilDest>(i)))
                model->setData(idx, i, kRoleValue);
            return;
        }

        auto *edit = qobject_cast<QLineEdit *>(editor);
        if (!edit) return;

        const QString text = edit->text().trimmed();
        if (text.isEmpty())
        {
            model->setData(idx, QVariant(), kRoleValue);   // blank
            return;
        }
        bool ok = false;
        const double v = text.toDouble(&ok);
        if (!ok) return;
        // QDoubleValidator lets an out-of-range prefix through as Intermediate,
        // so the range is only really enforced here.
        const NumericRange r = rangeFor(idx.column());
        model->setData(idx, qBound(r.lo, v, r.hi), kRoleValue);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MeshRegionDefaultsWidget::MeshRegionDefaultsWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({
        tr("Region"), tr("Manning's n"), tr("Initial depth"),
        tr("Infiltration method"),
        tr("P1"), tr("P2"), tr("P3"), tr("P4"), tr("P5"),
        tr("Destination")
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->setAlternatingRowColors(true);
    m_table->setItemDelegate(new RegionDefaultsDelegate(m_table));
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColRegion, QHeaderView::Stretch);
    m_table->setMinimumHeight(140);
    lay->addWidget(m_table, 1);

    auto *hint = new QLabel(
        tr("The * row applies to every cell. Region rows are added when "
           "\"Subcatchments → triangle regions\" is on (Sources tab); each one "
           "applies to the cells inside that subcatchment. Blank roughness / "
           "depth cells inherit the * row. Leaving a method at \"None\" writes "
           "no infiltration data at all."),
        this);
    hint->setWordWrap(true);
    openswmmvis::ui::theme::applyHintRole(hint);
    lay->addWidget(hint);

    RegionRow star;
    star.tag       = QStringLiteral("*");
    star.manningsN = m_starMannings;
    star.initDepth = m_starDepth;
    appendRow(star);
    refreshParamHeaders(kStarRow);

    connect(m_table, &QTableWidget::itemChanged,
            this, &MeshRegionDefaultsWidget::onItemChanged);
    connect(m_table, &QTableWidget::currentCellChanged,
            this, &MeshRegionDefaultsWidget::onCurrentCellChanged);
}

// ---------------------------------------------------------------------------
// Population
// ---------------------------------------------------------------------------

void MeshRegionDefaultsWidget::setDepthUnit(const QString &unit)
{
    if (QTableWidgetItem *h = m_table->horizontalHeaderItem(ColInitDepth))
        h->setText(unit.isEmpty() ? tr("Initial depth")
                                  : tr("Initial depth (%1)").arg(unit));
}

void MeshRegionDefaultsWidget::setStarHydraulics(double manningsN, double initDepth)
{
    m_starMannings = manningsN;
    m_starDepth    = initDepth;

    m_updating = true;
    m_table->item(kStarRow, ColMannings)->setData(kRoleValue, manningsN);
    m_table->item(kStarRow, ColInitDepth)->setData(kRoleValue, initDepth);
    // Every region row still inheriting has to re-render, or it would keep
    // showing the value the '*' row used to carry.
    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        renderHydraulicCell(r, ColMannings);
        renderHydraulicCell(r, ColInitDepth);
    }
    m_updating = false;
}

void MeshRegionDefaultsWidget::setRegionTags(const QStringList &tags)
{
    // Toggling the region source off and on again, or re-opening the dialog
    // against an edited model, must not throw away what the user typed.
    QHash<QString, RegionRow> keep;
    for (int r = kStarRow + 1; r < m_table->rowCount(); ++r)
    {
        const RegionRow row = rowAt(r);
        keep.insert(row.tag, row);
    }

    m_updating = true;
    while (m_table->rowCount() > kStarRow + 1)
        m_table->removeRow(m_table->rowCount() - 1);
    m_updating = false;

    for (const QString &tag : tags)
    {
        RegionRow row = keep.value(tag);
        row.tag = tag;
        appendRow(row);
    }
    refreshParamHeaders(m_table->currentRow());
}

void MeshRegionDefaultsWidget::appendRow(const RegionRow &values)
{
    const int  r    = m_table->rowCount();
    const bool star = (r == kStarRow);

    m_updating = true;
    m_table->insertRow(r);
    for (int c = 0; c < ColCount; ++c)
        m_table->setItem(r, c, new QTableWidgetItem);

    QTableWidgetItem *tag = m_table->item(r, ColRegion);
    tag->setText(values.tag);
    tag->setFlags(tag->flags() & ~Qt::ItemIsEditable);
    tag->setToolTip(star
        ? tr("Mesh-wide fallback. Cells whose region has no row of its own use "
             "this one. It cannot be removed.")
        : tr("Region tag written to [2D_TRIANGLES] for every cell of this "
             "subcatchment."));

    if (star)
    {
        // Mirror only — the dialog's two spin boxes remain the '*' editors.
        for (int c = ColMannings; c <= ColInitDepth; ++c)
        {
            QTableWidgetItem *it = m_table->item(r, c);
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            it->setData(kRoleValue,
                        (c == ColMannings) ? m_starMannings : m_starDepth);
        }
    }
    else
    {
        // A blank cell inherits; only a value the caller actually carries is
        // written back.
        if (!std::isnan(values.manningsN))
            m_table->item(r, ColMannings)->setData(kRoleValue, values.manningsN);
        if (!std::isnan(values.initDepth))
            m_table->item(r, ColInitDepth)->setData(kRoleValue, values.initDepth);
    }
    renderHydraulicCell(r, ColMannings);
    renderHydraulicCell(r, ColInitDepth);

    QTableWidgetItem *method = m_table->item(r, ColMethod);
    method->setData(kRoleValue, int(values.infil.method));
    method->setText(mesh::infilMethodLabel(values.infil.method));
    method->setToolTip(tr("Infiltration model for this region. \"None\" on a "
                          "region row means it inherits the * row."));

    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
        if (!std::isnan(values.infil.p[slot]))
            m_table->item(r, ColP0 + slot)->setData(kRoleValue, values.infil.p[slot]);

    QTableWidgetItem *dest = m_table->item(r, ColDest);
    dest->setData(kRoleValue, int(values.infil.dest));

    applyMethodMask(r);
    m_updating = false;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

namespace {

/*! Italic marks a cell whose value is not the row's own — inherited from the
 *  '*' row, or masked out by the row's method. Clearing the font role (rather
 *  than setting a non-italic font) keeps the view's own font. */
void setItalic(QTableWidgetItem *item, const QFont &base, bool italic)
{
    if (!italic)
    {
        item->setData(Qt::FontRole, QVariant());
        return;
    }
    QFont f = base;
    f.setItalic(true);
    item->setFont(f);
}

} // namespace

void MeshRegionDefaultsWidget::renderHydraulicCell(int row, int column)
{
    QTableWidgetItem *it = m_table->item(row, column);
    if (!it) return;

    const QVariant v         = it->data(kRoleValue);
    const bool     inherited = (row != kStarRow) && !v.isValid();
    const double   shown     = inherited
                                   ? (column == ColMannings ? m_starMannings : m_starDepth)
                                   : v.toDouble();

    setItalic(it, m_table->font(), inherited);
    it->setText(formatValue(shown));
    if (row == kStarRow)
        it->setToolTip(tr("Edited in the \"Roughness\" / \"Initial depth\" "
                          "fields above."));
    else
        it->setToolTip(inherited
            ? tr("Inherited from the * row. Type a value to override it; clear "
                 "the cell to inherit again.")
            : tr("Overrides the * row for this region."));
}

void MeshRegionDefaultsWidget::renderParamCell(int row, int slot)
{
    QTableWidgetItem *it = m_table->item(row, ColP0 + slot);
    if (!it) return;

    const mesh::InfilMethod m    = methodAt(row);
    const bool              used = mesh::infilUsesParam(m, slot);

    setItalic(it, m_table->font(), !used);
    if (used)
    {
        const QVariant v = it->data(kRoleValue);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        it->setText(v.isValid() ? formatValue(v.toDouble()) : QString());
        it->setToolTip(mesh::infilParamLabel(m, slot));
    }
    else
    {
        // The stored value is deliberately kept: flipping a row to another
        // method and back should not silently discard what was typed. rowAt()
        // reads only the slots the current method uses, so a masked value can
        // never reach the file.
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        it->setText(emDash());
        it->setToolTip(tr("Not used by the %1 method.")
                           .arg(mesh::infilMethodLabel(m)));
    }
}

void MeshRegionDefaultsWidget::applyMethodMask(int row)
{
    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
        renderParamCell(row, slot);

    QTableWidgetItem *dest = m_table->item(row, ColDest);
    if (!dest) return;

    const bool on = (methodAt(row) != mesh::InfilMethod::None);
    setItalic(dest, m_table->font(), !on);
    if (on)
    {
        const auto d = static_cast<mesh::InfilDest>(dest->data(kRoleValue).toInt());
        dest->setFlags(dest->flags() | Qt::ItemIsEditable);
        dest->setText(mesh::infilDestLabel(d));
        dest->setToolTip(tr("Where infiltrated water goes. Only \"%1\" is "
                            "available in this release; the aquifer "
                            "destinations arrive with the groundwater release.")
                             .arg(mesh::infilDestLabel(mesh::InfilDest::Lost)));
    }
    else
    {
        dest->setFlags(dest->flags() & ~Qt::ItemIsEditable);
        dest->setText(emDash());
        dest->setToolTip(tr("Choose an infiltration method first."));
    }
}

void MeshRegionDefaultsWidget::refreshParamHeaders(int row)
{
    const mesh::InfilMethod m = (row >= 0) ? methodAt(row) : mesh::InfilMethod::None;

    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
    {
        QTableWidgetItem *h = m_table->horizontalHeaderItem(ColP0 + slot);
        if (!h) continue;

        const QString label = mesh::infilParamLabel(m, slot);
        h->setText(label.isEmpty() ? tr("P%1").arg(slot + 1) : label);
        h->setToolTip(label.isEmpty()
            ? tr("Positional infiltration parameter %1. The heading follows the "
                 "method of the selected row.").arg(slot + 1)
            : label);
    }
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

void MeshRegionDefaultsWidget::onItemChanged(QTableWidgetItem *item)
{
    if (m_updating || !item) return;

    const int row = item->row();
    const int col = item->column();

    m_updating = true;
    switch (col)
    {
    case ColRegion:
        break;                                   // read-only
    case ColMannings:
    case ColInitDepth:
        renderHydraulicCell(row, col);
        break;
    case ColMethod:
        item->setText(mesh::infilMethodLabel(methodAt(row)));
        applyMethodMask(row);
        refreshParamHeaders(row);
        break;
    case ColDest:
        applyMethodMask(row);
        break;
    default:
        renderParamCell(row, col - ColP0);
        break;
    }
    m_updating = false;
}

void MeshRegionDefaultsWidget::onCurrentCellChanged(int row, int, int, int)
{
    refreshParamHeaders(row);
}

// ---------------------------------------------------------------------------
// Read-back
// ---------------------------------------------------------------------------

mesh::InfilMethod MeshRegionDefaultsWidget::methodAt(int row) const
{
    QTableWidgetItem *it = m_table->item(row, ColMethod);
    if (!it) return mesh::InfilMethod::None;

    const QVariant v = it->data(kRoleValue);
    return v.isValid() ? static_cast<mesh::InfilMethod>(v.toInt())
                       : mesh::InfilMethod::None;
}

MeshRegionDefaultsWidget::RegionRow MeshRegionDefaultsWidget::rowAt(int row) const
{
    RegionRow out;
    if (row < 0 || row >= m_table->rowCount()) return out;

    auto number = [this, row](int column) {
        const QTableWidgetItem *it = m_table->item(row, column);
        const QVariant v = it ? it->data(kRoleValue) : QVariant();
        return v.isValid() ? v.toDouble() : qQNaN();
    };

    out.tag       = m_table->item(row, ColRegion)->text();
    out.manningsN = number(ColMannings);
    out.initDepth = number(ColInitDepth);

    out.infil.method = methodAt(row);
    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
        if (mesh::infilUsesParam(out.infil.method, slot))
            out.infil.p[slot] = number(ColP0 + slot);

    const QVariant d = m_table->item(row, ColDest)->data(kRoleValue);
    out.infil.dest = d.isValid() ? static_cast<mesh::InfilDest>(d.toInt())
                                 : mesh::InfilDest::Lost;
    return out;
}

QVector<MeshRegionDefaultsWidget::RegionRow> MeshRegionDefaultsWidget::rows() const
{
    QVector<RegionRow> out;
    out.reserve(m_table->rowCount());
    for (int r = 0; r < m_table->rowCount(); ++r)
        out.append(rowAt(r));
    return out;
}

QVector<mesh::InfilDefaultRow> MeshRegionDefaultsWidget::infilDefaults() const
{
    // A "None" row contributes nothing: region rows then inherit the '*' row
    // (engine D-I3), and an untouched table produces an empty vector, which is
    // what keeps InpMeshWriter from emitting a [2D_INFILTRATION_DEFAULTS]
    // section that did not exist before this table did.
    QVector<mesh::InfilDefaultRow> out;
    const QVector<RegionRow> all = rows();
    for (const RegionRow &r : all)
    {
        if (r.infil.isNone()) continue;
        mesh::InfilDefaultRow d;
        d.tag = r.tag;
        d.row = r.infil;
        out.append(d);
    }
    return out;
}

QString MeshRegionDefaultsWidget::rowLabel(int row) const
{
    if (row == kStarRow) return tr("the mesh-wide default (*)");
    return tr("region \"%1\"").arg(m_table->item(row, ColRegion)->text());
}

bool MeshRegionDefaultsWidget::validate(QString *errOut) const
{
    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        const RegionRow row = rowAt(r);
        if (row.infil.isNone()) continue;

        for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
        {
            if (!mesh::infilUsesParam(row.infil.method, slot)) continue;
            if (!std::isnan(row.infil.p[slot]))                continue;

            if (errOut)
                *errOut = tr(
                    "Initial cell values: %1 uses the %2 infiltration method, "
                    "but its \"%3\" parameter is blank.\n\n"
                    "Fill in every parameter the method needs, or set the "
                    "method back to \"%4\".")
                    .arg(rowLabel(r),
                         mesh::infilMethodLabel(row.infil.method),
                         mesh::infilParamLabel(row.infil.method, slot),
                         mesh::infilMethodLabel(mesh::InfilMethod::None));
            return false;
        }
    }
    return true;
}
