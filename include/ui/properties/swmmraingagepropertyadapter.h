/**
 * @file      swmmraingagepropertyadapter.h
 * @brief     Property-tree adapter for [RAINGAGES].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [RAINGAGES]. Rain Gage is
 * technically a spatial object (has map coordinates) but was missed by
 * the original AG.3 adapter set; DA.2 closes that gap.
 *
 * Scalar properties cover rainfall representation, source, recording interval,
 * precipitation factors, series/file selectors and map coordinates. Each read
 * resolves current engine state; writes either call typed setters or emit
 * layer-mediated coordinate edit requests.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMRAINGAGEPROPERTYADAPTER_H
#define SWMMRAINGAGEPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"
#include "ui/properties/userflagseditref.h" // USER_FLAGS Phase 4
#include "ui/properties/dataobjectref.h"    // DA.2 parity — Series Name picker
#include "ui/properties/rainintervalref.h"  // DA.2 parity — H:MM interval combo

class SWMMRainGagePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT

public:
    /*! Type-safe enums declared with Q_ENUM so QPropertyModel renders the
     *  key name (e.g. "INTENSITY", "FILE", "MM") instead of a raw integer.
     *  Values mirror the engine codes and the Attribute Table value lists. */
    // Mixed-case enumerators deliberately avoid the all-caps Windows SDK
    // macros (IN/OUT) and the <cstdio> FILE typedef.
    enum RainType { Intensity = 0, Volume = 1, Cumulative = 2 };
    enum DataSource { Timeseries = 0, File = 1 };
    enum RainUnits { Inches = 0, Millimeters = 1 };
    /*! Rain-file grammar. Values are engine RainFileFormat codes, and these
     *  three are the only ones the engine ever assigns: the parser sets
     *  USER_CSV for a "path:col" token and STAN_PRCP for a plain path
     *  (CatchmentHandler), the resolver promotes STAN_PRCP → USER_CSV on
     *  detection, and a gage that has never had a file stays UNKNOWN. The
     *  NWS/DSI/HLY codes 0..4 are declared in the engine but never written, so
     *  offering them here would be offering states the model cannot reach. */
    enum RainFileFormat { AutoDetect = -1, StandardRainFile = 5, MultiColumn = 6 };
    Q_ENUM(RainType)
    Q_ENUM(DataSource)
    Q_ENUM(RainUnits)
    Q_ENUM(RainFileFormat)

private:
    /*! Map position, `[SYMBOLS]` X / Y in the project's map units. A rain
     *  gage IS a spatial object — the original DA.2 adapter set omitted these
     *  because it derives from the non-spatial data-object base, so the gage
     *  was the one placed object whose location could not be typed in. Writes
     *  are deferred to PropertiesPanel → SWMMModelLayer::applyGageMove (the
     *  same contract SWMMNodePropertyAdapter::setXCoord uses) so the cached
     *  scene point moves with the engine value. */
    Q_PROPERTY(double xCoord READ xCoord WRITE setXCoord NOTIFY changed)
    Q_PROPERTY(double yCoord READ yCoord WRITE setYCoord NOTIFY changed)
    Q_PROPERTY(SWMMRainGagePropertyAdapter::RainType rainType READ rainType WRITE setRainType NOTIFY
                   changed)
    /*! Recording interval. Legacy [RAINGAGES] token 2, edited as an H:MM
     *  clock combo (RainIntervalComboBox). The ref carries seconds; the
     *  engine stores GageData.interval_sec. */
    Q_PROPERTY(
        RainIntervalRef rainInterval READ rainIntervalRef WRITE setRainIntervalRef NOTIFY changed)
    /*! Snow-catch deficiency correction factor (legacy SCF, token 3).
     *  Distinct from the rainfall scale factor. */
    Q_PROPERTY(double snowFactor READ snowFactor WRITE setSnowFactor NOTIFY changed)
    /*! Rainfall scale factor — the optional trailing token of [RAINGAGES]
     *  (token 6 for TIMESERIES, token 9 for FILE). Multiplies all rainfall from
     *  this gage. Distinct from snowFactor above: this scales rainfall, SCF
     *  corrects snow catch. Must be > 0; default 1.0. */
    Q_PROPERTY(double scaleFactor READ scaleFactor WRITE setScaleFactor NOTIFY changed)
    Q_PROPERTY(SWMMRainGagePropertyAdapter::DataSource dataSource READ dataSource WRITE
                   setDataSource NOTIFY changed)
    /*! DA.2 parity — TIME SERIES source: the in-model series id. Renders
     *  as a TimeSeries-filtered picker (DataObjectPickerEditor). Only
     *  meaningful when dataSource == TIMESERIES (panel greys it otherwise). */
    Q_PROPERTY(DataObjectRef seriesName READ seriesNameRef WRITE setSeriesNameRef NOTIFY changed)
    /*! Current rainfall rate in project units — read-only display. */
    Q_PROPERTY(double currentRainfall READ currentRainfall NOTIFY changed)
    /*! Slice IO-11e — external rain-file path for this gage. The string
     *  is the original token (relative or absolute) as known to the
     *  engine; reads also surface the resolved absolute via
     *  resolvedFilePath() for tooltip / status display. */
    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath NOTIFY changed)
    Q_PROPERTY(QString resolvedFilePath READ resolvedFilePath NOTIFY changed)
    /*! Multi-column rain file (spec §4 task 4) — the data column selected
     *  inside a CSV/TSV/TSF file. Setting a non-empty column switches the
     *  engine gage to the USER_CSV file format ("FILE path:col"); the colon
     *  composition is the engine writer's job, never the user's. Only
     *  meaningful when dataSource == FILE. */
    Q_PROPERTY(QString fileColumn READ fileColumn WRITE setFileColumn NOTIFY changed)
    /*! Rain-file grammar, and the only way back out of USER_CSV: both
     *  setFileColumn() and setFilePath() preserve it by design (engine
     *  contract), so without this row a gage that ever had a column could
     *  never return to a standard `FILE "path" Station Units` rain file and
     *  its Station ID would stay greyed out for good. Selecting MULTI_COLUMN
     *  clears the station id; selecting STANDARD clears the file column — the
     *  two grammars' row selectors are mutually exclusive engine-side.
     *  Only meaningful when dataSource == FILE. */
    Q_PROPERTY(SWMMRainGagePropertyAdapter::RainFileFormat fileFormat READ fileFormatEnum WRITE
                   setFileFormat NOTIFY changed)
    /*! DA.2 parity — DATA FILE source: station id (standard SWMM rain file
     *  grammar `Fname Station Units`) and rain-depth units (0=IN, 1=MM).
     *  Only meaningful when dataSource == FILE. */
    Q_PROPERTY(QString stationId READ stationId WRITE setStationId NOTIFY changed)
    Q_PROPERTY(SWMMRainGagePropertyAdapter::RainUnits rainUnits READ rainUnits WRITE setRainUnits
                   NOTIFY changed)
    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter). */
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /**
     * @return GAGE flag-store reference and current count summary; the bound layer lazily creates its shared store, or model is null without a layer.
     */
    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    /**
     * @return Authored x coordinate in model horizontal CRS units, or zero on unresolved/read failure.
     */
    [[nodiscard]] double xCoord() const;
    /**
     * @return Authored y coordinate in model horizontal CRS units, or zero on unresolved/read failure.
     */
    [[nodiscard]] double yCoord() const;

    /**
     * @return Authored rainfall representation (intensity/volume/cumulative), default Intensity on unresolved/read failure.
     */
    [[nodiscard]] RainType rainType() const;
    /**
     * @return Ref carrying borrowed engine/layer, stored gage ID and nearest whole-second interval; unresolved/read-failed interval is zero.
     */
    [[nodiscard]] RainIntervalRef rainIntervalRef() const;
    /**
     * @return Dimensionless snow-catch correction factor, default one when unresolved/read fails.
     */
    [[nodiscard]] double snowFactor() const;
    /**
     * @return Dimensionless overall gage rainfall multiplier, default one when unresolved/read fails; distinct from the snow-catch correction.
     */
    [[nodiscard]] double scaleFactor() const;
    /**
     * @return TIMESERIES/FILE source selection, default Timeseries on unresolved/read failure.
     */
    [[nodiscard]] DataSource dataSource() const;
    /**
     * @return TimeSeries picker coordinate with borrowed engine/layer and current gage series ID, or empty name when unassigned/unreadable.
     */
    [[nodiscard]] DataObjectRef seriesNameRef() const;
    /**
     * @return Current rainfall intensity in project rainfall units (in/hr or mm/hr), zero on unresolved/read failure.
     */
    [[nodiscard]] double currentRainfall() const;
    /**
     * @return Authored original rain-file path token from the engine, possibly relative; empty when unavailable. Reading does not resolve/open the path.
     */
    [[nodiscard]] QString filePath() const; ///< .original token
    /**
     * @return Engine's cached absolute rain-file path after resolution, or empty when unresolved/read fails. A new path write clears this cache until resolution.
     */
    [[nodiscard]] QString resolvedFilePath() const; ///< .absolute (post-resolve)
    /**
     * @return Multi-column file selector (stored separately from path), or empty when unassigned/unreadable.
     */
    [[nodiscard]] QString fileColumn() const; ///< multi-column selector
    /**
     * @return Standard rain-file station selector from a 256-byte buffer, or empty when unavailable.
     */
    [[nodiscard]] QString stationId() const;
    /**
     * @return Rain-file depth unit enum, Inches on unresolved/read failure; independent of project flow-unit display.
     */
    [[nodiscard]] RainUnits rainUnits() const; ///< 0=IN, 1=MM
    /*! Raw RainFileFormat code: -1 UNKNOWN, 5 STAN_PRCP (standard SWMM rain
     *  file), 6 USER_CSV (multi-column CSV/TSV/TSF). The panel reads it to gate
     *  Station ID; the `fileFormat` property exposes it as an enum.
     * @return Raw engine file grammar code, or -1 for unresolved/read failure. Does not probe the file.
     */
    [[nodiscard]] int fileFormat() const;
    /*! The same value as a Q_ENUM, for the `fileFormat` property's combo. A
     *  code outside the three reachable ones reports StandardRainFile: every
     *  such code is station-based, which is the distinction that decides both
     *  the written grammar and the Station ID row.
     * @return MultiColumn for code 6, AutoDetect for -1, otherwise StandardRainFile (including historical station formats).
     */
    [[nodiscard]] RainFileFormat fileFormatEnum() const;

    /**
     * @param[in] property Exact Q_PROPERTY key.
     * @return Translated gage label, or empty when unknown; no value conversion occurs.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /*! Emit \ref coordChangeRequested rather than writing the engine, so the
     *  panel can route the move through the layer (scene cache + extent).
     * @brief Request a map move while preserving the other current coordinate.
     * @param[in] v Proposed x coordinate in model horizontal CRS units; no local finiteness check occurs.
     * @details Missing object or equal coordinate is ignored. Emits coordChangeRequested instead of writing directly; layer acceptance is external.
     */
    void setXCoord(double v);
    /**
     * @brief Request a map move while preserving the other current coordinate.
     * @param[in] v Proposed y coordinate in model horizontal CRS units; no local finiteness check occurs.
     * @details Missing object or equal coordinate is ignored. Emits coordChangeRequested instead of writing directly; layer acceptance is external.
     */
    void setYCoord(double v);
    /**
     * @brief Select how source values encode rainfall.
     * @param[in] v Engine RainType code: 0 intensity, 1 interval volume, 2 cumulative depth; passed unchanged without local enum validation.
     * @details Missing gage/refused pre-run geometry edit is silent; engine acceptance emits changed.
     */
    void setRainType(int v);
    /**
     * @brief Write the recording interval carried by the clock editor.
     * @param[in] r Only seconds is used, converted to double for the engine; other identity fields do not rebind this adapter.
     * @details Current engine stores whole seconds and defers physical interval validation to run setup. Missing gage or rejected lifecycle is silent; acceptance emits changed.
     */
    void setRainIntervalRef(const RainIntervalRef &r);
    /**
     * @brief Write the snow-catch correction multiplier.
     * @param[in] v Positive dimensionless factor; engine rejects values <= 0.
     * @details Supports engine-permitted runtime multiplier edits; acceptance emits changed, failures are not returned.
     */
    void setSnowFactor(double v);
    /**
     * @brief Write the overall rainfall multiplier for this gage.
     * @param[in] v Positive dimensionless factor; <=0 is locally ignored.
     * @details Engine-permitted runtime edits emit changed on acceptance. No separate error status is returned.
     */
    void setScaleFactor(double v);
    /**
     * @brief Select a time-series or external-file rainfall source.
     * @param[in] v Engine source code, 0 TIMESERIES or 1 FILE; no local enum validation occurs.
     * @details Engine lifecycle checks apply; only accepted writes emit changed.
     */
    void setDataSource(int v);
    /**
     * @brief Bind the gage to an existing in-model time series.
     * @param[in] r Nonempty currentName is passed as a UTF-8 series ID; empty selections are ignored.
     * @details Other ref fields do not rebind the adapter. Accepted engine writes emit changed; failures are silent.
     */
    void setSeriesNameRef(const DataObjectRef &r);
    /**
     * @brief Replace the authored rain-file path and reconcile its column selector.
     * @param[in] p Literal relative/absolute path token; no editor-level path normalization occurs.
     * @details On engine acceptance, probes/reconciles headers using resolved absolute path when available, otherwise the new token; may also change the file column/format. Emits changed after the path write even if a subsequent column write fails.
     */
    void setFilePath(const QString &p);
    /**
     * @brief Select one field from a multi-column rainfall file.
     * @param[in] c Literal column selector, encoded as UTF-8; engine handles empty/format semantics.
     * @details A nonempty selector switches to USER_CSV. Accepted writes emit changed; failures are silent.
     */
    void setFileColumn(const QString &c);
    /**
     * @brief Select rain-file grammar and let the engine clear incompatible selectors.
     * @param[in] v Engine file-format selector, normally -1 auto, 5 standard or 6 multi-column.
     * @details USER_CSV clears station ID; standard grammar clears column selection. Acceptance emits changed so dependent properties reread; no direct error result is returned.
     */
    void setFileFormat(int v);
    /**
     * @brief Change the standard rain-file station selector.
     * @param[in] s Literal station text encoded as UTF-8; empty clears it.
     * @details Only an accepted typed engine write emits changed; does not parse the file.
     */
    void setStationId(const QString &s);
    /**
     * @brief Set external rainfall depth units.
     * @param[in] v Zero means inches, one millimetres; engine rejects other codes and disallowed lifecycle states.
     * @details Acceptance emits changed; the adapter does not convert authored source values itself.
     */
    void setRainUnits(int v);
    /**
     * @brief Refresh flag properties after the dedicated editor completed its own writes.
     * @details The reference argument is ignored; emits changed unconditionally without writing the engine.
     */
    void setUserFlagsRef(const UserFlagsEditRef &) { emit changed(); }

signals:
    /*! X/Y edit intent (both coordinates, whichever one changed). The panel
     *  applies it through SWMMModelLayer::applyGageMove. Mirrors
     *  SWMMNodePropertyAdapter::coordChangeRequested.
     * @brief Request a layer-mediated map move so engine coordinates and scene caches stay synchronized.
     * @param[in] newX Proposed x in model horizontal CRS units.
     * @param[in] newY Proposed y in the same CRS units.
     * @details The signal does not itself change coordinates; the panel/layer applies the move.
     */
    void coordChangeRequested(double newX, double newY);

private:
    /**
     * @brief Resolve this stored gage ID against the current engine.
     * @return Zero-based gage index, or -1 for missing engine/name/object.
     */
    [[nodiscard]] int idx() const;
};

#endif // SWMMRAINGAGEPROPERTYADAPTER_H
