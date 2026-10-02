/*!
 * \file   dataobjectattributetablemodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Tabular model over one category of non-spatial data objects — pollutants,
 * land uses, aquifers, streets or inlets — so the Attribute Table dock can
 * list and bulk-edit them the same way it lists SWMM objects.
 *
 * MVC contract (CLAUDE.md §5.1): the model owns NO object data. Rows are the
 * providers of the layer's project-scoped registry (`ensureXxxRegistry()`),
 * the same staged objects the dedicated editor dialogs bind to. A cell edit
 * is a `QUndoCommand` that writes the provider and flushes the registry to
 * the engine; the registry's `providerParamsChanged` then refreshes this
 * table and any open editor dialog alike.
 *
 * Columns speak the shared `openswmmvis::ColumnSpec` vocabulary, so the
 * panel's delegates, query bar, copy/export and bulk "apply to selected
 * rows" flow work unchanged. Column 0 is the read-only object name —
 * renaming cascades into other objects and stays with the editors.
 */
#ifndef OPENSWMMVIS_UI_PANELS_DATAOBJECTATTRIBUTETABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_DATAOBJECTATTRIBUTETABLEMODEL_H

#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"
#include "ui/panels/swmmattributetablemodel.h"   // openswmmvis::ColumnSpec

#include <QAbstractTableModel>
#include <QList>
#include <QPointer>

#include <functional>

class QUndoStack;

class DataObjectAttributeTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit DataObjectAttributeTableModel(QObject *parent = nullptr);
    ~DataObjectAttributeTableModel() override;

    /*! True for the categories this model can list: pollutants, land uses,
     *  aquifers, streets and inlets. */
    [[nodiscard]] static bool supportsCategory(SWMMModelLayer::DataCategory cat);

    /*! Bind to \p layer's \p cat registry; rebuilds the columns and rows.
     *  A null layer or an unsupported category clears the model. */
    void setSource(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat);

    [[nodiscard]] SWMMModelLayer *layer() const { return m_layer.data(); }
    [[nodiscard]] SWMMModelLayer::DataCategory category() const { return m_category; }

    /*! Stack each cell edit is pushed onto. Null ⇒ edits apply directly. */
    void setUndoStack(QUndoStack *stack);
    [[nodiscard]] QUndoStack *undoStack() const;

    [[nodiscard]] QList<openswmmvis::ColumnSpec> columnSpecs() const;

    /*! Selection-bus type of the rows (Pollutant, LandUse, …). */
    [[nodiscard]] SWMMObjectRef::ObjectType objectType() const;

    /*! Object name on \p row, or empty. */
    [[nodiscard]] QString objectNameAt(int row) const;

    /*! Row of the object named \p name, or -1. */
    [[nodiscard]] int rowForName(const QString &name) const;

    /*! Write \p value into column \p key of the \p cat object named \p name
     *  on \p layer, then flush that registry to the engine. The single write
     *  path behind both setData (headless) and the undo command, so it does
     *  not depend on any model instance still being bound to that source.
     *  Returns false when the object, column or registry no longer exists. */
    static bool applyValue(SWMMModelLayer *layer,
                           SWMMModelLayer::DataCategory cat,
                           const QString &name, const QString &key,
                           const QVariant &value);

    // QAbstractTableModel ----------------------------------------------------
    int      rowCount(const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Re-read the registry's provider list and the reference-column choices,
     *  then reset. */
    void reload();

signals:
    /*! Emitted after a user-initiated cell edit commits — the same convention
     *  `SWMMAttributeTableModel::objectEdited` follows. */
    void objectEdited(const QString &name);

public:
    /*! One column: its spec plus typed access to a provider. Public only so
     *  the .cpp's per-category builders can return it. */
    struct Field {
        openswmmvis::ColumnSpec spec;
        std::function<QVariant(QObject *)>                 get;
        std::function<void(QObject *, const QVariant &)>   set;
        std::function<bool(QObject *)>                     applies;  ///< null ⇒ always
    };

private:
    void connectRegistry();
    void onProviderParamsChanged(QObject *provider);
    [[nodiscard]] bool cellApplies(int row, int col) const;
    [[nodiscard]] bool simulationRunning() const;

    QPointer<SWMMModelLayer>     m_layer;
    QPointer<QObject>            m_registry;
    QPointer<QUndoStack>         m_undoStack;
    SWMMModelLayer::DataCategory m_category = SWMMModelLayer::NumDataCategories;

    QList<Field>             m_fields;   ///< column 0 (Name) included
    QList<QPointer<QObject>> m_rows;     ///< providers in registry order
};

#endif // OPENSWMMVIS_UI_PANELS_DATAOBJECTATTRIBUTETABLEMODEL_H
