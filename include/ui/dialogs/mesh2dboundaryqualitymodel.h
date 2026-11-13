/**
 * @file      mesh2dboundaryqualitymodel.h
 * @brief     Staged [2D_BOUNDARY_QUALITY] rows: the constant concentration of each species in the
 *            water that enters the 2D mesh through an exterior boundary edge (review G-03,
 *            process-boundaries Phase 4d).
 * @details   Same apply protocol as Mesh2DSectionRowModel, so the 2D Surface Quality dialog
 *            applies it with its other tabs: load, edit, validateCommit, commit, restoreLoaded
 *            on a failed apply, acceptChanges.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DBOUNDARYQUALITYMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DBOUNDARYQUALITYMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui
{

class Mesh2DBoundaryQualityModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /*! One row: species concentration on (cell, local edge). Concentration is in the species'
     *  native units (degrees C for __TEMPERATURE__, which alone may be negative). */
    struct Row {
        int cell = -1;
        int edge = -1;
        QString species;
        double concentration = 0.0;
        bool operator==(const Row &) const = default;
    };
    enum Column { CellColumn, EdgeColumn, SpeciesColumn, ConcentrationColumn, ColumnCount };
    /** QStringList of the names the Species column offers. */
    static constexpr int ChoicesRole = Qt::UserRole + 1;

    /*! Selected edges sorted by where an inflow concentration may be set. */
    struct EdgeScope {
        QList<QPair<int, int>> exterior; ///< (cell, local edge) on the mesh boundary
        int interior = 0;                ///< shared by two cells: no inflow there
        int invalid = 0;                 ///< not an edge of the mesh
    };

    /**
     * @brief Create an empty staged table.
     * @param[in] parent Optional QObject owner.
     */
    explicit Mesh2DBoundaryQualityModel(QObject *parent = nullptr);

    /**
     * @brief Read the engine's rows and species names; the result is clean (not dirty).
     * @param[in] engine Borrowed engine; null clears the table.
     * @return Empty on success, otherwise why the rows could not be read.
     */
    QString load(SWMM_Engine engine);

    /** @return Staged rows in table order. */
    [[nodiscard]] const QList<Row> &rows() const { return m_rows; }
    /** @return True while the staged rows differ from those last loaded or applied. */
    [[nodiscard]] bool isDirty() const { return m_rows != m_loaded; }
    /** @return Species the Species column offers (surfaceSpeciesNames with reserved names). */
    [[nodiscard]] QStringList species() const { return m_species; }

    /**
     * @brief Append a row (typically from an Add button) and select nothing.
     * @param[in] row Staged as given; validateCommit() reports any problem.
     */
    void addRow(const Row &row);
    /**
     * @brief Remove one staged row.
     * @param[in] row Table row; out of range does nothing.
     */
    void removeRowAt(int row);

    /**
     * @brief Sort edges into exterior, interior and invalid, by mesh connectivity.
     * @details An edge is exterior when its vertex pair belongs to one cell only. Duplicates in
     *   @p edges are counted once.
     * @param[in] engine Borrowed engine with a mesh.
     * @param[in] edges (cell, local edge) pairs, for example the map selection.
     * @param[out] error Optional; set when connectivity cannot be read.
     * @return The classification; empty when @p engine is null.
     */
    static EdgeScope classifyEdges(SWMM_Engine engine, const QList<QPair<int, int>> &edges,
                                   QString *error = nullptr);

    /**
     * @brief Stage one species concentration on each of @p edges, replacing that species' row
     *   where an edge already has one.
     * @param[in] edges Exterior edges (from classifyEdges()); others are staged as given and
     *   refused by validateCommit().
     * @param[in] species Species name.
     * @param[in] concentration Native units.
     * @return Number of edges whose row was added or changed.
     */
    int setForEdges(const QList<QPair<int, int>> &edges, const QString &species,
                    double concentration);

    /**
     * @brief Check the staged rows against the engine before applying them.
     * @details A row needs a cell and local edge of the mesh, an exterior edge, a species the
     *   model carries, a finite concentration that is not negative (temperature excepted), and a
     *   (cell, edge, species) key no other row uses. The engine must still accept configuration
     *   edits (not running).
     * @param[in] engine Borrowed engine.
     * @return Empty when the rows can be applied, otherwise the first problem, naming its row.
     */
    [[nodiscard]] QString validateCommit(SWMM_Engine engine) const;

    /**
     * @brief Replace the engine's rows with the staged rows.
     * @param[in] engine Borrowed engine; validateCommit() should have passed.
     * @return Empty on success, otherwise the engine's refusal (the engine may hold part of the
     *   new rows: call restoreLoaded()).
     */
    QString commit(SWMM_Engine engine);

    /**
     * @brief Put back the rows last loaded or applied, after a failed apply.
     * @param[in] engine Borrowed engine.
     * @return Empty on success, otherwise why the rows could not be restored.
     */
    QString restoreLoaded(SWMM_Engine engine);

    /** @brief Mark the staged rows as applied (the new clean state). */
    void acceptChanges() { m_loaded = m_rows; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<Row> m_rows;
    QList<Row> m_loaded;
    QStringList m_species;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DBOUNDARYQUALITYMODEL_H
