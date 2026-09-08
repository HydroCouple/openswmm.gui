/*!
 * \file   processcomponentsmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * U1 (2026-09-07) — see the header.
 */
#include "ui/dialogs/processcomponentsmodel.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>

#include <openswmm/engine/openswmm_process_components.h>

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

ProcessComponentsModel::ProcessComponentsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

QList<ProcessComponentsModel::KnownId> ProcessComponentsModel::knownIds()
{
    static const QList<KnownId> cache = [] {
        QList<KnownId> out;
        const int n = swmm_process_component_known_count();
        for (int i = 0; i < n; ++i) {
            char id[160] = {};
            char desc[256] = {};
            int implemented = 0;
            if (swmm_process_component_known_get(i, id, sizeof id, desc, sizeof desc,
                                                 &implemented) != SWMM_OK)
                continue;
            KnownId k;
            k.id          = QString::fromUtf8(id);
            k.description = QString::fromUtf8(desc);
            k.implemented = implemented != 0;
            out.append(k);
        }
        return out;
    }();
    return cache;
}

int ProcessComponentsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int ProcessComponentsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QString ProcessComponentsModel::statusFor(const Row &r) const
{
    if (r.id.trimmed().isEmpty()) return tr("no id");
    bool known = false, implemented = false;
    QString desc;
    for (const KnownId &k : knownIds()) {
        if (k.id.compare(r.id, Qt::CaseInsensitive) == 0) {
            known = true; implemented = k.implemented; desc = k.description; break;
        }
    }
    QString status;
    if (!known)             status = tr("unknown id");
    else if (!implemented)  status = tr("planned");
    else                    status = tr("implemented");
    if (!r.config.trimmed().isEmpty()) {
        const QString path = QFileInfo(r.config).isRelative() && !m_modelDir.isEmpty()
                                 ? QDir(m_modelDir).filePath(r.config)
                                 : r.config;
        status += QFileInfo::exists(path) ? tr(", file found")
                                          : tr(", file missing (created on save by its editor)");
    } else {
        status += tr(", no config file");
    }
    return status;
}

QVariant ProcessComponentsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        switch (index.column()) {
        case ColId:     return r.id;
        case ColConfig: return r.config;
        case ColStatus: return statusFor(r);
        default:        return {};
        }
    case Qt::ToolTipRole:
        if (index.column() == ColId) {
            for (const KnownId &k : knownIds())
                if (k.id.compare(r.id, Qt::CaseInsensitive) == 0) return k.description;
            return tr("Not in the engine's built-in catalogue — the engine "
                      "diagnoses an unknown id at open.");
        }
        if (index.column() == ColConfig)
            return r.resolved.isEmpty()
                       ? tr("Config file path as written in [PROCESS_COMPONENTS] "
                            "(relative to the model file).")
                       : tr("Read from: %1").arg(r.resolved);
        return {};
    default:
        return {};
    }
}

bool ProcessComponentsModel::setData(const QModelIndex &index, const QVariant &value,
                                     int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    const QString v = value.toString().trimmed();
    switch (index.column()) {
    case ColId:     if (r.id == v) return false;     r.id = v;     break;
    case ColConfig: if (r.config == v) return false; r.config = v; break;
    default:        return false;
    }
    emit dataChanged(index.sibling(index.row(), ColId),
                     index.sibling(index.row(), ColStatus));
    return true;
}

QVariant ProcessComponentsModel::headerData(int section, Qt::Orientation orientation,
                                            int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColId:     return tr("Component id");
    case ColConfig: return tr("Config file");
    case ColStatus: return tr("Status");
    default:        return {};
    }
}

Qt::ItemFlags ProcessComponentsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() != ColStatus) f |= Qt::ItemIsEditable;
    return f;
}

bool ProcessComponentsModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_rows.size())
        return false;
    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i) m_rows.removeAt(row);
    endRemoveRows();
    return true;
}

void ProcessComponentsModel::load(SWMM_Engine engine, const QString &modelDir)
{
    beginResetModel();
    m_rows.clear();
    m_modelDir = modelDir;
    if (engine) {
        const int n = swmm_process_component_count(engine);
        for (int i = 0; i < n; ++i) {
            char id[160] = {}, cfg[1024] = {}, res[1024] = {};
            if (swmm_process_component_get(engine, i, id, sizeof id, cfg, sizeof cfg,
                                           res, sizeof res) != SWMM_OK)
                continue;
            Row r;
            r.id       = QString::fromUtf8(id);
            r.config   = QString::fromUtf8(cfg);
            r.resolved = QString::fromUtf8(res);
            m_rows.append(r);
        }
    }
    m_loaded = m_rows;
    endResetModel();
}

bool ProcessComponentsModel::isDirty() const
{
    if (m_rows.size() != m_loaded.size()) return true;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id != m_loaded[i].id || m_rows[i].config != m_loaded[i].config)
            return true;
    return false;
}

int ProcessComponentsModel::commit(SWMM_Engine engine)
{
    if (!engine || !isDirty()) return 0;
    int writes = 0;
    // Remove every engine registration that is not present unchanged in the
    // staged rows (by id + config), then register the staged rows that are
    // not in the engine. Ids are unique in the engine, so a row whose config
    // changed is a remove + register.
    auto stagedHas = [this](const QString &id, const QString &cfg) {
        for (const Row &r : m_rows)
            if (r.id == id && r.config == cfg) return true;
        return false;
    };
    for (int i = swmm_process_component_count(engine) - 1; i >= 0; --i) {
        char id[160] = {}, cfg[1024] = {}, res[1024] = {};
        if (swmm_process_component_get(engine, i, id, sizeof id, cfg, sizeof cfg,
                                       res, sizeof res) != SWMM_OK)
            continue;
        if (!stagedHas(QString::fromUtf8(id), QString::fromUtf8(cfg))) {
            if (swmm_process_component_remove(engine, i) == SWMM_OK) ++writes;
        }
    }
    for (const Row &r : m_rows) {
        if (r.id.trimmed().isEmpty()) continue;
        if (swmm_process_component_find(engine, r.id.toUtf8().constData()) >= 0)
            continue;   // unchanged row kept above
        if (swmm_process_component_register(engine, r.id.toUtf8().constData(),
                                            r.config.toUtf8().constData()) == SWMM_OK)
            ++writes;
    }
    load(engine, m_modelDir);
    return writes;
}

int ProcessComponentsModel::appendRow(const QString &id, const QString &config)
{
    const int row = m_rows.size();
    beginInsertRows(QModelIndex(), row, row);
    Row r;
    r.id = id;
    r.config = config;
    m_rows.append(r);
    endInsertRows();
    return row;
}

QString ProcessComponentsModel::idAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows[row].id : QString();
}

QString ProcessComponentsModel::configAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows[row].config : QString();
}

// ---------------------------------------------------------------------------
// Id delegate
// ---------------------------------------------------------------------------

ProcessComponentIdDelegate::ProcessComponentIdDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *ProcessComponentIdDelegate::createEditor(QWidget *parent,
                                                  const QStyleOptionViewItem &,
                                                  const QModelIndex &) const
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    for (const auto &k : ProcessComponentsModel::knownIds()) {
        combo->addItem(k.id, k.id);
        combo->setItemData(combo->count() - 1,
                           k.implemented ? k.description
                                         : tr("%1 (planned — not in this build)")
                                               .arg(k.description),
                           Qt::ToolTipRole);
    }
    return combo;
}

void ProcessComponentIdDelegate::setEditorData(QWidget *editor,
                                               const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo) return;
    const QString cur = index.data(Qt::EditRole).toString();
    const int i = combo->findText(cur, Qt::MatchFixedString);
    if (i >= 0) combo->setCurrentIndex(i);
    else        combo->setEditText(cur);
}

void ProcessComponentIdDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                              const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo) return;
    model->setData(index, combo->currentText().trimmed(), Qt::EditRole);
}
