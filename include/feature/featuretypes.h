/*!
 * \file   featuretypes.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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

namespace openswmmvis::feature {

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
enum class FieldType
{
    Text = 0,
    Integer,
    Real,
    Boolean
};

[[nodiscard]] QString    fieldTypeLabel(FieldType t);
[[nodiscard]] QString    fieldTypeToken(FieldType t);
[[nodiscard]] FieldType  fieldTypeFromToken(const QString &token);
/*! The OGRFieldType constant for \p t (Boolean is an integer with a subtype;
 *  see FeatureStore::createLayer, which sets OFSTBoolean). */
[[nodiscard]] int        ogrFieldTypeFor(FieldType t);
/*! Coerce \p v to \p t, returning a default-constructed value of the right
 *  type when the conversion fails. Never returns an invalid QVariant. */
[[nodiscard]] QVariant   coerceToFieldType(const QVariant &v, FieldType t);

/*!
 * \struct FieldDef
 * \brief One user-defined attribute column.
 */
struct FieldDef
{
    QString   name;                  ///< OGR field name; see \ref sanitizeFieldName.
    FieldType type = FieldType::Text;
    QVariant  defaultValue;          ///< Applied to new features; may be invalid.
    QString   description;           ///< Tooltip in the schema editor and property panel.

    [[nodiscard]] bool isValid() const { return !name.isEmpty(); }
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static FieldDef fromJson(const QJsonObject &o);

    [[nodiscard]] bool operator==(const FieldDef &o) const;
    [[nodiscard]] bool operator!=(const FieldDef &o) const { return !(*this == o); }
};

/*!
 * \brief Make \p raw safe as an OGR/GeoPackage column name: ASCII letters,
 *        digits and underscore, never starting with a digit, at most 63
 *        characters, never one of the reserved names ("fid", "geom").
 * \returns An empty string only when \p raw contains no usable character.
 */
[[nodiscard]] QString sanitizeFieldName(const QString &raw);

/*!
 * \brief Make \p raw safe as a GeoPackage table name, on the same rules as
 *        \ref sanitizeFieldName plus a "fl_" prefix when \p raw would
 *        otherwise start with a digit.
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
    Schema() = default;

    [[nodiscard]] int  count() const { return m_fields.size(); }
    [[nodiscard]] bool isEmpty() const { return m_fields.isEmpty(); }
    [[nodiscard]] const QVector<FieldDef> &fields() const { return m_fields; }

    [[nodiscard]] int indexOf(const QString &name) const;
    [[nodiscard]] bool contains(const QString &name) const { return indexOf(name) >= 0; }
    [[nodiscard]] const FieldDef *field(const QString &name) const;
    [[nodiscard]] FieldDef at(int i) const;
    [[nodiscard]] QStringList names() const;

    /*! Append \p f. Fails (returns false) when the name is empty or already
     *  present — name collisions are a user error the schema editor reports,
     *  not something to silently rename. */
    bool append(const FieldDef &f);
    bool remove(const QString &name);
    bool replace(const QString &name, const FieldDef &f);
    void clear() { m_fields.clear(); }

    /*! A QVariantMap with every field at its default (or a type-appropriate
     *  empty value), for a newly drawn feature. */
    [[nodiscard]] QVariantMap defaultAttributes() const;

    /*! Drop keys not in the schema and coerce the rest to their declared
     *  types. Called before every write so a stale attribute map from an
     *  undo record cannot reintroduce a removed column. */
    [[nodiscard]] QVariantMap conform(const QVariantMap &attrs) const;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] static Schema fromJson(const QJsonObject &o);

    [[nodiscard]] bool operator==(const Schema &o) const { return m_fields == o.m_fields; }
    [[nodiscard]] bool operator!=(const Schema &o) const { return !(*this == o); }

private:
    QVector<FieldDef> m_fields;
};

/*!
 * \struct Feature
 * \brief One editable feature: identity, geometry, attributes.
 */
struct Feature
{
    FeatureId       id = kInvalidFeatureId;
    FeatureGeometry geometry;
    QVariantMap     attributes;

    [[nodiscard]] bool isValid() const { return geometry.isValid(); }
    [[nodiscard]] bool isStored() const { return id != kInvalidFeatureId; }
};

}   // namespace openswmmvis::feature

Q_DECLARE_METATYPE(openswmmvis::feature::FieldType)

#endif // FEATURETYPES_H
