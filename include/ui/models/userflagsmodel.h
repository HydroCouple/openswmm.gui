/*!
 * \file   userflagsmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Engine-scoped store for user flags ([USER_FLAGS] /
 *         [USER_FLAG_VALUES]) — Phase 1 of
 *         docs/USER_FLAGS_UI_PLAN_2026-06-03.md.
 *
 * Thin QObject wrapper over the engine's user-flags C API. One live
 * instance per engine handle, lazily owned by SWMMModelLayer
 * (ensureUserFlagsModel()), shared by every UI surface that reads or
 * mutates flags (User Flags Manager dialog, Attribute Table columns,
 * Attribute Panel rows) so all views observe the same change signals.
 *
 * All engine mutation calls for user flags live here — the same
 * "only place" rule the layer's applyControlRule* helpers follow for
 * control rules.
 */
#ifndef OPENSWMMVIS_UI_MODELS_USERFLAGSMODEL_H
#define OPENSWMMVIS_UI_MODELS_USERFLAGSMODEL_H

#include <QObject>
#include <QString>
#include <QVector>

#include <openswmm/engine/openswmm_engine.h>  // SWMM_Engine

namespace openswmmvis::ui {

class UserFlagsModel : public QObject
{
    Q_OBJECT
public:
    /*! Value type of a flag — mirrors openswmm::UserFlagType. */
    enum class FlagType : int {
        Boolean = 0,
        Integer = 1,
        Real    = 2,
        String  = 3
    };
    Q_ENUM(FlagType)

    /*! One [USER_FLAGS] schema entry. Names are uppercase (engine
     *  normalises on define). */
    struct Def {
        QString  name;
        FlagType type = FlagType::String;
        QString  description;
    };

    explicit UserFlagsModel(SWMM_Engine engine, QObject *parent = nullptr);

    [[nodiscard]] SWMM_Engine engine() const noexcept { return m_engine; }

    // ----- Schema definitions ([USER_FLAGS]) ------------------------------

    /*! All flag definitions in engine insertion order (cached; the cache
     *  is invalidated by define()/undefine()). */
    [[nodiscard]] const QVector<Def> &defs() const;

    [[nodiscard]] bool isDefined(const QString &name) const;

    /*! Define (or redefine) a flag. Emits defsChanged() on success.
     *  Redefining keeps previously assigned per-object values as-is. */
    bool define(const QString &name, FlagType type,
                const QString &description, QString *outError = nullptr);

    /*! Remove a definition and all per-object values assigned to it.
     *  Emits defsChanged() on success. */
    bool undefine(const QString &name, QString *outError = nullptr);

    // ----- Per-object values ([USER_FLAG_VALUES]) -------------------------

    /*! Read the value assigned to (objType, objName, flagName) in string
     *  form (BOOLEAN → "YES"/"NO", INTEGER → "%d", REAL → "%g", STRING
     *  verbatim). Returns an empty string when unset; *found reports
     *  whether a value is assigned. */
    [[nodiscard]] QString value(const QString &objType, const QString &objName,
                                const QString &flagName,
                                bool *found = nullptr) const;

    /*! Assign a value from its string form. The flag must be defined; the
     *  declared type drives parsing (the engine rejects e.g. "abc" for an
     *  INTEGER flag). Emits valueChanged() on success. */
    bool setValue(const QString &objType, const QString &objName,
                  const QString &flagName, const QString &value,
                  QString *outError = nullptr);

    /*! Remove the assignment (mark unset). Idempotent. Emits
     *  valueChanged() when the engine accepts the call. */
    bool clearValue(const QString &objType, const QString &objName,
                    const QString &flagName);

    /*! Display label for a flag type ("Boolean", "Integer", …). */
    static QString typeLabel(FlagType type);

signals:
    /*! The set of definitions changed (define / redefine / undefine).
     *  Views with per-flag columns or rows should rebuild. */
    void defsChanged();

    /*! One per-object assignment changed (set or cleared). */
    void valueChanged(const QString &objType, const QString &objName,
                      const QString &flagName);

private:
    SWMM_Engine           m_engine = nullptr;
    mutable QVector<Def>  m_defsCache;
    mutable bool          m_defsCacheValid = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_USERFLAGSMODEL_H
