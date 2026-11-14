/**
 * @file      swmmvisproject.h
 * @brief     thin wrapper around the set of SWMMVisProjectWindow instances that share a single
 *            `.oswp` sidecar.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AA-3.2 — thin wrapper around the set of SWMMVisProjectWindow
 * instances that share a single `.oswp` sidecar.  Today there is
 * exactly one window per project; the class exists so that:
 *
 *   1. `.oswp` save can iterate `instances()` and emit one
 *      `sessions[N]` block per window (schema v4).
 *   2. Phase 13's multi-window project UI can drop in without
 *      reshuffling the IO layer.
 *
 * The wrapper is non-owning — `SWMMVis` (the main MDI host) still
 * controls window lifetime.  Removing a window from a project does
 * not delete the window; it just unregisters it from the project.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMVISPROJECT_AA3_H
#define SWMMVISPROJECT_AA3_H

#include <QObject>
#include <QString>
#include <QVector>

class SWMMVisProjectWindow;

class SWMMVisProject : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty, non-owning window group for one project sidecar.
     * @param[in] parent Optional QObject owner.
     */
    explicit SWMMVisProject(QObject *parent = nullptr);
    /**
     * @brief Destroy the grouping object; registered windows remain owned by the MDI host.
     */
    ~SWMMVisProject() override;

    /*! Path of the `.oswp` file backing this project, or empty if
     *  the project hasn't been saved yet (untitled). */
    [[nodiscard]] QString oswpPath() const noexcept { return mOswpPath; }
    /**
     * @brief Store the sidecar path and emit oswpPathChanged() only when it differs.
     * @param[in] path Path as supplied; empty denotes an untitled group. No filesystem check occurs.
     */
    void setOswpPath(const QString &path);

    /*! Optional human-readable title (currently mirrors the basename
     *  of the `.oswp` path).  Future: persisted in the v4 schema. */
    [[nodiscard]] QString title() const noexcept { return mTitle; }
    /**
     * @brief Store the display title and emit titleChanged() only on a value change.
     * @param[in] title Display text, including an empty string.
     */
    void setTitle(const QString &title);

    /*! Snapshot of the current instance list (one window per SWMM
     *  model).  Order reflects insertion order; not stable across
     *  add/remove. */
    [[nodiscard]] QVector<SWMMVisProjectWindow *> instances() const { return mInstances; }

    /*! Subset of `instances()` whose `hasChanges()` returns true. */
    [[nodiscard]] QVector<SWMMVisProjectWindow *> dirtyInstances() const;

    /*! Add an instance.  No-op if already present.  Does not take
     *  ownership of the window.
     * @param[in] pw Borrowed window pointer; null and duplicate pointers are ignored. The caller must unregister windows before their destruction.
     * @note Emits instanceAdded() after appending a new pointer.
     */
    void addInstance(SWMMVisProjectWindow *pw);

    /*! Remove an instance.  Returns true if it was present.
     * @param[in] pw Registered pointer to remove, without deleting the window.
     * @return True after removal and instanceRemoved(); false if the pointer was absent.
     */
    bool removeInstance(SWMMVisProjectWindow *pw);

    /*! Convenience — true when at least one instance is dirty. */
    [[nodiscard]] bool isDirty() const;

signals:
    /**
     * @brief Announce a changed sidecar path.
     * @param[in] path Newly stored path; may be empty.
     */
    void oswpPathChanged(const QString &path);
    /**
     * @brief Announce changed project display text.
     * @param[in] title Newly stored title.
     */
    void titleChanged(const QString &title);
    /**
     * @brief Announce a window newly appended to the group.
     * @param[in] pw Borrowed window registered by addInstance().
     */
    void instanceAdded(SWMMVisProjectWindow *pw);
    /**
     * @brief Announce a window removed from the group.
     * @param[in] pw Removed pointer; no ownership transfer or deletion occurs.
     */
    void instanceRemoved(SWMMVisProjectWindow *pw);

private:
    QString mOswpPath;
    QString mTitle;
    QVector<SWMMVisProjectWindow *> mInstances;
};

#endif // SWMMVISPROJECT_AA3_H
