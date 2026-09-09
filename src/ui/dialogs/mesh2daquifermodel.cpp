/*!
 * \file   mesh2daquifermodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/mesh2daquifermodel.h"

#include <openswmm/engine/openswmm_gw2d.h>

#include <QBrush>
#include <QColor>

#include <cmath>

namespace openswmmvis::ui {

namespace {

//! Optional row properties, by the key the C API takes.
constexpr const char *kKeyHg0    = "HG0";
constexpr const char *kKeyCLoss  = "C_LOSS";
constexpr const char *kKeyPsiB   = "PSI_B";
constexpr const char *kKeyLambda = "LAMBDA";
constexpr const char *kKeyN      = "N";
constexpr const char *kKeyL      = "L";
constexpr const char *kKeySoil   = "SOIL_CHAR";
constexpr const char *kKeyClosr  = "CLOSURE";
constexpr const char *kKeyLayers = "M_LAYERS";

double getProp(SWMM_Engine e, int row, const char *key, double fallback)
{
    double v = fallback;
    if (swmm_gw2d_row_get_property(e, row, key, &v) != SWMM_OK) return fallback;
    return v;
}

} // namespace

// ===========================================================================
// Mesh2DAquiferModel
// ===========================================================================

bool Mesh2DAquiferModel::Row::operator==(const Row &o) const
{
    return scope == o.scope && tag == o.tag && cell == o.cell &&
           ks == o.ks && zs == o.zs && thetaS == o.thetaS &&
           thetaR == o.thetaR && alpha == o.alpha &&
           soil == o.soil && soilSet == o.soilSet &&
           closure == o.closure && closureSet == o.closureSet &&
           hg0 == o.hg0 && cLoss == o.cLoss && psiB == o.psiB &&
           lambda == o.lambda && vgN == o.vgN && vgL == o.vgL &&
           mLayers == o.mLayers;
}

Mesh2DAquiferModel::Mesh2DAquiferModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QStringList Mesh2DAquiferModel::soilTokens()
{
    // ORDER IS THE ENUM. Index == SWMM_GW2D_SOIL_*; reordering these silently
    // reassigns every authored row's soil law.
    return {QStringLiteral("RUSSO"), QStringLiteral("GARDNER"),
            QStringLiteral("BROOKS_COREY"), QStringLiteral("VAN_GENUCHTEN")};
}

QStringList Mesh2DAquiferModel::closureTokens()
{
    return {QStringLiteral("AUTO"), QStringLiteral("CLOSED_FORM"),
            QStringLiteral("ENSLAVED"), QStringLiteral("SIGMA")};
}

int Mesh2DAquiferModel::closureCodeAt(int index)
{
    switch (index) {
    case 1:  return SWMM_GW2D_CLOSURE_CLOSED_FORM;
    case 2:  return SWMM_GW2D_CLOSURE_ENSLAVED;
    case 3:  return SWMM_GW2D_CLOSURE_SIGMA;
    default: return SWMM_GW2D_CLOSURE_AUTO;
    }
}

int Mesh2DAquiferModel::closureIndexOf(int code)
{
    switch (code) {
    case SWMM_GW2D_CLOSURE_CLOSED_FORM: return 1;
    case SWMM_GW2D_CLOSURE_ENSLAVED:    return 2;
    case SWMM_GW2D_CLOSURE_SIGMA:       return 3;
    default:                            return 0;
    }
}

int Mesh2DAquiferModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int Mesh2DAquiferModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant Mesh2DAquiferModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows.at(index.row());

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case ColScope:
            return tr("Which cells this row applies to. Precedence is "
                      "* < TAG < CELL, and within one scope the later row "
                      "wins — the same rule the 2D infiltration rows use.");
        case ColKs:
            return tr("Saturated hydraulic conductivity, in %1.")
                       .arg(m_rateLabel);
        case ColZs:
            return tr("Soil column thickness from the aquifer base to the "
                      "ground surface, in %1.").arg(m_lengthLabel);
        case ColThetaR:
            return tr("Residual water content. Must be below the porosity: "
                      "the difference is the drainable pore space, and it is "
                      "what sets how far the table moves per unit of "
                      "recharge.");
        case ColAlpha:
            return tr("Sorptive number, in 1/%1. With the column thickness "
                      "it forms αL, the group the AUTO closure reads: below "
                      "1 the column is enslaved to the table, above 5 it "
                      "needs the σ column.").arg(m_lengthLabel);
        case ColClosure:
            return tr("AUTO picks per cell from αL. CLOSED_FORM carries one "
                      "bulk unsaturated store; ENSLAVED drops it entirely; "
                      "SIGMA integrates a real column and is the one to "
                      "trust when the answer matters.");
        case ColHg0:
            return tr("Initial saturated thickness above the aquifer base, "
                      "in %1. Blank uses the model default.").arg(m_lengthLabel);
        case ColCLoss:
            return tr("Deep percolation out of the model at full "
                      "saturation, in %1. This water leaves the continuity "
                      "balance.").arg(m_rateLabel);
        default: break;
        }
        return {};
    }

    if (role == Qt::ForegroundRole && index.column() == ColTarget &&
        r.scope == SWMM_GW2D_SCOPE_GLOBAL)
        return QBrush(QColor(128, 128, 128));

    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    switch (index.column()) {
    case ColScope:
        if (role == Qt::EditRole) return r.scope;
        switch (r.scope) {
        case SWMM_GW2D_SCOPE_TAG:  return tr("Tag");
        case SWMM_GW2D_SCOPE_CELL: return tr("Cell");
        default:                   return tr("All cells");
        }
    case ColTarget:
        if (r.scope == SWMM_GW2D_SCOPE_TAG) return r.tag;
        if (r.scope == SWMM_GW2D_SCOPE_CELL) return r.cell + 1;   // 1-based
        return role == Qt::EditRole ? QVariant() : QVariant(QStringLiteral("—"));
    case ColKs:     return r.ks;
    case ColZs:     return r.zs;
    case ColThetaS: return r.thetaS;
    case ColThetaR: return r.thetaR;
    case ColAlpha:  return r.alpha;
    case ColSoil:
        if (role == Qt::EditRole) return r.soil;
        return r.soilSet ? soilTokens().value(r.soil) : tr("(model default)");
    case ColClosure:
        if (role == Qt::EditRole) return closureIndexOf(r.closure);
        return r.closureSet ? closureTokens().value(closureIndexOf(r.closure))
                            : tr("(model default)");
    case ColHg0:
        if (r.hg0 < 0.0)
            return role == Qt::EditRole ? QVariant()
                                        : QVariant(tr("(default)"));
        return r.hg0;
    case ColCLoss:  return r.cLoss;
    default: break;
    }
    return {};
}

bool Mesh2DAquiferModel::setData(const QModelIndex &index,
                                 const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() ||
        index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    bool ok = true;

    switch (index.column()) {
    case ColScope: {
        const int s = value.toInt(&ok);
        if (!ok || s < 0 || s > 2) return false;
        r.scope = s;
        // Leave the tag and the cell as they were: a user flipping between
        // TAG and CELL to compare should not lose the other target.
        emit dataChanged(index.sibling(index.row(), ColScope),
                         index.sibling(index.row(), ColTarget));
        return true;
    }
    case ColTarget:
        if (r.scope == SWMM_GW2D_SCOPE_TAG) {
            const QString t = value.toString().trimmed();
            if (t.isEmpty()) return false;
            r.tag = t;
        } else if (r.scope == SWMM_GW2D_SCOPE_CELL) {
            const int c = value.toInt(&ok);
            if (!ok || c < 1) return false;   // 1-based in the UI and the file
            r.cell = c - 1;
        } else {
            return false;
        }
        break;
    case ColKs: {
        const double v = value.toDouble(&ok);
        if (!ok || !(v > 0.0)) return false;
        r.ks = v;
        break;
    }
    case ColZs: {
        const double v = value.toDouble(&ok);
        if (!ok || !(v > 0.0)) return false;
        r.zs = v;
        break;
    }
    case ColThetaS: {
        const double v = value.toDouble(&ok);
        // Enforce the parser's rule HERE, not at commit: a table that accepts
        // a value the engine will refuse hands the user an error with no cell
        // attached to it.
        if (!ok || !(v > 0.0) || v > 1.0 || v <= r.thetaR) return false;
        r.thetaS = v;
        break;
    }
    case ColThetaR: {
        const double v = value.toDouble(&ok);
        if (!ok || v < 0.0 || v >= r.thetaS) return false;
        r.thetaR = v;
        break;
    }
    case ColAlpha: {
        const double v = value.toDouble(&ok);
        if (!ok || !(v > 0.0)) return false;
        r.alpha = v;
        break;
    }
    case ColSoil: {
        const int s = value.toInt(&ok);
        if (!ok || s < 0 || s >= soilTokens().size()) return false;
        r.soil = s;
        r.soilSet = true;
        break;
    }
    case ColClosure: {
        const int i = value.toInt(&ok);
        if (!ok || i < 0 || i >= closureTokens().size()) return false;
        r.closure = closureCodeAt(i);
        r.closureSet = true;
        break;
    }
    case ColHg0: {
        if (value.toString().trimmed().isEmpty()) { r.hg0 = -1.0; break; }
        const double v = value.toDouble(&ok);
        if (!ok || v < 0.0) return false;
        r.hg0 = v;
        break;
    }
    case ColCLoss: {
        const double v = value.toDouble(&ok);
        if (!ok || v < 0.0) return false;
        r.cLoss = v;
        break;
    }
    default:
        return false;
    }
    emit dataChanged(index, index);
    return true;
}

QVariant Mesh2DAquiferModel::headerData(int section, Qt::Orientation orientation,
                                        int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    switch (section) {
    case ColScope:   return tr("Apply to");
    case ColTarget:  return tr("Tag / Cell");
    case ColKs:      return tr("Ks (%1)").arg(m_rateLabel);
    case ColZs:      return tr("zs (%1)").arg(m_lengthLabel);
    case ColThetaS:  return tr("Porosity θs");
    case ColThetaR:  return tr("Residual θr");
    case ColAlpha:   return tr("α (1/%1)").arg(m_lengthLabel);
    case ColSoil:    return tr("Soil law");
    case ColClosure: return tr("Closure");
    case ColHg0:     return tr("Initial hg (%1)").arg(m_lengthLabel);
    case ColCLoss:   return tr("Deep loss (%1)").arg(m_rateLabel);
    default: break;
    }
    return {};
}

Qt::ItemFlags Mesh2DAquiferModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColTarget &&
        m_rows.at(index.row()).scope == SWMM_GW2D_SCOPE_GLOBAL)
        return f;   // a '*' row has no target to edit
    return f | Qt::ItemIsEditable;
}

bool Mesh2DAquiferModel::removeRows(int row, int count,
                                    const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 ||
        row + count > m_rows.size())
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
    endRemoveRows();
    return true;
}

void Mesh2DAquiferModel::load(SWMM_Engine engine)
{
    beginResetModel();
    m_rows.clear();
    if (engine) {
        int n = 0;
        if (swmm_gw2d_row_count(engine, &n) == SWMM_OK) {
            for (int i = 0; i < n; ++i) {
                Row r;
                char tag[128] = {0};
                if (swmm_gw2d_row_get(engine, i, &r.scope, tag, sizeof tag,
                                      &r.cell, &r.ks, &r.zs, &r.thetaS,
                                      &r.thetaR, &r.alpha) != SWMM_OK)
                    continue;
                r.tag     = QString::fromUtf8(tag);
                r.hg0     = getProp(engine, i, kKeyHg0,    -1.0);
                r.cLoss   = getProp(engine, i, kKeyCLoss,   0.0);
                r.psiB    = getProp(engine, i, kKeyPsiB,    0.20);
                r.lambda  = getProp(engine, i, kKeyLambda,  0.40);
                r.vgN     = getProp(engine, i, kKeyN,       1.6);
                r.vgL     = getProp(engine, i, kKeyL,       0.5);
                r.mLayers = static_cast<int>(
                    std::lround(getProp(engine, i, kKeyLayers, -1.0)));
                r.soil    = static_cast<int>(
                    std::lround(getProp(engine, i, kKeySoil, 0.0)));
                r.closure = static_cast<int>(
                    std::lround(getProp(engine, i, kKeyClosr, -1.0)));
                // The API reports the effective value, not whether the row
                // set it; treat a non-default as "set" so a round trip does
                // not quietly promote every row to explicit.
                r.soilSet    = false;
                r.closureSet = (r.closure != SWMM_GW2D_CLOSURE_AUTO);
                m_rows.append(r);
            }
        }
    }
    m_loaded = m_rows;
    endResetModel();
}

QString Mesh2DAquiferModel::commit(SWMM_Engine engine)
{
    if (!engine) return tr("No engine.");
    if (!isDirty()) return {};

    // The C API has no "set row", so the whole table is rewritten. That is
    // also what makes a reorder commit correctly — and order matters here,
    // because two rows of the same scope resolve last-wins.
    int n = 0;
    swmm_gw2d_row_count(engine, &n);
    for (int i = n - 1; i >= 0; --i) {
        if (swmm_gw2d_row_remove(engine, i) != SWMM_OK)
            return tr("The aquifer rows cannot be edited while a simulation "
                      "is running. Reset the run and try again.");
    }

    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        const QByteArray tag = r.tag.toUtf8();
        if (swmm_gw2d_row_add(engine, r.scope,
                              r.scope == SWMM_GW2D_SCOPE_TAG ? tag.constData()
                                                             : nullptr,
                              r.cell, r.ks, r.zs, r.thetaS, r.thetaR,
                              r.alpha) != SWMM_OK)
            return tr("Row %1 was refused by the engine. Check the porosity, "
                      "the residual water content and the scope target.")
                       .arg(i + 1);
        auto set = [&](const char *k, double v) {
            swmm_gw2d_row_set_property(engine, i, k, v);
        };
        if (r.hg0 >= 0.0) set(kKeyHg0, r.hg0);
        if (r.cLoss > 0.0) set(kKeyCLoss, r.cLoss);
        set(kKeyPsiB, r.psiB);
        set(kKeyLambda, r.lambda);
        set(kKeyN, r.vgN);
        set(kKeyL, r.vgL);
        if (r.soilSet)    set(kKeySoil, r.soil);
        if (r.closureSet) set(kKeyClosr, r.closure);
        if (r.mLayers > 0) set(kKeyLayers, r.mLayers);
    }
    m_loaded = m_rows;
    return {};
}

bool Mesh2DAquiferModel::isDirty() const { return m_rows != m_loaded; }

int Mesh2DAquiferModel::appendRow()
{
    const int at = m_rows.size();
    beginInsertRows(QModelIndex(), at, at);
    m_rows.append(Row{});
    endInsertRows();
    return at;
}

void Mesh2DAquiferModel::setUnitLabels(const QString &lengthLabel,
                                       const QString &rateLabel)
{
    m_lengthLabel = lengthLabel;
    m_rateLabel   = rateLabel;
    emit headerDataChanged(Qt::Horizontal, 0, ColCount - 1);
}

// ===========================================================================
// Mesh2DAquiferNodeModel
// ===========================================================================

bool Mesh2DAquiferNodeModel::Row::operator==(const Row &o) const
{
    return node == o.node && cell == o.cell && kc == o.kc && dc == o.dc &&
           area == o.area;
}

Mesh2DAquiferNodeModel::Mesh2DAquiferNodeModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int Mesh2DAquiferNodeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int Mesh2DAquiferNodeModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant Mesh2DAquiferNodeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows.at(index.row());

    if (role == Qt::ToolTipRole) {
        switch (index.column()) {
        case ColKc:
            return tr("Conductivity of the semi-confining bed between the "
                      "pipe and the aquifer, in %1. Leave at 0 for a direct "
                      "Darcy connection with no bed.").arg(m_rateLabel);
        case ColDc:
            return tr("Bed thickness in %1. Required whenever a bed "
                      "conductivity is given — a bed with no thickness has "
                      "infinite conductance.").arg(m_lengthLabel);
        case ColArea:
            return tr("Exchange area in %1. 0 uses the cell's own plan area.")
                       .arg(m_areaLabel);
        default: break;
        }
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    switch (index.column()) {
    case ColNode: return r.node;
    case ColCell: return r.cell + 1;   // 1-based, as in the file
    case ColKc:   return r.kc;
    case ColDc:   return r.dc;
    case ColArea:
        if (r.area <= 0.0 && role == Qt::DisplayRole) return tr("(cell area)");
        return r.area;
    default: break;
    }
    return {};
}

bool Mesh2DAquiferNodeModel::setData(const QModelIndex &index,
                                     const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() ||
        index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    bool ok = true;
    switch (index.column()) {
    case ColNode: {
        const QString n = value.toString().trimmed();
        if (n.isEmpty()) return false;
        r.node = n;
        break;
    }
    case ColCell: {
        const int c = value.toInt(&ok);
        if (!ok || c < 1) return false;
        r.cell = c - 1;
        break;
    }
    case ColKc: {
        const double v = value.toDouble(&ok);
        if (!ok || v < 0.0) return false;
        r.kc = v;
        break;
    }
    case ColDc: {
        const double v = value.toDouble(&ok);
        if (!ok || v < 0.0) return false;
        r.dc = v;
        break;
    }
    case ColArea: {
        if (value.toString().trimmed().isEmpty()) { r.area = 0.0; break; }
        const double v = value.toDouble(&ok);
        if (!ok || v < 0.0) return false;
        r.area = v;
        break;
    }
    default: return false;
    }
    emit dataChanged(index, index);
    return true;
}

QVariant Mesh2DAquiferNodeModel::headerData(int section,
                                            Qt::Orientation orientation,
                                            int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    switch (section) {
    case ColNode: return tr("Node");
    case ColCell: return tr("Cell");
    case ColKc:   return tr("Bed Kc (%1)").arg(m_rateLabel);
    case ColDc:   return tr("Bed thickness (%1)").arg(m_lengthLabel);
    case ColArea: return tr("Exchange area (%1)").arg(m_areaLabel);
    default: break;
    }
    return {};
}

Qt::ItemFlags Mesh2DAquiferNodeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool Mesh2DAquiferNodeModel::removeRows(int row, int count,
                                        const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 ||
        row + count > m_rows.size())
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
    endRemoveRows();
    return true;
}

void Mesh2DAquiferNodeModel::load(SWMM_Engine engine)
{
    beginResetModel();
    m_rows.clear();
    if (engine) {
        int n = 0;
        if (swmm_gw2d_node_count(engine, &n) == SWMM_OK) {
            for (int i = 0; i < n; ++i) {
                Row r;
                char name[128] = {0};
                if (swmm_gw2d_node_get(engine, i, name, sizeof name, &r.cell,
                                       &r.kc, &r.dc, &r.area) != SWMM_OK)
                    continue;
                r.node = QString::fromUtf8(name);
                m_rows.append(r);
            }
        }
    }
    m_loaded = m_rows;
    endResetModel();
}

QString Mesh2DAquiferNodeModel::commit(SWMM_Engine engine)
{
    if (!engine) return tr("No engine.");
    if (!isDirty()) return {};
    int n = 0;
    swmm_gw2d_node_count(engine, &n);
    for (int i = n - 1; i >= 0; --i) {
        if (swmm_gw2d_node_remove(engine, i) != SWMM_OK)
            return tr("The node beds cannot be edited while a simulation is "
                      "running. Reset the run and try again.");
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows.at(i);
        const QByteArray node = r.node.toUtf8();
        if (swmm_gw2d_node_add(engine, node.constData(), r.cell, r.kc, r.dc,
                               r.area) != SWMM_OK)
            return tr("Bed %1 (node '%2') was refused. A bed conductivity "
                      "needs a positive thickness.").arg(i + 1).arg(r.node);
    }
    m_loaded = m_rows;
    return {};
}

bool Mesh2DAquiferNodeModel::isDirty() const { return m_rows != m_loaded; }

int Mesh2DAquiferNodeModel::appendRow(const QString &node, int cell)
{
    const int at = m_rows.size();
    beginInsertRows(QModelIndex(), at, at);
    Row r;
    r.node = node;
    r.cell = cell;
    m_rows.append(r);
    endInsertRows();
    return at;
}

void Mesh2DAquiferNodeModel::setUnitLabels(const QString &lengthLabel,
                                           const QString &rateLabel,
                                           const QString &areaLabel)
{
    m_lengthLabel = lengthLabel;
    m_rateLabel   = rateLabel;
    m_areaLabel   = areaLabel;
    emit headerDataChanged(Qt::Horizontal, 0, ColCount - 1);
}

} // namespace openswmmvis::ui
