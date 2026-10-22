/**
 * @file      rulevalidator.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BR Phase 6.8.2 — engine-backed control-rule validator.
 *
 * Wraps engine gap BR-02 (`swmm_control_validate_rule`) — the production
 * parser runs against the live engine's name tables (NODE / LINK / CURVE
 * / TIMESERIES refs) without mutating engine state. Validator calls are
 * debounced 250 ms per editor on the GUI thread (the sandbox call is
 * microseconds for a typical 2-KB rule body).
 *
 * Result is a small POD that the dialog / list-model pick up via
 * `ControlRuleProvider::setValidation`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CONTROLS_RULEVALIDATOR_H
#define OPENSWMMVIS_CONTROLS_RULEVALIDATOR_H

#include "controls/controlruleprovider.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class SWMMModelLayer;

namespace openswmmvis::controls
{

class RuleValidator : public QObject
{
    Q_OBJECT

public:
    struct Result {
        ValidationState state = ValidationState::Pending;
        QString message;    ///< Empty when Valid.
        int errorLine = -1; ///< 1-based; -1 when unavailable.
    };

    /*! \brief Construct over a layer. The validator pulls the engine
     *  handle lazily on each `validate` so a project-switch surfaces
     *  the new engine without explicit re-binding.
     * @param[in] layer Borrowed layer guarded by QPointer; destruction or missing engine yields Pending.
     * @param[in] parent Optional QObject owner; owns the debounce timer.
     * @details Use from the GUI/engine-owning thread. The object has one shared pending validation slot, rather than a timer per provider.
     */
    explicit RuleValidator(SWMMModelLayer *layer, QObject *parent = nullptr);
    /**
     * @brief Destroy the debounce timer and discard pending work; never destroys the layer/provider.
     */
    ~RuleValidator() override;

    /*! \brief Synchronous validation. Returns immediately. Used by tests
     *  and the list-model's initial population pass.
     * @param[in] text Full rule body encoded as UTF-8 for the production parser.
     * @return Pending for blank text or no live engine, Valid on parser success, otherwise Invalid with a diagnostic and one-based line when available.
     * @details Synchronous and does not mutate provider or engine rule lists.
     */
    Result validate(const QString &text) const;

    /*! \brief Debounced validation. Schedules a `validate` call after
     *  `debounceMs()` of edit quiescence, then emits
     *  `provider->setValidation(...)` on completion. Multiple calls
     *  within the debounce window coalesce into one.
     * @param[in] provider Borrowed provider guarded by QPointer; null leaves previous pending work unchanged.
     * @param[in] text Copied text to validate, independent of later provider body edits.
     * @details Replaces the single pending provider/text, sets that provider Pending immediately, and restarts the timer. If the provider is destroyed before timeout, no verdict is delivered.
     */
    void validateDebounced(ControlRuleProvider *provider, const QString &text);

    /**
     * @brief Read the configured quiet-period interval.
     * @return Stored timer interval in milliseconds, initially 250.
     */
    int debounceMs() const noexcept { return m_debounceMs; }
    /**
     * @brief Set the interval for future debounce requests.
     * @param[in] ms Timer interval in milliseconds; pass a nonnegative value suitable for QTimer.
     * @details Stores verbatim without changing a currently running timer.
     */
    void setDebounceMs(int ms) noexcept { m_debounceMs = ms; }

private:
    /**
     * @brief Parse against the layer current engine name tables without changing installed rules.
     * @param[in] text Full rule body; blank text short-circuits to Pending.
     * @return Cached-value result with parser error text capped by the 256-byte diagnostic buffer; no provider notification occurs here.
     */
    Result validateImpl_(const QString &text) const;

    QPointer<SWMMModelLayer> m_layer;
    QPointer<ControlRuleProvider> m_pendingProvider;
    QString m_pendingText;
    QTimer *m_debounceTimer = nullptr;
    int m_debounceMs = 250;
};

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_RULEVALIDATOR_H
