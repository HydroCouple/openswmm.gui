/**
 * @file      simoptionspage.h
 * @brief     Base class for one sidebar page of the Simulation Options dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Base class for one sidebar page of the Simulation Options dialog.
 *
 * A page owns its widgets, its slice of the engine read, its slice of the
 * engine write, and its own intra-page gating. The dialog owns only the
 * registry, the sidebar and the cross-page gate table (HANDOFF §2.3).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SIMOPTIONSPAGE_H
#define SIMOPTIONSPAGE_H

#include <QString>
#include <QWidget>

#include "ui/dialogs/simoptions/simoptionscontext.h"

namespace openswmmvis::ui
{

class SimOptionsPage : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Create a page bound to a shared simulation-options context.
     * @param ctx Borrowed context that must outlive the page; no ownership transfer.
     * @param parent Optional widget owner; null leaves the page caller-owned.
     * Use the page/context on their owning GUI thread and invalidate backing handles
     * through the context before closing the model.
     */
    explicit SimOptionsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the derived page sidebar caption.
     * @return Owned already-localized title string; pure virtual, with no base fallback.
     */
    [[nodiscard]] virtual QString title() const = 0;

    /**
     * @brief Hydrate derived page controls from engine/context state.
     * Derived implementations provide fallback and signal-blocking behavior; callers
     * invoke this when binding/reloading rather than to Apply a draft.
     */
    virtual void read() = 0;

    /**
     * @brief Apply the derived page draft and report its successful write count.
     * @return Derived page write total. Common options use context change detection;
     * section/extended-option editors may use specialized APIs while recording keys.
     * The base interface provides no transaction rollback or detailed error result.
     */
    virtual int write() = 0;

    /**
     * @brief Recompute derived intra-page enablement/visibility from the draft state.
     * The base implementation is a no-op; does not itself apply options.
     */
    virtual void refreshGates() {}

    /**
     * @brief Apply derived per-widget capability restrictions/tooltips from the cached context.
     * The base implementation is a no-op and does not re-probe the engine.
     */
    virtual void applyCapabilities() {}

    /**
     * @brief Check whether this page draft may participate in Apply/OK.
     * @param warn Optional borrowed diagnostic output for a derived refusal. The base
     * implementation ignores it and leaves existing text unchanged.
     * @return True in the base implementation; derived false stops the dialog Apply
     * pass with its diagnostic. No base engine write or UI prompt occurs here.
     */
    virtual bool validate(QString *warn)
    {
        Q_UNUSED(warn);
        return true;
    }

signals:
    /**
     * @brief Notify the host dialog that a control feeding cross-page gating changed.
     * Derived pages emit this signal; no engine mutation or successful Apply is implied.
     */
    void gateInputsChanged();

protected:
    /**
     * @brief Accumulate option ownership metadata on an editor widget.
     * @param w Borrowed widget to modify, permitted null for unavailable controls.
     * @param key Borrowed non-null Latin-1 option/section token, normally nonempty and
     * without commas. Copied only when w is non-null.
     * Adds to the comma-separated optionKey dynamic property without exact duplicate
     * tokens. Does not normalize case, read/write an engine option or change ownership.
     */
    static void tagOption(QWidget *w, const char *key);

    SimOptionsContext &ctx_;
};

/**
 * @brief Map a transport class enum to its extended 2D option key.
 * @param speciesClass SWMM_TRANSPORT_CLASS_* category, not a chemical species index.
 * @return Borrowed static literal for pollutants/MSX/age/temperature, or an empty
 * literal for an unknown class. Never free the pointer; no engine query or build
 * capability validation is performed.
 */
const char *transport2DKey(int speciesClass);

} // namespace openswmmvis::ui

#endif // SIMOPTIONSPAGE_H
