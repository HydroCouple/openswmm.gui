/**
 * @file      featuretypes.h
 * @brief     Schema and feature value types for editable feature layers
 *            (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.1).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Schema and feature value types for editable feature layers
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §3.1).
 *
 * The repo has no runtime-defined attribute schema today: the 32 property
 * adapters in include/ui/properties/ are hand-written against fixed property
 * sets, SWMMAttributeTableModel::ColumnSpec is a compile-time table, and
 * GISVectorLayer::identifyAt returns QVariantMap as read-only OUTPUT. FieldDef
 * / Schema are the first field descriptors the user can author, and Feature is
 * the first value type that owns a QVariantMap of attributes.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATURETYPES_H
#define FEATURETYPES_H

#include "feature/featuregeometry.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

namespace openswmmvis::feature
{

/*! Feature id. Mirrors OGR's GIntBig FID. -1 means "not yet stored". */
using FeatureId = qint64;
inline constexpr FeatureId kInvalidFeatureId = -1;

/*!
 * \enum FieldType
 * \brief The attribute types an authored schema may declare.
 *
 * Deliberately small: these four cover every role template in plan §6 and map
 * 1:1 onto both OGR field types and QVariant. Adding a type means touching the
 * OGR mapping, the property adapter editor, and the table delegate together.
 */
enum class FieldType { Text = 0, Integer, Real, Boolean };

/**
 * @brief Describe an attribute storage type to the user.
 * @param[in] t FieldType selector.
 * @return Translated type label; unknown enum values use the Text fallback.
 */
[[nodiscard]] QString fieldTypeLabel(FieldType t);
/**
 * @brief Serialize a field type using a stable lowercase token.
 * @param[in] t FieldType selector.
 * @return text/integer/real/boolean token; unknown values fall back to text.
 */
[[nodiscard]] QString fieldTypeToken(FieldType t);
/**
 * @brief Decode a case-insensitive stored field-type token.
 * @param[in] token Text trimmed and normalized to lowercase.
 * @return Recognized type, or Text when unknown.
 */
[[nodiscard]] FieldType fieldTypeFromToken(const QString &token);
/*! The OGRFieldType constant for \p t (Boolean is an integer with a subtype;
 *  see FeatureStore::createLayer, which sets OFSTBoolean).
 * @param[in] t FieldType selector.
 * @return OGRFieldType constant; Boolean uses OFTInteger with subtype applied separately. Unknown values fall back to text.
 */
[[nodiscard]] int ogrFieldTypeFor(FieldType t);
/*! Coerce \p v to \p t, returning a default-constructed value of the right
 *  type when the conversion fails. Never returns an invalid QVariant.
 * @param[in] v Source value, possibly invalid/null.
 * @param[in] t Target type; integer coercion uses QVariant::toInt (32-bit), real uses toDouble and Boolean uses toBool.
 * @return Valid type-specific QVariant; failed integer/real conversion becomes zero, missing text becomes empty, and no finiteness/range constraints are added.
 */
[[nodiscard]] QVariant coerceToFieldType(const QVariant &v, FieldType t);

/*!
 * \struct FieldChoice
 * \brief One entry of a choice list
 *        (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md P2).
 *
 * The VALUE is what the table stores: a stable token ("triangles",
 * "TS_STAGE"), never a translated label (plan Q5).
 */
struct FieldChoice {
    QString value; ///< Stored token.
    QString label; ///< Shown in combos; empty means "show the value".

    /**
     * @return Nonempty explicit label, otherwise the stable stored choice token.
     */
    [[nodiscard]] QString displayLabel() const { return label.isEmpty() ? value : label; }
    /**
     * @brief Compare complete stored field/schema values.
     * @param[in] o Value to compare without mutation.
     * @return True for equal stored fields, including choice order/editor hints where present.
     */
    [[nodiscard]] bool operator==(const FieldChoice &o) const
    {
        return value == o.value && label == o.label;
    }
    /**
     * @brief Test whether stored field/schema values differ.
     * @param[in] o Value to compare without mutation.
     * @return Logical negation of operator==; schema/choice order remains significant.
     */
    [[nodiscard]] bool operator!=(const FieldChoice &o) const { return !(*this == o); }
};

/*! \brief Same (value, label) pairs, in any order. The GeoPackage driver
 *         reads a coded domain back sorted by value, not in the order it was
 *         written, so stored and registry lists are compared as sets.
 * @param[in] a First copied choice list, sorted locally by token then label.
 * @param[in] b Second copied list.
 * @return Equality of sorted value/label pairs; original order is ignored but duplicate multiplicity remains significant.
 */
[[nodiscard]] bool sameChoiceSet(QVector<FieldChoice> a, QVector<FieldChoice> b);

/*!
 * \enum ChoiceSource
 * \brief Where a field's dropdown gets its entries (plan P2).
 */
enum class ChoiceSource {
    None = 0, ///< Free value; no dropdown.
    Fixed,    ///< \ref FieldDef::choices; persisted as a GeoPackage coded domain.
    Model,    ///< Names from the open project (\ref ModelList); never persisted.
    Suggested ///< Editable combo of the values already used in the column.
};

/*! \enum ModelList \brief Which project list a \c ChoiceSource::Model field offers. */
enum class ModelList { None = 0, TimeSeries, Curves };

/*! \enum FieldUnit \brief The unit a numeric field is shown in. */
enum class FieldUnit {
    None = 0,
    Length ///< Project length unit (the spin-box suffix).
};

/*!
 * \struct FieldDef
 * \brief One user-defined attribute column.
 *
 * Only name, type, default, description and Fixed choices are stored in the
 * GeoPackage. The editor hints (Model / Suggested sources, unit, required)
 * come from the role registry for the role's own field names
 * (feature/featureroles.h, \c editorSchema).
 */
struct FieldDef {
    QString name; ///< OGR field name; see \ref openswmmvis::feature::sanitizeFieldName.
    FieldType type = FieldType::Text;
    QVariant defaultValue;        ///< Applied to new features; may be invalid.
    QString description;          ///< Tooltip in the schema editor and property panel.
    QVector<FieldChoice> choices; ///< For ChoiceSource::Fixed; empty otherwise.
    ChoiceSource choiceSource = ChoiceSource::None;
    ModelList modelList = ModelList::None; ///< For ChoiceSource::Model.
    FieldUnit unit = FieldUnit::None;
    bool required = false; ///< A role field the New dialog always creates.

    /**
     * @return True only for a nonempty field name; identifier sanitization/type/default validity is not checked here.
     */
    [[nodiscard]] bool isValid() const { return !name.isEmpty(); }
    /*! True for a Fixed choice field with at least one choice.
     * @return True when choiceSource is Fixed and at least one choice is stored.
 */
    [[nodiscard]] bool hasFixedChoices() const
    {
        return choiceSource == ChoiceSource::Fixed && !choices.isEmpty();
    }
    /*! Index of \p value in \ref choices, or -1. Exact match: tokens are stable.
     * @param[in] value Exact stable token to locate.
     * @return First matching index in stored choice order, or -1.
 */
    [[nodiscard]] int choiceIndex(const QString &value) const;
    /*! The label for \p value, or \p value itself when it is not in the list.
     * @param[in] value Exact stored token.
     * @return Matching choice displayLabel() or the original token when absent.
 */
    [[nodiscard]] QString choiceLabel(const QString &value) const;
    /**
     * @brief Serialize one field definition, including editor hints and optional default.
     * @return Independent JSON object; type/choice-source/unit/model-list use stable tokens.
     */
    [[nodiscard]] QJsonObject toJson() const;
    /**
     * @param[in] o JSON object whose missing values use FieldDef defaults.
     * @return Decoded field without identifier/default/choice validation; empty-token choices are skipped and unknown type tokens resolve to Text.
     */
    [[nodiscard]] static FieldDef fromJson(const QJsonObject &o);

    /**
     * @brief Compare complete stored field/schema values.
     * @param[in] o Value to compare without mutation.
     * @return True for equal stored fields, including choice order/editor hints where present.
     */
    [[nodiscard]] bool operator==(const FieldDef &o) const;
    /**
     * @brief Test whether stored field/schema values differ.
     * @param[in] o Value to compare without mutation.
     * @return Logical negation of operator==; schema/choice order remains significant.
     */
    [[nodiscard]] bool operator!=(const FieldDef &o) const { return !(*this == o); }
};

/*!
 * \brief Make \p raw safe as an OGR/GeoPackage column name: ASCII letters,
 *        digits and underscore, never starting with a digit, at most 63
 *        characters, never one of the reserved names ("fid", "geom").
 * \returns An empty string only when \p raw contains no usable character.
 * @param[in] raw User-entered identifier; ASCII letters/digits/underscores are retained, whitespace/dashes/dots become underscores, others are dropped.
 * @details Leading digits gain f, reserved field names gain _1. The 63-character truncation occurs before those affixes, so the final identifier can be longer.
 */
[[nodiscard]] QString sanitizeFieldName(const QString &raw);

/*!
 * \brief Make \p raw safe as a GeoPackage table name, on the same rules as
 *        \ref sanitizeFieldName plus a "fl_" prefix when \p raw would
 *        otherwise start with a digit.
 * @param[in] raw User-entered table identifier normalized like sanitizeIdentifier().
 * @details Leading digits gain fl_ after initial 63-character truncation; this table-name routine does not add the field-name reserved-word suffix.
 */
[[nodiscard]] QString sanitizeTableName(const QString &raw);

/*!
 * \class Schema
 * \brief An ordered list of \ref FieldDef, with name lookup.
 *
 * Field order is the column order in the attribute table and the row order in
 * the property panel, so it is preserved exactly as authored.
 */
class Schema
{
public:
    /**
     * @brief Construct an empty ordered schema.
     */
    Schema() = default;

    /**
     * @return Number of field definitions in storage/column order.
     */
    [[nodiscard]] int count() const { return m_fields.size(); }
    /**
     * @return True when no field definitions are stored.
     */
    [[nodiscard]] bool isEmpty() const { return m_fields.isEmpty(); }
    /**
     * @return Borrowed ordered definitions, invalidated by schema mutation/destruction.
     */
    [[nodiscard]] const QVector<FieldDef> &fields() const { return m_fields; }

    /**
     * @param[in] name Case-insensitive field name.
     * @return First matching zero-based index, or -1.
     */
    [[nodiscard]] int indexOf(const QString &name) const;
    /**
     * @param[in] name Field name matched case-insensitively.
     * @return True when indexOf() finds a field.
     */
    [[nodiscard]] bool contains(const QString &name) const { return indexOf(name) >= 0; }
    /**
     * @param[in] name Case-insensitive field name.
     * @return Borrowed field pointer, invalidated by schema mutation, or nullptr if absent.
     */
    [[nodiscard]] const FieldDef *field(const QString &name) const;
    /**
     * @param[in] i Zero-based field position.
     * @return Value copy or default invalid FieldDef when outside [0,count()).
     */
    [[nodiscard]] FieldDef at(int i) const;
    /**
     * @return Independent list of field names preserving schema order.
     */
    [[nodiscard]] QStringList names() const;

    /*! Append \p f. Fails (returns false) when the name is empty or already
     *  present — name collisions are a user error the schema editor reports,
     *  not something to silently rename.
     * @param[in] f Definition to copy; name must be nonempty and unique case-insensitively.
     * @return True when appended, false without mutation for an invalid/duplicate name.
 */
    bool append(const FieldDef &f);
    /**
     * @brief Remove the first case-insensitive name match and close its column position.
     * @param[in] name Field name to erase.
     * @return True on removal, false without mutation when absent.
     */
    bool remove(const QString &name);
    /**
     * @brief Replace one field definition in place, allowing a collision-free rename.
     * @param[in] name Existing field name, matched case-insensitively.
     * @param[in] f New definition with a nonempty name not held by another column.
     * @return True on replacement; false preserves the schema for missing/invalid/duplicate input.
     */
    bool replace(const QString &name, const FieldDef &f);
    /**
     * @brief Remove all field definitions; previously borrowed pointers/references to elements become invalid.
     */
    void clear() { m_fields.clear(); }

    /*! A QVariantMap with every field at its default (or a type-appropriate
     *  empty value), for a newly drawn feature.
     * @return Map keyed by stored spellings, coercing each default to its declared type; absent defaults become empty text, zero or false.
 */
    [[nodiscard]] QVariantMap defaultAttributes() const;

    /*! Drop keys not in the schema and coerce the rest to their declared
     *  types. Called before every write so a stale attribute map from an
     *  undo record cannot reintroduce a removed column.
     * @param[in] attrs Read-only value map; lookup uses exact stored field spellings.
     * @return New complete schema-keyed map, omitting extra keys and filling missing keys from coerced defaults. Type conversion failures use type-appropriate empty values.
 */
    [[nodiscard]] QVariantMap conform(const QVariantMap &attrs) const;

    /**
     * @brief Serialize the ordered schema as a fields array.
     * @return Independent JSON object using FieldDef::toJson() for each field.
     */
    [[nodiscard]] QJsonObject toJson() const;
    /**
     * @param[in] o JSON object containing a fields array.
     * @return Ordered decoded definitions; empty names and later case-insensitive duplicates are skipped through append(), without a diagnostic.
     */
    [[nodiscard]] static Schema fromJson(const QJsonObject &o);

    /**
     * @brief Compare complete stored field/schema values.
     * @param[in] o Value to compare without mutation.
     * @return True for equal stored fields, including choice order/editor hints where present.
     */
    [[nodiscard]] bool operator==(const Schema &o) const { return m_fields == o.m_fields; }
    /**
     * @brief Test whether stored field/schema values differ.
     * @param[in] o Value to compare without mutation.
     * @return Logical negation of operator==; schema/choice order remains significant.
     */
    [[nodiscard]] bool operator!=(const Schema &o) const { return !(*this == o); }

private:
    QVector<FieldDef> m_fields;
};

/*!
 * \struct Feature
 * \brief One editable feature: identity, geometry, attributes.
 */
struct Feature {
    FeatureId id = kInvalidFeatureId;
    FeatureGeometry geometry;
    QVariantMap attributes;

    /**
     * @return Lightweight geometry validity only; feature ID/attribute schema and full topology are not checked.
     */
    [[nodiscard]] bool isValid() const { return geometry.isValid(); }
    /**
     * @return True when id differs from -1; does not query whether the backing record still exists.
     */
    [[nodiscard]] bool isStored() const { return id != kInvalidFeatureId; }
};

} // namespace openswmmvis::feature

Q_DECLARE_METATYPE(openswmmvis::feature::FieldType)

#endif // FEATURETYPES_H
