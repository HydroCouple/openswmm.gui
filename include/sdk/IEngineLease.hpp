/**
 * @file IEngineLease.hpp
 * @brief RAII read/write access to the GUI-owned engine image.
 * @details Read leases can coexist. A write lease excludes all other leases and simulation runs. A handle must never be closed or destroyed by an extension and must not escape its lease.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
namespace OpenSWMM::Sdk
{
/** @brief Requested access to a GUI-owned model engine. */
enum class LeaseKind {
    Read, ///< Shared read access, excluding runs and any writer.
    Write ///< Exclusive native access, excluding all other leases and runs.
};
/** RAII access to the host's engine image. Never close, replace, or destroy this handle. */
class IEngineLease
{
public:
    /** @brief Release the lease on the GUI thread without closing the borrowed engine. */
    virtual ~IEngineLease() = default;
    /** @brief Borrow the current engine while this lease is alive.
     * @return SWMM_Engine-compatible void pointer, or null after model closure.
     * @note GUI thread only; never close, destroy or retain this handle beyond the lease. */
    virtual void *engine() const = 0;
};
}
