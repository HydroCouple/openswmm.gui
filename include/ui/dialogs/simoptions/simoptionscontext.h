/*!
 * \file   simoptionscontext.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The one thing every Simulation Options page is handed.
 *
 * Owns engine access (read one key, write one key if it actually changed) and
 * the per-pass record of which keys a write pass considered. Pages never touch
 * `swmm_options_*` directly, so the numeric-aware compare that keeps Apply
 * idempotent lives in exactly one place (HANDOFF I6, §2.2).
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
    SimOptionsContext(SWMM_Engine e, SWMMModelLayer *layer,
                      SWMMVisProjectWindow *pw, EngineCapabilities caps);

    [[nodiscard]] SWMM_Engine           engine()        const { return engine_; }
    [[nodiscard]] SWMMModelLayer       *modelLayer()    const { return layer_; }
    [[nodiscard]] SWMMVisProjectWindow *projectWindow() const { return pw_; }
    [[nodiscard]] const EngineCapabilities &caps()      const { return caps_; }

    /*!
     * \brief `swmm_options_get`, trimmed; \a fallback when the engine has no
     *        value or there is no engine.
     */
    [[nodiscard]] QString option(const char *key, const QString &fallback = {}) const;

    /*!
     * \brief Write \a newVal to \a key only if it differs numerically.
     *
     * The compare is numeric-aware because the engine renders numerics with
     * six decimals while the dialog formats to 2–4, so a plain string compare
     * would rewrite every key on each Apply and dirty the project.
     *
     * \return 1 if a write happened, else 0.
     * \note \a key is recorded in writtenKeys() whether or not it was written
     *       — that is the reachability seam the gates test asserts against.
     */
    int writeIfChanged(const char *key, const QString &newVal);

    void beginWritePass();                              ///< Clears writtenKeys().
    [[nodiscard]] QStringList writtenKeys() const { return written_; }

private:
    SWMM_Engine           engine_ = nullptr;
    SWMMModelLayer       *layer_  = nullptr;
    SWMMVisProjectWindow *pw_     = nullptr;
    EngineCapabilities    caps_;
    QStringList           written_;
};

} // namespace openswmmvis::ui

#endif // SIMOPTIONSCONTEXT_H
