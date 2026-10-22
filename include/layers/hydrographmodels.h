/**
 * @file      hydrographmodels.h
 * @brief     Slice BS Phase 6.9.2 — hydrograph MVC layer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice BS Phase 6.9.2 — hydrograph MVC layer.
 *
 * Four QAbstractItem/Table/ListModel subclasses owned by SWMMModelLayer.
 * Each is a thin Qt view onto the engine's [HYDROGRAPHS] / [RDII_DECAY]
 * state and listens to SWMMModelLayer::hydrographChanged(name) so every
 * subscribed view (HydrographGroupEditor, SWMMHydrographPropertyAdapter,
 * Object Browser, NewDataObjectDialog, NodeCompoundEditDialog) stays in
 * sync without polling.
 *
 * Mutation is one-way: views call layer->applyHydrograph*() /
 * applyRdiiDecay*(); the layer wraps the BS-02 C API and emits
 * hydrographChanged(). The models themselves are read-only with respect
 * to engine state — their setData() implementations forward to those
 * apply* helpers and rely on the resulting signal for the refresh.
 *
 * Per-(name, month) rebinding uses setContext() rather than per-key
 * cached instances; only the editor needs (name, month)-specific views
 * and only one at a time per tab.
 *
 * See: docs/GUI_IMPLEMENTATION_PLAN.md Slice BS Phase 6.9.2.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef HYDROGRAPHMODELS_H
#define HYDROGRAPHMODELS_H

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QObject>
#include <QString>

class SWMMModelLayer;

// ---------------------------------------------------------------------------
// Group list model — one row per UH group name, in engine first-occurrence
// order. Editable in place: setData on column 0 routes through
// SWMMModelLayer::applyHydrographRenameGroup.
// ---------------------------------------------------------------------------

class HydrographGroupListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    /**
     * @brief Bind a live engine-backed Qt view to one model layer.
     * @param[in] layer Optional source, also QObject owner; null yields no readable engine state. The raw layer pointer must remain valid for all model access.
     * @note Connects hydrographChanged() once; values are read on demand rather than cached. Use on the GUI thread and serialize engine access with simulation/edits. Group names in engine first-occurrence order.
     */
    explicit HydrographGroupListModel(SWMMModelLayer *layer);

    /**
     * @param[in] parent Valid parent indices have no children in this flat model.
     * @return Nonnegative current engine group count, or zero for missing layer/engine/valid parent.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] index Model-owned valid flat cell; table models range-check rows zero through two and columns explicitly.
     * @param[in] role Qt role; supported values depend on numeric/text/check columns.
     * @return Group-name QString for DisplayRole/EditRole; invalid variant for other roles, invalid index or engine lookup failure. Names are copied through a fixed 64-byte buffer.
     * @note Reads current engine state without caching, conversion to display-unit alternatives or mutation. Group/row matching for parameter lookup is exact-case and fixed-buffer based.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Target model-owned flat cell; invalid/response-label/table-out-of-range cells fail.
     * @param[in] value New name convertible to QString.
     * @param[in] role Only EditRole is accepted.
     * @return True when the layer apply helper succeeds; false for rejected role/index/context/conversion/engine operation. No separate error text or undo entry is created here.
     * @note Renames the selected group through applyHydrographRenameGroup(). Trims value.toString(), rejects empty/equal names, and forwards engine validation. Successful rename emits layer hydrographChanged(empty), which resets subscribed models. Successful layer changes emit hydrographChanged(), supplying model refresh; setData() does not emit an additional direct dataChanged().
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] index Model-owned flat cell; invalid returns NoItemFlags.
     * @return Valid indices are enabled/selectable/editable, invalid indices NoItemFlags; source editability is not consulted.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Zero-based column ordinal.
     * @param[in] orientation Only Horizontal is supported.
     * @param[in] role Only DisplayRole is supported.
     * @return Translated Unit Hydrograph heading for column zero, otherwise invalid. Parameter values remain in native authored units.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /*! Look up the row index of a group by name, or -1 if not present.
     * @param[in] name Exact case-sensitive group text.
     * @return First matching current group row or minus one; scans names through this model and returns no borrowed engine pointer.
     */
    int indexOf(const QString &name) const;
    /*! Read the group name at a given row, or empty if out of bounds.
     * @param[in] row Zero-based current group row.
     * @return Owned name text or empty for out-of-range/engine read failure.
     */
    QString nameAt(int row) const;

private slots:
    /**
     * @param[in] uhName Ignored, because any mutation can change group membership/order.
     * @note Always begin/endResetModel(), invalidating persistent indices.
     */
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
};

// ---------------------------------------------------------------------------
// RTK table model — 3 rows × 4 cols for one (group, month). Column 0 is the
// non-editable response label (Short-Term / Medium-Term / Long-Term). Columns
// 1..3 are R, T, K. Blank cells (no engine entry yet for that response)
// render as empty QVariant — distinct from explicit 0 — so the user can
// tell which responses have data.
// ---------------------------------------------------------------------------

class HydrographRtkTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColResponse = 0, ColR = 1, ColT = 2, ColK = 3, ColCount = 4 };

    /**
     * @brief Bind a live engine-backed Qt view to one model layer.
     * @param[in] layer Optional source, also QObject owner; null yields no readable engine state. The raw layer pointer must remain valid for all model access.
     * @note Connects hydrographChanged() once; values are read on demand rather than cached. Use on the GUI thread and serialize engine access with simulation/edits. R is a dimensionless rainfall fraction; T is hours to peak; K is dimensionless recession/peak-time ratio.
     */
    explicit HydrographRtkTableModel(SWMMModelLayer *layer);

    /**
     * @param[in] parent Valid parent indices have no children in this flat model.
     * @return Three response rows (short/medium/long) for an invalid root parent, otherwise zero, even without a bound source/context.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] parent Valid parent has no child columns.
     * @return Four root columns, otherwise zero; column enums define their order.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] index Model-owned valid flat cell; table models range-check rows zero through two and columns explicitly.
     * @param[in] role Qt role; supported values depend on numeric/text/check columns.
     * @return Response text in column zero and numeric R/T/K for DisplayRole/EditRole. Missing row/read failure/empty context produces invalid numeric variants, distinct from explicit zero. R is a dimensionless rainfall fraction; T is hours to peak; K is dimensionless recession/peak-time ratio.
     * @note Reads current engine state without caching, conversion to display-unit alternatives or mutation. Group/row matching for parameter lookup is exact-case and fixed-buffer based.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Target model-owned flat cell; invalid/response-label/table-out-of-range cells fail.
     * @param[in] value Numeric variant convertible with toDouble(); no independent finite/range guard. R is a dimensionless rainfall fraction; T is hours to peak; K is dimensionless recession/peak-time ratio.
     * @param[in] role Only EditRole is accepted.
     * @return True when the layer apply helper succeeds; false for rejected role/index/context/conversion/engine operation. No separate error text or undo entry is created here.
     * @note Reads other R/T/K cells (missing treated as zero), substitutes one numeric value, then forwards applyHydrographSetRtk(). Can create the response row; IA fields are retained on an existing row. Successful layer changes emit hydrographChanged(), supplying model refresh; setData() does not emit an additional direct dataChanged().
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] index Model-owned flat cell; invalid returns NoItemFlags.
     * @return Response labels are enabled/selectable, other valid indices also editable; does not enforce context existence or engine validation.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Zero-based column ordinal.
     * @param[in] orientation Only Horizontal is supported.
     * @param[in] role Only DisplayRole is supported.
     * @return Translated response/parameter heading in the enum column order, otherwise invalid. T header includes hours.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /*! Rebind to a different (group, month). month=-1 represents the engine's
     *  "ALL" encoding. Emits modelReset().
     * @param[in] name Exact group identity, copied without existence validation; empty makes engine-backed cells blank.
     * @param[in] month Raw month code: minus one for ALL, zero through eleven for January through December. Not validated; lookup requires an exact stored month rather than falling back to ALL.
     * @note Equal context is a no-op; changed context is installed inside begin/endResetModel(), invalidating persistent indices. Does not mutate engine rows.
     */
    void setContext(const QString &name, int month);

    /**
     * @return Copied current group context, possibly empty or absent from the engine; no lookup occurs.
     */
    QString currentName() const { return m_name; }
    /**
     * @return Stored raw context month code, initially minus one for ALL; no normalization/lookup.
     */
    int currentMonth() const { return m_month; }

private slots:
    /**
     * @param[in] uhName Exact group name; a nonempty different name is ignored, empty refreshes every context.
     * @note Emits dataChanged() for all three numeric response rows without resetting shape/context; labels remain unchanged.
     */
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
    int m_month = -1;
};

// ---------------------------------------------------------------------------
// Linear IA table model — 3 rows × 4 cols. Same shape as the RTK model but
// the editable columns are Dmax / Drec / Do.
// ---------------------------------------------------------------------------

class HydrographIaTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColResponse = 0, ColDmax = 1, ColDrec = 2, ColDo = 3, ColCount = 4 };

    /**
     * @brief Bind a live engine-backed Qt view to one model layer.
     * @param[in] layer Optional source, also QObject owner; null yields no readable engine state. The raw layer pointer must remain valid for all model access.
     * @note Connects hydrographChanged() once; values are read on demand rather than cached. Use on the GUI thread and serialize engine access with simulation/edits. Dmax/Do are project rainfall-depth units (inches or millimetres); Drec is those depth units per day.
     */
    explicit HydrographIaTableModel(SWMMModelLayer *layer);

    /**
     * @param[in] parent Valid parent indices have no children in this flat model.
     * @return Three response rows (short/medium/long) for an invalid root parent, otherwise zero, even without a bound source/context.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] parent Valid parent has no child columns.
     * @return Four root columns, otherwise zero; column enums define their order.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] index Model-owned valid flat cell; table models range-check rows zero through two and columns explicitly.
     * @param[in] role Qt role; supported values depend on numeric/text/check columns.
     * @return Response text in column zero and numeric Dmax/Drec/Do for DisplayRole/EditRole. Missing row/read failure/empty context produces invalid numeric variants, distinct from zero. Dmax/Do are project rainfall-depth units (inches or millimetres); Drec is those depth units per day.
     * @note Reads current engine state without caching, conversion to display-unit alternatives or mutation. Group/row matching for parameter lookup is exact-case and fixed-buffer based.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Target model-owned flat cell; invalid/response-label/table-out-of-range cells fail.
     * @param[in] value Numeric variant convertible with toDouble(); no independent finite/range guard. Dmax/Do are project rainfall-depth units (inches or millimetres); Drec is those depth units per day.
     * @param[in] role Only EditRole is accepted.
     * @return True when the layer apply helper succeeds; false for rejected role/index/context/conversion/engine operation. No separate error text or undo entry is created here.
     * @note Reads other Dmax/Drec/Do cells (missing treated as zero), substitutes one numeric value, then forwards applyHydrographSetIa(). Can create the response row; RTK fields are retained on an existing row. Successful layer changes emit hydrographChanged(), supplying model refresh; setData() does not emit an additional direct dataChanged().
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] index Model-owned flat cell; invalid returns NoItemFlags.
     * @return Response labels are enabled/selectable, other valid indices also editable; does not enforce context existence or engine validation.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Zero-based column ordinal.
     * @param[in] orientation Only Horizontal is supported.
     * @param[in] role Only DisplayRole is supported.
     * @return Translated response/parameter heading in the enum column order, otherwise invalid. Parameter values remain in native authored units.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /*! Rebind to a different (group, month). month=-1 = ALL.
     * @param[in] name Exact group identity, copied without existence validation; empty makes engine-backed cells blank.
     * @param[in] month Raw month code: minus one for ALL, zero through eleven for January through December. Not validated; lookup requires an exact stored month rather than falling back to ALL.
     * @note Equal context is a no-op; changed context is installed inside begin/endResetModel(), invalidating persistent indices. Does not mutate engine rows.
     */
    void setContext(const QString &name, int month);

    /**
     * @return Copied current group context, possibly empty or absent from the engine; no lookup occurs.
     */
    QString currentName() const { return m_name; }
    /**
     * @return Stored raw context month code, initially minus one for ALL; no normalization/lookup.
     */
    int currentMonth() const { return m_month; }

private slots:
    /**
     * @param[in] uhName Exact group name; a nonempty different name is ignored, empty refreshes every context.
     * @note Emits dataChanged() for all three numeric response rows without resetting shape/context; labels remain unchanged.
     */
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
    int m_month = -1;
};

// ---------------------------------------------------------------------------
// Exponential IA decay table model — 3 rows × 11 cols. Season-agnostic, so
// setContext() takes only a group name. Active is a Qt::CheckStateRole on
// column 1; existence of a [RDII_DECAY] row IS the "active" flag in the
// engine model. Unchecking Active calls applyRdiiDecayRemove(); checking
// a blank row calls applyRdiiDecaySet() with sane defaults (T_ref=10).
// Snow (column 8) is a second checkbox enabling the row's degree-day snow
// model; its two numeric columns (snow_T, snow_ddf) grey out until both
// Active and Snow are checked.
// ---------------------------------------------------------------------------

class HydrographDecayTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColResponse = 0,
        ColActive = 1,
        ColKdep = 2,
        ColK0 = 3,
        ColKT = 4,
        ColTref = 5,
        ColTheta = 6,
        ColTfreeze = 7,
        ColSnow = 8,
        ColSnowT = 9,
        ColSnowDdf = 10,
        ColCount = 11
    };

    /**
     * @brief Bind a live engine-backed Qt view to one model layer.
     * @param[in] layer Optional source, also QObject owner; null yields no readable engine state. The raw layer pointer must remain valid for all model access.
     * @note Connects hydrographChanged() once; values are read on demand rather than cached. Use on the GUI thread and serialize engine access with simulation/edits. k_dep is inverse project rainfall depth; k_0/k_T are per hour; T_ref/T_freeze/snow_T are degrees C; theta is inverse degrees C; snow_ddf is rainfall depth per degree C per day.
     */
    explicit HydrographDecayTableModel(SWMMModelLayer *layer);

    /**
     * @param[in] parent Valid parent indices have no children in this flat model.
     * @return Three response rows (short/medium/long) for an invalid root parent, otherwise zero, even without a bound source/context.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] parent Valid parent has no child columns.
     * @return Eleven root columns, otherwise zero; column enums define their order.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @param[in] index Model-owned valid flat cell; table models range-check rows zero through two and columns explicitly.
     * @param[in] role Qt role; supported values depend on numeric/text/check columns.
     * @return DisplayRole response text; CheckStateRole active/snow flags; DisplayRole/EditRole numeric values only when a decay row is readable. Snow numerics are invalid while snow is off. Other unsupported/out-of-range requests return invalid. k_dep is inverse project rainfall depth; k_0/k_T are per hour; T_ref/T_freeze/snow_T are degrees C; theta is inverse degrees C; snow_ddf is rainfall depth per degree C per day.
     * @note Reads current engine state without caching, conversion to display-unit alternatives or mutation. Group/row matching for parameter lookup is exact-case and fixed-buffer based.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Target model-owned flat cell; invalid/response-label/table-out-of-range cells fail.
     * @param[in] value Numeric variant convertible with toDouble(); no independent finite/range guard. Decay checkboxes interpret exactly Qt::Checked as true.
     * @param[in] role CheckStateRole for decay Active/Snow, EditRole for numeric columns.
     * @return True when the layer apply helper succeeds; false for rejected role/index/context/conversion/engine operation. No separate error text or undo entry is created here.
     * @note Active Checked always upserts default rates zero/T_ref ten/snow off even if already active, resetting existing values; any other check value removes the row. Snow checkbox requires an existing row and retains other fields. Numeric edits copy current/default values and upsert, even for a disabled/inactive numeric cell when invoked directly; flags() is UI policy, not enforced here. Successful layer changes emit hydrographChanged(), supplying model refresh; setData() does not emit an additional direct dataChanged().
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @param[in] index Model-owned flat cell; invalid returns NoItemFlags.
     * @return Response is enabled/selectable; Active is checkable. With a usable context, inactive numeric rows are selectable but disabled, Snow requires active, and snow numerics require snow on. Without engine/context, Snow is disabled but ordinary numeric cells may remain editable. Backend mutation can still reject edits.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] section Zero-based column ordinal.
     * @param[in] orientation Only Horizontal is supported.
     * @param[in] role Only DisplayRole is supported.
     * @return Translated response/parameter heading in the enum column order, otherwise invalid. Parameter values remain in native authored units.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /*! Rebind to a different group.
     * @param[in] name Exact group identity, copied without existence validation; empty makes engine-backed cells blank.
     * @note Equal context is a no-op; changed context is installed inside begin/endResetModel(), invalidating persistent indices. Does not mutate engine rows.
     */
    void setContext(const QString &name);

    /**
     * @return Copied current group context, possibly empty or absent from the engine; no lookup occurs.
     */
    QString currentName() const { return m_name; }

private slots:
    /**
     * @param[in] uhName Exact group name; a nonempty different name is ignored, empty refreshes every context.
     * @note Always begin/endResetModel(), invalidating persistent indices.
     */
    void onHydrographChanged(const QString &uhName);

private:
    SWMMModelLayer *m_layer = nullptr;
    QString m_name;
};

#endif // HYDROGRAPHMODELS_H
