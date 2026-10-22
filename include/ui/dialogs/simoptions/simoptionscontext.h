/**
 * @file      simoptionscontext.h
 * @brief     The one thing every Simulation Options page is handed.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The one thing every Simulation Options page is handed.
 *
 * Borrows engine/model handles, centralizes common OPTIONS access and owns
 * the per-pass record of considered keys. Specialized pages also call section
 * and extended-option APIs; they record those keys through this context.
 * Common numeric-aware change detection lives here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SIMOPTIONSCONTEXT_H
#define SIMOPTIONSCONTEXT_H

#include <QString>
#include <QStringList>

#include <openswmm/engine/openswmm_engine.h>

#include "ui/dialogs/simoptions/enginecapabilities.h"

class SWMMModelLayer;
class SWMMVisProjectWindow;

namespace openswmmvis::ui
{

class SimOptionsContext
{
public:
    /**
     * @brief Bind borrowed model handles and own a fixed capability snapshot.
     * @param e Borrowed engine, permitted null; caller keeps it valid while used.
     * @param layer Borrowed optional model layer, used for notifying option writes.
     * @param pw Borrowed optional project window exposed to page-specific operations.
     * @param caps Moved by-value capability snapshot; not dynamically re-probed.
     * Caller coordinates GUI-thread access, matching handle identities and lifetimes;
     * these raw pointers are not guarded or retained by ownership. Call invalidate
     * before any backing object becomes unusable.
     */
    SimOptionsContext(SWMM_Engine e, SWMMModelLayer *layer, SWMMVisProjectWindow *pw,
                      EngineCapabilities caps);

    /**
     * @brief Expose the currently borrowed engine.
     * @return Raw handle or null after invalidation; no validation, ownership transfer
     * or lifetime extension.
     */
    [[nodiscard]] SWMM_Engine engine() const { return engine_; }
    /**
     * @brief Expose the optional borrowed model layer.
     * @return Raw pointer or null; caller must not retain it past owner lifetime or invalidation.
     */
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return layer_; }
    /**
     * @brief Expose the optional borrowed project window.
     * @return Raw pointer or null, with no ownership or lifetime extension.
     */
    [[nodiscard]] SWMMVisProjectWindow *projectWindow() const { return pw_; }
    /**
     * @brief Borrow the fixed capability snapshot.
     * @return Const reference valid while this context lives; remains unchanged even
     * after invalidate() and does not imply the engine handle is still valid.
     */
    [[nodiscard]] const EngineCapabilities &caps() const { return caps_; }

    /**
     * @brief Read a trimmed common OPTIONS value through the borrowed engine.
     * @param key Borrowed non-null null-terminated engine key when an engine is bound.
     * @param fallback Copied result for no engine or getter failure, default empty.
     * @return Owned UTF-8 value from a 256-byte buffer on success, including an empty
     * trimmed value; success does not substitute fallback. No buffer growth, writes
     * or lifetime validation are performed.
     */
    [[nodiscard]] QString option(const char *key, const QString &fallback = {}) const;

    /**
     * @brief Record consideration and conditionally write one common OPTIONS key.
     * @param key Borrowed non-null Latin-1/null-terminated key, copied into the pass log.
     * @param newVal Borrowed proposed text, passed to the setter without normalization.
     * @return One for a successful layer/raw setter, zero for unchanged values,
     * missing engine or setter refusal; there is no separate failure result.
     * FV_IMPLICIT_ keys compare parsed numbers exactly (or text exactly); other keys
     * use the dialog trimmed/numeric relative comparison. Prefers layer setOption
     * for notification, otherwise the raw engine setter. Caller keeps layer/engine
     * identities consistent. Every call is logged, including no-ops/failures and
     * duplicates. No undo command, aggregate rollback or file save is created.
     */
    int writeIfChanged(const char *key, const QString &newVal);

    /**
     * @brief Append an option key to the current consideration log without an engine write.
     * @param key Borrowed non-null null-terminated Latin-1 key copied verbatim.
     * Used by specialized page setters such as extended 2D options; preserves call
     * order and duplicates without indicating setter success.
     */
    void recordWrittenKey(const char *key);

    /**
     * @brief Clear all three borrowed handles before their owners become invalid.
     * Does not destroy objects, clear the capability snapshot or reset the current
     * consideration log. This context must itself remain alive for its pages.
     */
    void invalidate()
    {
        engine_ = nullptr;
        layer_ = nullptr;
        pw_ = nullptr;
    }

    /**
     * @brief Clear the option-key consideration log for the next Apply pass.
     * Does not change engine state, capabilities or borrowed identities.
     */
    void beginWritePass(); ///< Clears writtenKeys().
    /**
     * @brief Copy the current pass consideration log.
     * @return Owned ordered key list, including repeated and failed/no-op attempts;
     * this is reachability evidence rather than a list of successful engine writes.
     */
    [[nodiscard]] QStringList writtenKeys() const { return written_; }

private:
    SWMM_Engine engine_ = nullptr;
    SWMMModelLayer *layer_ = nullptr;
    SWMMVisProjectWindow *pw_ = nullptr;
    EngineCapabilities caps_;
    QStringList written_;
};

} // namespace openswmmvis::ui

#endif // SIMOPTIONSCONTEXT_H
