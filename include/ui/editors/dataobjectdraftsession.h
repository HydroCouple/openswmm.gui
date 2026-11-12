/**
 * @file      dataobjectdraftsession.h
 * @brief     Explicit Apply / Revert for the data object editor dialogs.
 *
 * A data object editor holds its edits in a draft. Nothing reaches the model
 * until Apply; Revert discards the draft; closing with a draft asks. The model
 * and its registries therefore always hold the APPLIED state, so every other
 * view (Properties, Attribute Table, Object Browser) keeps showing applied
 * values and cannot be overwritten by, or overwrite, a half-finished draft.
 *
 * DataObjectDraftSession tracks the state and runs Apply / Revert through hooks
 * the dialog supplies. DataObjectDraftFooter is the visible half: a status
 * indicator and the Revert / Apply / Close buttons. The indicator reports
 * applied-to-model state only; whether the project FILE is saved stays on the
 * main window title.
 *
 * (workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md)
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_UI_EDITORS_DATAOBJECTDRAFTSESSION_H
#define OPENSWMMVIS_UI_EDITORS_DATAOBJECTDRAFTSESSION_H

#include <QList>
#include <QObject>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QUndoStack;

namespace openswmmvis::ui
{

class DataObjectDraftSession : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Clean,     ///< No draft; nothing applied in this session yet.
        Unapplied, ///< The draft differs from the model.
        Applied,   ///< The last Apply (or immediate action) reached the model.
        Rejected,  ///< The last Apply was refused; the draft is kept.
        Conflict   ///< Unapplied, and the model object changed elsewhere meanwhile.
    };

    /*! What the owning dialog supplies. */
    struct Hooks {
        std::function<void()> commitOpenEditors; ///< hand a value still being typed to the draft
        std::function<int()> dirtyCount;         ///< objects whose draft differs from the model
        std::function<bool(QString *)> apply;    ///< write every draft; false + reason on refusal
        std::function<void()> revert;            ///< drop every draft and show model values
    };

    /*! One object's model write, replayable in both directions for Undo. */
    struct Edit {
        std::function<bool(bool forward)> write; ///< false when the model refused the values
    };

    DataObjectDraftSession(Hooks hooks, QWidget *dialog);
    ~DataObjectDraftSession() override;

    /*! The model this session's dialog edits (its SWMMModelLayer), so Save,
     *  Run and Close can find editors holding unapplied changes. */
    void setModel(const QObject *model) { m_model = model; }

    /*! Sessions of \p model with unapplied changes, after each has handed a
     *  value still being typed to its draft. */
    static QList<DataObjectDraftSession *> withUnappliedChanges(const QObject *model);

    /*! The dialog's window title, for naming it in a prompt. */
    QString dialogTitle() const;

    State state() const noexcept { return m_state; }
    QString message() const { return m_message; }
    bool hasDraft() const { return m_hooks.dirtyCount && m_hooks.dirtyCount() > 0; }

    /*! Call after any draft edit: recomputes Unapplied / Clean. */
    void noteEdited();
    /*! Call when a model object that has a draft changed from another view. */
    void noteExternalChange();
    /*! Report an action that took effect at once (create, delete, rename). */
    void noteApplied(const QString &what);

    /*! Write all edits as ONE undo step, all or nothing.
     *  Each write runs forward; if one refuses, the ones already written are
     *  written back and nothing is pushed. With \p undo null the edits are
     *  applied without history.
     * @return False when a write refused. */
    static bool commit(const QVector<Edit> &edits, QUndoStack *undo, const QString &undoText);

public slots:
    /*! Apply the draft. Asks first when the model changed elsewhere.
     * @return True when there was nothing to apply or the model accepted it. */
    bool apply();
    void revert();

public:
    /*! Before the dialog closes: with a draft, ask Apply / Discard / Cancel.
     * @return False when the close must not proceed (Cancel, or Apply refused). */
    bool resolveBeforeClose();

signals:
    void changed();

private:
    void set(State state, const QString &message);

    Hooks m_hooks;
    QWidget *m_dialog;
    const QObject *m_model = nullptr;
    State m_state = State::Clean;
    QString m_message;
    bool m_conflict = false;
};

/*! What the project is about to do while editors hold unapplied changes. */
enum class UnappliedAction { Save, Run, Close };

/*! Before Save, Run or Close of \p model's project: when editors hold
 *  unapplied changes, ask Apply / go on without them / Cancel. Apply applies
 *  every such editor and stops at the first refusal (that editor is raised).
 *  Going on without them keeps the drafts, except for Close, which discards
 *  them. Saving itself never prompts — it writes the applied state.
 * @return False when the action must not go ahead (Cancel, or a refused Apply). */
bool resolveUnappliedChanges(const QObject *model, UnappliedAction action, QWidget *parent);

/*! Status indicator + Revert / Apply / Close for a draft session. */
class DataObjectDraftFooter : public QWidget
{
    Q_OBJECT

public:
    DataObjectDraftFooter(DataObjectDraftSession *session, QWidget *parent);

    QPushButton *applyButton() const noexcept { return m_apply; }
    QPushButton *revertButton() const noexcept { return m_revert; }
    QPushButton *closeButton() const noexcept { return m_close; }
    QLabel *statusLabel() const noexcept { return m_status; }

signals:
    void closeRequested();

private:
    void refresh();

    DataObjectDraftSession *m_session;
    QLabel *m_status;
    QPushButton *m_revert;
    QPushButton *m_apply;
    QPushButton *m_close;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_EDITORS_DATAOBJECTDRAFTSESSION_H
