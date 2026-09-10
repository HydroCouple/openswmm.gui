/*!
 * \file   simoptionspage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Base class for one sidebar page of the Simulation Options dialog.
 *
 * A page owns its widgets, its slice of the engine read, its slice of the
 * engine write, and its own intra-page gating. The dialog owns only the
 * registry, the sidebar and the cross-page gate table (HANDOFF §2.3).
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
    explicit SimOptionsPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /*! \brief Sidebar row text, already tr()'d. */
    [[nodiscard]] virtual QString title() const = 0;

    /*! \brief engine → widgets. Block signals while setting. */
    virtual void read() = 0;

    /*! \brief widgets → engine via ctx_.writeIfChanged; returns the sum. */
    virtual int write() = 0;

    /*! \brief Intra-page widget gates (PLAN §4.3 "widget" rows). */
    virtual void refreshGates() {}

    /*! \brief One-shot per-widget disable/tooltip from ctx_.caps(). */
    virtual void applyCapabilities() {}

    /*! \brief Validation run before Apply/OK; false pops \a warn and stops. */
    virtual bool validate(QString *warn) { Q_UNUSED(warn); return true; }

signals:
    /*! \brief Emit from any control that feeds a PageGate. */
    void gateInputsChanged();

protected:
    /*!
     * \brief Tag an option widget for the reachability test.
     *
     * Call for EVERY widget that reads or writes an option key:
     * `tagOption(m_minSlopeSpin, "MIN_SLOPE")`. Widgets that edit a
     * non-`[OPTIONS]` thing are tagged with the section name instead:
     * `tagOption(w, "[EVENTS]")`.
     *
     * Null-tolerant, and repeated tags accumulate into a comma-separated list
     * so one editor can own two keys (a QDateTimeEdit writes DATE and TIME).
     */
    static void tagOption(QWidget *w, const char *key);

    SimOptionsContext &ctx_;
};

/*!
 * \brief The `[2D_OPTIONS]` key carrying one `SWMM_TRANSPORT_CLASS_*`, or ""
 *        for an unknown class.
 *
 * Shared vocabulary rather than a page member: the 2D page owns the
 * TRANSPORT_* checkboxes, while the Models page's Domain × Species matrix
 * names the same keys in its cell tooltips (CLAUDE.md §5.1 — one model, two
 * views).
 */
const char *transport2DKey(int speciesClass);

} // namespace openswmmvis::ui

#endif // SIMOPTIONSPAGE_H
