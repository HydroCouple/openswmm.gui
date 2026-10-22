// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshresolution.h
 * @brief     Shared mesh resolution settings and validation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_MESH_RESOLUTION_H
#define OPENSWMM_MESH_RESOLUTION_H

#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>

namespace mesh
{

// Shared by project settings, Preferences and the generation dialog.
// Physical lengths are SI; absence means no maximum, inherit minimum,
// automatic tolerance, or match-minimum averaging respectively.
struct ResolutionSettings {
    double minimumM = 6.096;
    std::optional<double> maximumM, featureM, toleranceM, breaklineToleranceM, averagingM,
        minimumBreaklineLengthM;
    int maxBreaklines = 0;
    int coarsenPasses = 0; ///< Absent in older projects: retain their original mesh behaviour.
    double transition = 2.0;
    bool refineFeatures = false, alignTerrain = true;
    enum class Terrain { Adaptive, Legacy, Off } terrain = Terrain::Adaptive;
    enum class Reference { Averaged, Full, Direct } reference = Reference::Averaged;

    /**
     * @brief Check numeric mesh-resolution settings without modifying or normalizing them.
     * @return Empty string when accepted, otherwise the first English validation error. All
     * present lengths must be finite in (0,1e12] metres, maximum must not precede minimum, and
     * enabled feature refinement must lie within minimum/maximum. Coarsening passes must be
     * 0..10, breakline count 0..10000000, transition finite in [1.05,2]. Enum validity and
     * relationships between other tolerances are not checked. No project mutation or implicit
     * fallback application.
     */
    QString validate() const
    {
        auto positive = [](double v) { return std::isfinite(v) && v > 0 && v <= 1e12; };
        if (!positive(minimumM))
            return QStringLiteral("Minimum target cell size must be a positive finite length.");
        for (const auto &v : {maximumM, featureM, toleranceM, breaklineToleranceM, averagingM,
                              minimumBreaklineLengthM})
            if (v && !positive(*v))
                return QStringLiteral(
                    "Specified sizes and tolerances must be positive finite lengths.");
        if (coarsenPasses < 0 || coarsenPasses > 10)
            return QStringLiteral("Coarsening passes must be between 0 and 10.");
        if (maxBreaklines < 0 || maxBreaklines > 10000000)
            return QStringLiteral("Terrain breakline count is out of range.");
        if (maximumM && *maximumM < minimumM)
            return QStringLiteral(
                "Maximum target cell size must be at least the minimum target size.");
        if (refineFeatures && featureM && *featureM < minimumM)
            return QStringLiteral("Feature target size must be at least the minimum target size.");
        if (refineFeatures && featureM && maximumM && *featureM > *maximumM)
            return QStringLiteral(
                "Feature target size exceeds the maximum target size. Increase the maximum or reduce the feature target.");
        if (!std::isfinite(transition) || transition < 1.05 || transition > 2.0)
            return QStringLiteral("Size transition rate must be between 1.05 and 2.");
        return {};
    }

    /**
     * @brief Serialize every resolution field into the current project JSON schema.
     * @return Owned object with SI metre length controls, null for absent optional lengths, and
     * textual terrain/reference modes. Unknown enum values serialize as off/direct respectively
     * through fallback branches. Does not call validate(), perform unit conversion or write a
     * file; this object is unchanged.
     */
    QJsonObject toJson() const
    {
        auto value = [](std::optional<double> v) {
            return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
        };
        return {{"coarsenPasses", coarsenPasses},
                {"minimumM", minimumM},
                {"maximumM", value(maximumM)},
                {"featureM", value(featureM)},
                {"elevationToleranceM", value(toleranceM)},
                {"breaklineToleranceM", value(breaklineToleranceM)},
                {"averagingM", value(averagingM)},
                {"minimumBreaklineLengthM", value(minimumBreaklineLengthM)},
                {"maxBreaklines", maxBreaklines},
                {"transition", transition},
                {"refineFeatures", refineFeatures},
                {"alignTerrain", alignTerrain},
                {"terrain", terrain == Terrain::Adaptive ? "adaptive"
                            : terrain == Terrain::Legacy ? "legacy"
                                                         : "off"},
                {"reference", reference == Reference::Averaged ? "averaged"
                              : reference == Reference::Full   ? "full"
                                                               : "direct"}};
    }

    /**
     * @brief Decode current-schema resolution fields using defaults for missing or mismatched
     * values.
     * @param[in] o Borrowed object, unchanged; numeric optional lengths become present only for
     * JSON numbers.
     * @return Owned settings, distances in metres; unknown terrain/reference strings select
     * adaptive/averaged. Missing optional values remain absent. Does not validate ranges, clamp
     * values, convert project units or mutate project state; callers should call validate()
     * before using authoring inputs.
     */
    static ResolutionSettings fromJson(const QJsonObject &o)
    {
        ResolutionSettings r;
        auto value = [&](const char *key) -> std::optional<double> {
            const auto v = o.value(QLatin1String(key));
            return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt;
        };
        r.coarsenPasses = o.value("coarsenPasses").toInt(0);
        r.minimumM = o.value("minimumM").toDouble(r.minimumM);
        r.maximumM = value("maximumM");
        r.featureM = value("featureM");
        r.toleranceM = value("elevationToleranceM");
        r.breaklineToleranceM = value("breaklineToleranceM");
        r.minimumBreaklineLengthM = value("minimumBreaklineLengthM");
        r.maxBreaklines = o.value("maxBreaklines").toInt(0);
        r.averagingM = value("averagingM");
        r.transition = o.value("transition").toDouble(r.transition);
        r.refineFeatures = o.value("refineFeatures").toBool(false);
        r.alignTerrain = o.value("alignTerrain").toBool(true);
        const QString mode = o.value("terrain").toString(), ref = o.value("reference").toString();
        r.terrain = mode == "off"      ? Terrain::Off
                    : mode == "legacy" ? Terrain::Legacy
                                       : Terrain::Adaptive;
        r.reference = ref == "direct" ? Reference::Direct
                      : ref == "full" ? Reference::Full
                                      : Reference::Averaged;
        return r;
    }

    // Version 1 stored distance controls in metres and combo choices as indices.
    /**
     * @brief Overlay version-1 generation-dialog controls onto a supplied settings value.
     * @param[in] c Borrowed legacy control object with metre distances and integer combo
     * selections, unchanged.
     * @param[in] r Initial settings copied by value; fields without legacy controls retain their
     * supplied values, except coarsenPasses resets to zero and maximum/breakline tolerance are
     * derived.
     * @param[in] extentM Domain extent in metres, used as extentM/100 when legacy feature size
     * is nonpositive; if not positive the fallback is 1 metre. Caller supplies finite meaningful
     * values.
     * @return Owned migrated settings. Nonpositive minimum becomes feature/4, maximum becomes
     * feature times coarsening factor, nonpositive elevation tolerance becomes absent and copies
     * to breakline tolerance. Legacy terrain without a positive tolerance switches off. No range
     * validation or file/project mutation.
     */
    static ResolutionSettings migrate(const QJsonObject &c, ResolutionSettings r, double extentM)
    {
        r.coarsenPasses = 0;
        double feature = c.value("meshCellSizeSpin").toDouble(r.featureM.value_or(r.minimumM));
        if (feature <= 0)
            feature = extentM > 0 ? extentM / 100.0 : 1.0;
        r.featureM = feature;
        r.minimumM = c.value("meshMinCellSizeSpin").toDouble(r.minimumM);
        if (r.minimumM <= 0)
            r.minimumM = feature / 4;
        r.maximumM = feature * c.value("meshCoarsenSpin").toDouble(1000.0);
        const double tol = c.value("meshTerrainTolSpin").toDouble(r.toleranceM.value_or(0));
        r.toleranceM = tol > 0 ? std::optional<double>(tol) : std::nullopt;
        r.breaklineToleranceM = r.toleranceM;
        r.refineFeatures = c.value("meshRefineFeaturesBox").toBool(r.refineFeatures);
        r.alignTerrain = c.value("meshTerrainBreaklinesBox").toBool(r.alignTerrain);
        r.maxBreaklines = c.value("meshMaxBreaklinesSpin").toInt(0);
        r.transition = c.value("meshSizeRatioSpin").toDouble(r.transition);
        const int mode = c.value("meshTerrainModeCombo").toInt(0),
                  ref = c.value("meshTerrainReferenceCombo").toInt(0);
        r.terrain = mode == 2 ? Terrain::Off : mode == 1 ? Terrain::Legacy : Terrain::Adaptive;
        r.reference = ref == 2   ? Reference::Direct
                      : ref == 1 ? Reference::Full
                                 : Reference::Averaged;
        // Legacy zero explicitly disabled refinement, unlike adaptive auto.
        if (r.terrain == Terrain::Legacy && !r.toleranceM)
            r.terrain = Terrain::Off;
        return r;
    }
};
}
#endif
