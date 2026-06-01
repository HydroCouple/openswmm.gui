/*!
 * \file   controlruleprovider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "controls/controlruleprovider.h"

namespace openswmmvis::controls {

ControlRuleProvider::ControlRuleProvider(QString name, QString body, QObject *parent)
    : QObject(parent),
      m_name(std::move(name)),
      m_body(std::move(body))
{
}

ControlRuleProvider::~ControlRuleProvider() = default;

void ControlRuleProvider::setName(QString newName)
{
    if (newName == m_name) return;
    QString prev = m_name;
    m_name = std::move(newName);
    emit nameChanged(prev, m_name);
}

void ControlRuleProvider::setBody(QString newBody)
{
    if (newBody == m_body) return;
    m_body = std::move(newBody);
    // A body change invalidates the cached verdict — the validator will
    // re-run on the next debounce tick (Phase 6.8.2). Until then the
    // list-model icon falls back to the pending sentinel.
    m_validation    = ValidationState::Pending;
    m_lastError.clear();
    m_lastErrorLine = -1;
    emit bodyChanged();
    emit validationChanged();
}

void ControlRuleProvider::setValidation(ValidationState state,
                                          QString errorMessage,
                                          int errorLine)
{
    if (state == m_validation && errorMessage == m_lastError && errorLine == m_lastErrorLine)
        return;
    m_validation    = state;
    m_lastError     = std::move(errorMessage);
    m_lastErrorLine = errorLine;
    emit validationChanged();
}

} // namespace openswmmvis::controls
