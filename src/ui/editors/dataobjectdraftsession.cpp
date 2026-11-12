/**
 * @file      dataobjectdraftsession.cpp
 * @brief     Explicit Apply / Revert for the data object editor dialogs.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/editors/dataobjectdraftsession.h"

#include "ui/theme/themehelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QTime>
#include <QUndoCommand>
#include <QUndoStack>

namespace openswmmvis::ui
{

namespace
{

/** One Apply as one undo step. Pushed already applied, so the first redo is skipped. */
class DraftApplyCommand : public QUndoCommand
{
public:
    DraftApplyCommand(QVector<DataObjectDraftSession::Edit> edits, const QString &text)
        : QUndoCommand(text), m_edits(std::move(edits))
    {
    }
    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        for (const auto &edit : m_edits)
            if (!edit.write(true))
                setObsolete(true);
    }
    void undo() override
    {
        for (auto it = m_edits.crbegin(); it != m_edits.crend(); ++it)
            if (!it->write(false))
                setObsolete(true);
    }

private:
    QVector<DataObjectDraftSession::Edit> m_edits;
    bool m_first = true;
};

/*! Every live session, so Save / Run / Close can find unapplied changes. */
QList<DataObjectDraftSession *> &liveSessions()
{
    static QList<DataObjectDraftSession *> sessions;
    return sessions;
}

} // namespace

DataObjectDraftSession::DataObjectDraftSession(Hooks hooks, QWidget *dialog)
    : QObject(dialog), m_hooks(std::move(hooks)), m_dialog(dialog)
{
    set(State::Clean, tr("No unapplied changes"));
    liveSessions().append(this);
}

DataObjectDraftSession::~DataObjectDraftSession()
{
    liveSessions().removeAll(this);
}

QList<DataObjectDraftSession *> DataObjectDraftSession::withUnappliedChanges(const QObject *model)
{
    QList<DataObjectDraftSession *> out;
    if (!model)
        return out;
    for (auto *session : std::as_const(liveSessions())) {
        if (session->m_model != model)
            continue;
        if (session->m_hooks.commitOpenEditors)
            session->m_hooks.commitOpenEditors();
        if (session->hasDraft())
            out.append(session);
    }
    return out;
}

QString DataObjectDraftSession::dialogTitle() const
{
    return m_dialog ? m_dialog->windowTitle() : QString();
}

bool resolveUnappliedChanges(const QObject *model, UnappliedAction action, QWidget *parent)
{
    QList<QPointer<DataObjectDraftSession>> sessions;
    QStringList editors;
    for (auto *session : DataObjectDraftSession::withUnappliedChanges(model)) {
        sessions.append(session);
        editors << session->dialogTitle();
    }
    if (sessions.isEmpty())
        return true;

    const int n = sessions.size();
    QString text, applyText, skipText;
    switch (action) {
    case UnappliedAction::Save:
        text = DataObjectDraftSession::tr("%n editor(s) hold changes that are not applied to the "
                                          "model. Saving now leaves them out.",
                                          nullptr, n);
        applyText = DataObjectDraftSession::tr("Apply and Save");
        skipText = DataObjectDraftSession::tr("Save Without Them");
        break;
    case UnappliedAction::Run:
        text = DataObjectDraftSession::tr("%n editor(s) hold changes that are not applied to the "
                                          "model. Running now leaves them out.",
                                          nullptr, n);
        applyText = DataObjectDraftSession::tr("Apply and Run");
        skipText = DataObjectDraftSession::tr("Run Without Them");
        break;
    case UnappliedAction::Close:
        text = DataObjectDraftSession::tr("%n editor(s) hold changes that are not applied to the "
                                          "model. Closing the project discards them.",
                                          nullptr, n);
        applyText = DataObjectDraftSession::tr("Apply");
        skipText = DataObjectDraftSession::tr("Discard");
        break;
    }
    QMessageBox box(QMessageBox::Question, DataObjectDraftSession::tr("Unapplied Changes"), text,
                    QMessageBox::NoButton, parent);
    box.setInformativeText(editors.join(QLatin1Char('\n')));
    auto *applyButton = box.addButton(applyText, QMessageBox::AcceptRole);
    auto *skipButton = box.addButton(skipText, QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(applyButton);
    box.exec();

    if (box.clickedButton() == applyButton) {
        for (const auto &session : std::as_const(sessions)) {
            if (!session || session->apply())
                continue;
            if (auto *dialog = qobject_cast<QWidget *>(session->parent())) {
                dialog->show();
                dialog->raise();
                dialog->activateWindow();
            }
            return false;
        }
        return true;
    }
    if (box.clickedButton() == skipButton) {
        if (action == UnappliedAction::Close)
            for (const auto &session : std::as_const(sessions))
                if (session)
                    session->revert();
        return true;
    }
    return false;
}

void DataObjectDraftSession::set(State state, const QString &message)
{
    m_state = state;
    m_message = message;
    emit changed();
}

void DataObjectDraftSession::noteEdited()
{
    const int n = m_hooks.dirtyCount ? m_hooks.dirtyCount() : 0;
    if (n == 0) {
        m_conflict = false;
        // Keep "Applied …" / "Rejected …" visible until the next real edit.
        if (m_state == State::Unapplied || m_state == State::Conflict)
            set(State::Clean, tr("No unapplied changes"));
        else
            emit changed();
        return;
    }
    if (m_conflict)
        set(State::Conflict, tr("Unapplied changes (%n object(s)) · changed elsewhere", nullptr, n));
    else
        set(State::Unapplied, tr("Unapplied changes (%n object(s))", nullptr, n));
}

void DataObjectDraftSession::noteExternalChange()
{
    if (!hasDraft())
        return;
    m_conflict = true;
    noteEdited();
}

void DataObjectDraftSession::noteApplied(const QString &what)
{
    if (hasDraft()) {
        noteEdited(); // an unapplied draft is the more important thing to show
        return;
    }
    set(State::Applied, tr("%1 — applied to model").arg(what));
}

bool DataObjectDraftSession::commit(const QVector<Edit> &edits, QUndoStack *undo,
                                    const QString &undoText)
{
    for (int i = 0; i < edits.size(); ++i) {
        if (edits[i].write(true))
            continue;
        for (int j = i; j >= 0; --j) // the refused one may be half written
            edits[j].write(false);
        return false;
    }
    if (undo && !edits.isEmpty())
        undo->push(new DraftApplyCommand(edits, undoText));
    return true;
}

bool DataObjectDraftSession::apply()
{
    if (m_hooks.commitOpenEditors)
        m_hooks.commitOpenEditors();
    if (!hasDraft()) {
        noteEdited();
        return true;
    }
    if (m_conflict) {
        QMessageBox box(QMessageBox::Question, tr("Changed Elsewhere"),
                        tr("An object you are editing here was changed in another view since you "
                           "started."),
                        QMessageBox::NoButton, m_dialog);
        auto *overwrite = box.addButton(tr("Apply My Changes"), QMessageBox::AcceptRole);
        auto *reload = box.addButton(tr("Discard Mine and Reload"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == reload) {
            revert();
            return true;
        }
        if (box.clickedButton() != overwrite)
            return false;
    }
    QString reason;
    if (!m_hooks.apply(&reason)) {
        set(State::Rejected, tr("Rejected: %1").arg(reason));
        return false;
    }
    m_conflict = false;
    set(State::Applied,
        tr("Applied to model · %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm"))));
    return true;
}

void DataObjectDraftSession::revert()
{
    if (m_hooks.revert)
        m_hooks.revert();
    m_conflict = false;
    set(State::Clean, tr("No unapplied changes"));
}

bool DataObjectDraftSession::resolveBeforeClose()
{
    if (m_hooks.commitOpenEditors)
        m_hooks.commitOpenEditors();
    if (!hasDraft())
        return true;
    QMessageBox box(QMessageBox::Question, tr("Unapplied Changes"),
                    tr("This editor has changes that have not been applied to the model."),
                    QMessageBox::NoButton, m_dialog);
    auto *applyButton = box.addButton(tr("Apply"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(applyButton);
    box.exec();
    if (box.clickedButton() == applyButton)
        return apply() && !hasDraft();
    if (box.clickedButton() == discard) {
        revert();
        return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────

DataObjectDraftFooter::DataObjectDraftFooter(DataObjectDraftSession *session, QWidget *parent)
    : QWidget(parent), m_session(session)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("draftStatus"));
    m_status->setAccessibleName(tr("Apply status"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setContentsMargins(8, 3, 8, 3);
    row->addWidget(m_status);
    row->addStretch(1);

    m_revert = new QPushButton(tr("Revert"), this);
    m_revert->setObjectName(QStringLiteral("draftRevert"));
    m_revert->setToolTip(tr("Discard the changes that have not been applied"));
    m_apply = new QPushButton(tr("Apply"), this);
    m_apply->setObjectName(QStringLiteral("draftApply"));
    m_apply->setToolTip(tr("Write the changes to the model (%1)")
                            .arg(QKeySequence(Qt::CTRL | Qt::Key_Return)
                                     .toString(QKeySequence::NativeText)));
    m_close = new QPushButton(tr("Close"), this);
    m_close->setObjectName(QStringLiteral("draftClose"));
    for (QPushButton *button : {m_revert, m_apply, m_close}) {
        button->setAutoDefault(false); // Enter in a field must not press a button
        row->addWidget(button);
    }

    connect(m_apply, &QPushButton::clicked, session, &DataObjectDraftSession::apply);
    connect(m_revert, &QPushButton::clicked, session, &DataObjectDraftSession::revert);
    connect(m_close, &QPushButton::clicked, this, &DataObjectDraftFooter::closeRequested);
    auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), parent);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, session, &DataObjectDraftSession::apply);
    connect(session, &DataObjectDraftSession::changed, this, &DataObjectDraftFooter::refresh);
    refresh();
}

void DataObjectDraftFooter::refresh()
{
    using State = DataObjectDraftSession::State;
    const State state = m_session->state();
    m_status->setText(m_session->message());
    QString style;
    switch (state) {
    case State::Unapplied:
    case State::Conflict:
        style = theme::bannerStyle(theme::Banner::Info);
        break;
    case State::Applied:
        style = theme::bannerStyle(theme::Banner::Success);
        break;
    case State::Rejected:
        style = theme::bannerStyle(theme::Banner::Error);
        break;
    case State::Clean:
        style = theme::hintStyle();
        break;
    }
    m_status->setStyleSheet(style + QStringLiteral(" border-radius: 4px;"));
    const bool dirty = m_session->hasDraft();
    m_apply->setEnabled(dirty);
    m_revert->setEnabled(dirty);
}

} // namespace openswmmvis::ui
