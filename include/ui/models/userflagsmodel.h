/**
 * @file      userflagsmodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Engine-scoped store for user flags ([USER_FLAGS] /
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_MODELS_USERFLAGSMODEL_H
#define OPENSWMMVIS_UI_MODELS_USERFLAGSMODEL_H

#include <QObject>
#include <QString>
#include <QVector>

#include <openswmm/engine/openswmm_engine.h> // SWMM_Engine

namespace openswmmvis::ui
{

class UserFlagsModel : public QObject
{
    Q_OBJECT
public:
    /*! Value type of a flag — mirrors openswmm::UserFlagType. */
    enum class FlagType : int { Boolean = 0, Integer = 1, Real = 2, String = 3 };
    Q_ENUM(FlagType)

    /*! One [USER_FLAGS] schema entry. Names are uppercase (engine
     *  normalises on define). */
    struct Def {
        QString name;
        FlagType type = FlagType::String;
        QString description;
    };

    /**
     * @brief Bind an engine-scoped flag adapter with a lazy definition cache.
     * @param[in] engine Borrowed engine handle; null is permitted but cannot be queried/mutated successfully. Caller keeps a non-null handle alive.
     * @param[in] parent Optional QObject owner; coordinate engine access on the owning GUI thread.
     */
    explicit UserFlagsModel(SWMM_Engine engine, QObject *parent = nullptr);

    /**
     * @brief Access the borrowed engine used for all flag operations.
     * @return Original handle; this model never closes or destroys it.
     */
    [[nodiscard]] SWMM_Engine engine() const noexcept { return m_engine; }

    // ----- Schema definitions ([USER_FLAGS]) ------------------------------

    /*! All flag definitions in engine insertion order (cached; the cache
     *  is invalidated by define()/undefine()).
     * @return Model-owned cache reference; next successful define/undefine invalidates it. API enumeration failures are skipped or produce an empty cached list. Direct external engine edits do not invalidate this cache.
     */
    [[nodiscard]] const QVector<Def> &defs() const;

    /**
     * @brief Find a flag name in the cached definition list using uppercase normalization.
     * @param[in] name Flag identity, uppercased without trimming.
     * @return True if a cached definition matches; false also covers unavailable/failed enumeration.
     */
    [[nodiscard]] bool isDefined(const QString &name) const;

    /*! Define (or redefine) a flag. Emits defsChanged() on success.
     *  Redefining keeps previously assigned per-object values as-is.
     * @param[in] name UTF-8 flag name passed to the engine for validation/normalization.
     * @param[in] type Boolean/Integer/Real/String selector.
     * @param[in] description UTF-8 schema description.
     * @param[out] outError Optional missing-engine/rejection diagnostic; unchanged on success.
     * @return True invalidates the cache and emits defsChanged(); false retains this model's cache.
     */
    bool define(const QString &name, FlagType type, const QString &description,
                QString *outError = nullptr);

    /*! Remove a definition and all per-object values assigned to it.
     *  Emits defsChanged() on success.
     * @param[in] name Flag identity passed to the engine.
     * @param[out] outError Optional missing-engine/not-defined diagnostic; unchanged on success.
     * @return True invalidates the cache and emits defsChanged(); false does not notify.
     */
    bool undefine(const QString &name, QString *outError = nullptr);

    // ----- Per-object values ([USER_FLAG_VALUES]) -------------------------

    /*! Read the value assigned to (objType, objName, flagName) in string
     *  form (BOOLEAN → "YES"/"NO", INTEGER → "%d", REAL → "%g", STRING
     *  verbatim). Returns an empty string when unset; *found reports
     *  whether a value is assigned.
     * @param[in] objType Engine object-kind token.
     * @param[in] objName Object identity token.
     * @param[in] flagName Defined flag identity.
     * @param[out] found Optional flag initialized false, then true only for a successful assigned-value read.
     * @return Formatted value or empty for unset/empty string/API failure; found distinguishes assigned empty strings from absence. Uses a 1,024-byte read buffer.
     */
    [[nodiscard]] QString value(const QString &objType, const QString &objName,
                                const QString &flagName, bool *found = nullptr) const;

    /*! Assign a value from its string form. The flag must be defined; the
     *  declared type drives parsing (the engine rejects e.g. "abc" for an
     *  INTEGER flag). Emits valueChanged() on success.
     * @param[in] objType Engine object-kind token.
     * @param[in] objName Object identity token.
     * @param[in] flagName Defined flag identity.
     * @param[in] value UTF-8 string parsed according to the definition type.
     * @param[out] outError Optional missing-engine/type-or-identity rejection diagnostic.
     * @return True emits valueChanged() with the caller's identity strings; false does not notify. No undo command is created here.
     */
    bool setValue(const QString &objType, const QString &objName, const QString &flagName,
                  const QString &value, QString *outError = nullptr);

    /*! Remove the assignment (mark unset). Idempotent. Emits
     *  valueChanged() when the engine accepts the call.
     * @param[in] objType Engine object-kind token.
     * @param[in] objName Object identity token.
     * @param[in] flagName Flag assignment to clear.
     * @return True after engine acceptance and valueChanged(); false for no engine or a rejected identity/request.
     */
    bool clearValue(const QString &objType, const QString &objName, const QString &flagName);

    /*! Display label for a flag type ("Boolean", "Integer", …).
     * @param[in] type Flag enum selector; unrecognized values use String.
     * @return Translated display label.
     */
    static QString typeLabel(FlagType type);

signals:
    /*! The set of definitions changed (define / redefine / undefine).
     *  Views with per-flag columns or rows should rebuild. */
    void defsChanged();

    /*! One per-object assignment changed (set or cleared).
     * @param[in] objType Caller-supplied object-kind token.
     * @param[in] objName Caller-supplied object identity.
     * @param[in] flagName Caller-supplied flag identity; emission occurs for accepted set/clear requests.
     */
    void valueChanged(const QString &objType, const QString &objName, const QString &flagName);

private:
    SWMM_Engine m_engine = nullptr;
    mutable QVector<Def> m_defsCache;
    mutable bool m_defsCacheValid = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_USERFLAGSMODEL_H
