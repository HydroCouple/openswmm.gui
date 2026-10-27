/**
 * @file IProgressV1.hpp
 * @brief SDK 1.7 capability for shared application progress and operation logs.
 * @details Discover this optional interface with qobject_cast on ExtensionHost::lifetime().
 * It reports GUI-thread asynchronous work, including network transfers that do not use runJob().
 * The host qualifies titles with the plugin identity and records start, stage changes and one
 * terminal outcome in the normal Logs panel and the plugin's bounded Output channel. Existing
 * SDK interfaces and their binary layouts remain unchanged.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QObject>
#include <QString>
#include <QUuid>

/** @brief Frozen discovery identity for shared application progress. */
#define OPENSWMM_PROGRESS_V1_IID "io.opensimulator.openswmm.IProgress/1.0"

namespace OpenSWMM::Sdk
{
/** @brief Terminal classification for an extension-owned progress operation. */
enum class ProgressOutcome {
    Succeeded, ///< Work completed successfully.
    Failed,    ///< Work stopped with an error; detail may explain the failure.
    Cancelled  ///< Work was cancelled or its receiver lifetime ended.
};

/** @brief Optional host-owned progress service; every method requires the GUI thread.
 * @note Progress is informational and does not start, cancel or own the underlying work.
 * The extension must stop its work separately when its receiver is destroyed. Host runJob()
 * workers already publish through this service and should not create a duplicate operation. */
class IProgressV1
{
public:
    /** @brief Destroy an implementation; plugins only borrow the host capability. */
    virtual ~IProgressV1() = default;
    /** @brief Begin an operation and immediately publish indeterminate shared progress.
     * @param[in] title Nonempty user-facing title; the host adds the plugin identity.
     * @param[in] context Required GUI-thread lifetime guard, kept on the GUI thread.
     * @return Unique per-host operation UUID, or null for an empty title, invalid context,
     * wrong-thread call, stopping host, or operation released during synchronous publication.
     * @note Context destruction releases only its outstanding operations as cancelled.
     * Native statusbar/log publication is synchronous and may reenter application code. */
    virtual QUuid beginProgress(const QString &title, QObject *context) = 0;
    /** @brief Update progress and its current human-readable stage.
     * @param[in] operationId Active UUID issued by this exact host.
     * @param[in] fraction Finite negative values mean indeterminate; nonnegative values clamp
     * to [0,1]. NaN and infinity are rejected without altering progress.
     * @param[in] stage Current stage; use stable wording so byte/count updates do not flood Logs.
     * @return True for an accepted update/no-op, false for invalid, foreign or finished IDs,
     * unavailable/moved contexts, wrong-thread calls, nonfinite fractions or a stopping host.
     * @note Identical updates are coalesced. Only changed nonempty stages are logged. */
    virtual bool updateProgress(const QUuid &operationId, double fraction,
                                const QString &stage) = 0;
    /** @brief Remove an operation from shared progress and record its terminal outcome once.
     * @param[in] operationId Active UUID issued by this exact host.
     * @param[in] outcome Successful, failed or cancelled; invalid enum values are rejected.
     * @param[in] detail Optional literal terminal explanation, appended to the log entry.
     * @return True when finished, false for invalid, foreign or finished IDs, unavailable/moved
     * contexts, wrong-thread calls, invalid outcomes or a stopping host.
     * @note Host shutdown releases all outstanding native progress without further plugin
     * callbacks or log messages. Finishing one operation never clears another operation. */
    virtual bool finishProgress(const QUuid &operationId, ProgressOutcome outcome,
                                const QString &detail = {}) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IProgressV1, OPENSWMM_PROGRESS_V1_IID)
Q_DECLARE_METATYPE(OpenSWMM::Sdk::ProgressOutcome)
