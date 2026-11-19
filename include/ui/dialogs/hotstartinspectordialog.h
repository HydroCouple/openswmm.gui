// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      hotstartinspectordialog.h
 * @brief     Hot-start inspector (review G-09): a hot-start file's header and per-object values,
 *            with node depth and link flow/depth edits for native record files.
 * @details   HotStartRecordModel shows one record kind in the open model's units and stages
 *            edits; HotStartInspectorDialog writes them with HotStartDocument::save() after the
 *            user confirms. Legacy SWMM 5 files and files holding a full engine checkpoint are
 *            read-only.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include "selection/selectionmanager.h"
#include "simulation/hotstartdocument.h"

#include <QAbstractTableModel>
#include <QDialog>

#include <functional>

class QLabel;
class QPushButton;
class QTableView;
class QTabWidget;

namespace openswmmvis::ui
{

/*! One kind of hot-start record (nodes, links or subcatchments) as a table. */
class HotStartRecordModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum class Kind { Nodes, Links, Subcatchments };
    /// Columns before the species columns (nodes and links only).
    enum Column { IdColumn, InModelColumn, FirstValueColumn };
    static constexpr int ObjectIdRole = Qt::UserRole + 1; ///< record ID (string)

    explicit HotStartRecordModel(Kind kind, QObject *parent = nullptr);

    /** @brief Show @p document's records of this kind; staged edits are dropped. */
    void setDocument(const simulation::HotStartDocument *document,
                     const simulation::HotStartUnits &units);

    [[nodiscard]] Kind kind() const { return m_kind; }
    [[nodiscard]] bool isDirty() const { return !m_nodeDepth.isEmpty() || !m_linkState.isEmpty(); }
    /** Staged node depths (ft) and link (flow cfs, depth ft), by record ID. */
    [[nodiscard]] const QHash<QString, double> &nodeEdits() const { return m_nodeDepth; }
    [[nodiscard]] const QHash<QString, QPair<double, double>> &linkEdits() const
    {
        return m_linkState;
    }
    /** @brief Drop every staged edit. */
    void clearEdits();
    /** @return Column of a value: "depth", "head", "volume", "flow", "runoff", ...; -1 if none. */
    [[nodiscard]] int columnOf(const QString &value) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    /** @brief A staged edit was added or dropped. */
    void dirtyChanged(bool dirty);

private:
    [[nodiscard]] QStringList valueKeys() const;
    [[nodiscard]] double internalValue(int row, const QString &key, bool *edited) const;

    Kind m_kind;
    const simulation::HotStartDocument *m_document = nullptr;
    simulation::HotStartUnits m_units;
    QHash<QString, double> m_nodeDepth;
    QHash<QString, QPair<double, double>> m_linkState;
};

/*! Non-modal hot-start inspector. */
class HotStartInspectorDialog : public QDialog
{
    Q_OBJECT

public:
    using EngineSource = std::function<SWMM_Engine()>;
    using Confirm = std::function<bool(const QString &question)>;

    /**
     * @param[in] liveEngine Supplies the open model's engine (GUI thread); may return null.
     * @param[in] parent Optional owner.
     */
    explicit HotStartInspectorDialog(EngineSource liveEngine, QWidget *parent = nullptr);

    /** @brief Read @p path; on failure the window says why and keeps the previous file. */
    bool openFile(const QString &path);
    /** @brief Ask before writing; the default asks with a message box. */
    void setConfirm(Confirm confirm) { m_confirm = std::move(confirm); }
    /** @brief Write the staged edits after confirmation; false when refused or not written. */
    bool save();
    /** @brief Drop the staged edits. */
    void revert();

    [[nodiscard]] const simulation::HotStartDocument &document() const { return m_document; }
    [[nodiscard]] HotStartRecordModel *model(HotStartRecordModel::Kind kind) const;
    [[nodiscard]] QString noticeText() const;
    [[nodiscard]] QString infoText(const QString &field) const;

signals:
    /** @brief The user asked to go to a record's object (double-click or Enter on a row). */
    void objectActivated(const SWMMObjectRef &ref);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void chooseFile();
    void refresh();
    void updateButtons();
    void activate(QTableView *view, const QModelIndex &index);

    EngineSource m_liveEngine;
    Confirm m_confirm;
    simulation::HotStartDocument m_document;
    QLabel *m_path = nullptr;
    QLabel *m_notice = nullptr;
    QHash<QString, QLabel *> m_info;
    QTabWidget *m_tabs = nullptr;
    QList<QTableView *> m_views;
    QList<HotStartRecordModel *> m_models;
    QPushButton *m_save = nullptr;
    QPushButton *m_revert = nullptr;
};

} // namespace openswmmvis::ui
