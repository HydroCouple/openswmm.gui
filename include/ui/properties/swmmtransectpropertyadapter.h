/**
 * @file      swmmtransectpropertyadapter.h
 * @brief     Slice DA.2 / Phase 6.7.4 — Property-tree adapter for [TRANSECTS].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 / Phase 6.7.4 — Property-tree adapter for [TRANSECTS].
 *
 * Full scalar coverage backed by `openswmm_infrastructure.h` (DA-ENG-09
 * + BQ-TR-02 shipped): roughness triple, bank stations, encroachment
 * stations, modifier triple (xFactor / yFactor / lengthFactor), and
 * comments. Station-elevation point list is exposed as a read-only count
 * summary — full editing of the point list lives in TransectEditorDialog
 * (Slice BQ Phase 6.7.4).
 *
 * Per-field setters read the current engine triple, mutate the one field
 * being written, and write back atomically — the engine ships only triple
 * setters for roughness / bank / encroachment / modifiers.
 *
 * The adapter listens to the bound SWMMModelLayer's `transectChanged(name)`
 * signal so edits made elsewhere (TransectEditorDialog) refresh the
 * property panel in lock-step ([[feedback_mvc_synchronized_uis]]).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMTRANSECTPROPERTYADAPTER_H
#define SWMMTRANSECTPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMTransectPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT

    Q_PROPERTY(QString comments READ comments WRITE setComments NOTIFY changed)

    Q_PROPERTY(double nLeftBank READ nLeftBank WRITE setNLeftBank NOTIFY changed)
    Q_PROPERTY(double nRightBank READ nRightBank WRITE setNRightBank NOTIFY changed)
    Q_PROPERTY(double nChannel READ nChannel WRITE setNChannel NOTIFY changed)

    Q_PROPERTY(double xLeftBank READ xLeftBank WRITE setXLeftBank NOTIFY changed)
    Q_PROPERTY(double xRightBank READ xRightBank WRITE setXRightBank NOTIFY changed)

    Q_PROPERTY(
        double xLeftEncroachment READ xLeftEncroachment WRITE setXLeftEncroachment NOTIFY changed)
    Q_PROPERTY(double xRightEncroachment READ xRightEncroachment WRITE setXRightEncroachment NOTIFY
                   changed)

    Q_PROPERTY(
        double stationMultiplier READ stationMultiplier WRITE setStationMultiplier NOTIFY changed)
    Q_PROPERTY(double elevationOffset READ elevationOffset WRITE setElevationOffset NOTIFY changed)
    Q_PROPERTY(double meanderFactor READ meanderFactor WRITE setMeanderFactor NOTIFY changed)

    // Read-only station count (Property Browser surface — full point list
    // edited through TransectEditorDialog).
    Q_PROPERTY(int stationCount READ stationCount NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /**
     * @return UTF-8 description from a 2048-byte engine buffer, or empty when unresolved/read fails.
     */
    [[nodiscard]] QString comments() const;
    /**
     * @brief Read the left-bank Manning roughness coefficient.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double nLeftBank() const;
    /**
     * @brief Read the right-bank Manning roughness coefficient.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double nRightBank() const;
    /**
     * @brief Read the main-channel Manning roughness coefficient.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double nChannel() const;
    /**
     * @brief Read the left-bank station in the authored station coordinate units.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double xLeftBank() const;
    /**
     * @brief Read the right-bank station in the authored station coordinate units.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double xRightBank() const;
    /**
     * @brief Read the left encroachment station in authored station coordinate units.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double xLeftEncroachment() const;
    /**
     * @brief Read the right encroachment station in authored station coordinate units.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double xRightEncroachment() const;
    /**
     * @brief Read the dimensionless multiplier applied to station distances.
     * @return Authored component or one on unresolved/read failure.
     */
    [[nodiscard]] double stationMultiplier() const;
    /**
     * @brief Read the vertical elevation offset in authored elevation units.
     * @return Authored component or zero on unresolved/read failure.
     */
    [[nodiscard]] double elevationOffset() const;
    /**
     * @brief Read the dimensionless channel-length meander multiplier.
     * @return Authored component or one on unresolved/read failure.
     */
    [[nodiscard]] double meanderFactor() const;
    /**
     * @return Number of authored station/elevation pairs; zero for unresolved transect or negative engine result.
     */
    [[nodiscard]] int stationCount() const;

    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @brief Replace the transect description.
     * @param[in] v Literal text encoded as UTF-8; empty clears the description.
     * @details Emits changed only on successful engine write; unresolved objects are ignored.
     */
    void setComments(const QString &v);
    /**
     * @brief Update the left-bank Manning roughness coefficient.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setNLeftBank(double v);
    /**
     * @brief Update the right-bank Manning roughness coefficient.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setNRightBank(double v);
    /**
     * @brief Update the main-channel Manning roughness coefficient.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setNChannel(double v);
    /**
     * @brief Update the left-bank station in the authored station coordinate units.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setXLeftBank(double v);
    /**
     * @brief Update the right-bank station in the authored station coordinate units.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setXRightBank(double v);
    /**
     * @brief Update the left encroachment station in authored station coordinate units.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setXLeftEncroachment(double v);
    /**
     * @brief Update the right encroachment station in authored station coordinate units.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setXRightEncroachment(double v);
    /**
     * @brief Update the dimensionless multiplier applied to station distances.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setStationMultiplier(double v);
    /**
     * @brief Update the vertical elevation offset in authored elevation units.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setElevationOffset(double v);
    /**
     * @brief Update the dimensionless channel-length meander multiplier.
     * @param[in] v New component in the stated units; no local bounds/finiteness check is applied.
     * @details Rereads the complete sibling tuple, preserves other components, and ignores equal values. Failed read/write produces no signal; SWMM_OK emits changed.
     */
    void setMeanderFactor(double v);

private:
    /**
     * @brief Resolve the stored object name against the current engine collection.
     * @return Zero-based live index, or -1 for a missing engine, empty name or unresolved ID; no index is cached across renames.
     */
    [[nodiscard]] int idx() const;
};

#endif // SWMMTRANSECTPROPERTYADAPTER_H
