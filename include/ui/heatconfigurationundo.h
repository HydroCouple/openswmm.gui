/**
 * @file      heatconfigurationundo.h
 * @brief     Undoable edits to heat, climate, snow, and component configuration.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <openswmm/engine/openswmm_heat.h>
#include <QPointer>
#include <QObject>
#include <QUndoCommand>
#include <QString>
#include <functional>
#include <memory>

namespace OpenSWMMVis
{

struct HeatSnapshotDeleter {
    /**
     * @brief Release a uniquely owned heat snapshot through the engine API.
     * @param[in] snapshot Snapshot handle passed to swmm_heat_snapshot_destroy().
     */
    void operator()(void *snapshot) const { swmm_heat_snapshot_destroy(snapshot); }
};
using HeatSnapshot = std::unique_ptr<void, HeatSnapshotDeleter>;

struct ConfigSnapshotDeleter {
    /**
     * @brief Release a uniquely owned configuration snapshot through the engine API.
     * @param[in] snapshot Snapshot handle passed to swmm_config_snapshot_destroy().
     */
    void operator()(void *snapshot) const { swmm_config_snapshot_destroy(snapshot); }
};
/// Options, climate, snow, heat and component registrations
/// (swmm_config_snapshot_create).
using ConfigSnapshot = std::unique_ptr<void, ConfigSnapshotDeleter>;

// Exact values and authoring flags are restored together. Refuse undo rather
// than overwriting edits made outside this command's history.
template <class Snapshot, int (*Restore)(SWMM_Engine, void *)>
class EngineSnapshotUndo : public QUndoCommand
{
public:
    /**
     * @brief Own exact before/after snapshots for an already-applied engine edit.
     * @param[in] text Undo label copied to QUndoCommand.
     * @param[in] owner Borrowed QObject guard; command restoration is refused once it is destroyed.
     * @param[in] engine Borrowed engine handle identifying the edited instance.
     * @param[in] before Prior snapshot ownership moved into the command.
     * @param[in] after Applied snapshot ownership moved into the command.
     * @param[in] beforeIdentity Signature expected before redo.
     * @param[in] afterIdentity Signature expected before undo.
     * @param[in] current Callback resolving the owner's current engine; absent/mismatched engines refuse restoration.
     * @param[in] identity Required signature callback for the given live engine.
     * @param[in] changed Optional callback after accepted restoration.
     * @param[in] failed Optional callback explaining refused restoration; can be empty.
     * @note First redo is intentionally a no-op because the initial edit has already been applied; callbacks execute synchronously on the undo caller's thread.
     */
    EngineSnapshotUndo(const QString &text, QObject *owner, SWMM_Engine engine, Snapshot before,
                       Snapshot after, QString beforeIdentity, QString afterIdentity,
                       std::function<SWMM_Engine()> current,
                       std::function<QString(SWMM_Engine)> identity, std::function<void()> changed,
                       std::function<void(QString)> failed)
        : owner_(owner), engine_(engine), before_(std::move(before)), after_(std::move(after)),
          beforeIdentity_(std::move(beforeIdentity)), afterIdentity_(std::move(afterIdentity)),
          current_(std::move(current)), identity_(std::move(identity)),
          changed_(std::move(changed)), failed_(std::move(failed))
    {
        setText(text);
    }

    /**
     * @brief Restore the prior snapshot only when owner/engine/current identity still match the command's applied state.
     * @note Failure marks the command obsolete and invokes failed(); success invokes changed().
     */
    void undo() override { restore(false); }
    /**
     * @brief Skip the initial QUndoStack push, otherwise restore the applied snapshot under the same conflict checks.
     */
    void redo() override
    {
        if (first_) {
            first_ = false;
            return;
        }
        restore(true);
    }

private:
    /**
     * @brief Check ownership, engine identity and configuration signature before snapshot restoration.
     * @param[in] after True restores after with beforeIdentity precondition; false restores before with afterIdentity precondition.
     * @note Any guard/Restore failure marks the command obsolete and reports failure. Missing identity callback is not independently checked; the constructor requires it.
     */
    void restore(bool after)
    {
        if (!owner_ || !current_ || current_() != engine_ ||
            identity_(engine_) != (after ? beforeIdentity_ : afterIdentity_) ||
            Restore(engine_, after ? after_.get() : before_.get()) != SWMM_OK) {
            setObsolete(true);
            if (failed_)
                failed_(
                    QObject::tr(
                        "%1: the settings changed outside Undo, or the simulation is initialized. The saved edit was not restored.")
                        .arg(text()));
            return;
        }
        if (changed_)
            changed_();
    }
    QPointer<QObject> owner_;
    SWMM_Engine engine_;
    Snapshot before_, after_;
    QString beforeIdentity_, afterIdentity_;
    std::function<SWMM_Engine()> current_;
    std::function<QString(SWMM_Engine)> identity_;
    std::function<void()> changed_;
    std::function<void(QString)> failed_;
    bool first_ = true;
};

class HeatConfigurationUndo final
    : public EngineSnapshotUndo<HeatSnapshot, swmm_heat_snapshot_restore>
{
public:
    /**
     * @brief Specialize exact engine snapshot undo for an already-applied heat edit with a translated default label.
     * @param[in] owner Borrowed QObject guard; command restoration is refused once it is destroyed.
     * @param[in] engine Borrowed engine handle identifying the edited instance.
     * @param[in] before Prior snapshot ownership moved into the command.
     * @param[in] after Applied snapshot ownership moved into the command.
     * @param[in] beforeIdentity Signature expected before redo.
     * @param[in] afterIdentity Signature expected before undo.
     * @param[in] current Callback resolving the owner's current engine; absent/mismatched engines refuse restoration.
     * @param[in] identity Required signature callback for the given live engine.
     * @param[in] changed Optional callback after accepted restoration.
     * @param[in] failed Optional callback explaining refused restoration; can be empty.
     */
    HeatConfigurationUndo(QObject *owner, SWMM_Engine engine, HeatSnapshot before,
                          HeatSnapshot after, QString beforeIdentity, QString afterIdentity,
                          std::function<SWMM_Engine()> current,
                          std::function<QString(SWMM_Engine)> identity,
                          std::function<void()> changed, std::function<void(QString)> failed)
        : EngineSnapshotUndo(QObject::tr("Edit heat configuration"), owner, engine,
                             std::move(before), std::move(after), std::move(beforeIdentity),
                             std::move(afterIdentity), std::move(current), std::move(identity),
                             std::move(changed), std::move(failed))
    {
    }
};

using ConfigurationUndo = EngineSnapshotUndo<ConfigSnapshot, swmm_config_snapshot_restore>;

} // namespace OpenSWMMVis
