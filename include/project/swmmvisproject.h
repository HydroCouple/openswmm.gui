/*!
 * \file   swmmvisproject.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
    explicit SWMMVisProject(QObject *parent = nullptr);
    ~SWMMVisProject() override;

    /*! Path of the `.oswp` file backing this project, or empty if
     *  the project hasn't been saved yet (untitled). */
    [[nodiscard]] QString oswpPath() const noexcept { return mOswpPath; }
    void setOswpPath(const QString &path);

    /*! Optional human-readable title (currently mirrors the basename
     *  of the `.oswp` path).  Future: persisted in the v4 schema. */
    [[nodiscard]] QString title() const noexcept { return mTitle; }
    void setTitle(const QString &title);

    /*! Snapshot of the current instance list (one window per SWMM
     *  model).  Order reflects insertion order; not stable across
     *  add/remove. */
    [[nodiscard]] QVector<SWMMVisProjectWindow *> instances() const { return mInstances; }

    /*! Subset of `instances()` whose `hasChanges()` returns true. */
    [[nodiscard]] QVector<SWMMVisProjectWindow *> dirtyInstances() const;

    /*! Add an instance.  No-op if already present.  Does not take
     *  ownership of the window. */
    void addInstance(SWMMVisProjectWindow *pw);

    /*! Remove an instance.  Returns true if it was present. */
    bool removeInstance(SWMMVisProjectWindow *pw);

    /*! Convenience — true when at least one instance is dirty. */
    [[nodiscard]] bool isDirty() const;

signals:
    void oswpPathChanged(const QString &path);
    void titleChanged(const QString &title);
    void instanceAdded(SWMMVisProjectWindow *pw);
    void instanceRemoved(SWMMVisProjectWindow *pw);

private:
    QString mOswpPath;
    QString mTitle;
    QVector<SWMMVisProjectWindow *> mInstances;
};

#endif // SWMMVISPROJECT_AA3_H
