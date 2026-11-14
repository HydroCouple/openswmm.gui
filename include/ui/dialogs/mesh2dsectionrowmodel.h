/**
 * @file      mesh2dsectionrowmodel.h
 * @brief     Staged table model for the scoped 2D surface-quality and groundwater-transport
 *            authoring sections.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * One model class serves every row section that follows the GLOBAL < TAG < CELL scope model
 * (C8): `[2D_COVERAGES]`, `[2D_LOADINGS]`, `[2D_CURB_LENGTH]`, `[2D_INITIAL_QUALITY]`,
 * `[GW_TRANSPORT_PARAMS]`, `[GW_SORPTION]`, `[GW_INITIAL_QUALITY]`, plus the unscoped
 * `[2D_AQUIFER_LINKS]`. It follows the `Mesh2DAquiferModel` contract: `load()` reads the engine
 * rows, edits are staged here, and `commit()` rewrites the section after validation, reading it
 * back and restoring the loaded rows on failure. The engine stays the single source of truth;
 * values are the numbers the API takes and the `.inp` carries (project units for coverage,
 * loading, curb length and link rows, SI for the groundwater transport parameters).
 *
 * Names (species, land uses, conduits, tags) are offered from the engine registries through
 * ChoicesRole so a view never has to accept free text for them.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DSECTIONROWMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DSECTIONROWMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui
{

/**
 * @brief Read the distinct non-empty mesh cell tags, sorted case-insensitively.
 * @param[in] engine Borrowed engine; null or a model without a mesh yields an empty list.
 * @return Tag names a TAG-scoped row may address.
 */
QStringList meshCellTags(SWMM_Engine engine);

/**
 * @brief Surface species a quality row may name: pollutants, then aqueous reaction species, then
 *   the reserved __WATER_AGE__ / __TEMPERATURE__ names when that transport is on and @p reserved
 *   is set.
 * @param[in] engine Borrowed engine; null yields an empty list.
 * @param[in] reserved Include the reserved names.
 * @return Names in that order, without duplicates.
 */
QStringList surfaceSpeciesNames(SWMM_Engine engine, bool reserved);

/**
 * @brief Read the time-series names of the model ([TIMESERIES] tables only).
 * @param[in] engine Borrowed engine; null yields an empty list.
 * @return Series names in registry order.
 */
QStringList timeSeriesNames(SWMM_Engine engine);

class Mesh2DSectionRowModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum class Section {
        Coverages,             //!< [2D_COVERAGES]: one percent column per land use
        Loadings,              //!< [2D_LOADINGS]: species, initial buildup
        CurbLength,            //!< [2D_CURB_LENGTH]: length
        SurfaceInitialQuality, //!< [2D_INITIAL_QUALITY]: species, concentration
        GwParams,              //!< [GW_TRANSPORT_PARAMS]: nine SI property columns
        GwSorption,            //!< [GW_SORPTION]: species, Kd, decay (blank = pollutant decay)
        GwInitialQuality,      //!< [GW_INITIAL_QUALITY]: zone, species, value
        AquiferLinks           //!< [2D_AQUIFER_LINKS]: conduit, KC, DC, exchange
    };
    /** QStringList of the names a combo editor offers for a cell; empty = free entry. */
    static constexpr int ChoicesRole = Qt::UserRole + 1;

    /**
     * @brief Create an empty staged table for one section.
     * @param[in] section Section this model edits; fixed for the model's lifetime.
     * @param[in] parent Optional QObject owner.
     */
    explicit Mesh2DSectionRowModel(Section section, QObject *parent = nullptr);

    [[nodiscard]] Section section() const { return m_section; }
    /** The section's bracketed name, e.g. "[2D_LOADINGS]". */
    [[nodiscard]] QString sectionName() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Expose staged values. Display/Edit give text (numbers in their shortest exact
     * form, blank for an optional value that is not set), ChoicesRole the registry names for
     * scope/target/name/zone columns, CheckStateRole the exchange flag, ToolTip the column help.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief Stage one edit. Scope accepts its label or 0..2; target a tag name or a 1-based
     * cell; numbers parse from text (blank clears an optional value); the flag accepts a bool or
     * check state. Refused edits return false and change nothing. No engine write.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    /**
     * @brief Replace staged rows and baseline with the engine's, and refresh the name registries.
     * @param[in] engine Borrowed engine; null clears the model.
     * @return Empty on success, otherwise the read error (validation then refuses commits).
     * @details `[GW_INITIAL_QUALITY]` rows loaded from a FILE reference are not staged: they
     * belong to the file and are left untouched by commits (see fileReference()).
     */
    QString load(SWMM_Engine engine);
    /** True when staged rows differ from the loaded baseline (exact, NaN-aware). */
    [[nodiscard]] bool isDirty() const;
    /**
     * @brief Check that the engine is editable, its rows still equal the loaded baseline, and
     * every staged row is complete, in range and unique by its section key.
     * @return Empty when a commit may proceed, otherwise the first problem.
     */
    QString validateCommit(SWMM_Engine engine) const;
    /**
     * @brief Rewrite the section from the staged rows and verify the read-back.
     * @param[in,out] engine Borrowed BUILDING/OPENED engine.
     * @param[in] advanceBaseline True adopts the staged rows as the baseline on success.
     * @return Empty for a clean model or success; otherwise the error, after an attempt to put
     * the loaded rows back.
     */
    QString commit(SWMM_Engine engine, bool advanceBaseline = true);
    /** Put the loaded rows back when the engine no longer holds them. Empty on success. */
    QString restoreLoaded(SWMM_Engine engine) const;
    /** Adopt the staged rows as the clean baseline (inactive tag/cell targets cleared). */
    void acceptChanges();
    /**
     * @brief Append a `*` row seeded with the section defaults (the engine's defaults for
     * transport parameters, the first registry name where a name is needed).
     * @return The new row index.
     */
    int appendRow();

    /** Tag names offered for TAG-scoped targets (see meshCellTags()). */
    void setTagChoices(const QStringList &tags);
    /** Relabel unit-dependent headers for a US (false) or SI (true) project. */
    void setProjectUnits(bool si);
    /** `[GW_INITIAL_QUALITY] FILE` reference (read-only here); empty for other sections. */
    [[nodiscard]] QString fileReference() const { return m_file; }
    /** Number of `[GW_INITIAL_QUALITY]` rows the FILE reference loaded (not editable here). */
    [[nodiscard]] int fileRowCount() const { return m_fileRows; }

    /** One staged row. Scope codes 0/1/2 are the same in every section's API. */
    struct Row {
        int scope = 0;
        QString tag;
        int cell = -1;                  // 0-based
        QString name;                   // species or conduit
        int zone = 0, layer = -1;       // [GW_INITIAL_QUALITY]
        QMap<QString, double> percents; // [2D_COVERAGES]: land use -> percent
        QVector<double> values;         // NaN = blank optional value
        bool operator==(const Row &o) const;
    };

    /** Rows read by the last load() (normalized; FILE-owned [GW_INITIAL_QUALITY] rows excluded). */
    [[nodiscard]] const QList<Row> &loadedRows() const { return m_loaded; }
    /**
     * @brief Replace every staged row (no engine write); commit() then rewrites the section.
     * Used by the spatial assignment service so it shares this model's write, read-back and
     * restore path instead of binding the section API a second time.
     */
    void setStagedRows(const QList<Row> &rows);
    /** Land uses offered as [2D_COVERAGES] columns after load(). */
    [[nodiscard]] QStringList landUses() const { return m_landUses; }
    /** Registry names (species or conduits) offered for this section after load(). */
    [[nodiscard]] QStringList names() const { return m_names; }

private:
    enum class Kind { Scope, Target, Name, Zone, Value, Percent, Flag };
    struct Column {
        Kind kind;
        QString header;
        int value = -1;       // index into Row::values (Value/Flag)
        QString landUse;      // Percent columns
        bool optional = false;
        bool signedValue = false;
        QString tip;
    };

    void rebuildColumns();
    Row normalized(Row row) const;
    QList<Row> normalized(const QList<Row> &rows) const;
    QString key(const Row &row) const;
    QString rowError(const Row &row) const;
    QString read(SWMM_Engine engine, QList<Row> &rows, int *fileRows = nullptr,
                 QString *file = nullptr) const;
    QString write(SWMM_Engine engine, const QList<Row> &rows) const;
    int liveCount(SWMM_Engine engine) const;
    int removeLive(SWMM_Engine engine, int index) const;
    int setLive(SWMM_Engine engine, const Row &row) const;
    QStringList nameChoices(const Row &row) const;
    [[nodiscard]] bool scoped() const { return m_section != Section::AquiferLinks; }

    Section m_section;
    bool m_si = true;
    QVector<Column> m_columns;
    QList<Row> m_rows, m_loaded;
    QStringList m_names, m_landUses, m_tags;
    QString m_loadError, m_file;
    int m_fileRows = 0;
    int m_cellCount = 0;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DSECTIONROWMODEL_H
