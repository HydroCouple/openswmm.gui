/**
 * @file      ilayerstylesubject.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     One styleable subject for the unified LayerStyleDialog.
 *
 *         Slice U-2. A layer can present one or more ILayerStyleSubjects to
 *         the dialog, each becoming a tab (or tab-group) of property
 *         editors. The Q_PROPERTYs on subject->propertyObject() drive the
 *         editor UI through the existing QPropertyModel framework, so
 *         adding a new styleable thing is just a matter of writing a
 *         QObject with Q_PROPERTY declarations and exposing it via a
 *         subject.
 *
 *         Cancel rollback uses a JSON snapshot. The default helper
 *         The generic snapshot walks writable properties declared by the most-derived
 *         propertyObject metaobject
 *         (or, when the object is a SublayerStyle subclass, defers to its
 *         toJson()/fromJson() overrides which already exist for .oswp
 *         persistence).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_ILAYERSTYLESUBJECT_H
#define OPENSWMMVIS_UI_DIALOGS_ILAYERSTYLESUBJECT_H

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace openswmmvis::ui
{

/*!
 * \brief One styleable thing the dialog can edit.
 *
 *        Subjects are owned by the dialog (unique_ptr) and outlive a
 *        single dialog session. The propertyObject() must outlive the
 *        subject; typically it's owned by the parent layer/sublayer.
 */
class ILayerStyleSubject
{
public:
    /**
     * @brief Polymorphic subject destruction; default releases no property QObject, rollback or
     *   source ownership. Host preserves propertyObject lifetime while subject/editor/snapshots are
     *   used.
     */
    virtual ~ILayerStyleSubject() = default;

    /*! Tab title displayed in the dialog.
     * @return User-visible tab title by value, subclass-defined. No ownership transfer or
     *   uniqueness guarantee; UI-thread subject access follows property object lifetime.
     */
    [[nodiscard]] virtual QString title() const = 0;

    /*! Optional sub-section. When non-empty, subjects sharing a section
     *  get grouped into a single outer tab with sub-tabs inside.
     * @return Empty in base, meaning no outer group. Overrides may share nonempty grouping label;
     *   no automatic source mutation or grouping performed by getter.
     */
    [[nodiscard]] virtual QString section() const { return {}; }

    /*! The QObject whose Q_PROPERTYs drive the editor.
     * @return Borrowed editable property QObject, may be null for base snapshot/restore no-op.
     *   Subject/editor must not outlive nonnull object; no ownership transfer or lifetime guard in
     *   interface.
     */
    [[nodiscard]] virtual QObject *propertyObject() const = 0;

    /*! Stable id for routing (right-click → "focus this tab"). For
     *  sublayer subjects this should match the sublayer id; for layer-
     *  level subjects, use a fixed slug like "general" / "rendering".
     * @return Empty in base; overrides provide stable tab-routing slug. No uniqueness enforcement
     *   or source/selection mutation.
     */
    [[nodiscard]] virtual QString routingId() const { return {}; }

    /*! Capture writable declared Q_PROPERTYs into JSON. The default
     *  implementation reads the most-derived metaobject's declared properties;
     *  subjects whose propertyObject already has toJson()/fromJson()
     *  (i.e. SublayerStyle subclasses) can override to defer to that.
     * @return New snapshot, empty null object. SublayerStyle delegates to polymorphic toJson.
     *   Generic QObject captures ONLY properties declared by MOST-DERIVED metaobject
     *   (propertyOffset..count), both readable and writable, omitting inherited properties/dynamic
     *   properties.
     * @note UI-thread property reads, no writes. Generic conversion is limited: exact QColor
     *   HexArgb, QFont string, numbers/strings/bools and int/string fallback; large integer
     *   precision/type fidelity not universally lossless. Caller preserves object/source and same
     *   schema for restoration.
     */
    [[nodiscard]] virtual QJsonObject snapshot() const;

    /*! Restore from a snapshot captured by \ref snapshot.
     * @param[in] snapshot Borrowed prior JSON, extra/missing keys ignored in generic path; no
     *   object/type/version identity validation.
     * @note Null property object no-op. SublayerStyle delegates fromJson; generic loops
     *   MOST-DERIVED writable declared properties, converts by metatype and writes valid QVariant,
     *   ignores mp.write result. Setter notifications/side effects can fire per property, no signal
     *   block, transaction, undo or rollback of failed writes. Inherited/dynamic properties not
     *   restored; malformed primitive values can convert defaults, invalid QColor/QFont skipped.
     *   GUI-thread serialized source access required.
     */
    virtual void restore(const QJsonObject &snapshot);
};

/*! Trivial concrete subject — title + propertyObject + optional routingId.
 *  Most layers can use this directly without writing a derived class. */
class LayerStyleSubject : public ILayerStyleSubject
{
public:
    /**
     * @param[in] title Moved display caption.
     * @param[in] propertyObject Borrowed RAW QObject, optional null, must outlive subject/editor
     *   use; no ownership transfer.
     * @param[in] routingId Moved optional route key, no uniqueness/format validation.
     * @param[in] section Moved optional grouping caption.
     * @brief Store immutable metadata/pointer only, no object read, snapshot or editor creation.
     */
    LayerStyleSubject(QString title, QObject *propertyObject, QString routingId = {},
                      QString section = {})
        : m_title(std::move(title)), m_section(std::move(section)),
          m_routingId(std::move(routingId)), m_propertyObject(propertyObject)
    {
    }

    /**
     * @return Stored caption copy. No source mutation/lookup.
     */
    [[nodiscard]] QString title() const override { return m_title; }
    /**
     * @return Stored section copy, empty ungrouped. No source mutation/lookup.
     */
    [[nodiscard]] QString section() const override { return m_section; }
    /**
     * @return Stored route key copy, empty unscoped. No source mutation/lookup.
     */
    [[nodiscard]] QString routingId() const override { return m_routingId; }
    /**
     * @return Borrowed stored raw QObject or null, no destruction guard/ownership pin. No source
     *   mutation/lookup.
     */
    [[nodiscard]] QObject *propertyObject() const override { return m_propertyObject; }

private:
    QString m_title;
    QString m_section;
    QString m_routingId;
    QObject *m_propertyObject = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_ILAYERSTYLESUBJECT_H
