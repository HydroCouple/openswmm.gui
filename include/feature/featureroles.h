/**
 * @file      featureroles.h
 * @brief     The feature-layer role registry
 *            (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md P1, §4, §6).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The feature-layer role registry
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md P1, §4, §6).
 *
 * One table says, per role: its label and persisted token, the geometry
 * kinds it allows, and its fields — name, type, unit, default, description,
 * required or optional, and choices. The New Feature Layer dialog, the
 * Features dock, the attribute table, the mesh worker's readers and
 * "Update fields to role…" all take field names and choices from here, the
 * same way every mesh-cell editor takes its parameters from
 * mesh::cellParamSpecs().
 *
 * A role is a schema template plus a label — never a behaviour switch. The
 * layer stays a plain OGR table whatever its role, so every consumer that
 * accepts "a vector layer" keeps working.
 *
 * Leaf module: Qt Core + feature/featuretypes + mesh/meshbctype only.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef FEATUREROLES_H
#define FEATUREROLES_H

#include "feature/featuregeometry.h"
#include "feature/featuretypes.h"

#include <QHash>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

/*!
 * \enum FeatureLayerRole
 * \brief What a feature layer is FOR.
 *
 * Persisted by TOKEN (\ref featureLayerRoleToken), never by integer, so new
 * roles are appended at the end and existing .oswp files keep their meaning.
 */
enum class FeatureLayerRole {
    General = 0,       ///< No template beyond a name.
    DomainBoundary,    ///< Mesh domain polygon(s); interior rings become holes.
    Breakline,         ///< 3D constraint lines / hard points.
    Region,            ///< Areas with their own cell size; four-sided ones become quads.
    ParameterZone,     ///< Roughness, initial depth, land use, soil group.
    SwmmDelineation,   ///< Source for Import Feature Layer → SWMM objects.
    BoundaryCondition, ///< Lines mapped onto 2D mesh boundary edges.
    Corridor           ///< Lines with a width: quad strips (triangle engine D12.2).
};

/**
 * @brief Describe a layer role using current translations.
 * @param[in] r Role enum; unknown values select General.
 * @return Registry user-facing label.
 */
[[nodiscard]] QString featureLayerRoleLabel(FeatureLayerRole r);
/**
 * @brief Encode a role as a stable project-persistence token.
 * @param[in] r Role enum; unknown values select General.
 * @return Registry lowercase token.
 */
[[nodiscard]] QString featureLayerRoleToken(FeatureLayerRole r);
/**
 * @brief Decode a role token without relying on enum integer persistence.
 * @param[in] token Trimmed, case-insensitive token.
 * @return Matched role or General when unknown.
 */
[[nodiscard]] FeatureLayerRole featureLayerRoleFromToken(const QString &token);
/*! \brief The fields a newly created layer of role \p r starts with: every
 *         role field, required and optional, in registry order.
 * @param[in] r Role whose complete template is requested; unknown maps to General.
 * @return Owned schema with required and optional registry fields in display order.
 */
[[nodiscard]] openswmmvis::feature::Schema featureLayerRoleTemplate(FeatureLayerRole r);
/*! \brief The geometry type a role implies, for the New-layer dialog's default.
 *         GeometryType::None when the role does not imply one.
 * @param[in] r Role enum; unknown maps to General.
 * @return Default geometry kind, possibly None when the role leaves it to the user.
 */
[[nodiscard]] openswmmvis::feature::GeometryType featureLayerRoleGeometry(FeatureLayerRole r);

namespace openswmmvis::feature
{

/*!
 * \struct FeatureRoleSpec
 * \brief Everything the registry knows about one role.
 */
struct FeatureRoleSpec {
    FeatureLayerRole role = FeatureLayerRole::General;
    QString token;                    ///< Persisted in .oswp.
    QString label;                    ///< Translated.
    QString summary;                  ///< One line for the New dialog.
    QVector<GeometryType> geometries; ///< Allowed kinds, in display order.
    GeometryType defaultGeometry = GeometryType::None;
    bool wantsZ = false;      ///< Expand the Z section; nudge to 3D.
    QVector<FieldDef> fields; ///< Role fields, in display order.
    /*! Field names earlier versions of this role carried that nothing reads
     *  any more. "Update fields to role…" offers to drop them. */
    QStringList retiredFields;
    /*! Old field name → current name. Readers accept both; "Update fields to
     *  role…" renames. */
    QHash<QString, QString> legacyAliases;
    /*! Shown under the role summary (e.g. "assign from Mesh Editing for now"). */
    QString note;

    /**
     * @brief Find a field in this role's template.
     * @param[in] name Case-insensitive current field name; aliases are handled separately by callers.
     * @return Borrowed field pointer valid until this spec's fields mutate, or nullptr.
     */
    [[nodiscard]] const FieldDef *field(const QString &name) const;
    /**
     * @param[in] t Geometry kind being considered.
     * @return True for exact membership in the role's allowed-kind vector.
     */
    [[nodiscard]] bool allowsGeometry(GeometryType t) const { return geometries.contains(t); }
};

/*! \brief Every role, in the New dialog's display order. Stable for the
 *         process lifetime.
 * @return Borrowed immutable registry initialized on first use; translated text reflects the language at initialization.
 */
[[nodiscard]] const QVector<FeatureRoleSpec> &featureRoleSpecs();
/*! \brief The spec for \p r (General when \p r is unknown).
 * @param[in] r Role enum to resolve.
 * @return Borrowed process-lifetime spec; General for an unrecognized enum.
 */
[[nodiscard]] const FeatureRoleSpec &featureRoleSpec(FeatureLayerRole r);

/*!
 * \brief The stored schema with the role's editor hints filled in.
 *
 * \details For every stored field whose name is a field of \p role (or a
 *          legacy alias of one), the registry supplies what the GeoPackage
 *          cannot carry: the choice source, model list, unit and required
 *          flag; and, when the table stores none, the choices and the
 *          description. Name, type and default always stay as stored. This is
 *          what gives an old layer dropdowns without changing it (plan P7).
 * @param[in] role Role whose registry supplies editor hints/aliases.
 * @param[in] stored Stored field definitions copied without mutation.
 * @return Same ordered name/type/default definitions enriched only for matching-type registry fields or aliases; saved nonempty descriptions/custom choices are retained.
 */
[[nodiscard]] Schema editorSchema(FeatureLayerRole role, const Schema &stored);

// ----- What the mesh worker and its widgets read -----------------------------

/*! Region size field, then its legacy alias (§4.3).
 * @return Current region target-size key h followed by legacy quad_spacing, in reader precedence order.
 */
[[nodiscard]] QStringList regionSizeFieldNames();
/**
 * @return Stable stored field name cells.
 */
[[nodiscard]] QString regionCellsFieldName(); ///< "cells"
/**
 * @return Stable stored field name tag for region cell labeling.
 */
[[nodiscard]] QString regionTagFieldName(); ///< "tag"
/**
 * @return Stable stored corridor width field name width; values are lengths in the source/project coordinate convention.
 */
[[nodiscard]] QString corridorWidthFieldName(); ///< "width"
/**
 * @return Stable stored corridor tag key tag.
 */
[[nodiscard]] QString corridorTagFieldName(); ///< "tag"

/*! Values of the Region `cells` field. */
enum class RegionCells { Auto, Triangles, RequiredQuads };
/*!
 * \brief Interpret a Region `cells` value.
 * \param known  Set false for a non-empty value that is not a token; the
 *               caller chooses legacy fallback or refuses an invalid unified policy.
 * An empty or null value is Auto and known.
 * @param[in] value Trimmed case-insensitive cells token; empty means Auto.
 * @return RequiredQuads for required_quads, Triangles for triangles, Auto for auto/empty/unknown.
 */
[[nodiscard]] RegionCells regionCellsFromValue(const QString &value, bool *known = nullptr);
/**
 * @brief Encode a region cell-generation preference.
 * @param[in] c Preference enum.
 * @return required_quads, triangles, or auto.
 */
[[nodiscard]] QString regionCellsToken(RegionCells c);

/*!
 * \brief Whether boundary-condition field \p field carries a value for the
 *        BC type token \p bcType (§4.7: the editors grey out the others).
 * \details Mirrors MeshAttributeTableModel's per-type masking: each type
 *          reads one parameter; `bc_type`, `group` and `conveyance` apply
 *          throughout. Fields of other roles always apply.
 * @param[in] bcType Boundary-condition INP token interpreted by MeshBCTypes.
 * @param[in] field Case-insensitive field name; unknown/nonparameter fields always apply.
 * @return Whether the specific head/slope/flow/tseries/curve parameter is relevant; an unknown BC type keeps all fields enabled.
 */
[[nodiscard]] bool bcFieldApplies(const QString &bcType, const QString &field);

// ----- "Update fields to role…" (plan P7, R6) -------------------------------

/*!
 * \struct FieldUpdatePlan
 * \brief What "Update fields to role…" would change. Pure data, so the
 *        preview and the test read the same thing.
 */
struct FieldUpdatePlan {
    /*! Role fields the layer lacks (name, type, default, description, choices). */
    QVector<FieldDef> add;
    /*! Legacy name → current name, when the current name is absent. */
    QVector<QPair<QString, QString>> rename;
    /*! Retired fields present in the layer, with how many features hold a
     *  non-empty value in each. Dropping is opt-in per field. */
    QVector<QPair<QString, int>> retired;
    /*! Existing role fields whose stored default, description or choices
     *  differ from the registry: the registry's version, to attach. */
    QVector<FieldDef> attach;

    /**
     * @return True only when add, rename, retired and attach plans are all empty; does not inspect a live layer.
     */
    [[nodiscard]] bool isEmpty() const
    {
        return add.isEmpty() && rename.isEmpty() && retired.isEmpty() && attach.isEmpty();
    }
};

/*!
 * \param role     The layer's role.
 * \param stored   The layer's stored schema (FeatureLayer::schema()).
 * \param nonEmpty Per stored field name, the number of features with a
 *                 non-empty value (only retired fields are looked up).
 * @brief Compare a stored schema to a role template without applying changes.
 * @return Owned add/rename/retired/metadata-attach proposals. Existing incompatible field types are not silently converted.
 */
[[nodiscard]] FieldUpdatePlan planFieldUpdate(FeatureLayerRole role, const Schema &stored,
                                              const QHash<QString, int> &nonEmpty);

/*! True when \p v counts as "holding a value" for \ref planFieldUpdate.
 * @param[in] v Stored attribute value to count.
 * @return False for invalid/null or whitespace-only QString; true for every other value including numeric zero and false.
 */
[[nodiscard]] bool isNonEmptyValue(const QVariant &v);

} // namespace openswmmvis::feature

#endif // FEATUREROLES_H
