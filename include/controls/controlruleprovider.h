/**
 * @file      controlruleprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BR Phase 6.8.1 — MVC model for a single SWMM control rule.
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
 * Validation starts Pending and is replaced by synchronous or debounced
 * RuleValidator results. Changed body text invalidates the cached verdict.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CONTROLS_CONTROLRULEPROVIDER_H
#define OPENSWMMVIS_CONTROLS_CONTROLRULEPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::controls
{

/*! \brief Validator verdict for a rule body; initially Pending. */
enum class ValidationState {
    Pending = 0, ///< Not yet validated (or sandbox engine missing).
    Valid = 1,   ///< Engine parser accepted the body.
    Invalid = 2  ///< Engine parser rejected; see `lastError()`.
};

class ControlRuleProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create a local rule identity/body pair with pending validation.
     * @param[in] name Identifier copied verbatim, without consistency or uniqueness checks.
     * @param[in] body Full rule text, normally including its RULE header.
     * @param[in] parent Optional QObject owner.
     * @details Use from the QObject owning thread; layer helpers coordinate engine writes.
     */
    ControlRuleProvider(QString name, QString body, QObject *parent = nullptr);
    /**
     * @brief Destroy the local rule and cached validation data.
     */
    ~ControlRuleProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    /**
     * @brief Read the local rule lookup identifier.
     * @return Copied name; it may differ from the body header until layer synchronization.
     */
    QString name() const noexcept { return m_name; }
    /**
     * @brief Read the complete local rule text.
     * @return Copied body without engine parsing or validation.
     */
    QString body() const noexcept { return m_body; }

    // ── Validation cache (set by the validator; read by the list model) ─────

    /**
     * @brief Read the latest cached validator state.
     * @return Pending initially or after a body replacement, otherwise the explicitly cached verdict.
     */
    ValidationState validationState() const noexcept { return m_validation; }
    /**
     * @brief Read the cached validation diagnostic.
     * @return Copied message, cleared on a changed body and otherwise supplied by setValidation().
     */
    QString lastError() const noexcept { return m_lastError; }
    /**
     * @brief Read the cached parser location.
     * @return One-based body line or -1 when unavailable; values are stored verbatim.
     */
    int lastErrorLine() const noexcept { return m_lastErrorLine; }

    // ── Mutators (registry-internal — UI code must go through the layer) ────

    /*! \brief Rename the rule. Uniqueness is the registry's responsibility;
     *  this just stores + notifies. Body is NOT rewritten here — the
     *  layer rewrites the `RULE <name>` header and calls `setBody`
     *  to keep the two in sync.
     * @param[in] newName Identifier copied verbatim; identical text is a no-op.
     * @details Emits nameChanged() only after changing text; does not validate the body or update the engine.
     */
    void setName(QString newName);

    /*! \brief Replace the full rule body (including the `RULE <name>`
     *  header). Resets validation to Pending so the validator re-runs.
     * @param[in] newBody Complete replacement text; identical text is a no-op.
     * @details On change clears error/error-line, sets Pending, then emits bodyChanged() followed by validationChanged(). Does not update name or engine.
     */
    void setBody(QString newBody);

    /*! \brief Cache a validator verdict. Called by `RuleValidator` once
     *  Phase 6.8.2 is wired; the list model picks the icon up via
     *  the `validationChanged` signal.
     * @param[in] state Verdict to cache.
     * @param[in] errorMessage Associated diagnostic, normally empty for Valid/Pending.
     * @param[in] errorLine One-based body line, or -1 when unavailable.
     * @details Stores all three verbatim and emits validationChanged() if any differs; no engine parsing occurs here.
     */
    void setValidation(ValidationState state, QString errorMessage = {}, int errorLine = -1);

signals:
    /*! \brief Rule was renamed.
     * @param[in] prev Previous stored name.
     * @param[in] now Current stored name.
     */
    void nameChanged(QString prev, QString now);

    /*! \brief Body was replaced. */
    void bodyChanged();

    /*! \brief Cached validation result changed. */
    void validationChanged();

private:
    QString m_name;
    QString m_body;
    ValidationState m_validation = ValidationState::Pending;
    QString m_lastError;
    int m_lastErrorLine = -1;
};

} // namespace openswmmvis::controls

#endif // OPENSWMMVIS_CONTROLS_CONTROLRULEPROVIDER_H
