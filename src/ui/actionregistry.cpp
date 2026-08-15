#include "ui/actionregistry.h"
#include "ui/actioncatalog.h"

#include <QAction>
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSettings>
#include <QWidget>

namespace {

Q_LOGGING_CATEGORY(lcActionRegistry, "openswmm.ui.actionregistry")

const QString kSettingsGroup = QStringLiteral("SWMMVis::Shortcuts");

/*! Resolve a catalog "std:Name" token to the StandardKey enum. Only the
 *  names the catalog actually uses are mapped; the catalog integrity
 *  test fails on any token missing here. */
bool standardKeyForToken(const QString &token, QKeySequence::StandardKey *out)
{
    // Exactly the tokens the catalog uses. Platform-theme-dependent keys
    // like Preferences/Quit stay literal in the catalog instead — the
    // offscreen QPA (used by every gui test) resolves them to nothing.
    static const QHash<QString, QKeySequence::StandardKey> kMap = {
        {QStringLiteral("New"),          QKeySequence::New},
        {QStringLiteral("Open"),         QKeySequence::Open},
        {QStringLiteral("Save"),         QKeySequence::Save},
        {QStringLiteral("SaveAs"),       QKeySequence::SaveAs},
        {QStringLiteral("Print"),        QKeySequence::Print},
        {QStringLiteral("Copy"),         QKeySequence::Copy},
        {QStringLiteral("Find"),         QKeySequence::Find},
        {QStringLiteral("Undo"),         QKeySequence::Undo},
        {QStringLiteral("Redo"),         QKeySequence::Redo},
        {QStringLiteral("ZoomIn"),       QKeySequence::ZoomIn},
        {QStringLiteral("ZoomOut"),      QKeySequence::ZoomOut},
        {QStringLiteral("HelpContents"), QKeySequence::HelpContents},
    };
    const auto it = kMap.constFind(token);
    if (it == kMap.constEnd())
        return false;
    if (out)
        *out = it.value();
    return true;
}

}   // namespace

namespace openswmmvis::ui {

ActionRegistry::ActionRegistry(QObject *parent)
    : QObject(parent)
{
    mEntries.reserve(int(kActionCatalogSize));
    for (const ActionCatalogEntry &e : kActionCatalog)
        mEntries.insert(QString::fromLatin1(e.id), &e);
}

ActionRegistry *ActionRegistry::instance()
{
    // Same lifetime pattern as PreferencesManager: heap singleton parented
    // to the application so shutdown order is deterministic.
    static ActionRegistry *s_instance = nullptr;
    if (!s_instance)
        s_instance = new ActionRegistry(QCoreApplication::instance());
    return s_instance;
}

void ActionRegistry::registerAction(const QString &id, QAction *action)
{
    if (!action)
        return;
    const ActionCatalogEntry *entry = mEntries.value(id, nullptr);
    if (!entry) {
        qCWarning(lcActionRegistry) << "unknown catalog id" << id
                                    << "for action" << action->objectName();
        return;
    }
    QAction *existing = mActions.value(id, nullptr);
    if (existing == action)
        return;
    if (existing) {
        qCWarning(lcActionRegistry) << "id" << id
                                    << "already bound; ignoring re-registration of"
                                    << action->objectName();
        return;
    }
    mActions.insert(id, action);
    connect(action, &QObject::destroyed, this, [this, id]() {
        mActions.remove(id);
    });
    applyShortcut(id);
    emit actionRegistered(id);
}

int ActionRegistry::registerFromCatalog(QWidget *root)
{
    if (!root)
        return 0;
    int adopted = 0;
    for (const ActionCatalogEntry &e : kActionCatalog) {
        const QString id = QString::fromLatin1(e.id);
        if (mActions.contains(id))
            continue;
        const QString objectName = QString::fromLatin1(e.objectName);
        if (objectName.isEmpty())
            continue;
        if (auto *act = root->findChild<QAction *>(objectName)) {
            registerAction(id, act);
            ++adopted;
        }
    }
    return adopted;
}

QAction *ActionRegistry::action(const QString &id) const
{
    return mActions.value(id, nullptr);
}

QList<QAction *> ActionRegistry::allActions() const
{
    // Catalog order (stable for the palette / editor), not hash order.
    QList<QAction *> out;
    out.reserve(mActions.size());
    for (const ActionCatalogEntry &e : kActionCatalog) {
        if (QAction *act = mActions.value(QString::fromLatin1(e.id), nullptr))
            out.append(act);
    }
    return out;
}

QStringList ActionRegistry::registeredIds() const
{
    QStringList out;
    out.reserve(mActions.size());
    for (const ActionCatalogEntry &e : kActionCatalog) {
        const QString id = QString::fromLatin1(e.id);
        if (mActions.contains(id))
            out.append(id);
    }
    return out;
}

const ActionCatalogEntry *ActionRegistry::catalogEntry(const QString &id) const
{
    return mEntries.value(id, nullptr);
}

void ActionRegistry::setEnabledByTag(unsigned tag, bool enabled)
{
    for (auto it = mActions.constBegin(); it != mActions.constEnd(); ++it) {
        const ActionCatalogEntry *entry = mEntries.value(it.key(), nullptr);
        if (entry && (entry->tags & tag))
            it.value()->setEnabled(enabled);
    }
}

QList<QKeySequence> ActionRegistry::defaultShortcuts(const QString &id) const
{
    const ActionCatalogEntry *entry = mEntries.value(id, nullptr);
    if (!entry)
        return {};
    const QString spec = QString::fromLatin1(entry->defaultShortcut);
    if (spec.isEmpty())
        return {};
    if (spec.startsWith(QStringLiteral("std:"))) {
        QKeySequence::StandardKey sk;
        if (standardKeyForToken(spec.mid(4), &sk))
            return QKeySequence::keyBindings(sk);
        qCWarning(lcActionRegistry) << "unknown standard-key token" << spec
                                    << "for" << id;
        return {};
    }
    const QKeySequence seq(spec, QKeySequence::PortableText);
    return seq.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{seq};
}

QList<QKeySequence> ActionRegistry::effectiveShortcuts(const QString &id) const
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    const QVariant stored = settings.value(id);
    settings.endGroup();
    if (stored.isValid()) {
        const QString text = stored.toString();
        if (text.isEmpty())
            return {};   // deliberately cleared
        const QKeySequence seq =
            QKeySequence::fromString(text, QKeySequence::PortableText);
        return seq.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{seq};
    }
    return defaultShortcuts(id);
}

void ActionRegistry::setUserShortcut(const QString &id, const QKeySequence &seq)
{
    if (!mEntries.contains(id)) {
        qCWarning(lcActionRegistry) << "setUserShortcut: unknown id" << id;
        return;
    }
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(id, seq.toString(QKeySequence::PortableText));
    settings.endGroup();
    applyShortcut(id);
    emit shortcutChanged(id);
}

void ActionRegistry::resetShortcut(const QString &id)
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.remove(id);
    settings.endGroup();
    applyShortcut(id);
    emit shortcutChanged(id);
}

bool ActionRegistry::hasUserShortcut(const QString &id) const
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    const bool present = settings.contains(id);
    settings.endGroup();
    return present;
}

QString ActionRegistry::conflictingActionId(const QKeySequence &seq,
                                            const QString &excludeId) const
{
    if (seq.isEmpty())
        return {};
    for (auto it = mActions.constBegin(); it != mActions.constEnd(); ++it) {
        if (it.key() == excludeId)
            continue;
        if (it.value()->shortcuts().contains(seq))
            return it.key();
    }
    return {};
}

void ActionRegistry::applyShortcut(const QString &id)
{
    if (QAction *act = mActions.value(id, nullptr))
        act->setShortcuts(effectiveShortcuts(id));
}

}   // namespace openswmmvis::ui
