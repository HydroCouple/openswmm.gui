/*!
 * \file   nodecouplingselector.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The node coupling selector: every 1D node with its 2D coupling structure
 * type and a "coupleable" switch, edited in bulk (by category, map
 * selection, search) and applied as one undoable step through
 * SWMMModelLayer::pushNodeCoupling2DEdits. Shared by the meshing dialog's
 * "Set coupleable…" button and the Remap 1D↔2D dialog
 * (1D2D fine-scale coupling plan §7.2–7.3).
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
        nodecoupling2d::State original;   ///< as the engine holds it
        nodecoupling2d::State edited;     ///< staged
        int         category = 0;         ///< SWMMModelLayer::Category
        QString     categoryLabel;
        int         defaultType = nodecoupling2d::Manhole;
        QList<int>  allowed;
        QStringList extra;                ///< extra column values
    };

    explicit NodeCouplingTableModel(QObject *parent = nullptr);

    /*! Every node of \a layer (junctions, outfalls, storage, dividers). */
    void load(SWMMModelLayer *layer);
    /*! Read-only columns after the built-in ones (the remap dialog's
     *  current / proposed mapping); values are set per node. */
    void setExtraColumns(const QStringList &headers);
    void setExtraValues(const QString &node, const QStringList &values);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

    /*! Coupleable on: a NONE node takes its class default, or MANHOLE when
     *  that default is NONE (inlet / virtual junctions). Off: NONE. */
    void setCoupleable(const QList<int> &rows, bool on);
    /*! Set \a type on each row that allows it. */
    void setType(const QList<int> &rows, int type);
    /*! Drop the rows' [2D_NODE_COUPLING] entries (class defaults apply). */
    void resetToDefault(const QList<int> &rows);

    int  rowOf(const QString &node) const { return m_index.value(node, -1); }
    const Row &rowAt(int r) const { return m_rows[r]; }
    bool isCoupleable(const QString &node) const;   ///< staged state
    int  coupleableCount() const;
    /*! Staged states that differ from the engine's. */
    QVector<nodecoupling2d::State> changedStates() const;

signals:
    void countsChanged();

private:
    void stage(int row, const nodecoupling2d::State &s);

    QVector<Row>        m_rows;
    QHash<QString, int> m_index;
    QStringList         m_extraHeaders;
};

/*! The selector dialog: search, category filter, bulk switches, a readout
 *  "N of M nodes coupleable", and Apply (one undo step). */
class NodeCouplingSelectorDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \param mapSelection Node names selected on the map — "Select map
     *                      selection" selects their rows. */
    NodeCouplingSelectorDialog(SWMMModelLayer *layer, const QStringList &mapSelection,
                               QWidget *parent = nullptr);

    NodeCouplingTableModel *model() const { return m_model; }
    /*! Push the staged edits; false (with a message) when the engine
     *  refused one. Reloads the table on success. */
    bool applyEdits(QString *error = nullptr);

    /*! Builds the shared table + bulk-edit controls into \a host (used by
     *  this dialog and the remap dialog). */
    static QTableView *buildEditor(QWidget *host, NodeCouplingTableModel *model,
                                   const QStringList &mapSelection, QLabel **readout);

private:
    SWMMModelLayer         *m_layer = nullptr;
    NodeCouplingTableModel *m_model = nullptr;
    QLabel                 *m_readout = nullptr;
};

#endif // OPENSWMMVIS_UI_WIDGETS_NODECOUPLINGSELECTOR_H
