/**
 * @file      mesh2daquifermodel.h
 * @brief     GG2 / GG3 (2026-09-07) — MVC models for the two-zone groundwater kernel's authoring
 *            rows: `[2D_AQUIFER]` and `[2D_AQUIFER_NODE]`.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * GG2 / GG3 (2026-09-07) — MVC models for the two-zone groundwater kernel's
 * authoring rows: `[2D_AQUIFER]` and `[2D_AQUIFER_NODE]`.
 *
 * The engine is the single source of truth. `load()` reads the rows through
 * `swmm_gw2d_row_*` / `swmm_gw2d_node_*`, edits are staged here, and
 * `commit()` publishes the staged collection after its validation checks. Staging rather than writing through
 * matters for one reason worth stating: these rows seed the kernel at
 * initialize, so the C API refuses them mid-run. A model that wrote through
 * would fail silently on half a table; a model that stages can tell the user
 * up front that the run has to be reset.
 *
 * Aquifer and node-bed values are in the PROJECT's units — the same numbers the `.inp`
 * carries and the same numbers the API takes. The dialog labels the columns
 * from `UnitSystem`; nothing converts, so what the user types is what the
 * file holds. (Converting in the GUI is how a saved model stops matching
 * what was on screen.) Boundary forcing uses its own mixed API units: HEAD in
 * project elevation, FLOW in total m³/s, concentration in native species units,
 * MASSFLUX in native amount/s/m and HEATFLUX in W/m.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DAQUIFERMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DAQUIFERMODEL_H

#include <QAbstractTableModel>
#include <QByteArray>
#include <QList>
#include <QMap>
#include <memory>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui
{

/*! \brief The `[2D_AQUIFER]` rows: scope, the five positional columns, and
 *         the law/closure selections. */
class Mesh2DAquiferModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColScope = 0, //!< "*", "TAG <t>" or "CELL <n>"
        ColTarget,    //!< the tag name or the 1-based cell index
        ColKs,
        ColZs,
        ColThetaS,
        ColThetaR,
        ColAlpha,
        ColSoil,
        ColClosure,
        ColHg0,
        ColCLoss,
        ColCount
    };

    /**
     * @brief Create an empty staged groundwater table owned by Qt.
     * @param[in] parent Optional QObject owner of this model; no engine handle is retained by
     * construction.
     * @details Use model access/signals on its owning GUI thread. Engine arguments to later
     * load/validation/commit operations are borrowed and must remain valid for each synchronous
     * call.
     */
    explicit Mesh2DAquiferModel(QObject *parent = nullptr);

    /**
     * @brief Copy soil-law tokens in the exact SWMM_GW2D_SOIL enum order.
     * @return RUSSO, GARDNER, BROOKS_COREY and VAN_GENUCHTEN, in ordinal order; changing this
     * order would change stored enum interpretation.
     *
     * @details Soil-law tokens in the engine's enum order, so the row index IS the
     * SWMM_GW2D_SOIL_* code. Do not reorder.
     */
    [[nodiscard]] static QStringList soilTokens();
    /**
     * @brief Copy closure-choice labels in combo-index order.
     * @return AUTO, CLOSED_FORM, ENSLAVED and SIGMA; index zero represents code -1 and the rest
     * map through closureCodeAt().
     *
     * @details Closure tokens. Index 0 is AUTO (code −1); the rest are 0..2 — see
     * closureCodeAt().
     */
    [[nodiscard]] static QStringList closureTokens();
    /**
     * @brief Map a closure combo index to its engine enum.
     * @param[in] index One, two or three selects CLOSED_FORM, ENSLAVED or SIGMA.
     * @return Matching engine code, or AUTO for zero or every unsupported index.
     */
    [[nodiscard]] static int closureCodeAt(int index);
    /**
     * @brief Map an engine closure code to its combo index.
     * @param[in] code CLOSED_FORM, ENSLAVED or SIGMA enum, or AUTO/unknown.
     * @return One, two or three respectively; zero for AUTO and every unsupported code.
     */
    [[nodiscard]] static int closureIndexOf(int code);

    /**
     * @brief Count the flat table's staged rows.
     * @param[in] parent Parent index; a valid parent requests children and yields zero.
     * @return Current staged row count for an invalid/root parent, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Count columns in the flat staged table.
     * @param[in] parent Parent index; valid parents have no child table.
     * @return The class's column-count enum for the root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Expose an aquifer row's staged values, inheritance labels and parameter hints.
     * @param[in] index Index belonging to this model; invalid or out-of-range rows return
     * invalid data. Valid indices must have nonnegative rows.
     * @param[in] role Display/Edit expose values, ToolTip supplies parameter guidance, and
     * Foreground greys the unused global target.
     * @return Role-specific QVariant or invalid for unsupported index/column/role. Cell targets
     * display one-based indices; global target is blank/dash. Unset soil/closure edits use -1,
     * explicit closure edits use combo indices, and negative hg0 displays model-default state.
     * Numeric values retain project units without conversion.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief Change one staged aquifer field using its local edit constraints.
     * @param[in] index Valid index belonging to an existing row of this model; only supported
     * columns may change.
     * @param[in] value Scope enum 0..2; nonempty trimmed tag or one-based positive cell;
     * positive Ks/zs/alpha; porosity in (thetaR,1]; residual in [0,thetaS); soil/closure choice
     * or -1 to inherit; optional hg0 blank or nonnegative; nonnegative deep loss. Numbers retain
     * project units.
     * @param[in] role Must be Qt::EditRole.
     * @return True after assignment and dataChanged (also for an equal accepted value), false
     * for rejected conversion/constraint/index/role. No engine write occurs.
     * @details Scope switches preserve hidden tag/cell drafts and notify both columns.
     * Positivity checks do not universally reject NaN/infinity; validateCommit() performs the
     * complete finite-value check before publication.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @brief Format display headers without reading or changing engine data.
     * @param[in] section Zero-based column or row requested by Qt; unsupported horizontal
     * columns produce an empty label.
     * @param[in] orientation Horizontal selects column labels; Vertical returns section+1 as the
     * displayed row number.
     * @param[in] role Only Qt::DisplayRole is supported.
     * @return Localized column label (including configured unit labels where applicable),
     * one-based vertical row number, or an empty/invalid QVariant for unsupported requests.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @brief Mark staged aquifer cells selectable/enabled and conditionally editable.
     * @param[in] index Index belonging to this model; a valid row must exist.
     * @return NoItemFlags for invalid index; global-scope target is read-only, all other valid
     * cells editable. Does not validate row bounds for a forged/foreign valid index.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Remove a contiguous range from staged rows only.
     * @param[in] row Zero-based first row; must be nonnegative.
     * @param[in] count Positive number of rows, with row+count no greater than rowCount().
     * @param[in] parent Must be invalid/root; valid parents are rejected.
     * @return True after begin/endRemoveRows and erasure, false for invalid range/parent with no
     * change. No engine write occurs.
     */
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    /**
     * @brief Reset drafts and baseline from current authored aquifer rows.
     * @param[in] engine Borrowed engine queried synchronously; null resets to an empty clean
     * baseline.
     * @details Copies required fields, optional-property fallbacks and soil/closure inheritance
     * flags, then emits model reset. A failed required row/ownership read records a load error
     * and can retain a partial row prefix; row-count failure yields an empty baseline. Optional
     * property read failures use documented defaults. No engine write occurs; future validation
     * refuses a recorded load error.
     *
     * @details Replace the staged rows with the engine's.
     */
    void load(SWMM_Engine engine);
    /**
     * @brief Replace dirty authored aquifer rows after concurrency and parser validation.
     * @param[in,out] engine Borrowed editable BUILDING/OPENED engine; dirty commits may replace
     * its entire aquifer-row collection.
     * @param[in] advanceBaseline True accepts staged rows as the new baseline on success; false
     * leaves dirty state for a coordinating transaction/restore.
     * @return Empty for clean/no-op or successful commit, otherwise a translated first error
     * including restoration result if publication failed.
     * @details Clean drafts return before engine checks. Dirty drafts validate on a temporary
     * engine, then remove/add all live rows sequentially; an error attempts restoreLoaded(),
     * which may itself fail. No atomic-publication guarantee is supplied by this wrapper.
     *
     * @details Apply the staged rows to the engine. Every row is rewritten (the C API
     * has no "set row"), which is also what makes a reorder work.
     */
    QString commit(SWMM_Engine engine, bool advanceBaseline = true);
    /**
     * @brief Check engine editability, unchanged baseline and staged row parser acceptance.
     * @param[in] engine Borrowed engine; must be non-null and in BUILDING/OPENED state.
     * @return Empty when baseline/load checks, finite values, hg0<=zs and temporary-engine
     * row/property installation all pass, otherwise translated error.
     * @details Reloads current live rows for exact baseline comparison, then allocates/destroys
     * a separate engine for parser checks. Does not change the supplied engine; this is not a
     * complete initialized-mesh simulation validation.
     */
    QString validateCommit(SWMM_Engine engine) const;
    /**
     * @brief Replace live aquifer rows with the retained loaded snapshot.
     * @param[in,out] engine Borrowed engine whose row collection is rewritten through
     * replaceRows().
     * @return Empty after replacement, otherwise its first error; failure can leave partial live
     * rows.
     * @details Performs no independent identity/concurrency check and does not advance/reset the
     * local draft baseline; intended for a coordinating rollback after this editor's write.
     */
    QString restoreLoaded(SWMM_Engine engine) const;
    /**
     * @brief Normalize inactive targets and adopt staged aquifer rows as the clean baseline.
     * @details Clears tag outside TAG scope and cell outside CELL scope, then copies rows to
     * m_loaded without writing the engine or emitting a model reset.
     */
    void acceptChanges();
    /**
     * @brief Compare all staged aquifer fields with the loaded baseline.
     * @return True for any exact row/order/value/provenance difference; floating NaNs compare
     * unequal and no numerical tolerance is applied.
     */
    [[nodiscard]] bool isDirty() const;

    /**
     * @brief Append a parser-default-seeded aquifer draft row and emit insertion signals.
     * @return Zero-based appended row index. Defaults are global scope, Ks=0.5, zs=5,
     * thetaS=0.45, thetaR=0.10, alpha=2 in project units, with inherited soil/closure; no engine
     * write occurs.
     *
     * @details Append a row seeded with the defaults the parser uses.
     */
    int appendRow();

    /**
     * @brief Change presentation labels for aquifer dimensions without converting stored values.
     * @param[in] lengthLabel Copied project length label for zs/hg0 and reciprocal-alpha
     * headings.
     * @param[in] rateLabel Copied conductivity/deep-loss rate label.
     * @details Emits horizontal headerDataChanged for every column; values/tooltips read the new
     * labels on demand.
     *
     * @details Column labels want the project's length/rate words.
     */
    void setUnitLabels(const QString &lengthLabel, const QString &rateLabel);

private:
    struct Row {
        int scope = 0; // SWMM_GW2D_SCOPE_*
        QString tag;
        int cell = -1; // 0-based
        double ks = 0.5;
        double zs = 5.0;
        double thetaS = 0.45;
        double thetaR = 0.10;
        double alpha = 2.0;
        int soil = 0; // SWMM_GW2D_SOIL_*
        bool soilSet = false;
        int closure = -1; // SWMM_GW2D_CLOSURE_*
        bool closureSet = false;
        double hg0 = -1.0; // < 0 = "use the option default"
        double cLoss = 0.0;
        double psiB = 0.20;
        double lambda = 0.40;
        double vgN = 1.6;
        double vgL = 0.5;
        int mLayers = -1;
        /**
         * @brief Compare every authored aquifer field and inheritance flag exactly.
         * @param[in] o Borrowed other row, including hidden soil-law parameters and inactive
         * targets.
         * @return True only when all fields match; no tolerance, canonicalization or paired-NaN
         * equivalence.
         */
        bool operator==(const Row &o) const;
    };

    /**
     * @brief Sequentially replace all engine aquifer rows with supplied values/provenance.
     * @param[in,out] engine Borrowed engine accepting authoring edits; queried for count, then
     * rows are removed in reverse order and re-added.
     * @param[in] rows Borrowed ordered rows in project units, including optional properties and
     * explicit/inherited soil/closure flags; input pointers are not retained.
     * @return Empty after every operation succeeds, otherwise first translated API failure. Can
     * leave a partially removed/installed collection; caller controls rollback.
     * @details Only explicit soil/closure and positive layer-count overrides are written,
     * preserving inheritance. Does not validate concurrency or copy the original engine state
     * itself.
     */
    QString replaceRows(SWMM_Engine engine, const QList<Row> &rows) const;
    QString m_loadError;
    QList<Row> m_rows;
    QList<Row> m_loaded;
    QString m_lengthLabel = QStringLiteral("m");
    QString m_rateLabel = QStringLiteral("mm/hr");
};

/*! \brief The `[2D_AQUIFER_NODE]` beds: which 1D node exchanges with which
 *         cell, through what conductance. */
class Mesh2DAquiferNodeModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColNode = 0, ColCell, ColKc, ColDc, ColArea, ColExchange, ColBottom, ColCount };

    /**
     * @brief Create an empty staged groundwater table owned by Qt.
     * @param[in] parent Optional QObject owner of this model; no engine handle is retained by
     * construction.
     * @details Use model access/signals on its owning GUI thread. Engine arguments to later
     * load/validation/commit operations are borrowed and must remain valid for each synchronous
     * call.
     */
    explicit Mesh2DAquiferNodeModel(QObject *parent = nullptr);

    /**
     * @brief Count the flat table's staged rows.
     * @param[in] parent Parent index; a valid parent requests children and yields zero.
     * @return Current staged row count for an invalid/root parent, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Count columns in the flat staged table.
     * @param[in] parent Parent index; valid parents have no child table.
     * @return The class's column-count enum for the root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Expose staged node-bed hydraulics and cached LID bottom choices.
     * @param[in] index Index belonging to an existing model row; invalid/out-of-range rows
     * return invalid data and valid rows must be nonnegative.
     * @param[in] role Display/Edit, parameter ToolTip, or Exchange CheckStateRole.
     * @return Role-specific value or invalid for unsupported requests. Cell numbers are
     * one-based or AUTO; zero area displays inherited cell area; Exchange exposes bool/check
     * state; bottom mode is an ordinal or its label, with a no-storage-LID message when
     * unavailable. Numbers retain project length/rate/area units.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief Edit one authored node-bed draft or a cached LID bottom destination.
     * @param[in] index Existing row/column belonging to this model; automatic rows are refused.
     * @param[in] value Nonempty trimmed node ID, AUTO or positive one-based cell, finite
     * nonnegative Kc/thickness/area (blank area means zero), exchange bool/check state, or
     * bottom enum. Values retain project units.
     * @param[in] role Qt::EditRole, or Qt::CheckStateRole only for Exchange.
     * @return True after an accepted edit and dataChanged (possibly equal values), false on
     * invalid input/context/column; no engine write.
     * @details Local editing permits zero thickness with positive Kc temporarily;
     * validateCommit() rejects that combination. Bottom enum conversion uses toInt() without an
     * explicit conversion-success check.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @brief Format display headers without reading or changing engine data.
     * @param[in] section Zero-based column or row requested by Qt; unsupported horizontal
     * columns produce an empty label.
     * @param[in] orientation Horizontal selects column labels; Vertical returns section+1 as the
     * displayed row number.
     * @param[in] role Only Qt::DisplayRole is supported.
     * @return Localized column label (including configured unit labels where applicable),
     * one-based vertical row number, or an empty/invalid QVariant for unsupported requests.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @brief Apply automatic-row, Exchange-checkbox and LID-bottom availability edit policies.
     * @param[in] index Index belonging to a valid existing row; foreign/forged row bounds are
     * not checked.
     * @return Invalid yields NoItemFlags; automatic rows and absent-LID bottom cells are
     * selectable/read-only; Exchange is user-checkable and other valid authored fields editable.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Remove a contiguous range from staged rows only.
     * @param[in] row Zero-based first row; must be nonnegative.
     * @param[in] count Positive number of rows, with row+count no greater than rowCount().
     * @param[in] parent Must be invalid/root; valid parents are rejected.
     * @return True after begin/endRemoveRows and erasure, false for invalid range/parent with no
     * change. No engine write occurs; this method does not independently forbid removing a
     * model-owned automatic node row.
     */
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    /**
     * @brief Reset draft/baseline node beds and cache LID ownership/geometry fingerprints.
     * @param[in] engine Borrowed engine read synchronously; null creates an empty
     * baseline/cache.
     * @details Copies authoring rows/flags, LID bottom modes, footprint area, control
     * descriptions and exact geometry/material signatures used by concurrency checks. Required
     * row/flag/bottom/layer failures retain a load error and may leave partial caches; some
     * optional geometry API results use initialized fallbacks. Emits model reset and never
     * writes the engine.
     */
    void load(SWMM_Engine engine);
    /**
     * @brief Atomically replace dirty node beds and LID bottom modes after validation.
     * @param[in,out] engine Borrowed editable BUILDING/OPENED engine, whose expected baseline
     * must still match.
     * @param[in] advanceBaseline True adopts staged rows/bottoms on success; false retains the
     * prior baseline for coordinated restoration.
     * @return Empty for clean/no-op or success, otherwise validation/transaction error; failed
     * native replacement changes no node beds.
     * @details Clean drafts return before engine validation. Dirty commits include exact row,
     * LID ownership and geometry checks before the native compare-and-replace transaction.
     */
    QString commit(SWMM_Engine engine, bool advanceBaseline = true);
    /**
     * @brief Check node-bed draft values, ownership, live geometry and optional receiver
     * eligibility.
     * @param[in] engine Borrowed non-null engine in BUILDING/OPENED state; read/validated
     * without publication.
     * @param[in] validateReceiver True asks the native replacement transaction to validate dirty
     * receiver/bottom changes with apply=false; false stops after local/context checks for
     * coordinated multi-model edits.
     * @return Empty after checks pass, otherwise
     * load/state/concurrency/name/value/native-transaction error.
     * @details Requires unique exact node names, existing nodes, finite nonnegative bed values,
     * positive thickness for positive Kc and valid explicit/AUTO cell semantics.
     */
    QString validateCommit(SWMM_Engine engine, bool validateReceiver = true) const;
    /**
     * @brief Restore loaded node beds/bottoms if no unrelated live geometry or row edit
     * intervened.
     * @param[in,out] engine Borrowed engine; native replacement compares expected staged
     * rows/bottoms before installing loaded ones.
     * @return Empty when already restored or successful, otherwise load/geometry/transaction
     * error with no native node-bed publication on transaction failure.
     * @details Reloads live state, refuses changed LID geometry and leaves this model's
     * draft/baseline unchanged.
     */
    QString restoreLoaded(SWMM_Engine engine) const;
    /**
     * @brief Adopt staged node beds and LID bottom modes as the local clean baseline.
     * @details Copies collections only; does not query/write engine or emit reset signals.
     */
    void acceptChanges();
    /**
     * @brief Compare draft rows and LID bottom modes with their loaded baselines.
     * @return True for any exact row/order/value or bottom-map difference; no numerical
     * tolerance.
     */
    [[nodiscard]] bool isDirty() const;
    /**
     * @brief Append a local authored connection with default direct-contact bed values.
     * @param[in] node Borrowed node identity copied without existence/uniqueness validation.
     * @param[in] cell Zero-based cell index; any negative value selects AUTO location and is
     * retained until validation.
     * @return New zero-based row index after insertion signals. Kc/thickness/area default to
     * zero, exchange enabled and automatic ownership false; no engine write.
     */
    int appendRow(const QString &node, int cell);

    /**
     * @brief Copy staged node-bed state and concurrency caches into a separately owned model.
     * @return Parentless shared_ptr model copying drafts, baselines, bottom maps,
     * geometry/footprint/control caches and load error. Unit labels remain the new object's
     * defaults, not the source labels; QObject signal connections are not copied.
     */
    std::shared_ptr<Mesh2DAquiferNodeModel> transactionCopy() const;
    /**
     * @brief Copy cached storage-LID node identities.
     * @return QMap key-order name list from m_bottoms, independent of whether a staged bed row
     * exists.
     */
    QStringList lidNodes() const { return m_bottoms.keys(); }
    /**
     * @brief Read one cached LID bottom-mode ordinal.
     * @param[in] node Exact cached node identity, not normalized or looked up in the engine.
     * @return Stored mode (zero keep behavior, one sealed, two mesh aquifer), or -1 when no
     * cached storage-LID assignment exists.
     */
    int bottomDestination(const QString &node) const { return m_bottoms.value(node, -1); }
    /**
     * @brief Stage a bottom-mode change for an existing cached LID node.
     * @param[in] node Exact cached identity; must be present in m_bottoms.
     * @param[in] mode Zero keep behavior, one sealed or two mesh aquifer.
     * @return False for missing identity or mode outside 0..2, otherwise true after assignment,
     * even if unchanged.
     * @details Emits dataChanged for that node's first staged bed row when present; may update
     * the map without any bed row. No engine mutation or automatic-row guard occurs here.
     */
    bool setBottomDestination(const QString &node, int mode);
    /**
     * @brief Find the first staged bed row with an exact node identity.
     * @param[in] node Borrowed case-sensitive name to match.
     * @return Zero-based first row, or -1 when absent; no engine lookup.
     */
    int bedRow(const QString &node) const;
    /**
     * @brief Stage the cached positive geometric LID base area as a node bed's contact area.
     * @param[in] node Exact cached identity whose footprint must be finite and positive.
     * @return Empty on success, otherwise missing/nonpositive area or automatic-row read-only
     * explanation.
     * @details Appends an AUTO-located authored row if none exists, then writes area in project
     * area units and emits row dataChanged. Existing automatic rows are refused; no engine write
     * occurs.
     */
    QString useLidFootprint(const QString &node);
    /**
     * @brief Describe cached LID control, before/after bottom mode and staged bed connection.
     * @param[in] node Exact cached node identity; no live engine query is made.
     * @return Owned translated summary, or an assignment prerequisite when not in the LID cache.
     * Includes footprint units, receiver/location, contact area, bed/exchange mode and
     * sealed/aquifer semantics; not a validation result.
     */
    QString connectionSummary(const QString &node) const;
    /**
     * @brief Copy LID bottom labels in the engine-mode ordinal order.
     * @return Keep current behavior, Sealed and Mesh aquifer for modes zero, one and two.
     */
    static QStringList bottomLabels();

    /**
     * @brief Replace displayed node-bed dimensions without converting cached or staged numbers.
     * @param[in] lengthLabel Copied bed-thickness/length heading label.
     * @param[in] rateLabel Copied bed-conductivity rate label.
     * @param[in] areaLabel Copied exchange/footprint area label.
     * @details Emits horizontal headerDataChanged across the table.
     */
    void setUnitLabels(const QString &lengthLabel, const QString &rateLabel,
                       const QString &areaLabel);

private:
    struct Row {
        QString node;
        int cell = 0;    // 0-based
        double kc = 0.0; // 0 = direct Darcy, no semi-confining bed
        double dc = 0.0;
        double area = 0.0; // 0 = the cell's own area
        int locate = 0, exchange = 1, automatic = 0;
        /**
         * @brief Compare node identity, location/ownership flags and all bed values exactly.
         * @param[in] o Borrowed other row.
         * @return True for exact node/cell/Kc/thickness/area/locate/exchange/automatic equality;
         * no tolerance or NaN equivalence.
         */
        bool operator==(const Row &o) const;
    };

    /**
     * @brief Validate or publish one native expected-state node-bed/bottom transaction.
     * @param[in,out] engine Borrowed engine whose current rows/bottoms must match expected
     * state; apply=false performs native validation without mutation.
     * @param[in] expected Borrowed previously loaded/staged rows used as the compare-and-replace
     * precondition.
     * @param[in] rows Borrowed desired ordered rows in project units.
     * @param[in] expectedBottoms Borrowed node-to-prior-mode map; key set must match bottoms.
     * @param[in] bottoms Borrowed desired node-to-mode map; each node must resolve in engine.
     * @param[in] apply True publishes the native replacement; false requests validation only.
     * @return Empty on SWMM_OK, otherwise key/name/transaction error; the native failed
     * transaction preserves node beds.
     * @details Owns temporary UTF-8 buffers for the duration of the API call and retains no
     * caller pointer.
     */
    QString replaceRows(SWMM_Engine engine, const QList<Row> &expected, const QList<Row> &rows,
                        const QMap<QString, int> &expectedBottoms,
                        const QMap<QString, int> &bottoms, bool apply = true) const;
    QMap<QString, int> m_bottoms, m_loadedBottoms;
    QMap<QString, double> m_footprints;
    QMap<QString, QString> m_controls;
    QMap<QString, QList<double>> m_geometry;
    QString m_loadError;
    QList<Row> m_rows;
    QList<Row> m_loaded;
    QString m_lengthLabel = QStringLiteral("m");
    QString m_rateLabel = QStringLiteral("mm/hr");
    QString m_areaLabel = QStringLiteral("m²");
};

/*! Staged hydraulic edge and chemistry records, one shared model for editors. */
class Mesh2DAquiferBoundaryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Cell, Edge, Species, Kind, Value, Series, Columns };
    struct Row {
        int cell = 0, edge = 0;
        QString species, kind = "FLOW", series;
        double value = 0;
        /**
         * @brief Compare all boundary-row members by default exact equality.
         * @return True when the unnamed const Row reference has identical zero-based cell/edge,
         * species/kind/series text and constant value. No canonicalization, tolerance or NaN
         * equivalence.
         */
        bool operator==(const Row &) const = default;
    };
    /**
     * @brief Create an empty staged groundwater table owned by Qt.
     * @param[in] parent Optional QObject owner of this model; no engine handle is retained by
     * construction.
     * @details Use model access/signals on its owning GUI thread. Engine arguments to later
     * load/validation/commit operations are borrowed and must remain valid for each synchronous
     * call.
     */
    explicit Mesh2DAquiferBoundaryModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    /**
     * @brief Count the flat table's staged rows.
     * @param[in] parent Parent index; a valid parent requests children and yields zero.
     * @return Current staged row count for an invalid/root parent, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    /**
     * @brief Count columns in the flat staged table.
     * @param[in] parent Parent index; valid parents have no child table.
     * @return The class's column-count enum for the root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : Columns;
    }
    /**
     * @brief Expose staged water/species edge records and their mixed-unit guidance.
     * @param[in] index Borrowed model index; invalid or negative/out-of-range row returns
     * invalid data.
     * @param[in] role Display/Edit provide values; ToolTip provides cell/edge/unit/series
     * semantics.
     * @return One-based displayed cell, zero-based local edge, Water placeholder for empty
     * species, kind/series text or constant (17 significant digits for display), otherwise
     * invalid data.
     * @details HEAD is project elevation, FLOW total m³/s positive inward, CONC native
     * concentration, MASSFLUX native amount/s/m and HEATFLUX W/m. A series replaces the
     * constant; values are not converted here.
     */
    QVariant data(const QModelIndex &, int role = Qt::DisplayRole) const override;
    /**
     * @brief Format display headers without reading or changing engine data.
     * @param[in] section Zero-based column or row requested by Qt; unsupported horizontal
     * columns produce an empty label.
     * @param[in] orientation Horizontal selects column labels; Vertical returns section+1 as the
     * displayed row number.
     * @param[in] role Only Qt::DisplayRole is supported.
     * @return Localized column label (including configured unit labels where applicable),
     * one-based vertical row number, or an empty/invalid QVariant for unsupported requests.
     */
    QVariant headerData(int, Qt::Orientation, int role = Qt::DisplayRole) const override;
    /**
     * @brief Expose the boundary model's global editable flag through item flags.
     * @param[in] index Borrowed index; only isValid() is checked, not row ownership/bounds.
     * @return NoItemFlags for invalid index; otherwise enabled/selectable plus editable only
     * when m_editable is true.
     */
    Qt::ItemFlags flags(const QModelIndex &) const override;
    /**
     * @brief Stage one changed edge-record field after basic type/range validation.
     * @param[in] index Existing row/column of this model; invalid/negative/out-of-range rows are
     * refused.
     * @param[in] value Positive one-based cell, local edge 0..3, trimmed species/series,
     * uppercase kind or finite constant in the record's native units. Case-insensitive Water
     * normalizes to empty species.
     * @param[in] role Must be Qt::EditRole, and model must be editable.
     * @return True only for a changed accepted value after dataChanged; false for
     * unchanged/invalid/edit-disabled requests. No engine write.
     * @details Species availability, kind compatibility, mesh-edge extent and time-series
     * existence are checked later by validateCommit().
     */
    bool setData(const QModelIndex &, const QVariant &, int role = Qt::EditRole) override;
    /**
     * @brief Insert a default Water/FLOW draft after the current leading water-row prefix.
     * @details No-ops when read-only; default zero-based cell/edge and zero constant are
     * editable placeholders, not a validated engine boundary. Emits insertion signals only.
     */
    void addHydraulic();
    /**
     * @brief Append a placeholder concentration draft for later species selection.
     * @details No-ops when read-only; inserts species "Choose species", kind CONC and zero
     * cell/edge/value, with model insertion signals. Does not validate or write the engine.
     */
    void addChemistry();
    /**
     * @brief Remove one local boundary draft row with Qt removal signals.
     * @param[in] row Zero-based existing row; invalid index or read-only model is a no-op.
     * @details Does not remove an engine boundary until a successful commit.
     */
    void remove(int row);
    /**
     * @brief Set the boundary model's local editing gate.
     * @param[in] value True enables ordinary item edits/add/remove; false rejects them.
     * @details Stores the flag only, without notifying views or blocking explicit load/commit
     * operations.
     */
    void setEditable(bool value) { m_editable = value; }
    /**
     * @brief Reset drafts/baseline from water/chemistry records and capture mesh/species
     * identity.
     * @param[in] engine Borrowed engine queried synchronously; invalid state/read errors are
     * retained in m_error.
     * @return Empty on full record/identity read, otherwise first error. Partial record prefixes
     * can still replace the model and baseline; validation later refuses m_error.
     * @details Reads water before chemistry, emits model reset and never writes the engine.
     */
    QString load(SWMM_Engine);
    /**
     * @brief Compare canonical staged boundaries with the loaded ordering/content.
     * @return True when stable water-first canonical rows differ exactly from m_loaded; pure
     * interleaving changes can compare clean, with no float tolerance.
     */
    bool isDirty() const { return canonical(m_rows) != m_loaded; }
    /**
     * @brief Check editable engine state, identity/baseline consistency and optional physical
     * eligibility.
     * @param[in] engine Borrowed BUILDING/OPENED engine read without publication.
     * @param[in] physical True validates mesh coverage, exterior edges, enabled species,
     * units/kinds and inflow concentrations; false performs context/concurrency checks only for
     * coordinated edits.
     * @return Empty after checks pass, otherwise retained load error, invalid state, changed
     * mesh/species units, changed live rows or physical-validation error.
     * @details Does not consult the model's local m_editable UI flag.
     */
    QString validateCommit(SWMM_Engine, bool physical = true) const;
    /**
     * @brief Replace canonical boundary records and attempt restoration after write failure.
     * @param[in,out] engine Borrowed editable engine whose identity/current records must match
     * the loaded snapshot.
     * @param[in] accept True canonicalizes/adopts the successful draft as the baseline; false
     * preserves the old baseline for a coordinating transaction.
     * @return Empty after successful/no-op validation and commit, otherwise first error with
     * restoration failure details when needed.
     * @details Validates even a clean draft. Writes are sequential, not atomic; a failure
     * attempts write(m_loaded), which can also fail and leave partial live records. Does not
     * gate on local m_editable.
     */
    QString commit(SWMM_Engine, bool accept = true);
    /**
     * @brief Restore loaded boundaries only while mesh/species identity and expected live rows
     * match.
     * @param[in,out] engine Borrowed engine; may be rewritten sequentially after identity and
     * row checks.
     * @return Empty when already restored or on success, otherwise
     * identity/read/concurrency/write error.
     * @details Refuses unrelated live row changes unless live rows equal the current canonical
     * draft. write() failure may leave partial restoration; local drafts/baseline remain
     * unchanged.
     */
    QString restoreLoaded(SWMM_Engine);
    /**
     * @brief Canonicalize staged rows to water-first order and adopt them as the baseline.
     * @details Emits begin/endResetModel while replacing both collections; no engine write or
     * additional validation.
     */
    void acceptChanges();
    /**
     * @brief Copy staged boundaries and concurrency identity into a parentless shared model.
     * @return shared_ptr owning copied draft/baseline/load-error/identity state. QObject
     * connections are not copied and editable resets to the new model's default true even when
     * the source was read-only.
     */
    std::shared_ptr<Mesh2DAquiferBoundaryModel> transactionCopy() const;

private:
    /**
     * @brief Stable-partition a boundary-row copy into water followed by chemistry.
     * @param[in] rows Borrowed input list; empty species identifies water, nonempty identifies
     * chemistry.
     * @return New owned list preserving relative order inside each partition; does not sort,
     * deduplicate or validate values.
     */
    static QList<Row> canonical(const QList<Row> &);
    /**
     * @brief Replace an output list from current hydraulic and chemistry API records.
     * @param[in] engine Borrowed engine queried synchronously.
     * @param[out] rows Cleared first, then water and chemistry records appended; contains a
     * partial prefix on failure and must be discarded then.
     * @return Empty after all reads succeed, otherwise first count/read error.
     * @details Copies zero-based IDs and native values using fixed 32-KiB series/4-KiB species
     * buffers; this wrapper does not independently detect successful API string truncation.
     */
    static QString read(SWMM_Engine, QList<Row> &);
    /**
     * @brief Sequentially replace engine chemistry/hydraulic records and verify read-back
     * equality.
     * @param[in,out] engine Borrowed engine accepting authoring changes; existing chemistry is
     * removed first, then water.
     * @param[in] rows Borrowed already-validated rows in mixed native units; owned UTF-8
     * temporaries last through each setter call.
     * @return Empty only when all operations succeed and read-back equals canonical(rows); first
     * removal/set/read/mismatch error otherwise.
     * @details Can leave partial engine state; no concurrency guard, rollback or physical
     * validation is performed inside this helper.
     */
    static QString write(SWMM_Engine, const QList<Row> &);
    /**
     * @brief Validate water/species edge records against mesh, aquifer and enabled transport
     * state.
     * @param[in] engine Borrowed engine read synchronously for topology, aquifer rows,
     * species/unit options and time-series catalog.
     * @param[in] rows Borrowed proposed records in native units; empty succeeds immediately.
     * @return Empty when values/indices, series delimiters/existence, supported kinds/species,
     * unique edge terms, exterior hydraulic edges and active aquifer coverage pass, otherwise
     * the first explanatory error.
     * @details Every chemistry term needs a hydraulic edge. Potential inflow (HEAD, positive
     * FLOW or a series) needs concentrations for all enabled species; MASSFLUX requires declared
     * mass/count basis and HEATFLUX temperature. Imposed flux terms may coexist with one
     * concentration term per species/edge. No engine mutation occurs.
     */
    static QString validate(SWMM_Engine, const QList<Row> &);
    /**
     * @brief Fingerprint current mesh geometry/connectivity and pollutant/reaction unit
     * identities.
     * @param[in] engine Borrowed engine queried synchronously; geometry and species API failures
     * are reported.
     * @param[out] result Replaced with SHA-256 bytes only on success, unchanged on failure.
     * @return Empty after all identity inputs are read, otherwise first
     * mesh/coordinate/connectivity/species-unit error.
     * @details Hashes native representations and length-prefixed UTF-8 strings for local
     * concurrency detection, not a portable serialization or a complete model-state fingerprint.
     */
    static QString identity(SWMM_Engine, QByteArray &);
    QList<Row> m_rows, m_loaded;
    bool m_editable = true;
    QString m_error;
    QByteArray m_identity;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DAQUIFERMODEL_H
