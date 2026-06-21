/*!
 * \file   attributedelegates.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice Z.5.1 — Shared edit delegates used by both the Attribute
 * Table (Slice Z.1) and the Property Browser (Slice AG.3, future).
 *
 * Pure-Qt — no engine deps.  Each delegate is small enough to be a
 * single class.  The right one for a given column is picked by
 * `SWMMAttributeTableModel`'s `ColumnSpec` schema (Z.5.2) and
 * installed by `AttributeTablePanel` after `setSource()`.
 */

#ifndef ATTRIBUTEDELEGATES_H
#define ATTRIBUTEDELEGATES_H

#include <QStyledItemDelegate>
#include <QVariant>
#include <QVariantList>
#include <limits>

namespace openswmmvis {

/*! Numeric (double) editor with optional min/max/decimals. */
class NumericDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit NumericDelegate(QObject *parent = nullptr,
                              double minimum = -std::numeric_limits<double>::infinity(),
                              double maximum =  std::numeric_limits<double>::infinity(),
                              int    decimals = 4);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

private:
    double m_min;
    double m_max;
    int    m_decimals;
};

/*! Integer editor with optional min/max. */
class IntegerDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit IntegerDelegate(QObject *parent = nullptr,
                              int minimum = std::numeric_limits<int>::min(),
                              int maximum = std::numeric_limits<int>::max());

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

private:
    int m_min;
    int m_max;
};

/*! Compound-attribute editor — renders a NodeCompoundEditButton in
 *  the cell. Used by the Attribute Table for per-node multi-row
 *  attributes (Inflows / DWF / RDII / Treatment); reuses the same
 *  dialog the Property Browser opens. The model returns a
 *  NodeCompoundEditRef QVariant for these cells; displayText() shows
 *  the ref's summary so the cell stays informative when not in edit
 *  mode.
 *
 *  The metatype + QString converter need to be registered once at
 *  startup via registerNodeCompoundEditRefConverter() — the Attribute
 *  Table Panel does this in its constructor. */
class CompoundEditDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit CompoundEditDelegate(QObject *parent = nullptr);

    QString displayText(const QVariant &value,
                        const QLocale &locale) const override;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

/*! Enum editor.  The list is `{label, data}` pairs — display shows
 *  the label, the model stores the data value. */
class EnumDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /*! \param values  list of QVariantList entries, each a 2-element
     *                 [label QString, data QVariant] pair. */
    EnumDelegate(QObject *parent, QVariantList values);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

    /*! Helper: build the {label, data} pair-list from two parallel
     *  StringList / VariantList arrays.  Useful for static-table
     *  schema definitions. */
    static QVariantList makePairs(const QStringList &labels,
                                   const QVariantList &data);

private:
    QVariantList m_values;
};

/*! Editable interval editor — a QComboBox seeded with the legacy rain gage
 *  H:MM presets (0:01 … 24:00) but editable, so the user can also type a
 *  custom clock value (legacy esComboEdit). The cell value is the H:MM
 *  string; the model's setter wrappers convert to/from engine seconds. */
class IntervalDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit IntervalDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

} // namespace openswmmvis

#endif // ATTRIBUTEDELEGATES_H
