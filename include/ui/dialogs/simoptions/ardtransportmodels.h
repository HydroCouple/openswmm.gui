/**
 * @file      ardtransportmodels.h
 * @brief     Staged Eulerian ARD transport rows for the Simulation Options Quality page (review
 *            G-02): per-conduit dispersion ([CONDUIT_DISPERSION]) and boundary / source rows
 *            ([TRANSPORT_BOUNDARIES] / [TRANSPORT_SOURCES]).
 * @details   Same protocol as Mesh2DBoundaryQualityModel: load, edit, validateCommit, commit
 *            (replaces the engine's rows; puts the loaded rows back if the engine refuses one),
 *            acceptChanges. Numbers are shown and edited as C-locale text so no digit is lost.
 *            ChoicesRole lists the names a cell may take; it has the value of
 *            Mesh2DSectionRowModel::ChoicesRole, so Mesh2DChoiceDelegate edits these tables.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_SIMOPTIONS_ARDTRANSPORTMODELS_H
#define OPENSWMMVIS_UI_DIALOGS_SIMOPTIONS_ARDTRANSPORTMODELS_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStringList>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui
{

/** @return Names of the model's conduits, in link order; empty for a null engine. */
QStringList ardConduitNames(SWMM_Engine engine);
/** @return Names of the Reaction System's species, in their order; empty for a null engine. */
QStringList reactionSpeciesNames(SWMM_Engine engine);

/*! Staged per-conduit dispersion coefficients. */
class ArdConduitDispersionModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /*! One conduit's coefficient, in ft²/s or m²/s by the model's flow units. */
    struct Row {
        QString conduit;
        double coefficient = 0.0;
        bool operator==(const Row &) const = default;
    };
    enum Column { ConduitColumn, CoefficientColumn, ColumnCount };
    static constexpr int ChoicesRole = Qt::UserRole + 1;

    /**
     * @brief Create an empty staged table.
     * @param[in] parent Optional QObject owner.
     */
    explicit ArdConduitDispersionModel(QObject *parent = nullptr);

    /**
     * @brief Read the engine's rows and conduit names; the result is clean (not dirty).
     * @param[in] engine Borrowed engine; null clears the table.
     * @return Empty on success, otherwise why the rows could not be read.
     */
    QString load(SWMM_Engine engine);
    /** @brief Header unit shown after "Coefficient", e.g. "ft²/s". */
    void setUnits(const QString &units);

    [[nodiscard]] const QList<Row> &rows() const { return m_rows; }
    [[nodiscard]] bool isDirty() const { return m_rows != m_loaded; }
    /** @return Conduit names the Conduit column offers. */
    [[nodiscard]] QStringList conduits() const { return m_conduits; }

    /** @brief Append a row as given; validateCommit() reports any problem. */
    void addRow(const Row &row);
    /** @brief Remove one staged row; out of range does nothing. */
    void removeRowAt(int row);

    /**
     * @brief Check the staged rows before applying them.
     * @details Each row needs a conduit of the model (not a pump, orifice, weir or outlet), a
     *   finite coefficient that is not negative, and a conduit no other row uses. The engine must
     *   accept configuration edits (not running). Clean rows pass without a check.
     * @param[in] engine Borrowed engine.
     * @return Empty when the rows can be applied, otherwise the first problem, naming its row.
     */
    [[nodiscard]] QString validateCommit(SWMM_Engine engine) const;
    /**
     * @brief Replace the engine's rows with the staged rows (nothing when clean).
     * @param[in] engine Borrowed engine; validateCommit() should have passed.
     * @return Empty on success, otherwise the engine's refusal; the loaded rows are then put
     *   back.
     */
    QString commit(SWMM_Engine engine);
    /** @brief Mark the staged rows as applied (the new clean state). */
    void acceptChanges() { m_loaded = m_rows; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QString replaceRows(SWMM_Engine engine, const QList<Row> &rows) const;

    QList<Row> m_rows;
    QList<Row> m_loaded;
    QStringList m_conduits;
    QString m_units;
};

/*! Staged transport boundary rows (node, reaction species, concentration) or source rows
 *  (conduit, reaction species, mass rate; negative extracts), each a constant or a time
 *  series. Pollutants are not allowed: they enter through inflows. */
class ArdTransportRowModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum class Kind { Boundary, Source };
    /*! One row; @c value is used when @c series is false, @c timeSeries when it is true. */
    struct Row {
        QString element; ///< node (boundary) or conduit (source)
        QString species; ///< reaction species
        bool series = false;
        double value = 0.0; ///< concentration (boundary) or mass/s (source)
        QString timeSeries;
        bool operator==(const Row &) const = default;
    };
    enum Column { ElementColumn, SpeciesColumn, ValueFromColumn, ValueColumn, SeriesColumn,
                  ColumnCount };
    static constexpr int ChoicesRole = Qt::UserRole + 1;

    /**
     * @brief Create an empty staged table.
     * @param[in] kind Boundaries or sources.
     * @param[in] parent Optional QObject owner.
     */
    explicit ArdTransportRowModel(Kind kind, QObject *parent = nullptr);

    [[nodiscard]] Kind kind() const { return m_kind; }
    /**
     * @brief Read the engine's rows and the names the columns offer; the result is clean.
     * @param[in] engine Borrowed engine; null clears the table.
     * @return Empty on success, otherwise why the rows could not be read.
     */
    QString load(SWMM_Engine engine);

    [[nodiscard]] const QList<Row> &rows() const { return m_rows; }
    [[nodiscard]] bool isDirty() const { return m_rows != m_loaded; }
    /** @return Reaction species the Species column offers; empty means rows cannot be added. */
    [[nodiscard]] QStringList species() const { return m_species; }
    /** @return Nodes (boundaries) or conduits (sources) the first column offers. */
    [[nodiscard]] QStringList elements() const { return m_elements; }

    /** @brief Append a row as given; validateCommit() reports any problem. */
    void addRow(const Row &row);
    /** @brief Remove one staged row; out of range does nothing. */
    void removeRowAt(int row);

    /**
     * @brief Check the staged rows before applying them.
     * @details A row needs a node (boundary) or conduit (source) of the model, a reaction
     *   species, a time series of the model when it uses one, otherwise a finite value (a
     *   boundary concentration may not be negative), and an (element, species) pair no other row
     *   uses. The engine must accept configuration edits. Clean rows pass without a check.
     * @param[in] engine Borrowed engine.
     * @return Empty when the rows can be applied, otherwise the first problem, naming its row.
     */
    [[nodiscard]] QString validateCommit(SWMM_Engine engine) const;
    /**
     * @brief Replace the engine's rows with the staged rows (nothing when clean).
     * @param[in] engine Borrowed engine; validateCommit() should have passed.
     * @return Empty on success, otherwise the engine's refusal; the loaded rows are then put
     *   back.
     */
    QString commit(SWMM_Engine engine);
    /** @brief Mark the staged rows as applied (the new clean state). */
    void acceptChanges() { m_loaded = m_rows; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QString replaceRows(SWMM_Engine engine, const QList<Row> &rows) const;
    QStringList currentElements(SWMM_Engine engine) const;

    Kind m_kind;
    QList<Row> m_rows;
    QList<Row> m_loaded;
    QStringList m_elements;
    QStringList m_species;
    QStringList m_series;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SIMOPTIONS_ARDTRANSPORTMODELS_H
