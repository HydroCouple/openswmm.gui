/*!
 * @file frozengroundfields.h
 * @brief The ten values of a frozen-ground row as per-object fields.
 * @details A subcatchment's [FROZEN_GROUND] row is edited one value at a time in the attribute
 * table and the property grid under the keys "frost.<key>". The row itself stays in the engine.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_UI_FROZENGROUNDFIELDS_H
#define OPENSWMMVIS_UI_FROZENGROUNDFIELDS_H

#include <QCoreApplication>
#include <QString>

#include <array>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace OpenSWMMVis
{

inline constexpr int kFrozenGroundValueCount = 10;
using FrozenGroundValues = std::array<double, kFrozenGroundValueCount>;

struct FrozenGroundField {
    const char *key;   //!< stable key suffix after "frost."
    const char *label; //!< untranslated label with its unit
};

/*! The fields in the order the engine stores a row. */
inline const std::array<FrozenGroundField, kFrozenGroundValueCount> &frozenGroundFields()
{
    static const std::array<FrozenGroundField, kFrozenGroundValueCount> fields{{
        {"thickness", QT_TRANSLATE_NOOP("FrozenGround", "Thickness (m)")},
        {"conductivity", QT_TRANSLATE_NOOP("FrozenGround", "Conductivity (W/m/K)")},
        {"heatCapacity", QT_TRANSLATE_NOOP("FrozenGround", "Heat capacity (J/m³/K)")},
        {"waterContent", QT_TRANSLATE_NOOP("FrozenGround", "Water content")},
        {"deepTemperature", QT_TRANSLATE_NOOP("FrozenGround", "Deep temperature (°C)")},
        {"deepDepth", QT_TRANSLATE_NOOP("FrozenGround", "Deep depth (m)")},
        {"initialTemperature", QT_TRANSLATE_NOOP("FrozenGround", "Initial temperature (°C)")},
        {"exponent", QT_TRANSLATE_NOOP("FrozenGround", "Exponent")},
        {"snowConductivity", QT_TRANSLATE_NOOP("FrozenGround", "Snow conductivity (W/m/K)")},
        {"snowDensity", QT_TRANSLATE_NOOP("FrozenGround", "Snow density (kg/m³)")},
    }};
    return fields;
}

/*! Values a new row starts from when the model has no `*` row. */
inline FrozenGroundValues frozenGroundDefaults()
{
    return {0.15, 1.5, 2.4e6, 0.30, 8.0, 3.0, 5.0, 11.0, 0.25, 250.0};
}

inline QString frozenGroundFieldKey(int field)
{
    return QStringLiteral("frost.") + QLatin1String(frozenGroundFields().at(field).key);
}

inline QString frozenGroundFieldLabel(int field)
{
    return QCoreApplication::translate("FrozenGround", frozenGroundFields().at(field).label);
}

/*! Index of the field a key names, or -1 when the key is not a frozen-ground field. */
inline int frozenGroundFieldIndex(const QString &key)
{
    if (!key.startsWith(QLatin1String("frost.")))
        return -1;
    for (int i = 0; i < kFrozenGroundValueCount; ++i)
        if (key == frozenGroundFieldKey(i))
            return i;
    return -1;
}

/*! Read the subcatchment row with a name.
 * @param[in] engine Open engine.
 * @param[in] name Subcatchment name, or `*` for the default row.
 * @param[out] values Receives the ten values when the row exists; may be null.
 * @return True when the model has a row with exactly this name.
 */
inline bool frozenGroundRow(SWMM_Engine engine, const QString &name, FrozenGroundValues *values)
{
    int count = 0;
    if (!engine || swmm_frozen_ground_count(engine, 0, &count) != SWMM_OK)
        return false;
    for (int row = 0; row < count; ++row) {
        char stored[1024]{};
        FrozenGroundValues v{};
        if (swmm_frozen_ground_get(engine, 0, row, stored, sizeof(stored), v.data()) != SWMM_OK)
            return false;
        if (name == QString::fromUtf8(stored)) {
            if (values)
                *values = v;
            return true;
        }
    }
    return false;
}

} // namespace OpenSWMMVis

#endif // OPENSWMMVIS_UI_FROZENGROUNDFIELDS_H
