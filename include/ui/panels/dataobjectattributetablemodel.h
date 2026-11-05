/**
 * @file      dataobjectattributetablemodel.h
 * @brief     Tabular model over one category of non-spatial data objects — pollutants, land uses,
 *            aquifers, streets, inlets, time patterns, transects, LID controls or snowpacks — so the Attribute Table dock can list and bulk-edit them
 *            the same way it lists SWMM objects.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Tabular model over one category of non-spatial data objects — pollutants,
 * land uses, aquifers, streets, inlets, time patterns, transects, LID controls or snowpacks — so the Attribute Table dock can
 * list and bulk-edit them the same way it lists SWMM objects.
 *
 * MVC contract (CLAUDE.md §5.1): the model owns NO object data. Rows are the
 * providers of the layer's project-scoped registry (`ensureXxxRegistry()`),
 * the same staged objects the dedicated editor dialogs bind to. A cell edit
 * synchronizes the affected record with the engine before adding a
 * `QUndoCommand`; the registry's `providerParamsChanged` then refreshes this
 * table and any open editor dialog alike.
 *
 * Columns speak the shared `openswmmvis::ColumnSpec` vocabulary, so the
 * panel's delegates, query bar, copy/export and bulk "apply to selected
 * rows" flow work unchanged. Column 0 is the read-only object name —
 * renaming cascades into other objects and stays with the editors.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_DATAOBJECTATTRIBUTETABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_DATAOBJECTATTRIBUTETABLEMODEL_H

#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"
#include "ui/panels/swmmattributetablemodel.h" // openswmmvis::ColumnSpec

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QPointer>

#include <functional>

class QUndoStack;

class DataObjectAttributeTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QObject owner; no provider/registry/canvas/undo-stack ownership
     *   transferred.
     * @brief Create empty GUI-thread table with no source/undo stack and NumDataCategories
     *   sentinel. Does not create registries or query engine until binding.
     */
    explicit DataObjectAttributeTableModel(QObject *parent = nullptr);
    /**
     * @brief Destroy table and QObject connections; no registry flush, provider deletion or
     *   undo-stack clearing. Replay commands reference source independently of table lifetime.
     */
    ~DataObjectAttributeTableModel() override;

    /*! True for the categories this model can list: pollutants, land uses,
     *  aquifers, streets, inlets, time patterns, transects, LID controls and snowpacks.
     * @param[in] cat Any DataCategory, including sentinel/unsupported categories.
     * @return True only for pollutants, land uses, aquifers, streets, inlets, time patterns, transects, LID controls and snowpacks; pure
     *   classification, independent of source/project availability.
     */
    [[nodiscard]] static bool supportsCategory(SWMMModelLayer::DataCategory cat);

    /*! Bind to \p layer's \p cat registry; rebuilds the columns and rows.
     *  A null layer or an unsupported category clears the model.
     * @param[in] layer Optional borrowed source, guarded by QPointer; ignored for unsupported
     *   category.
     * @param[in] cat Category stored verbatim even unsupported or null source, so
     *   category/objectType may remain meaningful with zero rows.
     * @note Always disconnect old registry-to-model connections, ensure requested supported
     *   registry (may allocate/load project-scoped staged providers), connect and reset rows/specs.
     *   Equal binding still resets. Does not flush old registry, clear undo stack, subscribe to
     *   UnitSystem changes or emit objectEdited. Null layer yields zero rows but supported-category
     *   schema still built.
     */
    void setSource(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat);

    /**
     * @return Borrowed source or null after detach/destruction; unsupported category forces null
     *   even when nonnull source was passed. No ownership pin.
     */
    [[nodiscard]] SWMMModelLayer *layer() const { return m_layer.data(); }
    /**
     * @return Last requested category, initially NumDataCategories, regardless of source lifetime
     *   or category support.
     */
    [[nodiscard]] SWMMModelLayer::DataCategory category() const { return m_category; }

    /*! Stack each cell edit is pushed onto. Null ⇒ edits apply directly.
     * @param[in] stack Optional borrowed stack, guarded by QPointer; null makes future edits call
     *   static applyValue directly.
     * @note Store only; no clear/flush/push, migration of existing commands, ownership transfer or
     *   notification. Commands already on a different stack retain original source/name/key.
     */
    void setUndoStack(QUndoStack *stack);
    /**
     * @return Borrowed current edit stack or null if absent/destroyed; no source validity guarantee
     *   or ownership transfer.
     */
    [[nodiscard]] QUndoStack *undoStack() const;

    /**
     * @return Value copy of current field metadata in displayed order, Name first for supported
     *   category; enum name choices snapshot at last reload. Bounds configure editors and are checked again by applyValue. No provider callbacks exposed through copy.
     */
    [[nodiscard]] QList<openswmmvis::ColumnSpec> columnSpecs() const;

    /*! Selection-bus type of the rows (Pollutant, LandUse, …).
     * @return Selection-bus type mapped from stored category, Unknown for unsupported; can be
     *   non-Unknown with null source/empty rows. No selection mutation.
     */
    [[nodiscard]] SWMMObjectRef::ObjectType objectType() const;

    /*! Object name on \p row, or empty.
     * @param[in] row Zero-based current registry-provider row; negative/out-of-range ignored.
     * @return Current exact provider name or empty for invalid/deleted provider; no cache,
     *   uniqueness validation or ownership transfer.
     */
    [[nodiscard]] QString objectNameAt(int row) const;

    /*! Row of the object named \p name, or -1.
     * @param[in] name Case-insensitive engine identity; no trimming.
     * @return First matching provider row or -1; deleted providers name as empty, so empty lookup
     *   can match a stale guarded row before queued reset. No sorting/proxy mapping.
     */
    [[nodiscard]] int rowForName(const QString &name) const;

    /*! Write one field through the project registry and verify the engine value.
     *  Refreshes the named provider first so another view's edits are preserved.
     *  Checks conversion, bounds, choice membership, applicability and run state.
     *  Saves only this record, reloads it and restores the previous field on rejection.
     *  Success notifies the registry and layer and marks the project edited.
     *  Independent of the currently displayed table and its undo stack.
     * @return False for an unavailable source, target or field, or a rejected write.
     */
    static bool applyValue(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat,
                           const QString &name, const QString &key, const QVariant &value);
    /** Preflight every captured target, commit and verify the batch, then publish
     *  provider/layer notifications and one dirty change. A changed batch adds one
     *  Undo command; no-op/refused batches preserve history. Failure rolls back
     *  attempted records. Replay follows typed provider identities through renames,
     *  preserves independent scalar fields and rejects stale or closed contexts.
     */
    static bool applyValues(SWMMModelLayer *layer, SWMMModelLayer::DataCategory cat,
                            const QStringList &names, const QString &key, const QVariant &value,
                            QUndoStack *undo, QString *error = nullptr);
    /** Current accepted native LID record; process-local constituent identities never persist. */
    static QVariantMap lidProfile(SWMMModelLayer *layer, const QString &name);
    /** Single-control ordered profile operation (insert/remove/up/down/materials/initialize).
     * Validates the complete stack and outlet anchors before mutation; one Undo when supplied.
     */
    static bool editLidProfile(SWMMModelLayer *layer, const QString &name,
                              const QString &operation, int row, int kind,
                              QUndoStack *undo, QString *error = nullptr);
    /** Seed a newly created, empty design with documented complete starter layers. */
    static bool initializeLidDesign(SWMMModelLayer *layer, const QString &name);
    static SWMMModelLayer::DataCategory categoryForType(SWMMObjectRef::ObjectType type);
    int rowForProvider(QObject *provider) const;
    QObject *providerAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows[row].data() : nullptr; }


    // QAbstractTableModel ----------------------------------------------------
    /**
     * @param[in] parent Flat-table parent; valid returns zero.
     * @return Current guarded row-list size for invalid parent, including destroyed providers
     *   awaiting queued structural reload. No live registry query.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Flat-table parent; valid returns zero.
     * @return Cached field count for invalid parent, independent of source lifetime/row count. No
     *   schema rebuild.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected index from this current model; invalid/out-of-upper-bounds/deleted
     *   provider gives invalid QVariant.
     * @param[in] role DisplayRole, EditRole, ToolTipRole, ForegroundRole or shared attribute
     *   value/sort roles; other roles invalid.
     * @return Applicable raw provider value for EditRole, enum label or value for display; optional
     *   empty name references display blank. Inapplicable inlet fields blank with disabled
     *   text/tooltip. Custom attribute roles wrap DISPLAY data, including enum labels, rather than
     *   raw numeric enum values.
     * @note No formatting-unit conversion, numeric precision application, engine write or undo
     *   action. Applicability callbacks require live correctly typed provider;
     *   destruction-to-queued-reload interval must not be used for editable applicability queries.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /** Commit an editable field before adding its undo command. Rejected edits
     *  preserve history; accepted changes publish through the source layer.
     *  Unchanged converted values return true without adding history.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /** Apply a uniform value to editable rows as one checked, reversible undo command. */
    bool setDataForRows(int column, const QList<int> &rows, const QVariant &value, QString *error = nullptr);
    /**
     * @param[in] index Expected current model index; invalid returns base flags. Foreign/stale
     *   indexes not supported.
     * @return Base flags plus editable only for a setter-backed applicable live provider and
     *   an engine in BUILDING/OPENED state. Does not check editor bounds or mutate selection.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Zero-based logical column; horizontal out-of-range invalid. Vertical
     *   display returns section+1 without row-range check.
     * @param[in] orientation Horizontal selects field label/unit; Vertical supports DisplayRole
     *   numbering only.
     * @param[in] role Horizontal DisplayRole appends unit suffix; ToolTipRole gives Units text only
     *   when suffix exists; other roles invalid.
     * @return Source-project input-unit label, US fallback without an engine; percent always %.
     *   No model-value conversion. FLOW_UNITS changes notify the headers.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

public slots:
    /*! Synchronize one engine-backed record and notify its table row. */
    void refreshObject(const QString &name);
    /*! Re-read the registry's provider list and the reference-column choices,
     *  then reset.
     * @note begin/endResetModel rebuild specs/reference-name choices and guarded provider rows in
     *   registry order. May invalidate all view indexes/selection, no sorting/persistence or
     *   objectEdited. Does not re-ensure missing registry, flush staged edits or preserve old proxy
     *   mappings. Binding/source lifetime and GUI thread required.
     */
    void reload();

signals:
    /*! One notification per changed provider after a committed table edit.
     *  The writer already published the dirty update; this is a view hook only.
     *  Undo/Redo publish authoritative provider/layer notifications directly.
     */
    void objectEdited(const QString &name);

public:
    /*! One column: its spec plus typed access to a provider. Public only so
     *  the .cpp's per-category builders can return it. */
    struct Field {
        openswmmvis::ColumnSpec spec;
        std::function<QVariant(QObject *)> get;
        std::function<void(QObject *, const QVariant &)> set;
        std::function<bool(QObject *)> applies; ///< null ⇒ always
    };

    /**
     * @param[in] row Zero-based cached row; out-of-range false.
     * @param[in] col Zero-based cached field; out-of-range false.
     * @return True with no applicability callback, otherwise synchronous callback result for row
     *   provider. Deleted providers return false. No flags/selection/source mutation.
     */
    [[nodiscard]] bool cellApplies(int row, int col) const;

private:
    bool m_lidSchemaPending = false;
    /**
     * @brief Connect current typed registry structural changes to queued reload and parameter
     *   changes to synchronous row notification.
     * @note No-op with null registry. Caller must disconnect old connections before repeated
     *   binding, preserve category/type invariant and GUI event loop. Added/about-to-remove/renamed
     *   queue whole-model resets; no registry ownership transfer or engine flush.
     */
    void connectRegistry();
    /**
     * @param[in] provider Borrowed provider identity, compared without ownership transfer; null can
     *   match a stale null guarded row.
     * @brief Emit dataChanged for the first matching entire cached row, all roles, so inlet type
     *   applicability also refreshes.
     * @note No matching row no-op; no reload, objectEdited, unit/schema recalculation or engine
     *   write.
     */
    void onProviderParamsChanged(QObject *provider);


    QPointer<SWMMModelLayer> m_layer;
    QPointer<QObject> m_registry;
    QPointer<QUndoStack> m_undoStack;
    SWMMModelLayer::DataCategory m_category = SWMMModelLayer::NumDataCategories;

    QList<Field> m_fields;           ///< column 0 (Name) included
    QList<QPointer<QObject>> m_rows; ///< providers in registry order
    QHash<QObject *, int> m_providerRows; ///< provider notifications resolve without a row scan
};

#endif // OPENSWMMVIS_UI_PANELS_DATAOBJECTATTRIBUTETABLEMODEL_H
