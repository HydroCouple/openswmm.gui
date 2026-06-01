/*!
 * \file   rulevalidator.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "controls/rulevalidator.h"

#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_controls.h>

namespace openswmmvis::controls {

RuleValidator::RuleValidator(SWMMModelLayer *layer, QObject *parent)
    : QObject(parent),
      m_layer(layer)
{
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, [this]() {
        if (!m_pendingProvider) return;
        const Result r = validateImpl_(m_pendingText);
        m_pendingProvider->setValidation(r.state, r.message, r.errorLine);
        m_pendingProvider.clear();
        m_pendingText.clear();
    });
}

RuleValidator::~RuleValidator() = default;

RuleValidator::Result RuleValidator::validate(const QString &text) const
{
    return validateImpl_(text);
}

RuleValidator::Result RuleValidator::validateImpl_(const QString &text) const
{
    Result r;
    if (text.trimmed().isEmpty()) {
        // An empty body is "Pending" rather than "Invalid" — the user
        // hasn't typed anything yet and we don't want to flash a red
        // exclamation on a brand-new rule. Phase 6.8.2 dialog displays
        // the empty banner as a grey pending sentinel.
        r.state = ValidationState::Pending;
        return r;
    }

    SWMM_Engine eng = m_layer ? m_layer->engine() : nullptr;
    if (!eng) {
        // No engine = cannot run the production parser. Surface as
        // Pending so the user isn't punished with a red icon for a
        // closed project.
        r.state = ValidationState::Pending;
        return r;
    }

    char errbuf[256] = {};
    int  line = -1;
    const QByteArray utf8 = text.toUtf8();
    const int rc = swmm_control_validate_rule(
        eng, utf8.constData(), errbuf, sizeof(errbuf), &line);
    if (rc == SWMM_OK) {
        r.state = ValidationState::Valid;
        return r;
    }
    r.state     = ValidationState::Invalid;
    r.message   = QString::fromUtf8(errbuf);
    r.errorLine = line;
    return r;
}

void RuleValidator::validateDebounced(ControlRuleProvider *provider,
                                        const QString &text)
{
    if (!provider) return;
    m_pendingProvider = provider;
    m_pendingText     = text;
    // Provider state is Pending while the timer ticks — visible to the
    // list view's icon via `validationChanged`.
    provider->setValidation(ValidationState::Pending);
    m_debounceTimer->start(m_debounceMs);
}

} // namespace openswmmvis::controls
