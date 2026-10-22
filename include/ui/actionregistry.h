#ifndef ACTIONREGISTRY_H
#define ACTIONREGISTRY_H

/**
 * @file      actionregistry.h
 * @brief     UI redesign P1 — runtime registry over the action catalog (actioncatalog.h).
 *
 * UI redesign P1 — runtime registry over the action catalog
 * (actioncatalog.h). Adopts the existing QAction objects by objectName —
 * it never creates or re-creates actions, so `ui->actionXxx` pointers,
 * the objectName-keyed map-tool checked-state sync, and saved window
 * state all keep working unchanged.
 *
 * The catalog plus persisted user overrides are the authority for
 * shortcuts: registration applies the effective binding to the adopted
 * action, and the shortcut editor / command palette read everything
 * through here. Overrides persist under the QSettings group
 * "SWMMVis::Shortcuts" keyed by catalog id (portable-text sequence;
 * empty string = deliberately cleared; absent = catalog default).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QHash>
#include <QKeySequence>
#include <QObject>

class QAction;
class QWidget;

namespace openswmmvis::ui
{

struct ActionCatalogEntry;

class ActionRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Lazily allocate the process action registry, parented to the current QCoreApplication.
     * @return Borrowed singleton; call on the GUI thread and do not retain after application shutdown. No synchronization is supplied.
     */
    static ActionRegistry *instance();

    /*! Adopt \a action for catalog entry \a id and apply its effective
     *  shortcut. No-op with a warning if the id is unknown or already
     *  bound to a different action.
     * @param[in] id Exact catalog identity.
     * @param[in] action Borrowed QAction; null is ignored and its existing QObject owner remains unchanged.
     * @note Tracks destruction to remove the raw pointer, applies effective shortcuts and emits actionRegistered() after a new registration. Same-pointer registration is a no-op.
     */
    void registerAction(const QString &id, QAction *action);

    /*! Sweep the catalog and adopt every entry whose objectName resolves
     *  to a QAction under \a root. Entries that don't resolve are left
     *  unregistered (tolerated — mirrors the historical findChild
     *  behavior for dangling names). Returns the number adopted.
     * @param[in] root Optional widget whose QObject descendants are searched by exact catalog objectName; null returns zero.
     * @return Number newly adopted during this sweep; existing registered IDs are skipped.
     */
    int registerFromCatalog(QWidget *root);

    /**
     * @brief Resolve a registered catalog identity.
     * @param[in] id Exact identity.
     * @return Borrowed action or null when unregistered/destroyed; registry does not own it.
     */
    QAction *action(const QString &id) const;
    /**
     * @brief Snapshot live registered actions in catalog order.
     * @return Borrowed pointer list; action lifetime remains with original QObject owners.
     */
    QList<QAction *> allActions() const;
    /**
     * @brief Snapshot registered identities in stable catalog order.
     * @return Current identity list, excluding unregistered/destroyed actions.
     */
    QStringList registeredIds() const;
    /**
     * @brief Resolve immutable metadata independently of runtime action registration.
     * @param[in] id Exact catalog identity.
     * @return Borrowed static table entry or null when unknown.
     */
    const ActionCatalogEntry *catalogEntry(const QString &id) const;

    /*! Enable/disable every registered action carrying \a tag (see
     *  ActionTag in actioncatalog.h).
     * @param[in] tag Bit mask; every action with any overlapping catalog tag is affected. Zero matches none.
     * @param[in] enabled New QAction enabled state applied live.
     */
    void setEnabledByTag(unsigned tag, bool enabled);

    /*! Catalog-default bindings for \a id ("std:" entries expand to the
     *  platform's full standard-key binding list).
     * @param[in] id Catalog identity; unknown returns no shortcuts.
     * @return Platform standard-key bindings, a portable literal sequence, or empty when absent/unresolved. Unknown standard tokens log a warning.
     */
    QList<QKeySequence> defaultShortcuts(const QString &id) const;
    /*! Effective bindings — user override when present, else defaults.
     * @param[in] id Settings/catalog identity; stored overrides are consulted even for unregistered or unknown IDs.
     * @return One valid portable stored override, empty for an explicitly cleared/invalid override, or catalog defaults when no override exists.
     */
    QList<QKeySequence> effectiveShortcuts(const QString &id) const;

    /*! Persist \a seq as the user binding for \a id and apply it live.
     *  An empty sequence persists as "deliberately cleared".
     * @param[in] id Required known catalog identity; unknown logs and leaves state unchanged.
     * @param[in] seq Binding serialized as PortableText, including an empty deliberate-clear override.
     * @note Applies to a registered action and emits shortcutChanged() even if equal; no conflict enforcement or explicit QSettings sync/error reporting occurs here.
     */
    void setUserShortcut(const QString &id, const QKeySequence &seq);
    /*! Drop any user override for \a id and restore the catalog default.
     * @param[in] id Settings/catalog identity; unknown IDs are not rejected. Removes override, applies any registered action defaults and emits shortcutChanged() even if no override existed.
     */
    void resetShortcut(const QString &id);
    /**
     * @brief Check whether a settings override key exists, including a deliberate empty binding.
     * @param[in] id Exact settings identity.
     * @return Presence of that QSettings key; does not require catalog/action registration.
     */
    bool hasUserShortcut(const QString &id) const;

    /*! Id of the registered action whose effective bindings contain
     *  \a seq, excluding \a excludeId; empty if none.
     * @param[in] seq Exact candidate sequence; empty never conflicts.
     * @param[in] excludeId Registered identity to omit, optionally empty.
     * @return A conflicting registered identity or empty. Tests live QAction::shortcuts(), including externally modified bindings; multiple conflicts resolve in unspecified QHash order.
     */
    QString conflictingActionId(const QKeySequence &seq,
                                const QString &excludeId = QString()) const;

signals:
    /**
     * @brief Announce a newly adopted action after shortcut application.
     * @param[in] id Exact catalog identity.
     */
    void actionRegistered(const QString &id);
    /**
     * @brief Announce a user shortcut set/reset request after applying it live.
     * @param[in] id Affected identity; may be unknown for resetShortcut().
     */
    void shortcutChanged(const QString &id);

private:
    /**
     * @brief Index the immutable catalog metadata without creating QActions.
     * @param[in] parent Optional QObject owner, normally the application singleton.
     */
    explicit ActionRegistry(QObject *parent = nullptr);

    /**
     * @brief Push effective shortcut bindings onto a registered action.
     * @param[in] id Identity; unregistered actions are ignored. No registry notification is emitted here.
     */
    void applyShortcut(const QString &id);

    QHash<QString, QAction *> mActions; // id -> adopted action
    QHash<QString, const ActionCatalogEntry *> mEntries;
};

} // namespace openswmmvis::ui

#endif // ACTIONREGISTRY_H
