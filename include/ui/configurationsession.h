/**
 * @file      configurationsession.h
 * @brief     Restoration of editor-owned model configuration sessions.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include "ui/heatconfigurationundo.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QUndoStack;
class SWMMModelLayer;

namespace OpenSWMMVis
{

/// Identity of the configuration an editor session restores: heat, climate
/// (with snow and shared radiation) and the given option keys. Undo refuses
/// when it differs from the state the command left.
/**
 * @param[in] engine Borrowed engine; null returns empty.
 * @param[in] anchor Climatology path anchor used by configurationSignature().
 * @param[in] optionKeys Ordered generic/extension option keys, or GW_TRANSPORT: prefixed dedicated keys.
 * @return Heat plus climate signature and ordered key/value tokens separated by record separators; failed option reads contribute an empty value. No cryptographic hashing is performed here.
 */
QString configurationIdentity(SWMM_Engine engine, const QString &anchor,
                              const QStringList &optionKeys);

/// Pushes one ConfigurationUndo for the edits made since @p before, when the
/// identity changed. An empty @p beforeIdentity is computed by briefly
/// restoring @p before (exact; the CRS and mesh file are untouched).
/// @p failed reports a refused Undo/Redo; empty shows a warning box.
/**
 * @param[in,out] stack Required undo destination; null returns false.
 * @param[in] layer Borrowed model layer with a live engine, guarded by QPointer in retained callbacks.
 * @param[in] text Command label.
 * @param[in] before Snapshot ownership moved into this call and, on success, into the command.
 * @param[in] beforeIdentity Prior signature; empty requests a temporary engine snapshot restore to compute it.
 * @param[in] anchor Climatology signature path anchor retained by the command.
 * @param[in] optionKeys Ordered identity keys and future optionsChanged() payload; empty uses CLIMATE/HEAT_TRANSPORT notification tokens.
 * @param[in] failed Optional refusal callback; absent opens a warning box.
 * @return True only when a changed identity command is pushed. False also represents unchanged data, invalid prerequisites, snapshot failure or refused temporary restoration.
 * @note Edits must already be applied: the command's first redo is skipped. A failed restore of the after snapshot while discovering beforeIdentity can leave the engine at the before snapshot; this helper provides no further recovery then.
 */
bool pushConfigurationUndo(QUndoStack *stack, SWMMModelLayer *layer, const QString &text,
                           ConfigSnapshot before, QString beforeIdentity, const QString &anchor,
                           const QStringList &optionKeys, std::function<void(QString)> failed = {});

/// One Apply segment of an options dialog. Nested editors that push their own
/// Undo commands land in the segment's macro, so each Apply is one Undo entry
/// and Cancel reverts the edits accepted since the last Apply.
class ConfigurationSession
{
public:
    /**
     * @brief Track one options-dialog Apply segment and optional nested-editor undo macro.
     * @param[in] layer Borrowed layer guarded by QPointer.
     * @param[in] stack Borrowed stack guarded by QPointer; null disables macro/history handling.
     * @param[in] text Label moved into session/macro/command use.
     * @param[in] anchor Signature path anchor moved into the session.
     * @note No segment opens until openSegment(); engine access and undo calls must be serialized on the GUI thread.
     */
    ConfigurationSession(SWMMModelLayer *layer, QUndoStack *stack, QString text, QString anchor);
    /**
     * @brief Cancel any open segment, attempting its undo and discarding redo history.
     */
    ~ConfigurationSession();
    /**
     * @brief Disable copying to keep macro closure/cancellation responsibility unique.
     */
    ConfigurationSession(const ConfigurationSession &) = delete;
    /**
     * @brief Disable copy assignment so two sessions cannot manage the same open segment.
     */
    ConfigurationSession &operator=(const ConfigurationSession &) = delete;

    /// Call before opening a nested editor that may write.
    /**
     * @note No-op when already open or the stack has disappeared. Begins a stack macro but does not capture engine state itself.
     */
    void openSegment();
    /// Pushes the dialog's own edits since @p before into the segment and
    /// closes it.
    /**
     * @param[in] before Snapshot moved into pushConfigurationUndo() when stack/layer remain available.
     * @param[in] optionKeys Identity and options-change keys for the dialog's own edit.
     * @note Ignores pushConfigurationUndo()'s return, closes an open macro and removes an empty macro by undo/discard. Can push a standalone command even when no macro was opened.
     */
    void commit(ConfigSnapshot before, const QStringList &optionKeys);
    /// Undoes and discards an open segment. Returns whether it held edits.
    /**
     * @return True if an open completed macro contained children, false otherwise. This reports recorded edits, not guaranteed successful engine restoration.
     * @note Ends an open macro, calls undo(), then discards redo entries. External configuration/lifecycle conflicts may refuse restoration through the installed failure handler.
     */
    bool cancel();
    /**
     * @brief Inspect whether this session believes it has an open macro.
     * @return Stored flag; a destroyed stack does not itself clear it.
     */
    bool segmentOpen() const { return open_; }
    /**
     * @brief Access the currently live undo destination.
     * @return Borrowed pointer or null if absent/destroyed.
     */
    QUndoStack *stack() const { return stack_; }
    /// Closes an open segment without undoing it (the model is closing).
    /**
     * @note Closes the macro and removes an empty entry, preserving nonempty applied changes. Use when the model closes; does not attempt engine restoration.
     */
    void abandon();
    /**
     * @brief Replace the refusal callback used by subsequently committed snapshot commands.
     * @param[in] failed Callback moved into this session; empty selects pushConfigurationUndo()'s warning-box fallback. Existing commands retain their prior callback copies.
     */
    void setFailureHandler(std::function<void(QString)> failed) { failed_ = std::move(failed); }

private:
    QPointer<SWMMModelLayer> layer_;
    QPointer<QUndoStack> stack_;
    QString text_, anchor_;
    std::function<void(QString)> failed_;
    bool open_ = false;
};

} // namespace OpenSWMMVis
