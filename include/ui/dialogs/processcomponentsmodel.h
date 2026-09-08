/*!
 * \file   processcomponentsmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * U1 (2026-09-07) — MVC model for the model's [PROCESS_COMPONENTS]
 * registrations (Simulation Options → Files / Output / Plugins → Process
 * components). The engine is the single source of truth: load() reads the
 * registrations through swmm_process_component_*, edits are staged in the
 * model, and commit() applies the difference (remove + register — the C API
 * has no "set config"). The reaction editor's one-step "create component +
 * config file" registers straight into the engine; a dialog that re-loads
 * on show sees it.
 *
 * The Id column offers the engine's built-in catalogue
 * (swmm_process_component_known_*) and still accepts a free-form id, so a
 * component library id (PROG stream D, later) can be typed today.
 */
#ifndef PROCESSCOMPONENTSMODEL_H
#define PROCESSCOMPONENTSMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStyledItemDelegate>

#include <openswmm/engine/openswmm_engine.h>

class ProcessComponentsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColId = 0, ColConfig = 1, ColStatus = 2, ColCount };

    struct KnownId {
        QString id;
        QString description;
        bool    implemented = false;
    };

    explicit ProcessComponentsModel(QObject *parent = nullptr);

    //! The engine's built-in catalogue (static for the process).
    static QList<KnownId> knownIds();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count,
                    const QModelIndex &parent = QModelIndex()) override;

    //! Replace the rows with the engine's registrations. \p modelDir resolves
    //! relative config paths for the Status column.
    void load(SWMM_Engine engine, const QString &modelDir);
    //! Apply staged edits to the engine. Returns the number of engine writes.
    int  commit(SWMM_Engine engine);
    //! True when the rows differ from what load() read.
    bool isDirty() const;

    int  appendRow(const QString &id, const QString &config);
    QString idAt(int row) const;
    QString configAt(int row) const;

private:
    struct Row {
        QString id;
        QString config;
        QString resolved;   //!< path the engine read the file from (may be empty)
    };
    QString statusFor(const Row &r) const;

    QList<Row> m_rows;
    QList<Row> m_loaded;
    QString    m_modelDir;
};

//! Combo editor over the built-in catalogue, editable for free-form ids.
class ProcessComponentIdDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ProcessComponentIdDelegate(QObject *parent = nullptr);
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

#endif // PROCESSCOMPONENTSMODEL_H
