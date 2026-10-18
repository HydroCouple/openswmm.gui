#pragma once

#include "ui/heatconfigurationundo.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QUndoStack;
class SWMMModelLayer;

namespace OpenSWMMVis {

/// Identity of the configuration an editor session restores: heat, climate
/// (with snow and shared radiation) and the given option keys. Undo refuses
/// when it differs from the state the command left.
QString configurationIdentity(SWMM_Engine engine, const QString& anchor,
                              const QStringList& optionKeys);

/// Pushes one ConfigurationUndo for the edits made since @p before, when the
/// identity changed. An empty @p beforeIdentity is computed by briefly
/// restoring @p before (exact; the CRS and mesh file are untouched).
/// @p failed reports a refused Undo/Redo; empty shows a warning box.
bool pushConfigurationUndo(QUndoStack* stack, SWMMModelLayer* layer, const QString& text,
                           ConfigSnapshot before, QString beforeIdentity,
                           const QString& anchor, const QStringList& optionKeys,
                           std::function<void(QString)> failed = {});

/// One Apply segment of an options dialog. Nested editors that push their own
/// Undo commands land in the segment's macro, so each Apply is one Undo entry
/// and Cancel reverts the edits accepted since the last Apply.
class ConfigurationSession {
public:
    ConfigurationSession(SWMMModelLayer* layer, QUndoStack* stack, QString text, QString anchor);
    ~ConfigurationSession();
    ConfigurationSession(const ConfigurationSession&) = delete;
    ConfigurationSession& operator=(const ConfigurationSession&) = delete;

    /// Call before opening a nested editor that may write.
    void openSegment();
    /// Pushes the dialog's own edits since @p before into the segment and
    /// closes it.
    void commit(ConfigSnapshot before, const QStringList& optionKeys);
    /// Undoes and discards an open segment. Returns whether it held edits.
    bool cancel();
    bool segmentOpen() const { return open_; }
    QUndoStack* stack() const { return stack_; }
    /// Closes an open segment without undoing it (the model is closing).
    void abandon();
    void setFailureHandler(std::function<void(QString)> failed) { failed_ = std::move(failed); }

private:
    QPointer<SWMMModelLayer> layer_;
    QPointer<QUndoStack> stack_;
    QString text_, anchor_;
    std::function<void(QString)> failed_;
    bool open_ = false;
};

} // namespace OpenSWMMVis
