#include "ui/panels/dataobjectattributetablemodel.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
/**
 * @file      hydrographmodels.cpp
 * @brief     Slice BS Phase 6.9.2 — hydrograph MVC layer implementation.
 *
 * Each of the four models is a thin Qt view over engine state. The models
 * never cache parameter values — every data() call reads from the engine
 * via swmm_hydrograph_get / swmm_rdii_decay_get. This keeps the four
 * views (editor + property panel + Object Browser + picker combos)
 * trivially synchronized: any mutation routed through
 * SWMMModelLayer::applyHydrograph* fires hydrographChanged(), the models
 * emit modelReset() / dataChanged(), and all attached widgets repaint
 * from the (now-updated) engine state.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/hydrographmodels.h"
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_callbacks.h>

#include <QStringList>
#include <cstring>

namespace
{

// Engine model uses month = -1 for ALL, 0..11 for JAN..DEC.
/**
 * @param[in] row Response ordinal zero/one/two.
 * @return Borrowed static untranslated Short-Term/Medium-Term/Long-Term text, or empty literal for another ordinal.
 */
inline const char *responseLabel(int row)
{
    switch (row) {
    case 0:
        return "Short-Term";
    case 1:
        return "Medium-Term";
    case 2:
        return "Long-Term";
    default:
        return "";
    }
}

// Walk swmm_hydrograph_count and return the first entry index matching
// (uh, month, response), or -1 if none. The same lookup the engine impl
// does internally; replicated here so the GUI can read individual fields
// without a custom C API.
/**
 * @param[in] eng Borrowed stable engine, accessed without locking.
 * @param[in] uh Exact UTF-8 group name compared to names copied into a 64-byte buffer.
 * @param[in] month Minus one for ALL or zero through eleven; exact match, no fallback.
 * @param[in] response Zero/one/two for short/medium/long response.
 * @return First readable flat hydrograph row index matching all three keys, or minus one. Failed rows are skipped; indices can shift on later mutations.
 */
int findEntryIndex(SWMM_Engine eng, const QByteArray &uh, int month, int response)
{
    const int n = swmm_hydrograph_count(eng);
    char buf[64];
    int m = -2, r = -2;
    double r_, t_, k_, dmax_, drec_, dinit_;
    for (int i = 0; i < n; ++i) {
        if (swmm_hydrograph_get(eng, i, buf, sizeof(buf), &m, &r, &r_, &t_, &k_, &dmax_, &drec_,
                                &dinit_) != SWMM_OK)
            continue;
        if (std::strcmp(buf, uh.constData()) == 0 && m == month && r == response)
            return i;
    }
    return -1;
}

/**
 * @param[in] eng Borrowed stable engine, accessed without locking.
 * @param[in] uh Exact UTF-8 group name compared using a 64-byte name buffer.
 * @param[in] response Response ordinal zero/one/two; no month dimension exists.
 * @return First readable exact-case matching flat decay index or minus one; indices are transient across edits.
 */
int findDecayIndex(SWMM_Engine eng, const QByteArray &uh, int response)
{
    const int n = swmm_rdii_decay_count(eng);
    char buf[64];
    int r = -2;
    int snowOn = 0;
    double k_dep, k_0, k_T, T_ref, theta, T_freeze, snow_T, snow_ddf;
    for (int i = 0; i < n; ++i) {
        if (swmm_rdii_decay_get(eng, i, buf, sizeof(buf), &r, &k_dep, &k_0, &k_T, &T_ref, &theta,
                                &T_freeze, &snowOn, &snow_T, &snow_ddf) != SWMM_OK)
            continue;
        if (std::strcmp(buf, uh.constData()) == 0 && r == response)
            return i;
    }
    return -1;
}

// Snapshot of one [RDII_DECAY] row (defaults when no row exists yet —
// matches the engine struct defaults at InflowData.hpp).
struct DecayRowValues {
    double k_dep = 0.0, k_0 = 0.0, k_T = 0.0;
    double T_ref = 10.0, theta = 0.0, T_freeze = 0.0;
    bool snowOn = false;
    double snow_T = 1.0, snow_ddf = 0.0;
};

/**
 * @param[in] eng Borrowed stable engine.
 * @param[in] idx Current flat decay-row index.
 * @param[in,out] v Receives all raw numeric parameters and converted snow flag. Engine failure can leave partial numeric outputs; callers normally start from defaults.
 * @return Whether the engine getter succeeded. Does not mutate model state or convert authored units.
 */
bool readDecayRow(SWMM_Engine eng, int idx, DecayRowValues &v)
{
    char buf[64];
    int r = -2;
    int snowOn = 0;
    if (swmm_rdii_decay_get(eng, idx, buf, sizeof(buf), &r, &v.k_dep, &v.k_0, &v.k_T, &v.T_ref,
                            &v.theta, &v.T_freeze, &snowOn, &v.snow_T, &v.snow_ddf) != SWMM_OK)
        return false;
    v.snowOn = (snowOn != 0);
    return true;
}

} // namespace

// =========================================================================
// HydrographGroupListModel
// =========================================================================

HydrographGroupListModel::HydrographGroupListModel(SWMMModelLayer *layer)
    : QAbstractListModel(layer), m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged, this,
                         &HydrographGroupListModel::onHydrographChanged);
    }
}

int HydrographGroupListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_layer || !m_layer->engine())
        return 0;
    const int n = swmm_hydrograph_group_count(m_layer->engine());
    return n > 0 ? n : 0;
}

QVariant HydrographGroupListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_layer || !m_layer->engine())
        return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    char buf[64];
    if (swmm_hydrograph_group_id(m_layer->engine(), index.row(), buf, sizeof(buf)) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

bool HydrographGroupListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || !m_layer)
        return false;
    const QString oldName = data(index, Qt::DisplayRole).toString();
    const QString newName = value.toString().trimmed();
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName)
        return false;
    return m_layer->applyHydrographRenameGroup(oldName, newName);
}

Qt::ItemFlags HydrographGroupListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant HydrographGroupListModel::headerData(int section, Qt::Orientation orientation,
                                              int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal || section != 0)
        return {};
    return QObject::tr("Unit Hydrograph");
}

int HydrographGroupListModel::indexOf(const QString &name) const
{
    const int n = rowCount();
    for (int i = 0; i < n; ++i) {
        if (data(index(i), Qt::DisplayRole).toString() == name)
            return i;
    }
    return -1;
}

QString HydrographGroupListModel::nameAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return {};
    return data(index(row), Qt::DisplayRole).toString();
}

void HydrographGroupListModel::onHydrographChanged(const QString & /*uhName*/)
{
    // Any mutation can add, remove, or rename a group — the safe and cheap
    // refresh is a full modelReset.
    beginResetModel();
    endResetModel();
}

// =========================================================================
// HydrographRtkTableModel
// =========================================================================

HydrographRtkTableModel::HydrographRtkTableModel(SWMMModelLayer *layer)
    : QAbstractTableModel(layer), m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged, this,
                         &HydrographRtkTableModel::onHydrographChanged);
    }
}

int HydrographRtkTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 3;
}

int HydrographRtkTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant HydrographRtkTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col < 0 || col >= ColCount)
        return {};
    if (col == ColResponse)
        return QString::fromUtf8(responseLabel(row));

    if (!m_layer || !m_layer->engine() || m_name.isEmpty())
        return {};

    if (m_record.read) { // draft mode: the editor's record
        const QVariantMap rec = m_record.read();
        const QString p = QStringLiteral("rtk/%1/%2/").arg(m_month).arg(row);
        if (!rec.value(p + QStringLiteral("present")).toInt())
            return {}; // blank cell — distinct from explicit 0.
        return rec.value(p + QString::number(col - 1)).toDouble();
    }
    const QByteArray uh = m_name.toUtf8();
    const int idx = findEntryIndex(m_layer->engine(), uh, m_month, row);
    if (idx < 0)
        return {}; // blank cell — distinct from explicit 0.

    char buf[64];
    int m, r;
    double r_, t_, k_, dmax_, drec_, dinit_;
    if (swmm_hydrograph_get(m_layer->engine(), idx, buf, sizeof(buf), &m, &r, &r_, &t_, &k_, &dmax_,
                            &drec_, &dinit_) != SWMM_OK)
        return {};

    switch (col) {
    case ColR:
        return r_;
    case ColT:
        return t_;
    case ColK:
        return k_;
    }
    return {};
}

bool HydrographRtkTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || !m_layer || m_name.isEmpty())
        return false;
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col <= ColResponse || col >= ColCount)
        return false;

    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok)
        return false;

    // Read the current R/T/K (or 0,0,0 if no row yet), substitute the changed
    // field, and route through the upsert helper. Reading via the model's
    // own data() keeps blank-cell semantics aligned.
    auto readCell = [this, row](int c) -> double {
        const QVariant v = data(this->index(row, c), Qt::DisplayRole);
        return v.isValid() ? v.toDouble() : 0.0;
    };
    double r = readCell(ColR);
    double t = readCell(ColT);
    double k = readCell(ColK);
    if (col == ColR)
        r = v;
    else if (col == ColT)
        t = v;
    else if (col == ColK)
        k = v;

    return writeKey(QString("rtk/%1/%2/%3").arg(m_month).arg(row).arg(col-1),v);
}

Qt::ItemFlags HydrographRtkTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    if (index.column() == ColResponse)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant HydrographRtkTableModel::headerData(int section, Qt::Orientation orientation,
                                             int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColResponse:
            return QObject::tr("Response");
        case ColR:
            return QObject::tr("R");
        case ColT:
            return QObject::tr("T (h)");
        case ColK:
            return QObject::tr("K");
        }
    }
    return {};
}

void HydrographRtkTableModel::setContext(const QString &name, int month)
{
    if (m_name == name && m_month == month)
        return;
    beginResetModel();
    m_name = name;
    m_month = month;
    endResetModel();
}

void HydrographRtkTableModel::onHydrographChanged(const QString &uhName)
{
    if (!uhName.isEmpty() && uhName != m_name)
        return;
    emit dataChanged(index(0, ColR), index(2, ColK));
}

// =========================================================================
// HydrographIaTableModel
// =========================================================================

HydrographIaTableModel::HydrographIaTableModel(SWMMModelLayer *layer)
    : QAbstractTableModel(layer), m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged, this,
                         &HydrographIaTableModel::onHydrographChanged);
    }
}

int HydrographIaTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 3;
}

int HydrographIaTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant HydrographIaTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col < 0 || col >= ColCount)
        return {};
    if (col == ColResponse)
        return QString::fromUtf8(responseLabel(row));

    if (!m_layer || !m_layer->engine() || m_name.isEmpty())
        return {};

    if (m_record.read) { // draft mode: the editor's record
        const QVariantMap rec = m_record.read();
        const QString p = QStringLiteral("rtk/%1/%2/").arg(m_month).arg(row);
        if (!rec.value(p + QStringLiteral("present")).toInt())
            return {};
        return rec.value(p + QString::number(col + 2)).toDouble();
    }
    const QByteArray uh = m_name.toUtf8();
    const int idx = findEntryIndex(m_layer->engine(), uh, m_month, row);
    if (idx < 0)
        return {};

    char buf[64];
    int m, r;
    double r_, t_, k_, dmax_, drec_, dinit_;
    if (swmm_hydrograph_get(m_layer->engine(), idx, buf, sizeof(buf), &m, &r, &r_, &t_, &k_, &dmax_,
                            &drec_, &dinit_) != SWMM_OK)
        return {};

    switch (col) {
    case ColDmax:
        return dmax_;
    case ColDrec:
        return drec_;
    case ColDo:
        return dinit_;
    }
    return {};
}

bool HydrographIaTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || !m_layer || m_name.isEmpty())
        return false;
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col <= ColResponse || col >= ColCount)
        return false;

    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok)
        return false;

    auto readCell = [this, row](int c) -> double {
        const QVariant v = data(this->index(row, c), Qt::DisplayRole);
        return v.isValid() ? v.toDouble() : 0.0;
    };
    double dmax = readCell(ColDmax);
    double drecov = readCell(ColDrec);
    double dinit = readCell(ColDo);
    if (col == ColDmax)
        dmax = v;
    else if (col == ColDrec)
        drecov = v;
    else if (col == ColDo)
        dinit = v;

    return writeKey(QString("rtk/%1/%2/%3").arg(m_month).arg(row).arg(col+2),v);
}

Qt::ItemFlags HydrographIaTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    if (index.column() == ColResponse)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant HydrographIaTableModel::headerData(int section, Qt::Orientation orientation,
                                            int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColResponse:
            return QObject::tr("Response");
        case ColDmax:
            return QObject::tr("Dmax");
        case ColDrec:
            return QObject::tr("Drec");
        case ColDo:
            return QObject::tr("Do");
        }
    }
    return {};
}

void HydrographIaTableModel::setContext(const QString &name, int month)
{
    if (m_name == name && m_month == month)
        return;
    beginResetModel();
    m_name = name;
    m_month = month;
    endResetModel();
}

void HydrographIaTableModel::onHydrographChanged(const QString &uhName)
{
    if (!uhName.isEmpty() && uhName != m_name)
        return;
    emit dataChanged(index(0, ColDmax), index(2, ColDo));
}

// =========================================================================
// HydrographDecayTableModel
// =========================================================================

HydrographDecayTableModel::HydrographDecayTableModel(SWMMModelLayer *layer)
    : QAbstractTableModel(layer), m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged, this,
                         &HydrographDecayTableModel::onHydrographChanged);
    }
}

int HydrographDecayTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 3;
}

int HydrographDecayTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant HydrographDecayTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col < 0 || col >= ColCount)
        return {};

    if (col == ColResponse) {
        if (role == Qt::DisplayRole)
            return QString::fromUtf8(responseLabel(row));
        return {};
    }

    if (!m_layer || !m_layer->engine() || m_name.isEmpty())
        return {};

    if (m_record.read) { // draft mode: the editor's record
        const QVariantMap rec = m_record.read();
        const QString p = QStringLiteral("decay/%1/").arg(row);
        const bool on = rec.value(p + QStringLiteral("present")).toInt() != 0;
        const bool snow = rec.value(p + QStringLiteral("snow")).toInt() != 0;
        if (col == ColActive)
            return role == Qt::CheckStateRole ? QVariant(on ? Qt::Checked : Qt::Unchecked) : QVariant();
        if (col == ColSnow)
            return role == Qt::CheckStateRole && on ? QVariant(snow ? Qt::Checked : Qt::Unchecked)
                                                    : QVariant();
        if ((role != Qt::DisplayRole && role != Qt::EditRole) || !on)
            return {};
        if ((col == ColSnowT || col == ColSnowDdf) && !snow)
            return {};
        const QMap<int, QString> members{{ColKdep, "0"}, {ColK0, "1"},     {ColKT, "2"},
                                         {ColTref, "3"}, {ColTheta, "4"},  {ColTfreeze, "5"},
                                         {ColSnowT, "6"}, {ColSnowDdf, "7"}};
        return members.contains(col) ? QVariant(rec.value(p + members.value(col)).toDouble())
                                     : QVariant();
    }
    const QByteArray uh = m_name.toUtf8();
    const int idx = findDecayIndex(m_layer->engine(), uh, row);
    const bool active = (idx >= 0);

    if (col == ColActive) {
        if (role == Qt::CheckStateRole)
            return active ? Qt::Checked : Qt::Unchecked;
        return {};
    }

    DecayRowValues v;
    const bool haveRow = active && readDecayRow(m_layer->engine(), idx, v);

    if (col == ColSnow) {
        if (role == Qt::CheckStateRole && active)
            return (haveRow && v.snowOn) ? Qt::Checked : Qt::Unchecked;
        return {};
    }

    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (!haveRow)
        return {}; // numeric cells blank when the row is inactive.

    switch (col) {
    case ColKdep:
        return v.k_dep;
    case ColK0:
        return v.k_0;
    case ColKT:
        return v.k_T;
    case ColTref:
        return v.T_ref;
    case ColTheta:
        return v.theta;
    case ColTfreeze:
        return v.T_freeze;
    case ColSnowT:
        return v.snowOn ? QVariant(v.snow_T) : QVariant();
    case ColSnowDdf:
        return v.snowOn ? QVariant(v.snow_ddf) : QVariant();
    }
    return {};
}

bool HydrographDecayTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || !m_layer || m_name.isEmpty())
        return false;
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col <= ColResponse || col >= ColCount)
        return false;

    // Read the current row (defaults when no row exists yet — T_ref = 10,
    // snow off; matches the engine struct defaults at InflowData.hpp).
    DecayRowValues cur;
    const int idx = findDecayIndex(m_layer->engine(), m_name.toUtf8(), row);
    if (idx >= 0)
        readDecayRow(m_layer->engine(), idx, cur);

    auto apply=[&](const QString &member,const QVariant &v){return writeKey(QString("decay/%1/").arg(row)+member,v);};
    if(col==ColActive) return role==Qt::CheckStateRole && apply("present",value.toInt()==Qt::Checked?1:0);
    if(col==ColSnow)return role==Qt::CheckStateRole && apply("snow",value.toInt()==Qt::Checked?1:0);
    if (role != Qt::EditRole)
        return false;
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok)
        return false;

    const QMap<int,QString> members{{ColKdep,"0"},{ColK0,"1"},{ColKT,"2"},{ColTref,"3"},{ColTheta,"4"},{ColTfreeze,"5"},{ColSnowT,"6"},{ColSnowDdf,"7"}};
    return members.contains(col) && apply(members.value(col),v);
}

Qt::ItemFlags HydrographDecayTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    const int col = index.column();
    if (col == ColResponse)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (col == ColActive)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;

    // Numeric columns are editable only when this row's [RDII_DECAY] entry
    // exists (Active is checked) — gray them out otherwise. Existence of
    // the engine row IS the active flag. The snow numeric columns further
    // require the row's Snow checkbox to be on.
    if (m_record.read && !m_name.isEmpty()) { // draft mode: the editor's record
        const QVariantMap rec = m_record.read();
        const QString p = QStringLiteral("decay/%1/").arg(index.row());
        if (!rec.value(p + QStringLiteral("present")).toInt())
            return Qt::ItemIsSelectable;
        if (col == ColSnow)
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
        if ((col == ColSnowT || col == ColSnowDdf) && !rec.value(p + QStringLiteral("snow")).toInt())
            return Qt::ItemIsSelectable;
    } else if (m_layer && m_layer->engine() && !m_name.isEmpty()) {
        const int idx = findDecayIndex(m_layer->engine(), m_name.toUtf8(), index.row());
        if (idx < 0)
            return Qt::ItemIsSelectable;
        if (col == ColSnow)
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
        if (col == ColSnowT || col == ColSnowDdf) {
            DecayRowValues v;
            if (!readDecayRow(m_layer->engine(), idx, v) || !v.snowOn)
                return Qt::ItemIsSelectable;
        }
    } else if (col == ColSnow) {
        return Qt::ItemIsSelectable;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant HydrographDecayTableModel::headerData(int section, Qt::Orientation orientation,
                                               int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColResponse:
            return QObject::tr("Response");
        case ColActive:
            return QObject::tr("Active");
        case ColKdep:
            return QObject::tr("k_dep");
        case ColK0:
            return QObject::tr("k_0");
        case ColKT:
            return QObject::tr("k_T");
        case ColTref:
            return QObject::tr("T_ref");
        case ColTheta:
            return QObject::tr("theta_rec");
        case ColTfreeze:
            return QObject::tr("T_freeze");
        case ColSnow:
            return QObject::tr("Snow");
        case ColSnowT:
            return QObject::tr("snow_T");
        case ColSnowDdf:
            return QObject::tr("snow_ddf");
        }
    }
    return {};
}

void HydrographDecayTableModel::setContext(const QString &name)
{
    if (m_name == name)
        return;
    beginResetModel();
    m_name = name;
    endResetModel();
}

void HydrographDecayTableModel::onHydrographChanged(const QString &uhName)
{
    if (!uhName.isEmpty() && uhName != m_name)
        return;
    // Active flag transitions also affect the flags() result, so reset
    // rather than dataChanged() to force the view to re-query item flags.
    beginResetModel();
    endResetModel();
}

void HydrographRtkTableModel::setRecordAccess(HydrographRecordAccess access)
{
    beginResetModel();
    m_record = std::move(access);
    endResetModel();
}

bool HydrographRtkTableModel::writeKey(const QString &key, const QVariant &value)
{
    if (m_record.write) { // draft mode: the editor's draft takes the edit
        const bool taken = m_record.write(key, value);
        if (taken)
            emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
        return taken;
    }
    return DataObjectAttributeTableModel::applyValues(
        m_layer, SWMMModelLayer::DataHydrographs, {m_name}, key, value,
        m_layer->editCanvas() ? m_layer->editCanvas()->undoStack() : nullptr);
}

void HydrographIaTableModel::setRecordAccess(HydrographRecordAccess access)
{
    beginResetModel();
    m_record = std::move(access);
    endResetModel();
}

bool HydrographIaTableModel::writeKey(const QString &key, const QVariant &value)
{
    if (m_record.write) { // draft mode: the editor's draft takes the edit
        const bool taken = m_record.write(key, value);
        if (taken)
            emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
        return taken;
    }
    return DataObjectAttributeTableModel::applyValues(
        m_layer, SWMMModelLayer::DataHydrographs, {m_name}, key, value,
        m_layer->editCanvas() ? m_layer->editCanvas()->undoStack() : nullptr);
}

void HydrographDecayTableModel::setRecordAccess(HydrographRecordAccess access)
{
    beginResetModel();
    m_record = std::move(access);
    endResetModel();
}

bool HydrographDecayTableModel::writeKey(const QString &key, const QVariant &value)
{
    if (m_record.write) { // draft mode: the editor's draft takes the edit
        const bool taken = m_record.write(key, value);
        if (taken)
            emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
        return taken;
    }
    return DataObjectAttributeTableModel::applyValues(
        m_layer, SWMMModelLayer::DataHydrographs, {m_name}, key, value,
        m_layer->editCanvas() ? m_layer->editCanvas()->undoStack() : nullptr);
}
