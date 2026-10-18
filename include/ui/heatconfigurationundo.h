#pragma once

#include <openswmm/engine/openswmm_heat.h>
#include <QPointer>
#include <QObject>
#include <QUndoCommand>
#include <QString>
#include <functional>
#include <memory>

namespace OpenSWMMVis {

struct HeatSnapshotDeleter {
    void operator()(void* snapshot) const { swmm_heat_snapshot_destroy(snapshot); }
};
using HeatSnapshot = std::unique_ptr<void, HeatSnapshotDeleter>;

struct ConfigSnapshotDeleter {
    void operator()(void* snapshot) const { swmm_config_snapshot_destroy(snapshot); }
};
/// Options, climate, snow, heat and component registrations
/// (swmm_config_snapshot_create).
using ConfigSnapshot = std::unique_ptr<void, ConfigSnapshotDeleter>;

// Exact values and authoring flags are restored together. Refuse undo rather
// than overwriting edits made outside this command's history.
template <class Snapshot, int (*Restore)(SWMM_Engine, void*)>
class EngineSnapshotUndo : public QUndoCommand {
public:
    EngineSnapshotUndo(const QString& text, QObject* owner, SWMM_Engine engine,
        Snapshot before, Snapshot after, QString beforeIdentity,
        QString afterIdentity, std::function<SWMM_Engine()> current,
        std::function<QString(SWMM_Engine)> identity,
        std::function<void()> changed, std::function<void(QString)> failed)
        : owner_(owner), engine_(engine), before_(std::move(before)), after_(std::move(after)),
          beforeIdentity_(std::move(beforeIdentity)), afterIdentity_(std::move(afterIdentity)),
          current_(std::move(current)), identity_(std::move(identity)),
          changed_(std::move(changed)), failed_(std::move(failed))
    { setText(text); }

    void undo() override { restore(false); }
    void redo() override { if (first_) { first_=false; return; } restore(true); }

private:
    void restore(bool after) {
        if (!owner_ || !current_ || current_()!=engine_
            || identity_(engine_)!=(after?beforeIdentity_:afterIdentity_)
            || Restore(engine_,after?after_.get():before_.get())!=SWMM_OK) {
            setObsolete(true);
            if(failed_)failed_(QObject::tr("%1: the settings changed outside Undo, or the simulation is initialized. The saved edit was not restored.").arg(text()));
            return;
        }
        if(changed_)changed_();
    }
    QPointer<QObject> owner_;
    SWMM_Engine engine_;
    Snapshot before_, after_;
    QString beforeIdentity_, afterIdentity_;
    std::function<SWMM_Engine()> current_;
    std::function<QString(SWMM_Engine)> identity_;
    std::function<void()> changed_;
    std::function<void(QString)> failed_;
    bool first_=true;
};

class HeatConfigurationUndo final : public EngineSnapshotUndo<HeatSnapshot, swmm_heat_snapshot_restore> {
public:
    HeatConfigurationUndo(QObject* owner, SWMM_Engine engine,
        HeatSnapshot before, HeatSnapshot after, QString beforeIdentity,
        QString afterIdentity, std::function<SWMM_Engine()> current,
        std::function<QString(SWMM_Engine)> identity,
        std::function<void()> changed, std::function<void(QString)> failed)
        : EngineSnapshotUndo(QObject::tr("Edit heat configuration"), owner, engine, std::move(before),
              std::move(after), std::move(beforeIdentity), std::move(afterIdentity), std::move(current),
              std::move(identity), std::move(changed), std::move(failed)) {}
};

using ConfigurationUndo = EngineSnapshotUndo<ConfigSnapshot, swmm_config_snapshot_restore>;

} // namespace OpenSWMMVis
