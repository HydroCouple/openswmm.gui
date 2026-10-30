/**
 * @file      observedcsvrunlayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BL-Polish — IRunLayer for observed CSV / TSV / DAT files.
 *
 * Two-or-more-column data: column 0 = timestamp, columns 1..N = data
 * series. Each non-time column becomes a virtual "object" accessible via
 * `ObjectRef::Observed(label)`. The label is the CSV header text; rows
 * without a header are labeled "col_1", "col_2", etc.
 *
 * Supported time formats (tried in order):
 *   - ISO 8601:           2026-01-01T00:15:00, 2026-01-01 00:15:00
 *   - SWMM .dat:          01/01/2026 00:15
 *   - Numeric hours-since-start are disabled by this loader (no base is supplied).
 *
 * Unit system is inferred from CSV column suffix when present
 *   ("Depth_m", "Flow_ft3s"). Defaults to SI when unknown — the user can
 *   override later via a per-row toolbar (Slice BL polish).
 *
 * Series attribute is fixed at construction time: the same CSV file may
 * be assigned to different chart rows by the host; getSeriesAt() accepts
 * any attribute allowed by supportsAttribute(), independently of defaultAttr.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_OBSERVEDCSVRUNLAYER_H
#define OPENSWMMVIS_PLOT_OBSERVEDCSVRUNLAYER_H

#include "plot/irunlayer.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

namespace openswmmvis::plot
{

class ObservedCsvRunLayer final : public IRunLayer
{
public:
    /*!
     * \brief Load a CSV / TSV file and assign all its columns to a single
     *        chart attribute (the row they land on).
     * \param path           File path.
     * \param defaultAttr    PlotAttribute the columns plot against (e.g.
     *                       PlotAttribute::NodeDepth). All columns share
     *                       this; users wanting mixed-attribute observed
     *                       data load the same file multiple times.
     * \param unitSys        Unit system to advertise (mostly affects axis
     *                       labels). Auto-detected from header suffixes
     *                       if you pass UnitSystem::SI; non-SI value
     *                       wins.
     * \param errorOut       If non-null, populated with a human-readable
     *                       message on failure.
     * @return Newly owned immutable in-memory source, or null for open/column-count/no-valid-row failure. Successful loading leaves errorOut unchanged.
     * @note Reads UTF-8 and guesses tab/comma/semicolon from the first nonblank, non-#-prefixed line. Literal splitting does not interpret CSV quotes; headerless dated first rows receive col_1 etc. Numeric hours-since-start parsing is disabled here because no finite fallback base is supplied. Data order and duplicate timestamps are retained without sorting.
     * @note Blank/invalid-timestamp rows are skipped; missing/non-numeric cells become NaN and extra value columns are discarded. Header names should be unique and nonempty: duplicates share one hash entry and can produce longer value vectors than the common time vector. Values are never unit-converted. defaultAttr is stored but does not restrict subsequent getSeriesAt() requests.
     * @note Passing SI requests suffix inference rather than forcing SI; only a non-SI argument bypasses inference. Later read-stream errors are not surfaced separately.
     */
    static std::unique_ptr<ObservedCsvRunLayer> load(const QString &path, PlotAttribute defaultAttr,
                                                     UnitSystem unitSys = UnitSystem::SI,
                                                     QString *errorOut = nullptr);

    /**
     * @brief Release the in-memory copies; no file handle remains open after load().
     */
    ~ObservedCsvRunLayer() override = default;

    /*! \brief Column labels for all loaded series — order matches the
     *  CSV's column order. Useful for the dialog to populate a picker.
     * @return Borrowed immutable column-order list, valid for this source's lifetime. Names may repeat or be empty because the loader does not enforce uniqueness.
     */
    const QStringList &columnLabels() const noexcept { return m_labels; }

    /*! \brief File path used to load.
     * @return Borrowed original path token as supplied, not automatically made absolute or canonical.
     */
    const QString &sourcePath() const noexcept { return m_path; }

    // IRunLayer
    /**
     * @brief Report observed-source metadata.
     * @return Observed-prefixed file complete-base-name label, without path.
     */
    QString scenarioName() const override;
    /**
     * @brief Report observed-source metadata.
     * @return Advertised inferred/selected family; sample numbers were not converted.
     */
    UnitSystem unitSystem() const override { return m_unit; }
    /**
     * @brief Report observed-source metadata.
     * @return First stored SWMM/OLE-Automation date in input order, or NaN for no data; not the minimum of unsorted input.
     */
    double startDateJulian() const override;
    /**
     * @brief Report observed-source metadata.
     * @return Number of stored timestamp rows.
     */
    int periodCount() const override;
    /**
     * @brief Report observed-source metadata.
     * @return Rounded spacing of the first two timestamps in seconds, or zero for fewer than two. Unsorted input may produce a negative step.
     */
    int reportStepSeconds() const override;
    /**
     * @brief Report observed-source metadata.
     * @return observed:// followed by the original path token; no canonicalization or content identity check occurs.
     */
    QString persistenceKey() const override;

    /**
     * @brief Copy a named observed column without changing its numeric interpretation.
     * @param[in] ref Must have Observed kind and an exact case-sensitive loaded column name.
     * @param[in] attr Any identifier accepted by supportsAttribute(); it need not equal the construction defaultAttr.
     * @param[in,out] out Samples/ok/error are cleared, then the entire stored column/time vector is copied. firstPeriod is ignored and neither firstPeriod nor periodCount is changed; failures leave ok false with a diagnostic.
     * @note Duplicate header names can produce unequal time/value lengths; the loader does not reconcile them. No finite-value filtering or unit conversion is applied.
     */
    void getSeriesAt(const ObjectRef &ref, PlotAttribute attr, SeriesData &out) const override;

    /**
     * @param[in] attr Fixed quantity for a host-assigned row.
     * @return False for Unknown or the nine mesh-cell attributes recognized by isMesh2DAttribute(); all others, including mesh-edge identifiers, return true without availability/units checks.
     */
    bool supportsAttribute(PlotAttribute attr) const override;

private:
    /**
     * @brief Create an empty private loader target; callers obtain initialized sources through load().
     */
    ObservedCsvRunLayer() = default;

    /*! \brief Parse one line into a timestamp + value cells. Returns true
     *  iff the timestamp parsed; cells may be NaN if a value column is
     *  blank.
     * @param[in] line Raw delimited row.
     * @param[in] delim Literal separator, without CSV quote handling.
     * @param[out] timeJulianOut SWMM/OLE-Automation day value on successful timestamp conversion; do not consume on failure.
     * @param[out] valuesOut One numeric/NaN value per remaining cell on success; unchanged on failure.
     * @param[in] hoursSinceStartFallbackBase Finite SWMM date enables numeric-hour fallback; load() passes NaN to disable it.
     * @return io::parseRow() result; no additional validation/conversion occurs here.
     */
    static bool parseRow_(const QString &line, QChar delim, double &timeJulianOut,
                          std::vector<double> &valuesOut, double hoursSinceStartFallbackBase);

    QString m_path;
    QString m_label;
    UnitSystem m_unit = UnitSystem::SI;
    PlotAttribute m_defaultAttr = PlotAttribute::Unknown;
    QStringList m_labels;
    std::vector<double> m_timesJulian;
    QHash<QString, std::vector<double>> m_byLabel;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_OBSERVEDCSVRUNLAYER_H
