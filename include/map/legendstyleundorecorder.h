/**
 * @file      legendstyleundorecorder.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Puts every change to a map legend's settings on the map's undo stack, whichever
 *            view made it (the legend's menu, the Legend Properties dialog, the Properties
 *            panel, the Legend dock, dragging or resizing the legend).
 *
 *         The recorder keeps the style's saved settings. When the style changes it waits until
 *         control returns to the event loop, so one action (for example a reset that calls many
 *         setters) becomes one undo step, then pushes a command holding the settings before and
 *         after. Quick edits of the same settings (a spin box held down, a color dragged) merge
 *         into one step.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_LEGENDSTYLEUNDORECORDER_H
#define OPENSWMMVIS_MAP_LEGENDSTYLEUNDORECORDER_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUndoCommand>

class QUndoStack;

namespace OpenSWMM::Render
{
class LegendOverlayStyle;
}

namespace openswmmvis::map
{

/*!
 * \class LegendStyleUndoRecorder
 * \brief Records a legend style's changes as undo steps on an undo stack.
 *
 *        Created as a child of the style, so it lives as long as the style; find it with
 *        forStyle().
 */
class LegendStyleUndoRecorder : public QObject
{
    Q_OBJECT
public:
    /** @brief Start recording @p style's changes on @p stack.
     * @param style Style to watch; the recorder becomes its child.
     * @param stack Undo stack; borrowed, may be null (then nothing is recorded). */
    LegendStyleUndoRecorder(OpenSWMM::Render::LegendOverlayStyle *style, QUndoStack *stack);
    /** @brief Stop recording. */
    ~LegendStyleUndoRecorder() override;

    /** @brief The recorder of @p style, or null. @param style Style. */
    [[nodiscard]] static LegendStyleUndoRecorder *
    forStyle(const OpenSWMM::Render::LegendOverlayStyle *style);

    /** @brief Take the style's current settings as the starting point without recording a step
     *  (after loading a project, for example). A change still waiting to be recorded is
     *  dropped. */
    void resync();

    /** @brief The undo stack steps go on. */
    [[nodiscard]] QUndoStack *stack() const { return m_stack; }

    /** @brief Whether an undo or redo is being applied (its changes are not recorded). */
    [[nodiscard]] bool isApplying() const { return m_applying; }

    /** @brief Apply @p settings to the style without recording a step (used by undo and redo).
     * @param settings Saved settings. */
    void apply(const QJsonObject &settings);

    /** @brief Undo steps of the same settings within this many milliseconds merge. */
    static constexpr int kMergeMs = 800;

private:
    void onChanged();
    void record();

    QPointer<OpenSWMM::Render::LegendOverlayStyle> m_style;
    QPointer<QUndoStack> m_stack;
    QJsonObject m_settings; ///< settings the next step starts from
    bool m_pending = false;
    bool m_applying = false;
};

/*!
 * \class LegendStyleSnapshotCommand
 * \brief One undo step of a legend style: its settings before and after.
 *
 *        The change is already applied when the command is pushed, so its first redo does
 *        nothing.
 */
class LegendStyleSnapshotCommand : public QUndoCommand
{
public:
    /** @brief Make a step.
     * @param recorder Recorder of the style; borrowed.
     * @param before Settings before the change. @param after Settings after it. */
    LegendStyleSnapshotCommand(LegendStyleUndoRecorder *recorder, QJsonObject before,
                               QJsonObject after);

    /** @brief Put back the settings before the change. */
    void undo() override;
    /** @brief Apply the settings after the change (not on the first call: already applied). */
    void redo() override;
    /** @brief Command id shared by legend steps (for merging). */
    int id() const override { return 0x4c47; }
    /** @brief Merge a quick following edit of the same settings into this step.
     * @param other The newer command. @return Whether it was merged. */
    bool mergeWith(const QUndoCommand *other) override;

    /** @brief Names of the settings that differ between before and after. */
    [[nodiscard]] const QSet<QString> &changedKeys() const { return m_keys; }

private:
    QPointer<LegendStyleUndoRecorder> m_recorder;
    QJsonObject m_before;
    QJsonObject m_after;
    QSet<QString> m_keys;
    QElapsedTimer m_age;
    bool m_firstRedo = true;
};

} // namespace openswmmvis::map

#endif // OPENSWMMVIS_MAP_LEGENDSTYLEUNDORECORDER_H
