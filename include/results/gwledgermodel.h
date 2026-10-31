/**
 * @file      gwledgermodel.h
 * @brief     V4 — the saved run's two-zone aquifer water and species ledgers
 *            as one table model.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * §2 (`GwLedgerModel`) and V4. The engine saves both ledgers in the 2D
 * results file as CUMULATIVE series (`/groundwater_ledger` [time, term],
 * `/groundwater_species_ledger` [time, species, term]); before this model
 * nothing read them, so the balance was gone once a run ended.
 *
 * MVC (CLAUDE.md §5.1): every view reads this model — the Groundwater
 * dialog's State tab shows one row per ledger term with its final value, and
 * the comparison plot asks it for a term's cumulative series by key.
 *
 * Rows are identified by term NAME (the `terms` attribute) and species NAME,
 * never by column position. A ledger the file does not carry, or whose
 * identities cannot be resolved, is one "unavailable" row with the reason —
 * never a block of zeros.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_RESULTS_GWLEDGERMODEL_H
#define OPENSWMMVIS_RESULTS_GWLEDGERMODEL_H

#include "io/mesh2dh5reader.h"
#include "plot/irunlayer.h"

#include <QAbstractTableModel>
#include <QString>
#include <QVector>

#include <vector>

namespace openswmmvis::results
{

class GwLedgerModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { ColLedger = 0, ColTerm, ColFinal, ColUnits, ColumnCount };

    /**
     * @brief Create an empty model (no file loaded, no rows).
     * @param[in] parent Optional Qt owner.
     */
    explicit GwLedgerModel(QObject *parent = nullptr);

    /*!
     * \brief Replace the contents with both ledgers of the saved 2D results
     *        file \p h5Path (one model reset).
     * \returns true when at least one ledger was read. Either ledger that is
     *          absent or unresolvable becomes one unavailable row carrying
     *          the reason; an unreadable file leaves only status().
     */
    bool load(const QString &h5Path);
    /*! \brief Drop every row and the loaded path (one model reset). */
    void clear();

    /** @return Path of the last load(); empty when cleared. */
    const QString &path() const noexcept { return m_path; }
    /** @return One-line, human summary of what was read (file, final time, or why not). */
    const QString &status() const noexcept { return m_status; }

    /*!
     * \brief Stable identity of \p row for the comparison plot:
     *        `gwledger:water:<term>` or `gwledger:species:<name>:<term>`.
     * \returns empty for an unavailable row or an invalid index.
     */
    QString key(int row) const;
    /** @return Row whose key() equals \p key, or -1. */
    int rowForKey(const QString &key) const;
    /*! \brief Comparison-plot descriptor for \p row (a mesh-variable
     *  descriptor keyed by key(), labelled "Groundwater ledger — …,
     *  cumulative"); invalid for an unavailable row. */
    plot::ResultDescriptor descriptor(int row) const;
    /** @return descriptor() of every available row, in row order. */
    QVector<plot::ResultDescriptor> descriptors() const;
    /** @return Value of \p row at the last stored frame; NaN when unavailable. */
    double finalValue(int row) const;

    /*!
     * \brief Fill \p out with \p row's cumulative series: one sample per
     *        stored frame from `out.firstPeriod` on, times in SWMM DateTime
     *        days from `/time`, values as stored (non-finite values stay NaN
     *        gaps).
     * \returns false (out.ok false, errorMessage set) for an unavailable row.
     */
    bool series(int row, plot::SeriesData &out) const;

    // QAbstractTableModel
    /** @return Ledger rows; zero for a valid parent. */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /** @return ColumnCount; zero for a valid parent. */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /** @return Display text, the raw final value (Qt::UserRole), alignment or tooltips. */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /** @return Horizontal column titles. */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    struct Row {
        QString ledger;     ///< "Water" or the species name
        QString term;       ///< stored term name; empty for an unavailable row
        QString units;      ///< display units; empty = unresolved
        QString tooltip;    ///< long_name / layout, or the unavailability reason
        int species = -1;   ///< species row in the species ledger; -1 = water
        int column = -1;    ///< term column; -1 = unavailable
    };
    QVector<Row> m_rows;
    QString m_path;
    QString m_status;
    std::vector<double> m_times; ///< `/time`, SWMM DateTime days
    io::GroundwaterLedger m_water;
    io::GroundwaterLedger m_species;

    /** @return Stored value of \p r at \p frame; NaN when unavailable or out of range. */
    double value_(const Row &r, int frame) const;
    /** @return Frames with both a time and a ledger row for \p r. */
    int frames_(const Row &r) const;
};

} // namespace openswmmvis::results

#endif // OPENSWMMVIS_RESULTS_GWLEDGERMODEL_H
