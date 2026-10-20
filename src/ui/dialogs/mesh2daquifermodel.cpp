/*!
 * \file   mesh2daquifermodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/mesh2daquifermodel.h"

#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_tables.h>

#include <QBrush>
#include <QColor>
#include <QSet>
#include <QHash>
#include <QCryptographicHash>
#include <algorithm>

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
        if (role == Qt::EditRole) return r.soilSet?r.soil:-1;
        return r.soilSet ? soilTokens().value(r.soil) : tr("(model default)");
    case ColClosure:
        if (role == Qt::EditRole) return r.closureSet?closureIndexOf(r.closure):-1;
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
        if(ok&&s==-1){r.soil=0;r.soilSet=false;break;}
        if (!ok || s < 0 || s >= soilTokens().size()) return false;
        r.soil = s;
        r.soilSet = true;
        break;
    }
    case ColClosure: {
        const int i = value.toInt(&ok);
        if(ok&&i==-1){r.closure=-1;r.closureSet=false;break;}
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
    m_loadError.clear();
    if (engine) {
        int n = 0;
        if (swmm_gw2d_row_count(engine, &n) == SWMM_OK) {
            for (int i = 0; i < n; ++i) {
                Row r;
                char tag[4096] = {0};
                if (swmm_gw2d_row_get(engine, i, &r.scope, tag, sizeof tag,
                                      &r.cell, &r.ks, &r.zs, &r.thetaS,
                                      &r.thetaR, &r.alpha) != SWMM_OK) {m_loadError=tr("Cannot read every aquifer row.");break;}
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
                double soilSet=0,closureSet=0;
                if(swmm_gw2d_row_get_property(engine,i,"SOIL_CHAR_SET",&soilSet)!=SWMM_OK||swmm_gw2d_row_get_property(engine,i,"CLOSURE_SET",&closureSet)!=SWMM_OK){m_loadError=tr("This engine cannot preserve aquifer inheritance. Update the engine before editing.");break;}
                r.soilSet=soilSet!=0;r.closureSet=closureSet!=0;
                m_rows.append(r);
            }
        }
    }
    m_loaded = m_rows;
    endResetModel();
}

QString Mesh2DAquiferModel::replaceRows(SWMM_Engine engine,const QList<Row>&rows) const
{
    int n=0;if(swmm_gw2d_row_count(engine,&n)!=SWMM_OK)return tr("Cannot read aquifer row count.");
    for(int i=n-1;i>=0;--i)if(swmm_gw2d_row_remove(engine,i)!=SWMM_OK)return tr("Cannot remove aquifer row %1.").arg(i+1);
    for(int i=0;i<rows.size();++i){const auto&r=rows[i];auto tag=r.tag.toUtf8();
        if(swmm_gw2d_row_add(engine,r.scope,r.scope==SWMM_GW2D_SCOPE_TAG?tag.constData():nullptr,r.cell,r.ks,r.zs,r.thetaS,r.thetaR,r.alpha)!=SWMM_OK)return tr("Aquifer row %1 was refused.").arg(i+1);
        auto set=[&](const char*key,double value){return swmm_gw2d_row_set_property(engine,i,key,value)==SWMM_OK;};
        if(!set(kKeyHg0,r.hg0)||!set(kKeyCLoss,r.cLoss)||!set(kKeyPsiB,r.psiB)||!set(kKeyLambda,r.lambda)||!set(kKeyN,r.vgN)||!set(kKeyL,r.vgL)||(r.soilSet&&!set(kKeySoil,r.soil))||(r.closureSet&&!set(kKeyClosr,r.closure))||(r.mLayers>0&&!set(kKeyLayers,r.mLayers)))return tr("Optional properties of aquifer row %1 were refused.").arg(i+1);
    }
    return {};
}
QString Mesh2DAquiferModel::validateCommit(SWMM_Engine engine) const
{
    if(!engine)return tr("No engine.");if(!m_loadError.isEmpty())return m_loadError;
    int state=0;if(swmm_engine_get_state(engine,&state)!=SWMM_OK||(state!=SWMM_STATE_BUILDING&&state!=SWMM_STATE_OPENED))return tr("Reset the run before editing aquifer rows.");
    Mesh2DAquiferModel current;current.load(engine);if(!current.m_loadError.isEmpty())return current.m_loadError;
    if(current.m_rows!=m_loaded)return tr("Aquifer rows changed outside this editor. Reopen it to preserve newer edits.");
    for(int i=0;i<m_rows.size();++i){const auto&r=m_rows[i];for(double v:{r.ks,r.zs,r.thetaS,r.thetaR,r.alpha,r.hg0,r.cLoss,r.psiB,r.lambda,r.vgN,r.vgL})if(!std::isfinite(v))return tr("Aquifer row %1 contains a nonfinite value.").arg(i+1);
        if(r.hg0>r.zs)return tr("Initial saturated depth in row %1 exceeds aquifer thickness.").arg(i+1);
    }
    SWMM_Engine check=swmm_engine_new();if(!check)return tr("Cannot allocate validation model.");const auto error=replaceRows(check,m_rows);swmm_engine_destroy(check);return error;
}
QString Mesh2DAquiferModel::commit(SWMM_Engine engine,bool advanceBaseline)
{
    if(!isDirty())return {};auto error=validateCommit(engine);if(!error.isEmpty())return error;
    error=replaceRows(engine,m_rows);if(!error.isEmpty()){const auto rollback=restoreLoaded(engine);return rollback.isEmpty()?error+tr(" Original rows restored."):error+tr(" Rollback failed: ")+rollback;}
    if(advanceBaseline)acceptChanges();return {};
}
QString Mesh2DAquiferModel::restoreLoaded(SWMM_Engine engine)const{return replaceRows(engine,m_loaded);}
void Mesh2DAquiferModel::acceptChanges(){for(auto&r:m_rows){if(r.scope!=SWMM_GW2D_SCOPE_TAG)r.tag.clear();if(r.scope!=SWMM_GW2D_SCOPE_CELL)r.cell=-1;}m_loaded=m_rows;}

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
           area == o.area && locate == o.locate && exchange == o.exchange && automatic == o.automatic;
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
                      "node and the aquifer, in %1. Leave at 0 for a direct "
                      "Darcy connection with no bed.").arg(m_rateLabel);
        case ColDc:
            return tr("Bed thickness in %1. Required whenever a bed "
                      "conductivity is given — a bed with no thickness has "
                      "infinite conductance.").arg(m_lengthLabel);
        case ColArea:
            return tr("Exchange area in %1. 0 uses the cell's own plan area.")
                       .arg(m_areaLabel);
        case ColCell:
            return r.locate ? tr("Locate by node coordinates (AUTO). Resolved cell: %1.").arg(r.cell < 0 ? tr("unavailable") : QString::number(r.cell + 1))
                            : tr("Explicit receiving cell, numbered from 1.");
        case ColExchange:
            return tr("Aquifer exchange enabled. Disabling it preserves the bed and prevents aquifer exchange; it does not seal local seepage.");
        default: break;
        }
        return {};
    }
    if (role == Qt::CheckStateRole && index.column() == ColExchange)
        return r.exchange ? Qt::Checked : Qt::Unchecked;
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    switch (index.column()) {
    case ColNode: return r.node;
    case ColCell: return r.locate ? QVariant(QStringLiteral("AUTO")) : QVariant(r.cell + 1);
    case ColKc:   return r.kc;
    case ColDc:   return r.dc;
    case ColArea:
        if (r.area <= 0.0 && role == Qt::DisplayRole) return tr("(cell area)");
        return r.area;
    case ColExchange: return role == Qt::EditRole ? QVariant(bool(r.exchange)) : QVariant();
    case ColBottom: {
        const int mode=bottomDestination(r.node);
        return mode<0?QVariant(tr("No storage LID")):role==Qt::EditRole?QVariant(mode):QVariant(bottomLabels().at(mode));
    }
    default: break;
    }
    return {};
}

bool Mesh2DAquiferNodeModel::setData(const QModelIndex &index,
                                     const QVariant &value, int role)
{
    if ((role != Qt::EditRole && role != Qt::CheckStateRole) || !index.isValid() ||
        index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    if (r.automatic) return false;
    if (role == Qt::CheckStateRole && index.column() != ColExchange) return false;
    bool ok = true;
    switch (index.column()) {
    case ColNode: {
        const QString n = value.toString().trimmed();
        if (n.isEmpty()) return false;
        r.node = n;
        break;
    }
    case ColCell: {
        if (value.toString().trimmed().compare(QStringLiteral("AUTO"), Qt::CaseInsensitive) == 0) {
            r.cell = -1; r.locate = 1; break;
        }
        const int c = value.toInt(&ok);
        if (!ok || c < 1) return false;
        r.cell = c - 1;
        r.locate = 0;
        break;
    }
    case ColKc: {
        const double v = value.toDouble(&ok);
        if (!ok || !std::isfinite(v) || v < 0.0) return false;
        r.kc = v;
        break;
    }
    case ColDc: {
        const double v = value.toDouble(&ok);
        if (!ok || !std::isfinite(v) || v < 0.0) return false;
        r.dc = v;
        break;
    }
    case ColArea: {
        if (value.toString().trimmed().isEmpty()) { r.area = 0.0; break; }
        const double v = value.toDouble(&ok);
        if (!ok || !std::isfinite(v) || v < 0.0) return false;
        r.area = v;
        break;
    }
    case ColBottom: return setBottomDestination(r.node,value.toInt());
    case ColExchange:
        r.exchange = role == Qt::CheckStateRole ? value.toInt() == Qt::Checked : value.toBool();
        break;
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
    case ColExchange: return tr("Exchange");
    case ColBottom: return tr("LID bottom destination");
    default: break;
    }
    return {};
}

Qt::ItemFlags Mesh2DAquiferNodeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    if (m_rows.at(index.row()).automatic) return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColBottom && bottomDestination(m_rows.at(index.row()).node)<0) return Qt::ItemIsEnabled|Qt::ItemIsSelectable;
    if (index.column() == ColExchange) return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
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
    m_loadError.clear();m_bottoms.clear();m_footprints.clear();m_controls.clear();m_geometry.clear();
    if (engine) {
        int n = 0;
        if (swmm_gw2d_node_count(engine, &n) == SWMM_OK) {
            for (int i = 0; i < n; ++i) {
                Row r;
                char name[4096] = {0};
                if (swmm_gw2d_node_get(engine, i, name, sizeof name, &r.cell,
                                       &r.kc, &r.dc, &r.area) != SWMM_OK) {m_loadError=tr("Cannot read every node bed.");break;}
                r.node = QString::fromUtf8(name);
                if (swmm_gw2d_node_get_flags(engine, i, &r.locate, &r.exchange, &r.automatic) != SWMM_OK) {
                    m_loadError=tr("Cannot read node-bed ownership flags.");break;
                }
                m_rows.append(r);
            }
        }
    }
    if(engine)for(int node=0;node<swmm_node_count(engine);++node) {
        int control=-1,mode=0;double sat=0,area=0;
        if(swmm_node_get_lid(engine,node,&control,&sat)!=SWMM_OK||control<0)continue;
        const QString name=QString::fromUtf8(swmm_node_id(engine,node));
        if(swmm_node_get_lid_bottom(engine,node,&mode)!=SWMM_OK){m_loadError=tr("Cannot read LID bottom ownership.");break;}
        m_bottoms[name]=mode;
        if(swmm_lid_node_base_area(engine,node,&area)==SWMM_OK)m_footprints[name]=area;
        SWMM_LidRichardsOptions options{};swmm_lid_richards_options_get(engine,control,&options);
        m_controls[name]=tr("%1 · %2").arg(QString::fromUtf8(swmm_lid_id(engine,control)),options.model?tr("Richards 1D"):tr("Existing formulation with backwater"));
        QList<double> signature{double(node),double(control),sat,area,double(options.model),double(options.cells_per_layer),options.atol,options.rtol,options.max_step};
        double invert=0,depth=0;swmm_node_get_invert_elev(engine,node,&invert);swmm_node_get_max_depth(engine,node,&depth);signature<<invert<<depth;
        double x=0,y=0;const int located=swmm_spatial_get_node_coord(engine,node,&x,&y);signature<<double(located);
        if(located==SWMM_OK)signature<<x<<y;
        int shape=0,curve=-1;double a=0,b=0,c=0;
        swmm_node_get_storage_shape(engine,node,&shape);swmm_node_get_storage_curve(engine,node,&curve);
        signature<<double(shape)<<double(curve);
        swmm_node_get_storage_functional(engine,node,&a,&b,&c);signature<<a<<b<<c;
        swmm_node_get_storage_geometry(engine,node,&a,&b,&c);signature<<a<<b<<c;
        if(curve>=0){int points=0;swmm_table_get_point_count(engine,curve,&points);signature<<double(points);
            for(int k=0;k<points;++k){swmm_table_get_point(engine,curve,k,&x,&y);signature<<x<<y;}}
        const int count=swmm_lid_node_layer_count(engine,control);
        for(int k=0;k<count;++k){SWMM_LidNodeLayer layer{};SWMM_LidRichardsMaterial material{};
            if(swmm_lid_node_layer_get(engine,control,k,&layer)!=SWMM_OK){m_loadError=tr("Cannot read LID layers.");break;}
            signature<<double(layer.kind);for(double v:layer.params)signature<<v;
            if(options.model&&(layer.kind==1||layer.kind==2)&&swmm_lid_richards_material_get(engine,control,k,&material)==SWMM_OK)
                signature<<material.theta_r<<material.alpha<<material.n<<material.l<<material.specific_storage;
        }
        m_geometry[name]=signature;
    }
    m_loaded = m_rows;m_loadedBottoms=m_bottoms;
    endResetModel();
}

QString Mesh2DAquiferNodeModel::replaceRows(SWMM_Engine engine,const QList<Row>&expected,const QList<Row>&rows,const QMap<QString,int>&expectedBottoms,const QMap<QString,int>&bottoms,bool apply)const
{
    QList<QByteArray> names;names.reserve(expected.size()+rows.size());
    QList<SWMM_GwNodeBed> before,after;before.reserve(expected.size());after.reserve(rows.size());
    auto snapshot=[&](const QList<Row>&source,QList<SWMM_GwNodeBed>&out){
        for(const auto&r:source){names.append(r.node.toUtf8());out.append({names.back().constData(),r.cell,r.locate,r.exchange,r.automatic,r.kc,r.dc,r.area});}
    };
    snapshot(expected,before);snapshot(rows,after);
    QList<SWMM_LidNodeBottomEdit> edits;
    if(expectedBottoms.keys()!=bottoms.keys())return tr("LID assignments changed. Reopen this editor.");
    for(auto it=bottoms.cbegin();it!=bottoms.cend();++it) {
        const int node=swmm_node_index(engine,it.key().toUtf8().constData());
        if(node<0)return tr("A LID node was removed. Reopen this editor.");
        edits.append({node,expectedBottoms.value(it.key()),it.value()});
    }
    const auto rc=swmm_gw2d_node_replace(engine,before.constData(),before.size(),after.constData(),after.size(),edits.constData(),edits.size(),apply?1:0);
    return rc==SWMM_OK?QString{}:tr("Node-bed transaction refused. Check node names, duplicate connections, positive bed thickness and newer edits. No node beds were changed.");
}
QString Mesh2DAquiferNodeModel::validateCommit(SWMM_Engine engine,bool validateReceiver)const
{
    if(!engine)return tr("No engine.");if(!m_loadError.isEmpty())return m_loadError;
    int state=0;if(swmm_engine_get_state(engine,&state)!=SWMM_OK||(state!=SWMM_STATE_BUILDING&&state!=SWMM_STATE_OPENED))return tr("Reset the run before editing node beds.");
    Mesh2DAquiferNodeModel current;current.load(engine);if(!current.m_loadError.isEmpty())return current.m_loadError;
    if(current.m_rows!=m_loaded||current.m_bottoms!=m_loadedBottoms||current.m_geometry!=m_geometry)return tr("Node beds, LID assignments or geometry changed outside this editor. Reopen it to preserve newer edits.");
    QSet<QString> names;for(const auto&r:m_rows){
        if(r.node.trimmed().isEmpty()||names.contains(r.node))return tr("Node beds require unique node names.");names.insert(r.node);
        if(swmm_node_index(engine,r.node.toUtf8().constData())<0)return tr("Unknown node %1.").arg(r.node);
        if(!std::isfinite(r.kc)||!std::isfinite(r.dc)||!std::isfinite(r.area)||r.kc<0||r.dc<0||r.area<0||
            (r.kc>0&&r.dc<=0)||r.cell<-1||(!r.locate&&r.cell<0))return tr("Check node-bed area, conductivity, thickness and cell.");
    }
    if(!isDirty() || !validateReceiver)return {};return replaceRows(engine,m_loaded,m_rows,m_loadedBottoms,m_bottoms,false);
}
QString Mesh2DAquiferNodeModel::commit(SWMM_Engine engine,bool advanceBaseline)
{
    if(!isDirty())return {};auto error=validateCommit(engine);if(!error.isEmpty())return error;
    error=replaceRows(engine,m_loaded,m_rows,m_loadedBottoms,m_bottoms);if(!error.isEmpty())return error;
    if(advanceBaseline)acceptChanges();return {};
}
QString Mesh2DAquiferNodeModel::restoreLoaded(SWMM_Engine engine)const{
    Mesh2DAquiferNodeModel current;current.load(engine);
    if(!current.m_loadError.isEmpty())return current.m_loadError;
    if(current.m_rows==m_loaded&&current.m_bottoms==m_loadedBottoms)return {};
    if(current.m_geometry!=m_geometry)return tr("LID geometry changed after this edit. Reopen the connection editor.");
    return replaceRows(engine,m_rows,m_loaded,m_bottoms,m_loadedBottoms);
}
void Mesh2DAquiferNodeModel::acceptChanges(){m_loaded=m_rows;m_loadedBottoms=m_bottoms;}

bool Mesh2DAquiferNodeModel::isDirty() const { return m_rows != m_loaded||m_bottoms!=m_loadedBottoms; }

int Mesh2DAquiferNodeModel::appendRow(const QString &node, int cell)
{
    const int at = m_rows.size();
    beginInsertRows(QModelIndex(), at, at);
    Row r;
    r.node = node;
    r.cell = cell;
    r.locate = cell < 0;
    m_rows.append(r);
    endInsertRows();
    return at;
}

std::shared_ptr<Mesh2DAquiferNodeModel> Mesh2DAquiferNodeModel::transactionCopy()const {
    auto copy=std::make_shared<Mesh2DAquiferNodeModel>();
    copy->m_rows=m_rows;copy->m_loaded=m_loaded;copy->m_bottoms=m_bottoms;copy->m_loadedBottoms=m_loadedBottoms;
    copy->m_geometry=m_geometry;copy->m_footprints=m_footprints;copy->m_controls=m_controls;copy->m_loadError=m_loadError;
    return copy;
}
QStringList Mesh2DAquiferNodeModel::bottomLabels() {
    return {tr("Keep current behavior"),tr("Sealed"),tr("Mesh aquifer")};
}
int Mesh2DAquiferNodeModel::bedRow(const QString& node)const {
    for(int i=0;i<m_rows.size();++i)if(m_rows[i].node==node)return i;
    return -1;
}
bool Mesh2DAquiferNodeModel::setBottomDestination(const QString& node,int mode) {
    if(!m_bottoms.contains(node)||mode<0||mode>2)return false;
    m_bottoms[node]=mode;const int row=bedRow(node);
    if(row>=0)emit dataChanged(index(row,0),index(row,ColCount-1));
    return true;
}
QString Mesh2DAquiferNodeModel::useLidFootprint(const QString& node) {
    const double area=m_footprints.value(node,0);
    if(!std::isfinite(area)||area<=0)return tr("This LID base has no positive geometric area. Enter an explicit positive contact area.");
    int row=bedRow(node);
    if(row<0)row=appendRow(node,-1);
    if(m_rows[row].automatic)return tr("An automatic row is read-only. Use authored ROWS enrollment before reviewing a connection.");
    m_rows[row].area=area;emit dataChanged(index(row,0),index(row,ColCount-1));return {};
}
QString Mesh2DAquiferNodeModel::connectionSummary(const QString& node)const {
    if(!m_bottoms.contains(node))return tr("Assign a storage LID control to edit its bottom destination.");
    const int mode=m_bottoms.value(node),row=bedRow(node);
    QString text=tr("%1. LID base footprint: %2 %3. Bottom: %4 → %5.").arg(m_controls.value(node),QString::number(m_footprints.value(node,0),'g',12),m_areaLabel,bottomLabels().value(m_loadedBottoms.value(node)),bottomLabels().value(mode));
    if(row<0)return text+tr(" No aquifer bed is assigned.");
    const auto& r=m_rows[row];
    text+=tr(" Receiver: %1; contact: %2; %3; exchange %4.").arg(r.locate?tr("by coordinates"):QString::number(r.cell+1),r.area>0?QString::number(r.area,'g',12)+" "+m_areaLabel:tr("existing whole-cell area"),r.kc>0?tr("semi-confining bed"):tr("direct aquifer contact"),r.exchange?tr("enabled"):tr("disabled"));
    if(mode==1)text+=tr(" Sealed masks aquifer and outside bottom loss.");
    if(mode==2)text+=tr(" Aquifer exchange replaces local BOTTOM loss. Positive area and enabled exchange are required; the single-cell contact is a lumped representation.");
    return text;
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



QList<Mesh2DAquiferBoundaryModel::Row> Mesh2DAquiferBoundaryModel::canonical(const QList<Row>& rows)
{
    QList<Row> result;for(const auto& row:rows)if(row.species.isEmpty())result.append(row);
    for(const auto& row:rows)if(!row.species.isEmpty())result.append(row);return result;
}
QVariant Mesh2DAquiferBoundaryModel::data(const QModelIndex& index,int role)const
{
    if(!index.isValid()||index.row()<0||index.row()>=m_rows.size())return {};
    if(role==Qt::ToolTipRole)return tr("Local edge numbers start at 0. HEAD uses project elevation units. FLOW is total m³/s, positive inward. CONC uses native concentration; MASSFLUX uses native amount/s/m; HEATFLUX uses W/m. A series replaces the constant value.");
    if(role!=Qt::DisplayRole&&role!=Qt::EditRole)return {};
    const auto& row=m_rows[index.row()];
    switch(index.column()) {
    case Cell:return row.cell+1;case Edge:return row.edge;
    case Species:return role==Qt::DisplayRole&&row.species.isEmpty()?tr("Water"):row.species;
    case Kind:return row.kind;case Value:return role==Qt::EditRole?QVariant(row.value):QVariant(QString::number(row.value,'g',17));
    case Series:return row.series;default:return {};
    }
}
QVariant Mesh2DAquiferBoundaryModel::headerData(int section,Qt::Orientation orientation,int role)const
{
    if(role!=Qt::DisplayRole)return {};
    if(orientation==Qt::Vertical)return section+1;
    const QStringList names={tr("Cell"),tr("Local edge"),tr("Water / species"),tr("Kind"),tr("Constant value"),tr("Time series")};
    return section>=0&&section<names.size()?names[section]:QString();
}
Qt::ItemFlags Mesh2DAquiferBoundaryModel::flags(const QModelIndex& index)const
{return index.isValid()?(Qt::ItemIsEnabled|Qt::ItemIsSelectable|(m_editable?Qt::ItemIsEditable:Qt::NoItemFlags)):Qt::NoItemFlags;}
bool Mesh2DAquiferBoundaryModel::setData(const QModelIndex& index,const QVariant& value,int role)
{
    if(!m_editable||role!=Qt::EditRole||!index.isValid()||index.row()<0||index.row()>=m_rows.size())return false;
    auto next=m_rows[index.row()];bool ok=true;
    switch(index.column()) {
    case Cell:next.cell=value.toInt(&ok)-1;if(next.cell<0)ok=false;break;
    case Edge:next.edge=value.toInt(&ok);if(next.edge<0||next.edge>3)ok=false;break;
    case Species:next.species=value.toString().trimmed();if(next.species.compare("Water",Qt::CaseInsensitive)==0)next.species.clear();break;
    case Kind:next.kind=value.toString().trimmed().toUpper();break;
    case Value:next.value=value.toDouble(&ok);ok=ok&&std::isfinite(next.value);break;
    case Series:next.series=value.toString().trimmed();break;default:return false;
    }
    if(!ok||next==m_rows[index.row()])return false;m_rows[index.row()]=next;emit dataChanged(index,index);return true;
}
void Mesh2DAquiferBoundaryModel::addHydraulic()
{if(!m_editable)return;int row=0;while(row<m_rows.size()&&m_rows[row].species.isEmpty())++row;beginInsertRows({},row,row);m_rows.insert(row,Row{});endInsertRows();}
void Mesh2DAquiferBoundaryModel::addChemistry()
{if(!m_editable)return;const int row=int(m_rows.size());beginInsertRows({},row,row);Row value;value.species="Choose species";value.kind="CONC";m_rows.append(value);endInsertRows();}
void Mesh2DAquiferBoundaryModel::remove(int row)
{if(!m_editable||row<0||row>=m_rows.size())return;beginRemoveRows({},row,row);m_rows.removeAt(row);endRemoveRows();}
QString Mesh2DAquiferBoundaryModel::read(SWMM_Engine engine,QList<Row>& rows)
{
    rows.clear();const int water=swmm_gw2d_boundary_count(engine),chemistry=swmm_gw_boundary_quality_count(engine);
    if(water<0||chemistry<0)return tr("Cannot read groundwater edge records.");
    for(int i=0;i<water;++i) {
        SWMM_GwBoundary source{};char series[32768]{};
        if(swmm_gw2d_boundary_get(engine,i,&source,series,sizeof series)!=SWMM_OK)return tr("Cannot read a hydraulic edge.");
        Row row;row.cell=source.cell;row.edge=source.edge;row.kind=source.kind==SWMM_GW2D_BOUNDARY_HEAD?"HEAD":"FLOW";
        row.value=source.value;row.series=QString::fromUtf8(series);rows.append(row);
    }
    for(int i=0;i<chemistry;++i) {
        Row row;char species[4096]{},kind[64]{},series[32768]{};
        if(swmm_gw_boundary_quality_get(engine,i,&row.cell,&row.edge,species,sizeof species,kind,sizeof kind,&row.value,series,sizeof series)!=SWMM_OK)return tr("Cannot read edge chemistry.");
        row.species=QString::fromUtf8(species);row.kind=QString::fromUtf8(kind);row.series=QString::fromUtf8(series);rows.append(row);
    }
    return {};
}
QString Mesh2DAquiferBoundaryModel::identity(SWMM_Engine engine,QByteArray& result)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto bytes=[&](const auto& value){hash.addData(QByteArrayView(reinterpret_cast<const char*>(&value),sizeof(value)));};
    const auto text=[&](const QString& value){const auto data=value.toUtf8();const auto size=data.size();bytes(size);hash.addData(data);};
    int vertices=0,cells=0;if(swmm_2d_vertex_count(engine,&vertices)!=SWMM_OK||swmm_2d_cell_count(engine,&cells)!=SWMM_OK)return tr("Cannot read boundary mesh identity.");
    bytes(vertices);bytes(cells);
    for(int i=0;i<vertices;++i){double x=0,y=0,z=0;if(swmm_2d_vertex_get_xyz(engine,i,&x,&y,&z)!=SWMM_OK)return tr("Cannot read boundary coordinates.");bytes(x);bytes(y);bytes(z);}
    for(int i=0;i<cells;++i){int v[4]{},nv=0;if(swmm_2d_cell_get_vertices(engine,i,v,&nv)!=SWMM_OK)return tr("Cannot read boundary connectivity.");bytes(nv);for(int j=0;j<nv;++j)bytes(v[j]);}
    for(int i=0;i<swmm_pollutant_count(engine);++i){const char* name=swmm_pollutant_id(engine,i);int unit=-1;if(!name||swmm_pollutant_get_units(engine,i,&unit)!=SWMM_OK)return tr("Cannot read pollutant units.");text(QString::fromUtf8(name));bytes(unit);}
    for(int i=0;i<swmm_reaction_species_count(engine);++i){char name[4096]{},units[4096]{};int wall=0;double a=0,r=0;if(swmm_reaction_species_get(engine,i,name,sizeof name,&wall,units,sizeof units,&a,&r)!=SWMM_OK)return tr("Cannot read reaction units.");text(QString::fromUtf8(name));text(QString::fromUtf8(units));bytes(wall);}
    result=hash.result();return {};
}
QString Mesh2DAquiferBoundaryModel::load(SWMM_Engine engine)
{QList<Row> rows;m_error=read(engine,rows);if(m_error.isEmpty())m_error=identity(engine,m_identity);beginResetModel();m_rows=rows;m_loaded=rows;endResetModel();return m_error;}
QString Mesh2DAquiferBoundaryModel::validate(SWMM_Engine engine,const QList<Row>& rows)
{
    if(rows.isEmpty())return {};
    int count=0;if(swmm_2d_cell_count(engine,&count)!=SWMM_OK)return tr("Cannot read the mesh.");
    char mode[128]{};if(swmm_gw2d_option_get(engine,"MODE",mode,sizeof mode)!=SWMM_OK||QString::fromUtf8(mode)=="PER_SUBCATCH")return tr("Hydraulic edge forcing requires a mesh aquifer.");
    const auto flag=[&](const char* key,bool groundwater) {char value[128]{};const int rc=groundwater?swmm_gw_transport_option_get(engine,key,value,sizeof value):swmm_options_get(engine,key,value,sizeof value);return rc==SWMM_OK&&QString::fromUtf8(value)=="YES";};
    QSet<QString> species,massSpecies;
    if(!flag("IGNORE_QUALITY",false)) {
        if(flag("TRANSPORT_POLLUTANTS",true))for(int p=0;p<swmm_pollutant_count(engine);++p)if(const char* name=swmm_pollutant_id(engine,p)){species.insert(QString::fromUtf8(name));massSpecies.insert(QString::fromUtf8(name));}
        if(flag("TRANSPORT_MSX",true))for(int s=0;s<swmm_reaction_species_count(engine);++s) {
            char name[4096]{},unit[4096]{};int wall=0;double a=0,r=0;
            if(swmm_reaction_species_get(engine,s,name,sizeof name,&wall,unit,sizeof unit,&a,&r)!=SWMM_OK)return tr("Cannot read reaction species.");
            if(!wall) {
                species.insert(QString::fromUtf8(name));
                QString basis=QString::fromUtf8(unit).trimmed().toUpper();if(basis.endsWith("/L"))basis.chop(2);
                const QSet<QString> known{"MG","UG","G","KG","MOL","MMOL","UMOL","COUNT","#"};
                if(known.contains(basis))massSpecies.insert(QString::fromUtf8(name));
            }
        }
    }
    if(flag("WATER_AGE",false)&&flag("TRANSPORT_AGE",true))species.insert("__WATER_AGE__");
    if(flag("HEAT_TRANSPORT",false)&&flag("TRANSPORT_TEMPERATURE",true))species.insert("__TEMPERATURE__");
    const auto edgeKey=[](int a,int b){if(a>b)std::swap(a,b);return (quint64(quint32(a))<<32)|quint32(b);};
    QMap<QPair<int,int>,Row> hydraulic;QHash<quint64,int> occurrences;
    QSet<QString> terms,concentrations;
    const auto key=[](const Row& row){return QString::number(row.cell)+":"+QString::number(row.edge)+":"+row.species;};
    for(const auto& row:rows) {
        int vertices[4]{},nv=0;
        if(row.cell<0||row.cell>=count||swmm_2d_cell_get_vertices(engine,row.cell,vertices,&nv)!=SWMM_OK||row.edge<0||row.edge>=nv||!std::isfinite(row.value))return tr("A boundary cell, local edge or value is invalid.");
        if(row.series.contains('\n')||row.series.contains('\r')||row.series.contains('"'))return tr("A time-series name contains an invalid delimiter.");
        if(!row.series.isEmpty()) {
            bool found=false;for(int t=0;t<swmm_table_count(engine);++t){const char* id=swmm_table_id(engine,t);int type=-1;if(id&&swmm_table_get_type(engine,t,&type)==SWMM_OK&&type==0&&row.series==QString::fromUtf8(id)){found=true;break;}}
            if(!found)return tr("A boundary time series is unavailable: %1.").arg(row.series);
        }
        if(row.species.isEmpty()) {
            if(row.kind!="HEAD"&&row.kind!="FLOW")return tr("A Water row must use HEAD or FLOW.");
            const QPair<int,int> location{row.cell,row.edge};if(hydraulic.contains(location))return tr("Each cell edge may have one hydraulic boundary.");hydraulic[location]=row;
            const int a=(row.edge+1)%nv,b=(row.edge+2)%nv;
            occurrences[edgeKey(vertices[a],vertices[b])]=0;
        } else {
            if(!species.contains(row.species))return tr("The groundwater species is unavailable: %1.").arg(row.species);
            const bool flux=row.kind=="MASSFLUX"||row.kind=="HEATFLUX";
            if(!flux&&row.kind!="CONC"&&row.kind!="TS")return tr("Chemistry uses CONC, TS, MASSFLUX or HEATFLUX.");
            if(row.kind=="TS"&&row.series.isEmpty())return tr("TS chemistry requires a time series.");
            if(row.kind=="HEATFLUX"&&row.species!="__TEMPERATURE__")return tr("HEATFLUX requires the temperature species.");
            if(row.kind=="MASSFLUX"&&!massSpecies.contains(row.species))return tr("MASSFLUX requires a species with declared mass or count units.");
            if(!flux&&row.species!="__TEMPERATURE__"&&row.value<0&&row.series.isEmpty())return tr("Concentration and age must be nonnegative.");
            const QString identity=key(row)+(flux?":flux":":conc");if(terms.contains(identity))return tr("A species may have one concentration and one imposed flux on each edge.");terms.insert(identity);if(!flux)concentrations.insert(key(row));
        }
    }
    // Build only requested edge counts from authoring connectivity: neighbour
    // geometry need not have been initialized when this editor opens.
    for(int cell=0;cell<count;++cell) {
        int vertices[4]{},nv=0;if(swmm_2d_cell_get_vertices(engine,cell,vertices,&nv)!=SWMM_OK)return tr("Cannot read cell connectivity.");
        for(int edge=0;edge<nv;++edge){const int a=(edge+1)%nv,b=(edge+2)%nv;auto found=occurrences.find(edgeKey(vertices[a],vertices[b]));if(found!=occurrences.end())++found.value();}
    }
    for(int found:occurrences)if(found!=1)return tr("Hydraulic groundwater boundaries must lie on exterior mesh edges.");
    int aquifers=0;if(swmm_gw2d_row_count(engine,&aquifers)!=SWMM_OK)return tr("Cannot read aquifer coverage.");
    for(auto it=hydraulic.cbegin();it!=hydraulic.cend();++it) {
        char tag[4096]{};if(swmm_2d_get_triangle_tag(engine,it->cell,tag,sizeof tag)!=SWMM_OK)return tr("Cannot read the boundary cell tag.");
        bool covered=false;
        for(int pass=0;pass<3;++pass)for(int i=0;i<aquifers;++i) {
            int scope=0,cell=-1;char rowTag[4096]{};double ks=0,zs=0,ts=0,tr=0,alpha=0;
            if(swmm_gw2d_row_get(engine,i,&scope,rowTag,sizeof rowTag,&cell,&ks,&zs,&ts,&tr,&alpha)!=SWMM_OK)return Mesh2DAquiferBoundaryModel::tr("Cannot read an aquifer row.");
            if(scope!=pass||(scope==2&&cell!=it->cell)||(scope==1&&QString::fromUtf8(rowTag)!=QString::fromUtf8(tag)))continue;
            covered=ks>0&&zs>0&&ts>tr;
        }
        if(!covered)return tr("Define an active aquifer below each hydraulic boundary cell.");
        if(it->kind=="HEAD"||it->value>0||!it->series.isEmpty())for(const auto& name:species) {
            Row row=*it;row.species=name;if(!concentrations.contains(key(row)))return tr("Potential groundwater inflow requires an explicit concentration for %1 on cell %2, edge %3.").arg(name).arg(it->cell+1).arg(it->edge);
        }
    }
    for(const auto& row:rows)if(!row.species.isEmpty()&&!hydraulic.contains({row.cell,row.edge}))return tr("Every chemistry row requires a matching hydraulic edge.");
    return {};
}
QString Mesh2DAquiferBoundaryModel::validateCommit(SWMM_Engine engine,bool physical)const
{
    if(!m_error.isEmpty())return m_error;
    int state=0;if(swmm_engine_get_state(engine,&state)!=SWMM_OK||(state!=SWMM_STATE_BUILDING&&state!=SWMM_STATE_OPENED))return tr("Reset the simulation before editing groundwater boundaries.");
    QByteArray currentIdentity;auto error=identity(engine,currentIdentity);if(!error.isEmpty())return error;
    if(currentIdentity!=m_identity)return tr("The mesh or species units changed outside this editor. Reopen it before applying.");
    QList<Row> current;error=read(engine,current);if(!error.isEmpty())return error;
    if(current!=m_loaded)return tr("Groundwater edges changed outside this editor. Reopen it before applying.");
    return physical?validate(engine,canonical(m_rows)):QString();
}
QString Mesh2DAquiferBoundaryModel::write(SWMM_Engine engine,const QList<Row>& rows)
{
    for(int i=swmm_gw_boundary_quality_count(engine)-1;i>=0;--i)if(swmm_gw_boundary_quality_remove(engine,i)!=SWMM_OK)return tr("Cannot replace edge chemistry.");
    for(int i=swmm_gw2d_boundary_count(engine)-1;i>=0;--i)if(swmm_gw2d_boundary_remove(engine,i)!=SWMM_OK)return tr("Cannot replace hydraulic edges.");
    for(const auto& row:rows) {
        int status=0;
        if(row.species.isEmpty()) {const SWMM_GwBoundary native{row.cell,row.edge,row.kind=="HEAD"?SWMM_GW2D_BOUNDARY_HEAD:SWMM_GW2D_BOUNDARY_FLOW,row.value};status=swmm_gw2d_boundary_set(engine,&native,row.series.toUtf8().constData());}
        else status=swmm_gw_boundary_quality_set(engine,row.cell,row.edge,row.species.toUtf8().constData(),row.kind.toUtf8().constData(),row.value,row.series.toUtf8().constData());
        if(status!=SWMM_OK)return tr("The engine refused an edge record.");
    }
    QList<Row> actual;const auto error=read(engine,actual);if(!error.isEmpty())return error;
    return actual==canonical(rows)?QString():tr("Saved edge records differ from the requested values.");
}
QString Mesh2DAquiferBoundaryModel::commit(SWMM_Engine engine,bool accept)
{
    auto error=validateCommit(engine);if(!error.isEmpty()||!isDirty())return error;
    error=write(engine,canonical(m_rows));
    if(!error.isEmpty()){const auto rollback=write(engine,m_loaded);if(!rollback.isEmpty())error+=tr(" Restoration failed: %1. Reload the saved model.").arg(rollback);return error;}
    if(accept)acceptChanges();return {};
}
QString Mesh2DAquiferBoundaryModel::restoreLoaded(SWMM_Engine engine)
{
    QByteArray currentIdentity;const auto changed=identity(engine,currentIdentity);if(!changed.isEmpty())return changed;
    if(currentIdentity!=m_identity)return tr("The mesh or species units changed; boundary restoration was refused.");
    QList<Row> current;auto error=read(engine,current);if(!error.isEmpty())return error;
    if(current==m_loaded)return {};
    if(current!=canonical(m_rows))return tr("Groundwater boundaries changed after this edit; restoration was refused.");
    return write(engine,m_loaded);
}
void Mesh2DAquiferBoundaryModel::acceptChanges()
{beginResetModel();m_rows=canonical(m_rows);m_loaded=m_rows;endResetModel();}
std::shared_ptr<Mesh2DAquiferBoundaryModel> Mesh2DAquiferBoundaryModel::transactionCopy()const
{auto copy=std::make_shared<Mesh2DAquiferBoundaryModel>();copy->m_rows=m_rows;copy->m_loaded=m_loaded;copy->m_error=m_error;copy->m_identity=m_identity;return copy;}

} // namespace openswmmvis::ui
