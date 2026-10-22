/**
 * @file      processcomponentsmodel.h
 * @brief     U1 (2026-09-07) — MVC model for the model's [PROCESS_COMPONENTS] registrations
 *            (Simulation Options → Files / Output / Plugins → Process components).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
        bool implemented = false;
    };

    /**
     * @brief Create an empty process-registration draft model.
     * @param parent Optional QObject owner; null leaves it caller-owned.
     * Use on the GUI thread. Owns copied row/baseline strings, without retaining an
     * engine handle or opening component configuration files.
     */
    explicit ProcessComponentsModel(QObject *parent = nullptr);

    //! The engine's built-in catalogue (static for the process).
    /**
     * @brief Copy the once-per-process built-in component catalogue.
     * @return Owned list of ID/description/implemented values cached on first call.
     * The initializer queries engine catalogue APIs using 160/256-byte text buffers
     * and skips failed rows; later calls do not refresh the catalogue or discover
     * external component libraries.
     */
    static QList<KnownId> knownIds();

    /**
     * @brief Count top-level staged registration rows.
     * @param parent Valid parents have no children in this flat table.
     * @return Draft size at root, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Count the component ID/config/status columns.
     * @param parent Valid parents have no child columns.
     * @return Three at root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Read a staged component cell, status or tooltip.
     * @param index Borrowed index from this model with a current nonnegative row;
     * invalid or past-end indices return empty.
     * @param role Display/Edit returns ID/config/status text; ToolTip returns a known
     * ID description or config provenance. Other roles return empty.
     * @return Owned QVariant. Status performs an existence check, resolving relative
     * config paths under the loaded model directory; it does not parse configuration.
     * Resolved-path tooltip metadata remains from load until the next reload, even
     * when the staged config changes. No engine read is performed here.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief Trim and stage a component ID or config-path edit.
     * @param index Borrowed current cell from this model with a nonnegative row.
     * @param value Converts to QString and is trimmed on both ends.
     * @param role Must be EditRole.
     * @return True only for a real accepted ID/config change; false for unchanged
     * text, unsupported role/column or invalid/past-end row. Emits dataChanged across
     * all three columns on change. Does not validate IDs/duplicates/path contents,
     * refresh resolved provenance or apply an engine edit.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @brief Localize the component table column captions.
     * @param section Zero-based ID/config/status column.
     * @param orientation Must be Horizontal.
     * @param role Must be DisplayRole.
     * @return Owned caption or empty QVariant for other requests.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @brief Expose ID/config as editable and status as read-only.
     * @param index Borrowed model index; only isValid and column are inspected,
     * without current-row or model-identity validation.
     * @return NoItemFlags when invalid; otherwise enabled/selectable and editable
     * for every column except ColStatus.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Remove a contiguous staged registration range.
     * @param row Zero-based first row.
     * @param count Positive count fitting the remaining rows and int index arithmetic.
     * @param parent Must be invalid for the flat model.
     * @return True after removal notifications, false for invalid parent/range.
     * Does not remove engine registrations or delete component config files.
     */
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    //! Replace the rows with the engine's registrations. \p modelDir resolves
    //! relative config paths for the Status column.
    /**
     * @brief Replace the draft and dirty baseline with current engine registrations.
     * @param engine Borrowed loaded engine for the duration of the read, or null to
     * clear rows/baseline. No engine handle is retained.
     * @param modelDir Copied directory used to resolve relative config paths for
     * status; an empty directory leaves paths relative to the process working directory.
     * Resets the model, discards pending edits and skips failed getters. Copies IDs
     * and configured/resolved paths through 160/1024/1024-byte buffers; no error result
     * is exposed and no component config file is opened.
     */
    void load(SWMM_Engine engine, const QString &modelDir);
    //! Apply staged edits to the engine. Returns the number of engine writes.
    /**
     * @brief Apply the staged registration difference and reload actual engine state.
     * @param engine Borrowed mutable engine in an API-permitted editing state; null
     * is a no-op. Caller coordinates model lifetime and concurrent/external edits.
     * @return Count of successful remove/register calls, zero for null/clean model.
     * Removes current engine rows absent as exact ID/config pairs in the draft, then
     * registers nonblank IDs not found in the engine. Failed getters/writes are
     * ignored; no rollback or per-row error is returned. Duplicate staged IDs may
     * collapse, and external registrations absent from the draft can be removed.
     * Always reloads after a dirty non-null attempt, discarding rejected draft rows
     * and resetting the baseline even after partial failure. Does not write input files.
     */
    int commit(SWMM_Engine engine);
    //! True when the rows differ from what load() read.
    /**
     * @brief Compare staged rows with the last loaded baseline.
     * @return True on size/order/exact ID or config differences. Resolved-path metadata
     * is ignored; a dirty draft does not prove any engine change has occurred.
     */
    bool isDirty() const;

    /**
     * @brief Append an unvalidated staged component registration.
     * @param id Borrowed ID copied verbatim, without trimming or uniqueness checks.
     * @param config Borrowed config path copied verbatim; no existence/content check.
     * @return Zero-based appended row after insertion notifications. Resolved provenance
     * starts empty; no engine registration or file write occurs.
     */
    int appendRow(const QString &id, const QString &config);
    /**
     * @brief Copy a staged registration ID.
     * @param row Zero-based row, permitted invalid.
     * @return Owned ID or empty for an invalid row; a blank draft ID is also empty.
     */
    QString idAt(int row) const;
    /**
     * @brief Copy a staged config-path string.
     * @param row Zero-based row, permitted invalid.
     * @return Owned path or empty for an invalid row; a valid row may also have no path.
     */
    QString configAt(int row) const;

private:
    struct Row {
        QString id;
        QString config;
        QString resolved; //!< path the engine read the file from (may be empty)
    };
    /**
     * @brief Describe catalogue recognition/implementation and config-path existence.
     * @param r Borrowed staged row with copied ID/config/provenance.
     * @return Owned localized status. Blank trimmed ID returns no-id immediately;
     * otherwise ID recognition is case-insensitive and file status uses the staged
     * path plus modelDir. Does not validate content, register a component or use
     * resolved provenance as the existence-check path.
     */
    QString statusFor(const Row &r) const;

    QList<Row> m_rows;
    QList<Row> m_loaded;
    QString m_modelDir;
};

//! Combo editor over the built-in catalogue, editable for free-form ids.
class ProcessComponentIdDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    /**
     * @brief Create the editable catalogue/free-form component-ID delegate.
     * @param parent Optional QObject owner; null leaves lifetime to the caller.
     */
    explicit ProcessComponentIdDelegate(QObject *parent = nullptr);
    /**
     * @brief Allocate a free-form combo containing built-in component IDs.
     * @param parent Borrowed owner of the new editor; null makes it caller-owned.
     * @param option Unused style options.
     * @param index Unused cell identity; hydration is separate.
     * @return New editable QComboBox with ID item data and implementation/plan
     * tooltips. Does not register IDs or install an immediate-commit callback.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    /**
     * @brief Hydrate a component-ID combo from the model EditRole text.
     * @param editor Borrowed combo; non-QComboBox widgets are ignored.
     * @param index Borrowed source cell.
     * Selects a Qt MatchFixedString catalogue match or displays free-form text. No
     * explicit signal blocker is installed and this method does not call setData.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @brief Commit trimmed component-ID editor text to the draft model.
     * @param editor Borrowed combo; a different widget type returns without writing.
     * @param model Borrowed non-null destination model when the editor type matches.
     * @param index Borrowed destination cell passed to setData.
     * Uses EditRole and ignores the model return status; does not validate the ID or
     * write engine/config files.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

#endif // PROCESSCOMPONENTSMODEL_H
