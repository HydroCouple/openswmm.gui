/**
 * @file INativeEditScope.hpp
 * @brief Transactional native edits with snapshot rollback and undo.
 * @details Scopes borrow a live GUI engine under an exclusive lease. Successful commit rebuilds the GUI mirror and adds one undo command. Uncommitted scopes restore their before snapshot on destruction.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QString>
namespace OpenSWMM::Sdk
{
/** Live engine transaction: destruction aborts unless commit succeeded. GUI thread only. */
class INativeEditScope
{
public:
    /** @brief Abort an uncommitted transaction before releasing its exclusive lease. */
    virtual ~INativeEditScope() = default;
    /** @brief Borrow the live SWMM_Engine for native C API calls.
     * @return Mutable handle, or null after commit/abort/closure.
     * @note Never close, destroy or cache the handle beyond this scope. GUI thread only. */
    virtual void *engine() const = 0;
    /** @brief Write the after snapshot, rebuild GUI caches and append one undo command.
     * @param[out] error Optional snapshot failure detail.
     * @return True on successful commit; false attempts restoration of the before snapshot.
     * @note Success releases the lease. Snapshot files are retained for review and undo/redo. */
    virtual bool commit(QString *error = nullptr) = 0;
    /** @brief Restore the before snapshot and release the exclusive lease.
     * @param[out] error Optional restoration failure detail.
     * @return True after successful restoration or a previously finished scope; false if unavailable.
     * @note Reopening can change the engine handle; no borrowed handle survives rollback or undo. */
    virtual bool abort(QString *error = nullptr) = 0;
};
}
