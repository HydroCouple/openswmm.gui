/**
 * @file      swmmsubcatchpropertyadapter.h
 * @brief     Property-tree adapter for SWMM subcatchments.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AG.3 — Property-tree adapter for SWMM subcatchments.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMSUBCATCHPROPERTYADAPTER_H
#define SWMMSUBCATCHPROPERTYADAPTER_H

#include <QObject>
#include <QString>
#include <QUuid> // Stats-source identity (QA.2 mirror)

#include <openswmm/engine/openswmm_engine.h>

#include "ui/properties/userflagseditref.h"        // USER_FLAGS Phase 4
#include "ui/properties/dataobjectref.h"           // Phase 3 — gage / outlet pickers
#include "ui/properties/subcatchcompoundeditref.h" // Phase 3 — landuse/GW/LID

class SWMMModelLayer;

namespace openswmmvis
{
class OutputStatsRegistry;
} // QA.2 mirror

class SWMMSubcatchPropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName)
    /*! Slice TA — free-form `[TAGS]` label. Direct engine read/write
     *  (mirror of node/link tag wiring); the existing `changed()`
     *  signal + PropertiesPanel.objectEdited fan-out is sufficient for
     *  two-way sync with the Attribute Table since tag changes don't
     *  affect map symbology or attribute-table layout. */
    Q_PROPERTY(QString tag READ tag WRITE setTag NOTIFY changed)
    Q_PROPERTY(double area READ area WRITE setArea NOTIFY changed)
    Q_PROPERTY(double width READ width WRITE setWidth NOTIFY changed)
    Q_PROPERTY(double slope READ slope WRITE setSlope NOTIFY changed)
    Q_PROPERTY(double impervPct READ impervPct WRITE setImpervPct NOTIFY changed)
    Q_PROPERTY(double pctZeroImperv READ pctZeroImperv WRITE setPctZeroImperv NOTIFY changed)
    Q_PROPERTY(double nImperv READ nImperv WRITE setNImperv NOTIFY changed)
    Q_PROPERTY(double nPerv READ nPerv WRITE setNPerv NOTIFY changed)
    Q_PROPERTY(double dsImperv READ dsImperv WRITE setDsImperv NOTIFY changed)
    Q_PROPERTY(double dsPerv READ dsPerv WRITE setDsPerv NOTIFY changed)
    /*! Subcatchment rainfall scale factor — optional token 9 of [SUBCATCHMENTS].
     *  Multiplies gage-derived rainfall for this subcatchment only, composing
     *  multiplicatively with the gage's own scale factor. Must be > 0;
     *  default 1.0 (no scaling). */
    Q_PROPERTY(double rainScaleFactor READ rainScaleFactor WRITE setRainScaleFactor NOTIFY changed)
    /*! Subcatchment snowfall scale factor — optional token 10 of
     *  [SUBCATCHMENTS]. Composes with the gage snow catch factor (SCF): SCF
     *  corrects the physical gage's snow-catch deficiency, this represents
     *  spatial variation across the catchment. Must be > 0; default 1.0. */
    Q_PROPERTY(double snowScaleFactor READ snowScaleFactor WRITE setSnowScaleFactor NOTIFY changed)

    // Phase 3 — Subcatchment gaps (docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md).
    /*! Rain gage assignment (R3 picker over gage names). */
    Q_PROPERTY(DataObjectRef snowPack READ snowPackRef WRITE setSnowPackRef NOTIFY changed)
    Q_PROPERTY(DataObjectRef rainGage READ rainGageRef WRITE setRainGageRef NOTIFY changed)
    /*! Outlet target — a node OR another subcatchment (cascade). Combined
     *  picker; the WRITE slot resolves the name to the right engine call. */
    Q_PROPERTY(DataObjectRef outlet READ outletRef WRITE setOutletRef NOTIFY changed)
    /*! Infiltration model (Horton / Mod-Horton / Green-Ampt / Mod-GA / CN). */
    Q_PROPERTY(InfilModel infilModel READ infilModel WRITE setInfilModel NOTIFY changed)
    // Per-model parameter rows. Editability is gated by the live model in
    // PropertiesPanel (mirror of the storage Functional/Tabular greying).
    Q_PROPERTY(double hortonF0 READ hortonF0 WRITE setHortonF0 NOTIFY changed)
    Q_PROPERTY(double hortonFmin READ hortonFmin WRITE setHortonFmin NOTIFY changed)
    Q_PROPERTY(double hortonDecay READ hortonDecay WRITE setHortonDecay NOTIFY changed)
    Q_PROPERTY(double hortonDryTime READ hortonDryTime WRITE setHortonDryTime NOTIFY changed)
    Q_PROPERTY(double gaSuction READ gaSuction WRITE setGaSuction NOTIFY changed)
    Q_PROPERTY(double gaConductivity READ gaConductivity WRITE setGaConductivity NOTIFY changed)
    Q_PROPERTY(double gaInitDeficit READ gaInitDeficit WRITE setGaInitDeficit NOTIFY changed)
    Q_PROPERTY(double cnNumber READ cnNumber WRITE setCnNumber NOTIFY changed)
    Q_PROPERTY(double cnDryTime READ cnDryTime WRITE setCnDryTime NOTIFY changed)
    // Compound cells (open SubcatchCompoundEditDialog tabs).
    Q_PROPERTY(SubcatchCompoundEditRef landUse READ landUseRef WRITE setLandUseRef NOTIFY changed)
    /*! Receiving aquifer ([GROUNDWATER] aquifer column). Picker over
     *  [AQUIFERS] names; an empty pick clears the assignment (-1). The
     *  routing parameters live in the `groundwater` exchange editor. */
    Q_PROPERTY(DataObjectRef aquifer READ aquiferRef WRITE setAquiferRef NOTIFY changed)
    Q_PROPERTY(SubcatchCompoundEditRef groundwater READ groundwaterRef WRITE setGroundwaterRef
                   NOTIFY changed)
    Q_PROPERTY(
        SubcatchCompoundEditRef lidUsage READ lidUsageRef WRITE setLidUsageRef NOTIFY changed)
    Q_PROPERTY(
        SubcatchCompoundEditRef loadings READ loadingsRef WRITE setLoadingsRef NOTIFY changed)

    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter). */
    Q_PROPERTY(UserFlagsEditRef userFlags READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)

    // Read-only post-run summary (no WRITE → non-editable). Mirrors the
    // Attribute Table's subcatchment dynamics columns; values follow the
    // bound stats source (see setStatsSource below).
    Q_PROPERTY(double statPrecip READ statPrecip NOTIFY changed)
    Q_PROPERTY(double statRunoffVol READ statRunoffVol NOTIFY changed)
    Q_PROPERTY(double statMaxRunoff READ statMaxRunoff NOTIFY changed)

public:
    /*! Infiltration model codes (engine [INFILTRATION] order). */
    enum InfilModel { Horton = 0, ModHorton = 1, GreenAmpt = 2, ModGreenAmpt = 3, CurveNumber = 4 };
    Q_ENUM(InfilModel)

    /**
     * @brief Bind live properties and output summaries to a named subcatchment.
     * @param[in] engine Borrowed engine that must remain valid while this adapter is used.
     * @param[in] name Case-preserved object ID copied into the adapter; indexes are resolved afresh.
     * @param[in] parent Optional Qt owner; construct and use on the GUI thread.
     * @details Forwards active-project unit changes as displayLabelsChanged; does not own the engine or create model data.
     */
    SWMMSubcatchPropertyAdapter(SWMM_Engine engine, QString name, QObject *parent = nullptr);

    /*! USER_FLAGS Phase 4 — bind the owning layer so userFlagsRef() can
     *  reach the shared UserFlagsModel (mirror of
     *  SWMMNodePropertyAdapter::setModelLayer; nullptr-safe).
     * @param[in] layer Borrowed layer for shared stores, picker flows and layer-mediated edits; nullptr detaches. This raw pointer must outlive adapter use.
     */
    void setModelLayer(SWMMModelLayer *layer) { m_layer = layer; }
    /**
     * @return Borrowed currently bound model layer, or nullptr; no lifetime extension occurs.
     */
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return m_layer; }

    /**
     * @brief Build the per-object flag editor payload and current set-count summary.
     * @return Reference to the layer's shared flag store, lazily created by ensureUserFlagsModel(), or null model when no layer is bound. All pointers remain borrowed.
     */
    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    /**
     * @return Stored object ID, independent of whether it still resolves in the engine.
     */
    [[nodiscard]] QString name() const { return m_name; }
    /**
     * @return UTF-8 descriptive tag from a 256-byte buffer, or empty on unresolved/unreadable object.
     */
    [[nodiscard]] QString tag() const;

    /**
     * @brief Read the catchment area in project land-area units (acres or hectares).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double area() const;
    /**
     * @brief Read the equivalent overland-flow width in project length units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double width() const;
    /**
     * @brief Read the average surface slope in percent (2 means 2%, not 0.02).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double slope() const;
    /**
     * @brief Read the impervious share of total catchment area in percent.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double impervPct() const;
    /**
     * @brief Read the share of impervious area with zero depression storage in percent.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double pctZeroImperv() const;
    /**
     * @brief Read the impervious overland Manning roughness coefficient.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double nImperv() const;
    /**
     * @brief Read the pervious overland Manning roughness coefficient.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double nPerv() const;
    /**
     * @brief Read the impervious depression-storage depth in project rainfall units (in or mm).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double dsImperv() const;
    /**
     * @brief Read the pervious depression-storage depth in project rainfall units (in or mm).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double dsPerv() const;
    /**
     * @brief Read the catchment-specific precipitation multiplier applied after its gage values.
     * @return Dimensionless factor; one (neutral) when the object is unresolved/read fails.
     */
    [[nodiscard]] double rainScaleFactor() const;
    /**
     * @brief Read the catchment-specific precipitation multiplier applied after its gage values.
     * @return Dimensionless factor; one (neutral) when the object is unresolved/read fails.
     */
    [[nodiscard]] double snowScaleFactor() const;

    // Phase 3 — picker / enum / infiltration param accessors.
    /**
     * @return Snowpack picker coordinate and current assigned snowpack name, empty if unavailable/unassigned.
     */
    [[nodiscard]] DataObjectRef snowPackRef() const;
    /**
     * @return RainGage picker coordinate with current gage ID or empty when unavailable; engine/layer pointers stay borrowed.
     */
    [[nodiscard]] DataObjectRef rainGageRef() const;
    /**
     * @return Combined SubcatchOutlet picker; an authored cascade subcatchment takes display precedence over a node outlet.
     */
    [[nodiscard]] DataObjectRef outletRef() const;
    /**
     * @return Current engine infiltration-model code as InfilModel; zero (Horton) on unresolved/read failure.
     */
    [[nodiscard]] InfilModel infilModel() const;
    /**
     * @brief Read the initial maximum Horton infiltration capacity in project rainfall rate units (in/hr or mm/hr).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double hortonF0() const;
    /**
     * @brief Read the minimum Horton infiltration capacity in project rainfall rate units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double hortonFmin() const;
    /**
     * @brief Read the Horton capacity decay constant in reciprocal hours.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double hortonDecay() const;
    /**
     * @brief Read the Horton soil drying time in days.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double hortonDryTime() const;
    /**
     * @brief Read the Green-Ampt wetting-front suction in project rainfall depth units (in or mm).
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double gaSuction() const;
    /**
     * @brief Read the Green-Ampt saturated conductivity in project rainfall rate units.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double gaConductivity() const;
    /**
     * @brief Read the Green-Ampt initial moisture deficit as a dimensionless fraction.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double gaInitDeficit() const;
    /**
     * @brief Read the SCS runoff curve number.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double cnNumber() const;
    /**
     * @brief Read the Curve Number soil drying time in days.
     * @return Current engine value in the stated units; zero for unresolved object/read failure. No input is changed.
     */
    [[nodiscard]] double cnDryTime() const;
    /**
     * @brief Build an editor coordinate and live summary for positive land-use coverages on this subcatchment.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] SubcatchCompoundEditRef landUseRef() const;
    /**
     * @return Aquifer picker coordinate and current receiving aquifer ID, or empty for absent/unreadable assignment.
     */
    [[nodiscard]] DataObjectRef aquiferRef() const;
    /**
     * @brief Build an editor coordinate and live summary for receiving aquifer/node and custom GWF-expression presence.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] SubcatchCompoundEditRef groundwaterRef() const;
    /**
     * @brief Build an editor coordinate and live summary for LID usage records assigned to this subcatchment.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] SubcatchCompoundEditRef lidUsageRef() const;
    /**
     * @brief Build an editor coordinate and live summary for pollutants with positive initial buildup on this subcatchment.
     * @return Value payload with borrowed engine/layer and stored object ID; engine/model ownership stays with the project. Summary reads do not apply edits.
     */
    [[nodiscard]] SubcatchCompoundEditRef loadingsRef() const;

    // Read-only post-run summary getters. Zero until a run's results are
    // bound via setStatsSource (mirror of SWMMNodePropertyAdapter QA.2).
    /**
     * @brief Read the total precipitation volume in cubic project length units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statPrecip() const;
    /**
     * @brief Read the total runoff volume in cubic project length units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statRunoffVol() const;
    /**
     * @brief Read the peak runoff in project flow units from the selected simulation source.
     * @return Ambient engine statistic when source UUID is null or no registry is bound; otherwise the resolved result-layer statistic for this object ID. Missing engine objects/output layers return zero.
     * @details Duration wrappers convert ambient engine seconds to hours; external result layers already expose hours. Statistics do not alter current model inputs.
     */
    [[nodiscard]] double statMaxRunoff() const;

    /*! Stats-source dispatch — see SWMMNodePropertyAdapter (Slice QA.2)
     *  for the full contract.
     * @param[in] registry Borrowed output-statistics registry; nullptr uses the engine fallback. Registry must outlive adapter use.
     * @details Replaces the raw registry pointer without emitting changed; caller selects/refreshed source separately.
     */
    void setStatsRegistry(openswmmvis::OutputStatsRegistry *registry);
    /**
     * @param[in] id Output identity UUID; null chooses the editing engine's ambient statistics.
     * @details A changed UUID emits changed to reread summaries. A missing resolved output layer returns the statistic getter's unavailable value.
     */
    void setStatsSource(const QUuid &id);
    /**
     * @return Selected statistics identity; null means the editing engine rather than an external result layer.
     */
    [[nodiscard]] QUuid statsSourceId() const { return m_statsSourceId; }

    /*! See SWMMNodePropertyAdapter::displayLabelFor.
     * @param[in] property Exact case-sensitive Q_PROPERTY key.
     * @return Translated label using the active project units where applicable, or empty for an unsupported key. Values are not converted by this label method.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @param[in] newName Proposed ID trimmed before comparison; empty or unchanged names are ignored.
     * @details Emits renameRequested only; layer-side acceptance must call updateStoredName() before this adapter uses the new ID.
     */
    void setName(const QString &newName);
    /**
     * @brief Replace the descriptive engine tag.
     * @param[in] t Literal tag text encoded as UTF-8; empty clears it.
     * @details Missing object is a no-op; emits changed only on engine acceptance, without returning a failure diagnostic.
     */
    void setTag(const QString &t);
    /**
     * @brief Update the catchment area in project land-area units (acres or hectares).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setArea(double v);
    /**
     * @brief Update the equivalent overland-flow width in project length units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setWidth(double v);
    /**
     * @brief Update the average surface slope in percent (2 means 2%, not 0.02).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setSlope(double v);
    /**
     * @brief Update the impervious share of total catchment area in percent.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setImpervPct(double v);
    /**
     * @brief Update the share of impervious area with zero depression storage in percent.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setPctZeroImperv(double v);
    /**
     * @brief Update the impervious overland Manning roughness coefficient.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setNImperv(double v);
    /**
     * @brief Update the pervious overland Manning roughness coefficient.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setNPerv(double v);
    /**
     * @brief Update the impervious depression-storage depth in project rainfall units (in or mm).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setDsImperv(double v);
    /**
     * @brief Update the pervious depression-storage depth in project rainfall units (in or mm).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Resolves the current object and passes the value to the typed engine setter; local range/finiteness checks are not added. Missing objects or engine refusal return silently; successful writes emit changed.
     */
    void setDsPerv(double v);
    /**
     * @brief Apply a catchment-specific precipitation multiplier.
     * @param[in] v Positive dimensionless factor; values <= 0 are locally ignored.
     * @details Missing subcatchment or engine rejection is silent; SWMM_OK emits changed. The gage's own factors remain separate and compose with this multiplier.
     */
    void setRainScaleFactor(double v);
    /**
     * @brief Apply a catchment-specific precipitation multiplier.
     * @param[in] v Positive dimensionless factor; values <= 0 are locally ignored.
     * @details Missing subcatchment or engine rejection is silent; SWMM_OK emits changed. The gage's own factors remain separate and compose with this multiplier.
     */
    void setSnowScaleFactor(double v);

    // Phase 3 — picker / enum / infiltration param write slots.
    /**
     * @brief Assign or clear snowmelt parameterization for this subcatchment.
     * @param[in] r currentName supplies the snowpack ID; empty clears, and equal names are ignored.
     * @details Engine validates existence/state; only successful writes emit changed.
     */
    void setSnowPackRef(const DataObjectRef &r);
    /**
     * @brief Assign an existing rain gage to this subcatchment.
     * @param[in] r currentName must resolve to a gage; empty is ignored because this adapter requires a gage assignment.
     * @details Missing object/unknown gage/refusal is silent; engine acceptance emits changed.
     */
    void setRainGageRef(const DataObjectRef &r);
    /**
     * @brief Assign a node or cascading subcatchment outlet.
     * @param[in] r Nonempty currentName; node names win on collisions, then another subcatchment is considered. Empty, unknown and self-cascade picks are ignored.
     * @details Typed engine setters make node/cascade assignments mutually exclusive and enforce additional routing rules. Acceptance emits changed; failures are silent.
     */
    void setOutletRef(const DataObjectRef &r);
    /**
     * @brief Select the subcatchment infiltration formulation.
     * @param[in] m Engine-aligned InfilModel enum, including modified variants.
     * @details Delegates model validation to the engine; missing subcatchment/refusal is silent and acceptance emits changed.
     */
    void setInfilModel(InfilModel m);
    /**
     * @brief Update the initial maximum Horton infiltration capacity in project rainfall rate units (in/hr or mm/hr).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads sibling infiltration parameters and the current model, updates the tuple, then restores the model code so modified Horton/Green-Ampt choices survive. Emits changed after the tuple setter succeeds; restoration status is not returned.
     */
    void setHortonF0(double v);
    /**
     * @brief Update the minimum Horton infiltration capacity in project rainfall rate units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads sibling infiltration parameters and the current model, updates the tuple, then restores the model code so modified Horton/Green-Ampt choices survive. Emits changed after the tuple setter succeeds; restoration status is not returned.
     */
    void setHortonFmin(double v);
    /**
     * @brief Update the Horton capacity decay constant in reciprocal hours.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads sibling infiltration parameters and the current model, updates the tuple, then restores the model code so modified Horton/Green-Ampt choices survive. Emits changed after the tuple setter succeeds; restoration status is not returned.
     */
    void setHortonDecay(double v);
    /**
     * @brief Update the Horton soil drying time in days.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads sibling infiltration parameters and the current model, updates the tuple, then restores the model code so modified Horton/Green-Ampt choices survive. Emits changed after the tuple setter succeeds; restoration status is not returned.
     */
    void setHortonDryTime(double v);
    /**
     * @brief Update the Green-Ampt wetting-front suction in project rainfall depth units (in or mm).
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads sibling infiltration parameters and the current model, updates the tuple, then restores the model code so modified Horton/Green-Ampt choices survive. Emits changed after the tuple setter succeeds; restoration status is not returned.
     */
    void setGaSuction(double v);
    /**
     * @brief Update the Green-Ampt saturated conductivity in project rainfall rate units.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads sibling infiltration parameters and the current model, updates the tuple, then restores the model code so modified Horton/Green-Ampt choices survive. Emits changed after the tuple setter succeeds; restoration status is not returned.
     */
    void setGaConductivity(double v);
    /**
     * @brief Update the Green-Ampt initial moisture deficit as a dimensionless fraction.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads sibling infiltration parameters and the current model, updates the tuple, then restores the model code so modified Horton/Green-Ampt choices survive. Emits changed after the tuple setter succeeds; restoration status is not returned.
     */
    void setGaInitDeficit(double v);
    /**
     * @brief Update the SCS runoff curve number.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the sibling Curve Number/drying value and calls writeCurveNumber(), preserving the current infiltration model code. Emits changed after tuple acceptance; failed reads/writes have no separate status.
     */
    void setCnNumber(double v);
    /**
     * @brief Update the Curve Number soil drying time in days.
     * @param[in] v New authored value in the stated units; engine lifecycle/physical validation remains authoritative.
     * @details Rereads the sibling Curve Number/drying value and calls writeCurveNumber(), preserving the current infiltration model code. Emits changed after tuple acceptance; failed reads/writes have no separate status.
     */
    void setCnDryTime(double v);
    /**
     * @brief Assign or clear the receiving groundwater aquifer.
     * @param[in] r currentName resolves the aquifer; empty clears with -1 and unknown nonempty names are ignored.
     * @details Engine acceptance emits changed; pointer fields do not rebind this adapter.
     */
    void setAquiferRef(const DataObjectRef &r);
    // Compound refs are coordinates only; the dialog performs the writes.
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setLandUseRef(const SubcatchCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setGroundwaterRef(const SubcatchCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setLidUsageRef(const SubcatchCompoundEditRef &) { emit changed(); }
    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setLoadingsRef(const SubcatchCompoundEditRef &) { emit changed(); }

    /**
     * @brief Complete the property-delegate round trip after the editor has applied its own changes.
     * @details The reference argument is intentionally ignored; emits changed unconditionally and performs no engine write.
     */
    void setUserFlagsRef(const UserFlagsEditRef &) { emit changed(); }

    /*! Round-4 follow-up 2026-05-12 — see SWMMNodePropertyAdapter::refresh.
     * @details Emits changed unconditionally to reread live properties; no engine input is written.
     */
    void refresh() { emit changed(); }

    /*! See SWMMNodePropertyAdapter::updateStoredName.
     * @param[in] newName Accepted engine ID copied verbatim; no engine rename or signal occurs here.
     */
    void updateStoredName(const QString &newName) { m_name = newName; }

signals:
    /**
     * @brief Request property-model rereads after accepted writes, refresh calls or edit-dialog round trips.
     * @details Some delegated edit paths also emit after refusal so controls restore effective engine values; this is not a universal write-success signal.
     */
    void changed();
    /**
     * @param[in] oldName Stored engine ID before the proposed rename.
     * @param[in] newName Nonempty trimmed proposed ID; the receiving layer validates uniqueness and lifecycle.
     */
    void renameRequested(const QString &oldName, const QString &newName);
    /*! See SWMMNodePropertyAdapter::displayLabelsChanged.
     * @brief Request property labels to be rebuilt after active-project units change, without changing stored input values.
     */
    void displayLabelsChanged();

private:
    /**
     * @brief Resolve the stored subcatchment ID in the current engine.
     * @return Zero-based index, or -1 for missing engine/name/object; no index is cached across renames.
     */
    [[nodiscard]] int idx() const;
    /*! Curve Number and its drying time share one engine setter, so both
     *  Q_PROPERTY writers funnel through here after re-reading the sibling.
     * @brief Write curve-number and drying-time terms while preserving the selected infiltration model.
     * @param[in] cn Dimensionless SCS curve number.
     * @param[in] dryTime Soil drying duration in days.
     * @details Missing subcatchment is a no-op. Tuple acceptance triggers a model-code restoration attempt and changed; restoration failure has no separate status.
     */
    void writeCurveNumber(double cn, double dryTime);
    SWMM_Engine m_engine;
    QString m_name;
    SWMMModelLayer *m_layer = nullptr; ///< USER_FLAGS Phase 4 — borrow.

    /// Stats-source dispatch state — see SWMMNodePropertyAdapter.
    QUuid m_statsSourceId;
    openswmmvis::OutputStatsRegistry *m_statsRegistry = nullptr;
};

#endif // SWMMSUBCATCHPROPERTYADAPTER_H
