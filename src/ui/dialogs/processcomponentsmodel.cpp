/**
 * @file      processcomponentsmodel.cpp
 * @brief     U1 (2026-09-07) — MVC model for the model's [PROCESS_COMPONENTS] registrations
 *            (Simulation Options → Files / Output / Plugins → Process components).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * U1 (2026-09-07) — see the header.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/processcomponentsmodel.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <cstring>
#include <vector>

#include <openswmm/engine/openswmm_process_components.h>

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------
namespace {
QString argumentValue(const QList<QPair<QString, QString>> &arguments, const QString &name)
{
    for (const auto &argument : arguments)
        if (argument.first == name) return argument.second;
    return {};
}
}

bool ProcessComponentsModel::readRow(SWMM_Engine engine, int index, Row &row)
{
    for (int size = 256;; size *= 2) {
        if (size > 64 * 1024 * 1024) return false;
        std::vector<char> id(size), config(size), resolved(size);
        if (swmm_process_component_get(engine, index, id.data(), size, config.data(), size,
                                       resolved.data(), size) != SWMM_OK) return false;
        if (std::strlen(id.data()) == size_t(size - 1) || std::strlen(config.data()) == size_t(size - 1) ||
            std::strlen(resolved.data()) == size_t(size - 1)) continue;
        row.id = QString::fromUtf8(id.data());
        row.config = QString::fromUtf8(config.data());
        row.resolved = QString::fromUtf8(resolved.data());
        break;
    }
    row.arguments.clear();
    const int count = swmm_process_component_argument_count(engine, index);
    if (count < 0) return false;
    for (int argument = 0; argument < count; ++argument) {
        for (int size = 256;; size *= 2) {
            if (size > 64 * 1024 * 1024) return false;
            std::vector<char> key(size), value(size);
            if (swmm_process_component_argument_get(engine, index, argument, key.data(), size,
                                                    value.data(), size) != SWMM_OK) return false;
            if (std::strlen(key.data()) == size_t(size - 1) || std::strlen(value.data()) == size_t(size - 1)) continue;
            row.arguments.append({QString::fromUtf8(key.data()), QString::fromUtf8(value.data())});
            break;
        }
    }
    return true;
}

ProcessComponentsModel::ProcessComponentsModel(QObject *parent) : QAbstractTableModel(parent) {}

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
            k.id = QString::fromUtf8(id);
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
    if (r.id.trimmed().isEmpty())
        return tr("no id");
    bool known = false, implemented = false;
    QString desc;
    for (const KnownId &k : knownIds()) {
        if (k.id.compare(r.id, Qt::CaseInsensitive) == 0) {
            known = true;
            implemented = k.implemented;
            desc = k.description;
            break;
        }
    }
    QString status;
    const auto library = argumentValue(r.arguments, QStringLiteral("library"));
    if (!library.isEmpty()) {
        const auto path = QFileInfo(library).isRelative() ? QDir(m_modelDir).filePath(library) : library;
        status = argumentValue(r.arguments, QStringLiteral("component")).isEmpty()
            ? tr("component type missing")
            : QFileInfo::exists(path) ? tr("external component, library found") : tr("external component, library missing");
    } else if (!known)
        status = tr("unknown id");
    else if (!implemented)
        status = tr("planned");
    else
        status = tr("implemented");
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
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        switch (index.column()) {
        case ColId:
            return r.id;
        case ColConfig:
            return r.config;
        case ColComponent:
            return argumentValue(r.arguments, QStringLiteral("component"));
        case ColLibrary:
            return argumentValue(r.arguments, QStringLiteral("library"));
        case ColStatus:
            return statusFor(r);
        default:
            return {};
        }
    case Qt::ToolTipRole:
        if (index.column() == ColId) {
            for (const KnownId &k : knownIds())
                if (k.id.compare(r.id, Qt::CaseInsensitive) == 0)
                    return k.description;
            return tr("Use a unique instance name for an external component, with its "
                      "type and library, or choose a built-in process id.");
        }
        if (index.column() == ColConfig)
            return r.resolved.isEmpty() ? tr("Config file path as written in [PROCESS_COMPONENTS] "
                                             "(relative to the model file).")
                                        : tr("Read from: %1").arg(r.resolved);
        return {};
    default:
        return {};
    }
}

bool ProcessComponentsModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.row() >= m_rows.size())
        return false;
    Row &r = m_rows[index.row()];
    const QString v = value.toString().trimmed();
    switch (index.column()) {
    case ColId:
        if (r.id == v)
            return false;
        r.id = v;
        break;
    case ColConfig:
        if (r.config == v)
            return false;
        r.config = v;
        break;
    case ColComponent:
    case ColLibrary: {
        const auto key = index.column() == ColComponent ? QStringLiteral("component") : QStringLiteral("library");
        if (argumentValue(r.arguments, key) == v) return false;
        for (auto it = r.arguments.begin(); it != r.arguments.end();)
            if (it->first == key) it = r.arguments.erase(it); else ++it;
        if (!v.isEmpty()) r.arguments.append({key, v});
        break;
    }
    default:
        return false;
    }
    emit dataChanged(index.sibling(index.row(), ColId), index.sibling(index.row(), ColStatus));
    return true;
}

QVariant ProcessComponentsModel::headerData(int section, Qt::Orientation orientation,
                                            int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColId:
        return tr("Instance / built-in id");
    case ColConfig:
        return tr("Config file");
    case ColStatus:
        return tr("Status");
    case ColComponent:
        return tr("Component type");
    case ColLibrary:
        return tr("Library");
    default:
        return {};
    }
}

Qt::ItemFlags ProcessComponentsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() != ColStatus)
        f |= Qt::ItemIsEditable;
    return f;
}

bool ProcessComponentsModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_rows.size())
        return false;
    beginRemoveRows(parent, row, row + count - 1);
    for (int i = 0; i < count; ++i)
        m_rows.removeAt(row);
    endRemoveRows();
    return true;
}

void ProcessComponentsModel::load(SWMM_Engine engine, const QString &modelDir)
{
    beginResetModel();
    m_rows.clear();
    m_error.clear();
    m_modelDir = modelDir;
    if (engine) {
        const int n = swmm_process_component_count(engine);
        for (int i = 0; i < n; ++i) {
            Row r;
            if (!readRow(engine, i, r)) continue;
            m_rows.append(r);
        }
    }
    m_loaded = m_rows;
    endResetModel();
}

bool ProcessComponentsModel::isDirty() const
{
    if (m_rows.size() != m_loaded.size())
        return true;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].id != m_loaded[i].id || m_rows[i].config != m_loaded[i].config ||
            m_rows[i].arguments != m_loaded[i].arguments)
            return true;
    return false;
}

QString ProcessComponentsModel::validationError() const
{
    QSet<QString> identities;
    for (const auto &row : m_rows) {
        if (row.id.trimmed().isEmpty()) continue;
        if (identities.contains(row.id))
            return tr("Each component instance needs a unique id: %1").arg(row.id);
        identities.insert(row.id);
        const bool library = !argumentValue(row.arguments, QStringLiteral("library")).isEmpty();
        const bool type = !argumentValue(row.arguments, QStringLiteral("component")).isEmpty();
        if (library != type)
            return tr("Component %1 needs both a library and a component type.").arg(row.id);
    }
    return {};
}

int ProcessComponentsModel::commit(SWMM_Engine engine)
{
    if (!engine || !isDirty())
        return 0;
    m_error = validationError();
    if (!m_error.isEmpty()) return -1;
    int writes = 0;
    // Remove every engine registration that is not present unchanged in the
    // staged rows (by id + config), then register the staged rows that are
    // not in the engine. Ids are unique in the engine, so a row whose config
    // changed is a remove + register.
    // Borrow id/cfg during this synchronous lookup; compare exact copied pairs
    // against the owned draft and return membership without changing any row.
    auto stagedHas = [this](const Row &current) {
        for (const Row &r : m_rows)
            if (r.id == current.id && r.config == current.config && r.arguments == current.arguments)
                return true;
        return false;
    };
    for (int i = swmm_process_component_count(engine) - 1; i >= 0; --i) {
        Row current;
        if (!readRow(engine, i, current)) {
            m_error = tr("Cannot read the current component registrations.");
            return -1;
        }
        if (!stagedHas(current)) {
            if (swmm_process_component_remove(engine, i) != SWMM_OK) {
                m_error = tr("Cannot edit components while this model is running.");
                return -1;
            }
            ++writes;
        }
    }
    for (const Row &r : m_rows) {
        if (r.id.trimmed().isEmpty())
            continue;
        if (swmm_process_component_find(engine, r.id.toUtf8().constData()) >= 0)
            continue; // unchanged row kept above
        if (swmm_process_component_register(engine, r.id.toUtf8().constData(),
                                            r.config.toUtf8().constData()) != SWMM_OK) {
            m_error = tr("Cannot register component %1.").arg(r.id);
            return -1;
        }
        ++writes;
        const int index = swmm_process_component_find(engine, r.id.toUtf8().constData());
        for (const auto &argument : r.arguments) {
            if (swmm_process_component_argument_set(engine, index, argument.first.toUtf8().constData(),
                                                    argument.second.toUtf8().constData()) != SWMM_OK) {
                m_error = tr("Cannot save argument %1 for component %2.").arg(argument.first, r.id);
                return -1;
            }
            ++writes;
        }
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

QWidget *ProcessComponentIdDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &,
                                                  const QModelIndex &) const
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    for (const auto &k : ProcessComponentsModel::knownIds()) {
        combo->addItem(k.id, k.id);
        combo->setItemData(combo->count() - 1,
                           k.implemented
                               ? k.description
                               : tr("%1 (planned — not in this build)").arg(k.description),
                           Qt::ToolTipRole);
    }
    return combo;
}

void ProcessComponentIdDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo)
        return;
    const QString cur = index.data(Qt::EditRole).toString();
    const int i = combo->findText(cur, Qt::MatchFixedString);
    if (i >= 0)
        combo->setCurrentIndex(i);
    else
        combo->setEditText(cur);
}

void ProcessComponentIdDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                              const QModelIndex &index) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo)
        return;
    model->setData(index, combo->currentText().trimmed(), Qt::EditRole);
}
