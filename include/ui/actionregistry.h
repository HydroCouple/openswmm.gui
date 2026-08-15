#ifndef ACTIONREGISTRY_H
#define ACTIONREGISTRY_H

/*!
 * \file actionregistry.h
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
 */

#include <QHash>
#include <QKeySequence>
#include <QObject>

class QAction;
class QWidget;

namespace openswmmvis::ui {

struct ActionCatalogEntry;

class ActionRegistry : public QObject
{
    Q_OBJECT

public:
    static ActionRegistry *instance();

    /*! Adopt \a action for catalog entry \a id and apply its effective
     *  shortcut. No-op with a warning if the id is unknown or already
     *  bound to a different action. */
    void registerAction(const QString &id, QAction *action);

    /*! Sweep the catalog and adopt every entry whose objectName resolves
     *  to a QAction under \a root. Entries that don't resolve are left
     *  unregistered (tolerated — mirrors the historical findChild
     *  behavior for dangling names). Returns the number adopted. */
    int registerFromCatalog(QWidget *root);

    QAction *action(const QString &id) const;
    QList<QAction *> allActions() const;
    QStringList registeredIds() const;
    const ActionCatalogEntry *catalogEntry(const QString &id) const;

    /*! Enable/disable every registered action carrying \a tag (see
     *  ActionTag in actioncatalog.h). */
    void setEnabledByTag(unsigned tag, bool enabled);

    /*! Catalog-default bindings for \a id ("std:" entries expand to the
     *  platform's full standard-key binding list). */
    QList<QKeySequence> defaultShortcuts(const QString &id) const;
    /*! Effective bindings — user override when present, else defaults. */
    QList<QKeySequence> effectiveShortcuts(const QString &id) const;

    /*! Persist \a seq as the user binding for \a id and apply it live.
     *  An empty sequence persists as "deliberately cleared". */
    void setUserShortcut(const QString &id, const QKeySequence &seq);
    /*! Drop any user override for \a id and restore the catalog default. */
    void resetShortcut(const QString &id);
    bool hasUserShortcut(const QString &id) const;

    /*! Id of the registered action whose effective bindings contain
     *  \a seq, excluding \a excludeId; empty if none. */
    QString conflictingActionId(const QKeySequence &seq,
                                const QString &excludeId = QString()) const;

signals:
    void actionRegistered(const QString &id);
    void shortcutChanged(const QString &id);

private:
    explicit ActionRegistry(QObject *parent = nullptr);

    void applyShortcut(const QString &id);

    QHash<QString, QAction *> mActions;              // id -> adopted action
    QHash<QString, const ActionCatalogEntry *> mEntries;
};

}   // namespace openswmmvis::ui

#endif // ACTIONREGISTRY_H
