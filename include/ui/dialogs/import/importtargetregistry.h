/**
 * @file      importtargetregistry.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — static schema registry for the "Import Feature Layer →
 *            SWMM Objects" dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — static schema registry for the
 * "Import Feature Layer → SWMM Objects" dialog. Describes every
 * importable SWMM target kind and its mappable attributes so the
 * mapping table, planner, and executor all consume one source of
 * truth. Pure Qt Core data — no engine, GDAL, or widget includes —
 * so the planner unit tests stay dependency-light.
 *
 * Attribute keys marked with a non-empty \c adapterProperty map 1:1
 * to a Q_PROPERTY on the matching property adapter
 * (SWMM*PropertyAdapter). Keys with an empty adapterProperty
 * ("name", "fromNode", "toNode") are consumed by the creation call
 * itself (applyNodeAdd / applyLinkAdd / applyGageAdd).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTTARGETREGISTRY_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTTARGETREGISTRY_H

#include <QCoreApplication>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace openswmmvis::import
{

/*! Importable SWMM object kinds. Point-geometry kinds first, then
 *  polyline-geometry kinds. Order is the dialog's combo order. */
enum class TargetKind {
    Junction = 0,
    Outfall,
    Storage,
    Divider,
    RainGage, // points
    Conduit,
    Pump,
    Orifice,
    Weir,
    Outlet // polylines
};

/*! One (label, engine code) pair of an enum-typed attribute.
 *  Codes mirror the adapter Q_ENUMs, which mirror the engine. */
struct EnumChoice {
    QString label; ///< canonical key, e.g. "FREE" — matched case-insensitively
    int code;      ///< engine integer written through the adapter
};

/*! One mappable attribute of a target kind. */
struct TargetAttribute {
    QString key;                              ///< stable id, e.g. "invertElev"
    QString label;                            ///< translated display label
    QMetaType::Type type = QMetaType::Double; ///< Double / QString / Int
    bool required = false;                    ///< must be mapped for import
    QString adapterProperty;                  ///< Q_PROPERTY name; empty = ctor-consumed
    QVector<EnumChoice> enumChoices;          ///< non-empty ⇒ enum-typed (type == Int)
};

class ImportTargetRegistry
{
    Q_DECLARE_TR_FUNCTIONS(ImportTargetRegistry)
public:
    /**
     * @brief Static-only metadata namespace class; construction intentionally unavailable, no
     *   registry object/state/lifetime to manage.
     */
    ImportTargetRegistry() = delete;

    /*! All kinds in dialog display order.
     * @return Value list of ten supported target kinds in enum/dialog order, node kinds then
     *   RainGage then five link kinds. No source availability/capability filtering or mutation.
     */
    [[nodiscard]] static QVector<TargetKind> allKinds();

    /*! Translated display label, e.g. "Rain Gage".
     * @param[in] k Target enum, unknown returns empty.
     * @return Translated user label, no engine/provider/name validation.
     */
    [[nodiscard]] static QString kindLabel(TargetKind k);

    /*! True for Junction / Outfall / Storage / Divider. RainGage is a
     *  point kind but NOT a node kind (separate engine namespace).
     * @param[in] k Any TargetKind including unknown cast values.
     * @return True for Junction/Outfall/Storage/Divider only. Pure enum classification, no
     *   geometry/source inspection.
     */
    [[nodiscard]] static bool isNodeKind(TargetKind k);
    /**
     * @param[in] k Any TargetKind including unknown cast values.
     * @return True for Conduit/Pump/Orifice/Weir/Outlet only. Pure enum classification, no
     *   geometry/source inspection.
     */
    [[nodiscard]] static bool isLinkKind(TargetKind k);
    /*! Point-geometry sources: node kinds + RainGage.
     * @param[in] k Any TargetKind including unknown cast values.
     * @return True for Node kind or RainGage, excluding every link/unknown. Pure enum
     *   classification, no geometry/source inspection.
     */
    [[nodiscard]] static bool isPointKind(TargetKind k);

    /*! SWMM_NodeType code (0=Junction, 1=Outfall, 2=Storage, 3=Divider);
     *  -1 when \p k is not a node kind.
     * @param[in] k Target kind, nonnode -1.
     * @return Engine Junction0/Outfall1/Storage2/Divider3 code, not existing node type lookup. Pure
     *   mapping.
     */
    [[nodiscard]] static int swmmNodeType(TargetKind k);
    /*! SWMM_LinkType code (0=Conduit, 1=Pump, 2=Orifice, 3=Weir,
     *  4=Outlet); -1 when \p k is not a link kind.
     * @param[in] k Target kind, nonlink -1.
     * @return Engine Conduit0/Pump1/Orifice2/Weir3/Outlet4 code, not existing link lookup. Pure
     *   mapping.
     */
    [[nodiscard]] static int swmmLinkType(TargetKind k);

    /*! The mappable attributes of \p kind in display order. "name" is
     *  always first and always required. Link kinds additionally carry
     *  "fromNode"/"toNode" rows (required-ness of those is decided
     *  dynamically by the endpoint-resolution options — the planner
     *  treats them as optional inputs; see ImportMapping).
     * @param[in] kind Target type; unknown still returns required name metadata only.
     * @return New ordered metadata: required name, optional link endpoints, adapter-backed
     *   scalar/enum attributes, then 6 node or 14 conduit authored heat keys. Native project
     *   adapter units (length, area, flow, rainfall-rate), shape-dependent geoms, orifice rate 1/s;
     *   heat SI/°C. No numerical min/max/unit conversion stored/enforced by registry.
     * @note Outfall fixed-stage write can switch type, storage/infiltration special groups absent
     *   beyond explicitly listed attributes. Optional endpoint mapping requirement resolved by
     *   planner strategy, no immutable schema/reference borrowing or source/engine writes.
     */
    [[nodiscard]] static QVector<TargetAttribute> attributesFor(TargetKind kind);

    /*! Lookup one attribute by key; returns an entry with empty key
     *  when \p key is unknown for \p kind.
     * @param[in] kind Target type, unknown has name only.
     * @param[in] key Exact case-sensitive target key, no trim.
     * @return COPY of first matching attributesFor entry, default TargetAttribute with empty key
     *   absent; label/type defaults are not proof of lookup success. No source query/write.
     */
    [[nodiscard]] static TargetAttribute attribute(TargetKind kind, const QString &key);
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTTARGETREGISTRY_H
