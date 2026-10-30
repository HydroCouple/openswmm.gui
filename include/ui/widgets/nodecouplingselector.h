/**
 * @file      nodecouplingselector.h
 * @brief     The node coupling selector: every 1D node with its 2D coupling structure type and a
 *            "coupleable" switch, edited in bulk (by category, map selection, search) and applied
 *            as one undoable step through SWMMModelLayer::pushNodeCoupling2DEdits.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The node coupling selector: every 1D node with its 2D coupling structure
 * type and a "coupleable" switch, edited in bulk (by category, map
 * selection, search) and applied as one undoable step through
 * SWMMModelLayer::pushNodeCoupling2DEdits. Shared by the meshing dialog's
 * "Set coupleable…" button and the Remap 1D↔2D dialog
 * (1D2D fine-scale coupling plan §7.2–7.3).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_NODECOUPLINGSELECTOR_H
#define OPENSWMMVIS_UI_WIDGETS_NODECOUPLINGSELECTOR_H

#include "layers/nodecoupling2d.h"

#include <QAbstractTableModel>
#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QVector>

class SWMMModelLayer;
class QComboBox;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTableView;

/*! Nodes and their coupling states, with edits staged until applied. */
class NodeCouplingTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColName = 0, ColCategory, ColType, ColCoupleable, ColFirstExtra };
    enum Role { CategoryRole = Qt::UserRole + 1, AllowedTypesRole };

    struct Row {
        nodecoupling2d::State original; ///< as the engine holds it
        nodecoupling2d::State edited;   ///< staged
        int category = 0;               ///< SWMMModelLayer::Category
        QString categoryLabel;
        int defaultType = nodecoupling2d::Manhole;
        QList<int> allowed;
        QStringList extra; ///< extra column values
    };

    /**
     * @brief Create an empty staged node-coupling table.
     * @param[in] parent Optional QObject owner; use with engine-backed layers on the GUI thread.
     */
    explicit NodeCouplingTableModel(QObject *parent = nullptr);

    /*! Every node of \a layer (junctions, outfalls, storage, dividers).
     * @param[in] layer Borrowed source layer read synchronously, not retained by the model. Null/no engine clears rows.
     * @note Resets all drafts to engine snapshots, skipping empty identities/failed state reads, then emits countsChanged(). Extra-column headings survive.
     */
    void load(SWMMModelLayer *layer);
    /*! Read-only columns after the built-in ones (the remap dialog's
     *  current / proposed mapping); values are set per node.
     * @param[in] headers Read-only column labels copied after the four built-in columns. Resets model indexes and resizes each row's extra values without changing staged coupling.
     */
    void setExtraColumns(const QStringList &headers);
    /**
     * @brief Replace read-only extra values for an exact node identity.
     * @param[in] node Case-sensitive stored identity; absent nodes are ignored.
     * @param[in] values Entries copied to header count; missing values become empty and surplus values are ignored. Emits dataChanged() for extra columns.
     */
    void setExtraValues(const QString &node, const QStringList &values);

    /**
     * @brief Count staged node rows at root.
     * @param[in] parent Invalid for root; valid parents have no rows.
     * @return Current row count or zero for children.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Count built-in and configured extra columns.
     * @param[in] parent Invalid for root; valid parents have no columns.
     * @return Four plus extra heading count at root, zero for children.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Read staged coupling state, allowed types, category and changed-row emphasis.
     * @param[in] index Existing nonnegative model cell.
     * @param[in] role DisplayRole gives text, EditRole the type integer, CheckStateRole the coupleable flag; custom roles give category/allowed-type list and FontRole bolds changed built-in fields.
     * @return Requested value or invalid QVariant for unavailable role/cell. Default types are annotated (default) when unauthored.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief Route an editable type or checkbox request into the local draft.
     * @param[in] index Existing type/coupleable cell.
     * @param[in] value Enum integer for EditRole or Qt::Checked to enable coupling.
     * @param[in] role EditRole on Type or CheckStateRole on Coupleable.
     * @return True for a routed request, even when a disallowed type/no-op changes nothing; false for unsupported cells/roles. No engine mutation occurs.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    /**
     * @brief Add type editing/coupleable checkboxes to base table flags.
     * @param[in] index Model cell; this method keys off its column without independent row validation.
     * @return Base flags plus ItemIsEditable for Type or ItemIsUserCheckable for Coupleable.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Supply built-in and extra horizontal headings.
     * @param[in] section Zero-based column.
     * @param[in] o Only Horizontal is supported.
     * @param[in] role Only DisplayRole is supported.
     * @return Heading or empty string for unknown extra column; invalid QVariant for another orientation/role.
     */
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    /*! Coupleable on: a NONE node takes its class default, or MANHOLE when
     *  that default is NONE (inlet / virtual junctions). Off: NONE.
     * @param[in] rows Source-model row indexes; out-of-range entries are skipped.
     * @param[in] on False authors NONE and clears parameters. True restores a usable original authored structure or class default, falling back to authored MANHOLE for default-NONE nodes.
     * @note Stages changed rows and emits countsChanged() once even if none changed.
     */
    void setCoupleable(const QList<int> &rows, bool on);
    /*! Set \a type on each row that allows it.
     * @param[in] rows Source-model indexes; invalid/disallowed rows are skipped.
     * @param[in] type Allowed engine structure enum to author. Changing type clears type-specific parameters; same-type unauthored states become authored.
     * @note Emits countsChanged() after processing; engine application is deferred.
     */
    void setType(const QList<int> &rows, int type);
    /*! Drop the rows' [2D_NODE_COUPLING] entries (class defaults apply).
     * @param[in] rows Source-model indexes; invalid entries are skipped.
     * @note Replaces staged state with unauthored class-default type and no parameters, then emits countsChanged().
     */
    void resetToDefault(const QList<int> &rows);

    /**
     * @brief Look up an exact node identity in the loaded snapshot.
     * @param[in] node Case-sensitive identity.
     * @return Row index or -1.
     */
    int rowOf(const QString &node) const { return m_index.value(node, -1); }
    /**
     * @brief Access the complete original/staged snapshot for one row.
     * @param[in] r Existing zero-based row; no bounds check occurs.
     * @return Model-owned reference invalidated by load() or other row storage changes.
     */
    const Row &rowAt(int r) const { return m_rows[r]; }
    /**
     * @brief Inspect whether a node's staged type is other than NONE.
     * @param[in] node Exact loaded identity.
     * @return False for absent nodes or staged NONE, true otherwise.
     */
    bool isCoupleable(const QString &node) const; ///< staged state
    /**
     * @brief Count loaded rows whose staged type is not NONE.
     * @return Draft coupleable count; engine state is not reread.
     */
    int coupleableCount() const;
    /*! Staged states that differ from the engine's.
     * @return Copies of staged states differing from their original engine snapshots, in model row order.
     */
    QVector<nodecoupling2d::State> changedStates() const;

signals:
    /**
     * @brief Announce load/bulk staging completion so counts can be recomputed; may emit even without a changed row.
     */
    void countsChanged();

private:
    /**
     * @brief Replace one row's staged state and announce changed built-in cells if it differs.
     * @param[in] row Required valid source-model row.
     * @param[in] s Complete staged state; no independent type/identity validation occurs here.
     */
    void stage(int row, const nodecoupling2d::State &s);

    QVector<Row> m_rows;
    QHash<QString, int> m_index;
    QStringList m_extraHeaders;
};

/*! The selector dialog: search, category filter, bulk switches, a readout
 *  "N of M nodes coupleable", and Apply (one undo step). */
class NodeCouplingSelectorDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \param mapSelection Node names selected on the map — "Select map
     *                      selection" selects their rows.
     * @brief Build a modal-capable node coupling editor over an engine snapshot.
     * @param[in] layer Borrowed model layer retained as a raw pointer; caller keeps it alive for apply operations. Null creates an empty editor that cannot apply.
     * @param[in] parent Optional QWidget owner. Apply/OK route accepted drafts through the layer undo path; Cancel leaves earlier applied changes in place.
     */
    NodeCouplingSelectorDialog(SWMMModelLayer *layer, const QStringList &mapSelection,
                               QWidget *parent = nullptr);

    /**
     * @brief Access the dialog-owned staged model for integration.
     * @return Borrowed child pointer; do not delete independently.
     */
    NodeCouplingTableModel *model() const { return m_model; }
    /*! Push the staged edits; false (with a message) when the engine
     *  refused one. Reloads the table on success.
     * @param[out] error Optional layer rejection diagnostic; a null layer returns false without necessarily writing it.
     * @return True for no staged changes or accepted pushNodeCoupling2DEdits(); successful changed pushes reload the draft. False preserves the staged table for correction.
     */
    bool applyEdits(QString *error = nullptr);

    /*! Builds the shared table + bulk-edit controls into \a host (used by
     *  this dialog and the remap dialog).
     * @param[in,out] host Required QWidget receiving an owned layout, filters, table/proxy/delegate and bulk controls; use an initially layout-free host.
     * @param[in] model Required borrowed staged model; caller must keep it alive while host controls are used.
     * @param[in] mapSelection Snapshot of exact node names for the Select map selection button; hidden filtered nodes are omitted from that selection.
     * @param[out] readout Optional destination receiving a borrowed host-owned counts label.
     * @return Borrowed host-owned table, whose proxy indexes differ from source-model row indexes. This helper stages only; engine application remains the host's responsibility.
     */
    static QTableView *buildEditor(QWidget *host, NodeCouplingTableModel *model,
                                   const QStringList &mapSelection, QLabel **readout);

private:
    SWMMModelLayer *m_layer = nullptr;
    NodeCouplingTableModel *m_model = nullptr;
    QLabel *m_readout = nullptr;
};

#endif // OPENSWMMVIS_UI_WIDGETS_NODECOUPLINGSELECTOR_H
