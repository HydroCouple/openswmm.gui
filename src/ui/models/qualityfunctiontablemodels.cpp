#include "ui/panels/dataobjectattributetablemodel.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "pollutant/pollutantregistry.h"
/**
 * @file      qualityfunctiontablemodels.cpp
 * @brief     Washoff tabs (iteration 4).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/qualityfunctiontablemodels.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_reactions.h>

#include <QComboBox>

namespace openswmmvis::ui
{

namespace
{
/**
 * @brief Cast a borrowed opaque model handle to the engine API handle type.
 * @param[in] h Handle, including null.
 * @return The same handle without validation or ownership transfer.
 */
inline SWMM_Engine eng(void *h)
{
    return static_cast<SWMM_Engine>(h);
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Base
// ─────────────────────────────────────────────────────────────────────────────

void QualityFunctionTableModel::setProjectContext(SWMMModelLayer *layer){m_layer=layer;}
void QualityFunctionTableModel::setRecordAccess(std::function<QVariantMap()> read,
                                                std::function<bool(const QString &, const QVariant &)> write)
{
    beginResetModel();
    m_readRecord = std::move(read);
    m_writeRecord = std::move(write);
    endResetModel();
}
void QualityFunctionTableModel::recordChanged()
{
    if (m_rows > 0)
        emit dataChanged(index(0, 0), index(m_rows - 1, columnCount() - 1));
}
QString QualityFunctionTableModel::constituentKey(int row) const
{
    if (!m_layer)
        return {};
    if (row < m_npollut) {
        auto *reg = qobject_cast<openswmmvis::pollutant::PollutantRegistry *>(m_layer->ensurePollutantRegistry());
        auto *p = reg ? reg->findByName(pollutantName(row)) : nullptr;
        return p ? "p/" + QString::number(reinterpret_cast<quintptr>(p), 16) : QString();
    }
    return "s/" + QString::fromLatin1(pollutantName(row).toUtf8().toPercentEncoding());
}
bool QualityFunctionTableModel::applyShared(const QString &group,int row,const QString &member,const QVariant &value) {
    if(!m_layer)return false;const QString token=constituentKey(row);if(token.isEmpty())return false;
    if (m_writeRecord) { // draft mode: the editor's draft takes the edit
        const bool taken = m_writeRecord(group + "/" + token + "/" + member, value);
        if (taken)
            emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        return taken;
    }
    const auto name=QString::fromUtf8(swmm_landuse_id(eng(m_engine),m_luIndex));
    return DataObjectAttributeTableModel::applyValues(m_layer,SWMMModelLayer::DataLandUses,{name},group+"/"+token+"/"+member,value,m_layer->editCanvas()?m_layer->editCanvas()->undoStack():nullptr);
}

void QualityFunctionTableModel::bind(void *engineHandle, int luIndex)
{
    beginResetModel();
    m_engine = engineHandle;
    m_luIndex = luIndex;
    m_species.clear();
    m_npollut = 0;
    if (m_engine && m_luIndex >= 0) {
        m_npollut = qMax(0, swmm_pollutant_count(eng(m_engine)));
        // Multispecies Phase 2: bulk reaction species build up and wash off
        // like pollutants; wall species cannot and are not listed.
        const int nm = swmm_reaction_species_count(eng(m_engine));
        for (int m = 0; m < nm; ++m) {
            char name[128], units[32];
            int is_wall = 0;
            double atol = 0, rtol = 0;
            if (swmm_reaction_species_get(eng(m_engine), m, name, int(sizeof name), &is_wall, units,
                                          int(sizeof units), &atol, &rtol) == SWMM_OK &&
                !is_wall)
                m_species.push_back(m);
        }
    }
    m_rows = m_npollut + int(m_species.size());
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
    if (!m_engine)
        return {};
    if (row >= m_npollut) {
        const int k = row - m_npollut;
        if (k < 0 || k >= m_species.size())
            return {};
        char name[128], units[32];
        int is_wall = 0;
        double atol = 0, rtol = 0;
        if (swmm_reaction_species_get(eng(m_engine), m_species[k], name, int(sizeof name), &is_wall,
                                      units, int(sizeof units), &atol, &rtol) != SWMM_OK)
            return {};
        return QString::fromUtf8(name);
    }
    const char *raw = swmm_pollutant_id(eng(m_engine), row);
    return raw ? QString::fromUtf8(raw) : QString();
}

int QualityFunctionTableModel::constituentIndex(int row) const
{
    if (row < m_npollut)
        return row;
    const int k = row - m_npollut;
    return (k >= 0 && k < m_species.size()) ? m_npollut + m_species[k] : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Buildup
// ─────────────────────────────────────────────────────────────────────────────

void BuildupTableModel::readBuildup_(int row, int &ft, double &c1, double &c2, double &c3, int &norm,
                                     QString &series) const
{
    if (m_readRecord) { // draft mode: the editor's record
        const QVariantMap r = m_readRecord();
        const QString p = "buildup/" + constituentKey(row) + "/";
        ft = r.value(p + "type").toInt();
        c1 = r.value(p + "0").toDouble();
        c2 = r.value(p + "1").toDouble();
        c3 = r.value(p + "2").toDouble();
        norm = r.value(p + "normalizer").toInt();
        series = r.value(p + "series").toString();
        return;
    }
    swmm_buildup_get(eng(m_engine), m_luIndex, constituentIndex(row), &ft, &c1, &c2, &c3, &norm);
}

QStringList BuildupTableModel::functionNames()
{
    return {QStringLiteral("NONE"), QStringLiteral("POW"), QStringLiteral("EXP"),
            QStringLiteral("SAT"), QStringLiteral("EXT")};
}

QStringList BuildupTableModel::normalizerNames()
{
    return {QStringLiteral("AREA"), QStringLiteral("CURB")};
}

int BuildupTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant BuildupTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
    case ColPollutant:
        return tr("Pollutant");
    case ColFunction:
        return tr("Function");
    case ColC1:
        return tr("Max/C1");
    case ColC2:
        return tr("Rate/C2");
    case ColC3:
        return tr("Power/Sat/C3");
    case ColNormalizer:
        return tr("Per Unit");
    }
    return {};
}

QVariant BuildupTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_engine || m_luIndex < 0)
        return {};

    int ft = 0, norm = 0;
    double c1 = 0, c2 = 0, c3 = 0;
    QString series;
    readBuildup_(index.row(), ft, c1, c2, c3, norm, series);

    if (role == EnumComboDelegate::OptionsRole) {
        if (index.column() == ColFunction)
            return functionNames();
        if (index.column() == ColNormalizer)
            return normalizerNames();
        return {};
    }
    if (role == Qt::ToolTipRole && index.column() == ColC3 && ft == 4)
        return tr("EXT buildup: C3 references a time series (stored as a "
                  "table index) — edit it in the .inp, not as a number.");
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    switch (index.column()) {
    case ColPollutant:
        return pollutantName(index.row());
    case ColFunction:
        return functionNames().value(ft);
    case ColC1:
        return c1;
    case ColC2:
        return c2;
    case ColC3:
        if (ft == 4 && role == Qt::DisplayRole)
            return m_readRecord ? tr("(series %1)").arg(series) : tr("(series #%1)").arg(int(c3));
        return c3;
    case ColNormalizer:
        return normalizerNames().value(norm);
    }
    return {};
}

bool BuildupTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || !m_engine || m_luIndex < 0)
        return false;

    int ft = 0, norm = 0;
    double c1 = 0, c2 = 0, c3 = 0;
    QString series;
    readBuildup_(index.row(), ft, c1, c2, c3, norm, series);

    switch (index.column()) {
    case ColFunction: {
        const int nf = int(functionNames().indexOf(value.toString()));
        if (nf < 0)
            return false;
        ft = nf;
        break;
    }
    case ColC1:
        c1 = value.toDouble();
        break;
    case ColC2:
        c2 = value.toDouble();
        break;
    case ColC3:
        if (ft == 4)
            return false; // EXT: table index, never a number
        c3 = value.toDouble();
        break;
    case ColNormalizer: {
        const int nn = int(normalizerNames().indexOf(value.toString()));
        if (nn < 0)
            return false;
        norm = nn;
        break;
    }
    default:
        return false;
    }

    if(m_layer) {
        const QStringList members{"","type","0","1","2","normalizer"};
        QVariant v=value;if(index.column()==ColFunction)v=ft;if(index.column()==ColNormalizer)v=norm;
        return applyShared("buildup",index.row(),members.value(index.column()),v);
    }
    if (swmm_buildup_set(eng(m_engine), m_luIndex, constituentIndex(index.row()), ft, c1, c2, c3,
                         norm) != SWMM_OK)
        return false;
    emit dataChanged(this->index(index.row(), ColFunction),
                     this->index(index.row(), ColNormalizer));
    return true;
}

Qt::ItemFlags BuildupTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return f;
    if (index.column() == ColPollutant)
        return f; // identity column
    if (index.column() == ColC3 && m_engine && m_luIndex >= 0) {
        int ft = 0, norm = 0;
        double c1 = 0, c2 = 0, c3 = 0;
        QString series;
        readBuildup_(index.row(), ft, c1, c2, c3, norm, series);
        if (ft == 4)
            return f; // EXT guard: read-only
    }
    return f | Qt::ItemIsEditable;
}

// ─────────────────────────────────────────────────────────────────────────────
// Washoff
// ─────────────────────────────────────────────────────────────────────────────

void WashoffTableModel::readWashoff_(int row, int &ft, double &coeff, double &expon, double &sweep,
                                     double &bmp) const
{
    if (m_readRecord) { // draft mode: the editor's record
        const QVariantMap r = m_readRecord();
        const QString p = "washoff/" + constituentKey(row) + "/";
        ft = r.value(p + "type").toInt();
        coeff = r.value(p + "0").toDouble();
        expon = r.value(p + "1").toDouble();
        sweep = r.value(p + "2").toDouble();
        bmp = r.value(p + "3").toDouble();
        return;
    }
    swmm_washoff_get(eng(m_engine), m_luIndex, constituentIndex(row), &ft, &coeff, &expon, &sweep,
                     &bmp);
}

QStringList WashoffTableModel::functionNames()
{
    return {QStringLiteral("NONE"), QStringLiteral("EXP"), QStringLiteral("RC"),
            QStringLiteral("EMC")};
}

int WashoffTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant WashoffTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section) {
    case ColPollutant:
        return tr("Pollutant");
    case ColFunction:
        return tr("Function");
    case ColCoeff:
        return tr("Coefficient");
    case ColExponent:
        return tr("Exponent");
    case ColSweepEffic:
        return tr("Sweep Effic (%)");
    case ColBmpEffic:
        return tr("BMP Effic (%)");
    }
    return {};
}

QVariant WashoffTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_engine || m_luIndex < 0)
        return {};

    if (role == EnumComboDelegate::OptionsRole)
        return index.column() == ColFunction ? QVariant(functionNames()) : QVariant();
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    int ft = 0;
    double coeff = 0, expon = 0, sweep = 0, bmp = 0;
    readWashoff_(index.row(), ft, coeff, expon, sweep, bmp);
    switch (index.column()) {
    case ColPollutant:
        return pollutantName(index.row());
    case ColFunction:
        return functionNames().value(ft);
    case ColCoeff:
        return coeff;
    case ColExponent:
        return expon;
    case ColSweepEffic:
        return sweep;
    case ColBmpEffic:
        return bmp;
    }
    return {};
}

bool WashoffTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || !m_engine || m_luIndex < 0)
        return false;

    int ft = 0;
    double coeff = 0, expon = 0, sweep = 0, bmp = 0;
    readWashoff_(index.row(), ft, coeff, expon, sweep, bmp);

    switch (index.column()) {
    case ColFunction: {
        const int nf = int(functionNames().indexOf(value.toString()));
        if (nf < 0)
            return false;
        ft = nf;
        break;
    }
    case ColCoeff:
        coeff = value.toDouble();
        break;
    case ColExponent:
        expon = value.toDouble();
        break;
    case ColSweepEffic:
        sweep = value.toDouble();
        break;
    case ColBmpEffic:
        bmp = value.toDouble();
        break;
    default:
        return false;
    }

    if(m_layer) {
        const QStringList members{"","type","0","1","2","3"};QVariant v=value;if(index.column()==ColFunction)v=ft;
        return applyShared("washoff",index.row(),members.value(index.column()),v);
    }
    if (swmm_washoff_set(eng(m_engine), m_luIndex, constituentIndex(index.row()), ft, coeff, expon,
                         sweep, bmp) != SWMM_OK)
        return false;
    emit dataChanged(this->index(index.row(), ColFunction), this->index(index.row(), ColBmpEffic));
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

QWidget *EnumComboDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    const QStringList options = index.data(OptionsRole).toStringList();
    if (options.isEmpty())
        return QStyledItemDelegate::createEditor(parent, option, index);
    auto *combo = new QComboBox(parent);
    combo->addItems(options);
    return combo;
}

void EnumComboDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
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
