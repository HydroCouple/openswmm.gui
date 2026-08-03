/*!
 * \file   qualityfunctiontablemodels.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/qualityfunctiontablemodels.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>

#include <QComboBox>

namespace openswmmvis::ui {

namespace {
inline SWMM_Engine eng(void *h) { return static_cast<SWMM_Engine>(h); }
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Base
// ─────────────────────────────────────────────────────────────────────────────

void QualityFunctionTableModel::bind(void *engineHandle, int luIndex)
{
    beginResetModel();
    m_engine  = engineHandle;
    m_luIndex = luIndex;
    m_rows = (m_engine && m_luIndex >= 0)
                 ? qMax(0, swmm_pollutant_count(eng(m_engine)))
                 : 0;
    endResetModel();
}

void QualityFunctionTableModel::refresh()
{
    bind(m_engine, m_luIndex);
}

int QualityFunctionTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows;
}

QString QualityFunctionTableModel::pollutantName(int row) const
{
    if (!m_engine) return {};
    const char *raw = swmm_pollutant_id(eng(m_engine), row);
    return raw ? QString::fromUtf8(raw) : QString();
}

// ─────────────────────────────────────────────────────────────────────────────
// Buildup
// ─────────────────────────────────────────────────────────────────────────────

QStringList BuildupTableModel::functionNames()
{
    return { QStringLiteral("NONE"), QStringLiteral("POW"),
             QStringLiteral("EXP"), QStringLiteral("SAT"),
             QStringLiteral("EXT") };
}

QStringList BuildupTableModel::normalizerNames()
{
    return { QStringLiteral("AREA"), QStringLiteral("CURB") };
}

int BuildupTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant BuildupTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
        case ColPollutant:  return tr("Pollutant");
        case ColFunction:   return tr("Function");
        case ColC1:         return tr("Max/C1");
        case ColC2:         return tr("Rate/C2");
        case ColC3:         return tr("Power/Sat/C3");
        case ColNormalizer: return tr("Per Unit");
    }
    return {};
}

QVariant BuildupTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_engine || m_luIndex < 0) return {};

    int ft = 0, norm = 0;
    double c1 = 0, c2 = 0, c3 = 0;
    swmm_buildup_get(eng(m_engine), m_luIndex, index.row(),
                     &ft, &c1, &c2, &c3, &norm);

    if (role == EnumComboDelegate::OptionsRole) {
        if (index.column() == ColFunction)   return functionNames();
        if (index.column() == ColNormalizer) return normalizerNames();
        return {};
    }
    if (role == Qt::ToolTipRole && index.column() == ColC3 && ft == 4)
        return tr("EXT buildup: C3 references a time series (stored as a "
                  "table index) — edit it in the .inp, not as a number.");
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    switch (index.column()) {
        case ColPollutant:  return pollutantName(index.row());
        case ColFunction:   return functionNames().value(ft);
        case ColC1:         return c1;
        case ColC2:         return c2;
        case ColC3:
            return (ft == 4 && role == Qt::DisplayRole)
                       ? QVariant(tr("(series #%1)").arg(int(c3)))
                       : QVariant(c3);
        case ColNormalizer: return normalizerNames().value(norm);
    }
    return {};
}

bool BuildupTableModel::setData(const QModelIndex &index, const QVariant &value,
                                int role)
{
    if (!index.isValid() || role != Qt::EditRole || !m_engine || m_luIndex < 0)
        return false;

    int ft = 0, norm = 0;
    double c1 = 0, c2 = 0, c3 = 0;
    swmm_buildup_get(eng(m_engine), m_luIndex, index.row(),
                     &ft, &c1, &c2, &c3, &norm);

    switch (index.column()) {
        case ColFunction: {
            const int nf = int(functionNames().indexOf(value.toString()));
            if (nf < 0) return false;
            ft = nf;
            break;
        }
        case ColC1: c1 = value.toDouble(); break;
        case ColC2: c2 = value.toDouble(); break;
        case ColC3:
            if (ft == 4) return false;   // EXT: table index, never a number
            c3 = value.toDouble();
            break;
        case ColNormalizer: {
            const int nn = int(normalizerNames().indexOf(value.toString()));
            if (nn < 0) return false;
            norm = nn;
            break;
        }
        default: return false;
    }

    if (swmm_buildup_set(eng(m_engine), m_luIndex, index.row(),
                         ft, c1, c2, c3, norm) != SWMM_OK)
        return false;
    emit dataChanged(this->index(index.row(), ColFunction),
                     this->index(index.row(), ColNormalizer));
    return true;
}

Qt::ItemFlags BuildupTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid()) return f;
    if (index.column() == ColPollutant) return f;   // identity column
    if (index.column() == ColC3 && m_engine && m_luIndex >= 0) {
        int ft = 0, norm = 0;
        double c1 = 0, c2 = 0, c3 = 0;
        swmm_buildup_get(eng(m_engine), m_luIndex, index.row(),
                         &ft, &c1, &c2, &c3, &norm);
        if (ft == 4) return f;                       // EXT guard: read-only
    }
    return f | Qt::ItemIsEditable;
}

// ─────────────────────────────────────────────────────────────────────────────
// Washoff
// ─────────────────────────────────────────────────────────────────────────────

QStringList WashoffTableModel::functionNames()
{
    return { QStringLiteral("NONE"), QStringLiteral("EXP"),
             QStringLiteral("RC"), QStringLiteral("EMC") };
}

int WashoffTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant WashoffTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
        case ColPollutant:  return tr("Pollutant");
        case ColFunction:   return tr("Function");
        case ColCoeff:      return tr("Coefficient");
        case ColExponent:   return tr("Exponent");
        case ColSweepEffic: return tr("Sweep Effic (%)");
        case ColBmpEffic:   return tr("BMP Effic (%)");
    }
    return {};
}

QVariant WashoffTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_engine || m_luIndex < 0) return {};

    if (role == EnumComboDelegate::OptionsRole)
        return index.column() == ColFunction ? QVariant(functionNames())
                                             : QVariant();
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    int ft = 0;
    double coeff = 0, expon = 0, sweep = 0, bmp = 0;
    swmm_washoff_get(eng(m_engine), m_luIndex, index.row(),
                     &ft, &coeff, &expon, &sweep, &bmp);
    switch (index.column()) {
        case ColPollutant:  return pollutantName(index.row());
        case ColFunction:   return functionNames().value(ft);
        case ColCoeff:      return coeff;
        case ColExponent:   return expon;
        case ColSweepEffic: return sweep;
        case ColBmpEffic:   return bmp;
    }
    return {};
}

bool WashoffTableModel::setData(const QModelIndex &index, const QVariant &value,
                                int role)
{
    if (!index.isValid() || role != Qt::EditRole || !m_engine || m_luIndex < 0)
        return false;

    int ft = 0;
    double coeff = 0, expon = 0, sweep = 0, bmp = 0;
    swmm_washoff_get(eng(m_engine), m_luIndex, index.row(),
                     &ft, &coeff, &expon, &sweep, &bmp);

    switch (index.column()) {
        case ColFunction: {
            const int nf = int(functionNames().indexOf(value.toString()));
            if (nf < 0) return false;
            ft = nf;
            break;
        }
        case ColCoeff:      coeff = value.toDouble(); break;
        case ColExponent:   expon = value.toDouble(); break;
        case ColSweepEffic: sweep = value.toDouble(); break;
        case ColBmpEffic:   bmp   = value.toDouble(); break;
        default: return false;
    }

    if (swmm_washoff_set(eng(m_engine), m_luIndex, index.row(),
                         ft, coeff, expon, sweep, bmp) != SWMM_OK)
        return false;
    emit dataChanged(this->index(index.row(), ColFunction),
                     this->index(index.row(), ColBmpEffic));
    return true;
}

Qt::ItemFlags WashoffTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() != ColPollutant)
        f |= Qt::ItemIsEditable;
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Enum combo delegate
// ─────────────────────────────────────────────────────────────────────────────

QWidget *EnumComboDelegate::createEditor(QWidget *parent,
                                         const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    const QStringList options = index.data(OptionsRole).toStringList();
    if (options.isEmpty())
        return QStyledItemDelegate::createEditor(parent, option, index);
    auto *combo = new QComboBox(parent);
    combo->addItems(options);
    return combo;
}

void EnumComboDelegate::setEditorData(QWidget *editor,
                                      const QModelIndex &index) const
{
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        combo->setCurrentText(index.data(Qt::EditRole).toString());
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void EnumComboDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                     const QModelIndex &index) const
{
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        model->setData(index, combo->currentText(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

} // namespace openswmmvis::ui
