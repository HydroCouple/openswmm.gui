/*!
 * \file   controlruleprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BR Phase 6.8.1 — MVC model for a single SWMM control rule.
 *
 * One `ControlRuleProvider` per `[CONTROLS]` `RULE` block in the project.
 * Owned by a project-scoped `ControlRuleRegistry`. Acts as the **single
 * source of truth** for that rule: the editor dialog's list-pane row,
 * the code-editor body, the validator badge, the Object Browser leaf,
 * and the `SWMMControlRulePropertyAdapter` property panel all subscribe
 * to the provider's Qt signals (per [[feedback_mvc_synchronized_uis]]).
 *
 * Storage. The provider carries the rule's `name` (parsed from the
 * `RULE <name>` header, mirroring `swmm_control_get_id`), the full
 * rule `body` text including that header, and a cached
 * `ValidationState` (Pending / Valid / Invalid). All mutations route
 * through `ControlRuleRegistry` (so the registry's name index stays
 * coherent) and ultimately through `SWMMModelLayer::applyControlRule*`
 * (so the engine round-trips and `controlRulesChanged(name)` fires).
 *
 * This file ships in Slice BR Phase 6.8.1 (MVC backbone). The
 * code-editor dialog, syntax highlighter, completer, and validator
 * land in Phase 6.8.2 — until then `m_validation` stays Pending and
 * the list-view icon defaults to the pending sentinel.
 */
#ifndef OPENSWMMVIS_CONTROLS_CONTROLRULEPROVIDER_H
#define OPENSWMMVIS_CONTROLS_CONTROLRULEPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::controls {

/*! \brief Validator verdict for a rule body. Default `Pending` until the
 *  validator (Phase 6.8.2) has been wired. */
enum class ValidationState {
    Pending = 0,   ///< Not yet validated (or sandbox engine missing).
    Valid   = 1,   ///< Engine parser accepted the body.
    Invalid = 2    ///< Engine parser rejected; see `lastError()`.
};

class ControlRuleProvider : public QObject
{
    Q_OBJECT

public:
    ControlRuleProvider(QString name, QString body, QObject *parent = nullptr);
    ~ControlRuleProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    QString name() const noexcept { return m_name; }
    QString body() const noexcept { return m_body; }

    // ── Validation cache (set by the validator; read by the list model) ─────

    ValidationState validationState() const noexcept { return m_validation; }
    QString         lastError()       const noexcept { return m_lastError; }
    int             lastErrorLine()   const noexcept { return m_lastErrorLine; }

    // ── Mutators (registry-internal — UI code must go through the layer) ────

    /*! \brief Rename the rule. Uniqueness is the registry's responsibility;
     *  this just stores + notifies. Body is NOT rewritten here — the
     *  registry rewrites the `RULE <name>` header and calls `setBody`
     *  to keep the two in sync. */
    void setName(QString newName);

    /*! \brief Replace the full rule body (including the `RULE <name>`
     *  header). Resets validation to Pending so the validator re-runs. */
    void setBody(QString newBody);

    /*! \brief Cache a validator verdict. Called by `RuleValidator` once
     *  Phase 6.8.2 is wired; the list model picks the icon up via
     *  the `validationChanged` signal. */
    void setValidation(ValidationState state,
                        QString errorMessage = {},
                        int errorLine = -1);

signals:
    /*! \brief Rule was renamed. */
    void nameChanged(QString prev, QString now);

    /*! \brief Body was replaced. */
    void bodyChanged();

    /*! \brief Cached validation result changed. */
    void validationChanged();

private:
    QString         m_name;
    QString         m_body;
    ValidationState m_validation     = ValidationState::Pending;
    QString         m_lastError;
    int             m_lastErrorLine  = -1;
};

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_CONTROLRULEPROVIDER_H
