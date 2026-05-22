/*!
 * \file   observedcsvrunlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL-Polish — IRunLayer for observed CSV / TSV / DAT files.
 *
 * Two-or-more-column data: column 0 = timestamp, columns 1..N = data
 * series. Each non-time column becomes a virtual "object" accessible via
 * `ObjectRef::Observed(label)`. The label is the CSV header text; rows
 * without a header are labeled "col_1", "col_2", etc.
 *
 * Supported time formats (tried in order):
 *   - ISO 8601:           2026-01-01T00:15:00, 2026-01-01 00:15:00
 *   - SWMM .dat:          01/01/2026 00:15
 *   - Hours-since-start:  0.25  (numeric; only when no other format matches)
 *
 * Unit system is inferred from CSV column suffix when present
 *   ("Depth_m", "Flow_ft3s"). Defaults to SI when unknown — the user can
 *   override later via a per-row toolbar (Slice BL polish).
 *
 * Series attribute is fixed at construction time: the same CSV file may
 * be added as different `RunSource`s if the user wants to plot the same
 * data against multiple attribute rows.
 */
#ifndef OPENSWMMVIS_PLOT_OBSERVEDCSVRUNLAYER_H
#define OPENSWMMVIS_PLOT_OBSERVEDCSVRUNLAYER_H

#include "plot/irunlayer.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

namespace openswmmvis::plot {

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
     *                       if you pass UnitSystem::SI; explicit value
     *                       wins.
     * \param errorOut       If non-null, populated with a human-readable
     *                       message on failure.
     */
    static std::unique_ptr<ObservedCsvRunLayer> load(
        const QString& path,
        PlotAttribute defaultAttr,
        UnitSystem unitSys = UnitSystem::SI,
        QString *errorOut = nullptr);

    ~ObservedCsvRunLayer() override = default;

    /*! \brief Column labels for all loaded series — order matches the
     *  CSV's column order. Useful for the dialog to populate a picker. */
    const QStringList& columnLabels() const noexcept { return m_labels; }

    /*! \brief File path used to load. */
    const QString& sourcePath() const noexcept { return m_path; }

    // IRunLayer
    QString    scenarioName()      const override;
    UnitSystem unitSystem()        const override { return m_unit; }
    double     startDateJulian()   const override;
    int        periodCount()       const override;
    int        reportStepSeconds() const override;
    QString    persistenceKey()    const override;

    void getSeriesAt(const ObjectRef& ref,
                     PlotAttribute attr,
                     SeriesData& out) const override;

    bool supportsAttribute(PlotAttribute attr) const override;

private:
    ObservedCsvRunLayer() = default;

    /*! \brief Parse one line into a timestamp + value cells. Returns true
     *  iff the timestamp parsed; cells may be NaN if a value column is
     *  blank. */
    static bool parseRow_(const QString& line,
                          QChar delim,
                          double& timeJulianOut,
                          std::vector<double>& valuesOut,
                          double hoursSinceStartFallbackBase);

    QString                 m_path;
    QString                 m_label;
    UnitSystem              m_unit = UnitSystem::SI;
    PlotAttribute           m_defaultAttr = PlotAttribute::Unknown;
    QStringList             m_labels;
    std::vector<double>     m_timesJulian;
    QHash<QString, std::vector<double>> m_byLabel;
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_OBSERVEDCSVRUNLAYER_H
