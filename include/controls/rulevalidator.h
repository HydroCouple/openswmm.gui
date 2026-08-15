/*!
 * \file   rulevalidator.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BR Phase 6.8.2 — engine-backed control-rule validator.
 *
 * Wraps engine gap BR-02 (`swmm_control_validate_rule`) — the production
 * parser runs against the live engine's name tables (NODE / LINK / CURVE
 * / TIMESERIES refs) without mutating engine state. Validator calls are
 * debounced 250 ms per editor on the GUI thread (the sandbox call is
 * microseconds for a typical 2-KB rule body).
 *
 * Result is a small POD that the dialog / list-model pick up via
 * `ControlRuleProvider::setValidation`.
 */
#ifndef OPENSWMMVIS_CONTROLS_RULEVALIDATOR_H
#define OPENSWMMVIS_CONTROLS_RULEVALIDATOR_H

#include "controls/controlruleprovider.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class SWMMModelLayer;

namespace openswmmvis::controls {

class RuleValidator : public QObject
{
    Q_OBJECT

public:
    struct Result {
        ValidationState state      = ValidationState::Pending;
        QString         message;            ///< Empty when Valid.
        int             errorLine  = -1;    ///< 1-based; -1 when unavailable.
    };

    /*! \brief Construct over a layer. The validator pulls the engine
     *  handle lazily on each `validate` so a project-switch surfaces
     *  the new engine without explicit re-binding. */
    explicit RuleValidator(SWMMModelLayer *layer, QObject *parent = nullptr);
    ~RuleValidator() override;

    /*! \brief Synchronous validation. Returns immediately. Used by tests
     *  and the list-model's initial population pass. */
    Result validate(const QString &text) const;

    /*! \brief Debounced validation. Schedules a `validate` call after
     *  `debounceMs()` of edit quiescence, then emits
     *  `provider->setValidation(...)` on completion. Multiple calls
     *  within the debounce window coalesce into one. */
    void validateDebounced(ControlRuleProvider *provider, const QString &text);

    int debounceMs() const noexcept { return m_debounceMs; }
    void setDebounceMs(int ms) noexcept { m_debounceMs = ms; }

private:
    Result validateImpl_(const QString &text) const;

    QPointer<SWMMModelLayer>            m_layer;
    QPointer<ControlRuleProvider>       m_pendingProvider;
    QString                              m_pendingText;
    QTimer                              *m_debounceTimer = nullptr;
    int                                  m_debounceMs    = 250;
};

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_RULEVALIDATOR_H
