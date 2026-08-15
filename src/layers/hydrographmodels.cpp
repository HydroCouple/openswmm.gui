/*!
 * \file   hydrographmodels.cpp
 * \brief  Slice BS Phase 6.9.2 — hydrograph MVC layer implementation.
 *
 * Each of the four models is a thin Qt view over engine state. The models
 * never cache parameter values — every data() call reads from the engine
 * via swmm_hydrograph_get / swmm_rdii_decay_get. This keeps the four
 * views (editor + property panel + Object Browser + picker combos)
 * trivially synchronized: any mutation routed through
 * SWMMModelLayer::applyHydrograph* fires hydrographChanged(), the models
 * emit modelReset() / dataChanged(), and all attached widgets repaint
 * from the (now-updated) engine state.
 */

#include "layers/hydrographmodels.h"
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_callbacks.h>

#include <QStringList>
#include <cstring>

namespace {

// Engine model uses month = -1 for ALL, 0..11 for JAN..DEC.
inline const char *responseLabel(int row) {
    switch (row) {
        case 0: return "Short-Term";
        case 1: return "Medium-Term";
        case 2: return "Long-Term";
        default: return "";
    }
}

// Walk swmm_hydrograph_count and return the first entry index matching
// (uh, month, response), or -1 if none. The same lookup the engine impl
// does internally; replicated here so the GUI can read individual fields
// without a custom C API.
int findEntryIndex(SWMM_Engine eng, const QByteArray &uh,
                    int month, int response) {
    const int n = swmm_hydrograph_count(eng);
    char buf[64]; int m = -2, r = -2;
    double r_, t_, k_, dmax_, drec_, dinit_;
    for (int i = 0; i < n; ++i) {
        if (swmm_hydrograph_get(eng, i, buf, sizeof(buf), &m, &r,
                                &r_, &t_, &k_, &dmax_, &drec_, &dinit_) != SWMM_OK)
            continue;
        if (std::strcmp(buf, uh.constData()) == 0 && m == month && r == response)
            return i;
    }
    return -1;
}

int findDecayIndex(SWMM_Engine eng, const QByteArray &uh, int response) {
    const int n = swmm_rdii_decay_count(eng);
    char buf[64]; int r = -2; int snowOn = 0;
    double k_dep, k_0, k_T, T_ref, theta, T_freeze, snow_T, snow_ddf;
    for (int i = 0; i < n; ++i) {
        if (swmm_rdii_decay_get(eng, i, buf, sizeof(buf), &r,
                                &k_dep, &k_0, &k_T, &T_ref, &theta, &T_freeze,
                                &snowOn, &snow_T, &snow_ddf) != SWMM_OK)
            continue;
        if (std::strcmp(buf, uh.constData()) == 0 && r == response) return i;
    }
    return -1;
}

// Snapshot of one [RDII_DECAY] row (defaults when no row exists yet —
// matches the engine struct defaults at InflowData.hpp).
struct DecayRowValues {
    double k_dep = 0.0, k_0 = 0.0, k_T = 0.0;
    double T_ref = 10.0, theta = 0.0, T_freeze = 0.0;
    bool   snowOn = false;
    double snow_T = 1.0, snow_ddf = 0.0;
};

bool readDecayRow(SWMM_Engine eng, int idx, DecayRowValues &v) {
    char buf[64]; int r = -2; int snowOn = 0;
    if (swmm_rdii_decay_get(eng, idx, buf, sizeof(buf), &r,
                            &v.k_dep, &v.k_0, &v.k_T,
                            &v.T_ref, &v.theta, &v.T_freeze,
                            &snowOn, &v.snow_T, &v.snow_ddf) != SWMM_OK)
        return false;
    v.snowOn = (snowOn != 0);
    return true;
}

}  // namespace

// =========================================================================
// HydrographGroupListModel
// =========================================================================

HydrographGroupListModel::HydrographGroupListModel(SWMMModelLayer *layer)
    : QAbstractListModel(layer)
    , m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged,
                         this, &HydrographGroupListModel::onHydrographChanged);
    }
}

int HydrographGroupListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid() || !m_layer || !m_layer->engine()) return 0;
    const int n = swmm_hydrograph_group_count(m_layer->engine());
    return n > 0 ? n : 0;
}

QVariant HydrographGroupListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || !m_layer || !m_layer->engine()) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
    char buf[64];
    if (swmm_hydrograph_group_id(m_layer->engine(), index.row(),
                                  buf, sizeof(buf)) != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

bool HydrographGroupListModel::setData(const QModelIndex &index,
                                        const QVariant &value, int role) {
    if (!index.isValid() || role != Qt::EditRole || !m_layer) return false;
    const QString oldName = data(index, Qt::DisplayRole).toString();
    const QString newName = value.toString().trimmed();
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) return false;
    return m_layer->applyHydrographRenameGroup(oldName, newName);
}

Qt::ItemFlags HydrographGroupListModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant HydrographGroupListModel::headerData(int section, Qt::Orientation orientation,
                                                int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal || section != 0)
        return {};
    return QObject::tr("Unit Hydrograph");
}

int HydrographGroupListModel::indexOf(const QString &name) const {
    const int n = rowCount();
    for (int i = 0; i < n; ++i) {
        if (data(index(i), Qt::DisplayRole).toString() == name) return i;
    }
    return -1;
}

QString HydrographGroupListModel::nameAt(int row) const {
    if (row < 0 || row >= rowCount()) return {};
    return data(index(row), Qt::DisplayRole).toString();
}

void HydrographGroupListModel::onHydrographChanged(const QString & /*uhName*/) {
    // Any mutation can add, remove, or rename a group — the safe and cheap
    // refresh is a full modelReset.
    beginResetModel();
    endResetModel();
}

// =========================================================================
// HydrographRtkTableModel
// =========================================================================

HydrographRtkTableModel::HydrographRtkTableModel(SWMMModelLayer *layer)
    : QAbstractTableModel(layer)
    , m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged,
                         this, &HydrographRtkTableModel::onHydrographChanged);
    }
}

int HydrographRtkTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 3;
}

int HydrographRtkTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant HydrographRtkTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col < 0 || col >= ColCount) return {};
    if (col == ColResponse) return QString::fromUtf8(responseLabel(row));

    if (!m_layer || !m_layer->engine() || m_name.isEmpty()) return {};

    const QByteArray uh = m_name.toUtf8();
    const int idx = findEntryIndex(m_layer->engine(), uh, m_month, row);
    if (idx < 0) return {};  // blank cell — distinct from explicit 0.

    char buf[64]; int m, r;
    double r_, t_, k_, dmax_, drec_, dinit_;
    if (swmm_hydrograph_get(m_layer->engine(), idx, buf, sizeof(buf), &m, &r,
                            &r_, &t_, &k_, &dmax_, &drec_, &dinit_) != SWMM_OK)
        return {};

    switch (col) {
        case ColR: return r_;
        case ColT: return t_;
        case ColK: return k_;
    }
    return {};
}

bool HydrographRtkTableModel::setData(const QModelIndex &index,
                                       const QVariant &value, int role) {
    if (!index.isValid() || role != Qt::EditRole || !m_layer || m_name.isEmpty())
        return false;
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col <= ColResponse || col >= ColCount) return false;

    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok) return false;

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
    if      (col == ColR) r = v;
    else if (col == ColT) t = v;
    else if (col == ColK) k = v;

    return m_layer->applyHydrographSetRtk(m_name, m_month, row, r, t, k);
}

Qt::ItemFlags HydrographRtkTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (index.column() == ColResponse)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant HydrographRtkTableModel::headerData(int section, Qt::Orientation orientation,
                                              int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
            case ColResponse: return QObject::tr("Response");
            case ColR:        return QObject::tr("R");
            case ColT:        return QObject::tr("T (h)");
            case ColK:        return QObject::tr("K");
        }
    }
    return {};
}

void HydrographRtkTableModel::setContext(const QString &name, int month) {
    if (m_name == name && m_month == month) return;
    beginResetModel();
    m_name  = name;
    m_month = month;
    endResetModel();
}

void HydrographRtkTableModel::onHydrographChanged(const QString &uhName) {
    if (!uhName.isEmpty() && uhName != m_name) return;
    emit dataChanged(index(0, ColR), index(2, ColK));
}

// =========================================================================
// HydrographIaTableModel
// =========================================================================

HydrographIaTableModel::HydrographIaTableModel(SWMMModelLayer *layer)
    : QAbstractTableModel(layer)
    , m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged,
                         this, &HydrographIaTableModel::onHydrographChanged);
    }
}

int HydrographIaTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 3;
}

int HydrographIaTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant HydrographIaTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col < 0 || col >= ColCount) return {};
    if (col == ColResponse) return QString::fromUtf8(responseLabel(row));

    if (!m_layer || !m_layer->engine() || m_name.isEmpty()) return {};

    const QByteArray uh = m_name.toUtf8();
    const int idx = findEntryIndex(m_layer->engine(), uh, m_month, row);
    if (idx < 0) return {};

    char buf[64]; int m, r;
    double r_, t_, k_, dmax_, drec_, dinit_;
    if (swmm_hydrograph_get(m_layer->engine(), idx, buf, sizeof(buf), &m, &r,
                            &r_, &t_, &k_, &dmax_, &drec_, &dinit_) != SWMM_OK)
        return {};

    switch (col) {
        case ColDmax: return dmax_;
        case ColDrec: return drec_;
        case ColDo:   return dinit_;
    }
    return {};
}

bool HydrographIaTableModel::setData(const QModelIndex &index,
                                      const QVariant &value, int role) {
    if (!index.isValid() || role != Qt::EditRole || !m_layer || m_name.isEmpty())
        return false;
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col <= ColResponse || col >= ColCount) return false;

    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok) return false;

    auto readCell = [this, row](int c) -> double {
        const QVariant v = data(this->index(row, c), Qt::DisplayRole);
        return v.isValid() ? v.toDouble() : 0.0;
    };
    double dmax   = readCell(ColDmax);
    double drecov = readCell(ColDrec);
    double dinit  = readCell(ColDo);
    if      (col == ColDmax) dmax   = v;
    else if (col == ColDrec) drecov = v;
    else if (col == ColDo)   dinit  = v;

    return m_layer->applyHydrographSetIa(m_name, m_month, row, dmax, drecov, dinit);
}

Qt::ItemFlags HydrographIaTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (index.column() == ColResponse)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant HydrographIaTableModel::headerData(int section, Qt::Orientation orientation,
                                             int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
            case ColResponse: return QObject::tr("Response");
            case ColDmax:     return QObject::tr("Dmax");
            case ColDrec:     return QObject::tr("Drec");
            case ColDo:       return QObject::tr("Do");
        }
    }
    return {};
}

void HydrographIaTableModel::setContext(const QString &name, int month) {
    if (m_name == name && m_month == month) return;
    beginResetModel();
    m_name  = name;
    m_month = month;
    endResetModel();
}

void HydrographIaTableModel::onHydrographChanged(const QString &uhName) {
    if (!uhName.isEmpty() && uhName != m_name) return;
    emit dataChanged(index(0, ColDmax), index(2, ColDo));
}

// =========================================================================
// HydrographDecayTableModel
// =========================================================================

HydrographDecayTableModel::HydrographDecayTableModel(SWMMModelLayer *layer)
    : QAbstractTableModel(layer)
    , m_layer(layer)
{
    if (m_layer) {
        QObject::connect(m_layer, &SWMMModelLayer::hydrographChanged,
                         this, &HydrographDecayTableModel::onHydrographChanged);
    }
}

int HydrographDecayTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 3;
}

int HydrographDecayTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColCount;
}

QVariant HydrographDecayTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid()) return {};

    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col < 0 || col >= ColCount) return {};

    if (col == ColResponse) {
        if (role == Qt::DisplayRole) return QString::fromUtf8(responseLabel(row));
        return {};
    }

    if (!m_layer || !m_layer->engine() || m_name.isEmpty()) return {};

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

    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
    if (!haveRow) return {};  // numeric cells blank when the row is inactive.

    switch (col) {
        case ColKdep:    return v.k_dep;
        case ColK0:      return v.k_0;
        case ColKT:      return v.k_T;
        case ColTref:    return v.T_ref;
        case ColTheta:   return v.theta;
        case ColTfreeze: return v.T_freeze;
        case ColSnowT:   return v.snowOn ? QVariant(v.snow_T)   : QVariant();
        case ColSnowDdf: return v.snowOn ? QVariant(v.snow_ddf) : QVariant();
    }
    return {};
}

bool HydrographDecayTableModel::setData(const QModelIndex &index,
                                         const QVariant &value, int role) {
    if (!index.isValid() || !m_layer || m_name.isEmpty()) return false;
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row > 2 || col <= ColResponse || col >= ColCount) return false;

    // Read the current row (defaults when no row exists yet — T_ref = 10,
    // snow off; matches the engine struct defaults at InflowData.hpp).
    DecayRowValues cur;
    const int idx = findDecayIndex(m_layer->engine(), m_name.toUtf8(), row);
    if (idx >= 0) readDecayRow(m_layer->engine(), idx, cur);

    if (col == ColActive) {
        if (role != Qt::CheckStateRole) return false;
        const bool checked = (value.toInt() == Qt::Checked);
        if (checked) {
            // Seed defaults: T_ref = 10 deg C, others zero (matches the
            // engine struct defaults at InflowData.hpp and the editor
            // contract documented in GUI_IMPLEMENTATION_PLAN.md).
            return m_layer->applyRdiiDecaySet(m_name, row,
                                               0.0, 0.0, 0.0, 10.0, 0.0, 0.0);
        }
        return m_layer->applyRdiiDecayRemove(m_name, row);
    }

    if (col == ColSnow) {
        if (role != Qt::CheckStateRole || idx < 0) return false;
        const bool checked = (value.toInt() == Qt::Checked);
        return m_layer->applyRdiiDecaySet(m_name, row,
                                           cur.k_dep, cur.k_0, cur.k_T,
                                           cur.T_ref, cur.theta, cur.T_freeze,
                                           checked, cur.snow_T, cur.snow_ddf);
    }

    if (role != Qt::EditRole) return false;
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok) return false;

    switch (col) {
        case ColKdep:    cur.k_dep    = v; break;
        case ColK0:      cur.k_0      = v; break;
        case ColKT:      cur.k_T      = v; break;
        case ColTref:    cur.T_ref    = v; break;
        case ColTheta:   cur.theta    = v; break;
        case ColTfreeze: cur.T_freeze = v; break;
        case ColSnowT:   cur.snow_T   = v; break;
        case ColSnowDdf: cur.snow_ddf = v; break;
    }
    return m_layer->applyRdiiDecaySet(m_name, row,
                                       cur.k_dep, cur.k_0, cur.k_T,
                                       cur.T_ref, cur.theta, cur.T_freeze,
                                       cur.snowOn, cur.snow_T, cur.snow_ddf);
}

Qt::ItemFlags HydrographDecayTableModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    const int col = index.column();
    if (col == ColResponse)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (col == ColActive)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;

    // Numeric columns are editable only when this row's [RDII_DECAY] entry
    // exists (Active is checked) — grey them out otherwise. Existence of
    // the engine row IS the active flag. The snow numeric columns further
    // require the row's Snow checkbox to be on.
    if (m_layer && m_layer->engine() && !m_name.isEmpty()) {
        const int idx = findDecayIndex(m_layer->engine(),
                                        m_name.toUtf8(), index.row());
        if (idx < 0) return Qt::ItemIsSelectable;
        if (col == ColSnow)
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                   Qt::ItemIsUserCheckable;
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
                                                int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
            case ColResponse: return QObject::tr("Response");
            case ColActive:   return QObject::tr("Active");
            case ColKdep:     return QObject::tr("k_dep");
            case ColK0:       return QObject::tr("k_0");
            case ColKT:       return QObject::tr("k_T");
            case ColTref:     return QObject::tr("T_ref");
            case ColTheta:    return QObject::tr("theta_rec");
            case ColTfreeze:  return QObject::tr("T_freeze");
            case ColSnow:     return QObject::tr("Snow");
            case ColSnowT:    return QObject::tr("snow_T");
            case ColSnowDdf:  return QObject::tr("snow_ddf");
        }
    }
    return {};
}

void HydrographDecayTableModel::setContext(const QString &name) {
    if (m_name == name) return;
    beginResetModel();
    m_name = name;
    endResetModel();
}

void HydrographDecayTableModel::onHydrographChanged(const QString &uhName) {
    if (!uhName.isEmpty() && uhName != m_name) return;
    // Active flag transitions also affect the flags() result, so reset
    // rather than dataChanged() to force the view to re-query item flags.
    beginResetModel();
    endResetModel();
}
