// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMM_MESH_RESOLUTION_H
#define OPENSWMM_MESH_RESOLUTION_H

#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>

namespace mesh {

// Shared by project settings, Preferences and the generation dialog.
// Physical lengths are SI; absence means no maximum, inherit minimum,
// automatic tolerance, or match-minimum averaging respectively.
struct ResolutionSettings
{
    double minimumM = 6.096;
    std::optional<double> maximumM, featureM, toleranceM, breaklineToleranceM, averagingM, minimumBreaklineLengthM;
    int maxBreaklines=0;
    double transition = 2.0;
    bool refineFeatures = false, alignTerrain = true;
    enum class Terrain { Adaptive, Legacy, Off } terrain = Terrain::Adaptive;
    enum class Reference { Averaged, Full, Direct } reference = Reference::Averaged;

    QString validate() const
    {
        auto positive=[](double v) { return std::isfinite(v) && v>0 && v<=1e12; };
        if (!positive(minimumM)) return QStringLiteral("Minimum target cell size must be a positive finite length.");
        for (const auto &v:{maximumM,featureM,toleranceM,breaklineToleranceM,averagingM,minimumBreaklineLengthM})
            if (v && !positive(*v)) return QStringLiteral("Specified sizes and tolerances must be positive finite lengths.");
        if (maxBreaklines<0 || maxBreaklines>10000000) return QStringLiteral("Terrain breakline count is out of range.");
        if (maximumM && *maximumM<minimumM) return QStringLiteral("Maximum target cell size must be at least the minimum target size.");
        if (refineFeatures && featureM && *featureM<minimumM) return QStringLiteral("Feature target size must be at least the minimum target size.");
        if (refineFeatures && featureM && maximumM && *featureM>*maximumM)
            return QStringLiteral("Feature target size exceeds the maximum target size. Increase the maximum or reduce the feature target.");
        if (!std::isfinite(transition) || transition<1.05 || transition>2.0)
            return QStringLiteral("Size transition rate must be between 1.05 and 2.");
        return {};
    }

    QJsonObject toJson() const
    {
        auto value=[](std::optional<double> v) { return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null); };
        return {{"minimumM",minimumM},{"maximumM",value(maximumM)}, {"featureM",value(featureM)},
            {"elevationToleranceM",value(toleranceM)},{"breaklineToleranceM",value(breaklineToleranceM)},
            {"averagingM",value(averagingM)},{"minimumBreaklineLengthM",value(minimumBreaklineLengthM)},{"maxBreaklines",maxBreaklines},{"transition",transition},{"refineFeatures",refineFeatures},
            {"alignTerrain",alignTerrain},
            {"terrain",terrain==Terrain::Adaptive?"adaptive":terrain==Terrain::Legacy?"legacy":"off"},
            {"reference",reference==Reference::Averaged?"averaged":reference==Reference::Full?"full":"direct"}};
    }

    static ResolutionSettings fromJson(const QJsonObject &o)
    {
        ResolutionSettings r;
        auto value=[&](const char *key)->std::optional<double> {
            const auto v=o.value(QLatin1String(key));
            return v.isDouble()?std::optional<double>(v.toDouble()):std::nullopt;
        };
        r.minimumM=o.value("minimumM").toDouble(r.minimumM);
        r.maximumM=value("maximumM"); r.featureM=value("featureM");
        r.toleranceM=value("elevationToleranceM"); r.breaklineToleranceM=value("breaklineToleranceM");
        r.minimumBreaklineLengthM=value("minimumBreaklineLengthM"); r.maxBreaklines=o.value("maxBreaklines").toInt(0);
        r.averagingM=value("averagingM"); r.transition=o.value("transition").toDouble(r.transition);
        r.refineFeatures=o.value("refineFeatures").toBool(false); r.alignTerrain=o.value("alignTerrain").toBool(true);
        const QString mode=o.value("terrain").toString(), ref=o.value("reference").toString();
        r.terrain=mode=="off"?Terrain::Off:mode=="legacy"?Terrain::Legacy:Terrain::Adaptive;
        r.reference=ref=="direct"?Reference::Direct:ref=="full"?Reference::Full:Reference::Averaged;
        return r;
    }

    // Version 1 stored distance controls in metres and combo choices as indices.
    static ResolutionSettings migrate(const QJsonObject &c, ResolutionSettings r, double extentM)
    {
        double feature=c.value("meshCellSizeSpin").toDouble(r.featureM.value_or(r.minimumM));
        if (feature<=0) feature=extentM>0?extentM/100.0:1.0;
        r.featureM=feature;
        r.minimumM=c.value("meshMinCellSizeSpin").toDouble(r.minimumM);
        if (r.minimumM<=0) r.minimumM=feature/4;
        r.maximumM=feature*c.value("meshCoarsenSpin").toDouble(1000.0);
        const double tol=c.value("meshTerrainTolSpin").toDouble(r.toleranceM.value_or(0));
        r.toleranceM=tol>0?std::optional<double>(tol):std::nullopt;
        r.breaklineToleranceM=r.toleranceM;
        r.refineFeatures=c.value("meshRefineFeaturesBox").toBool(r.refineFeatures);
        r.alignTerrain=c.value("meshTerrainBreaklinesBox").toBool(r.alignTerrain);
        r.maxBreaklines=c.value("meshMaxBreaklinesSpin").toInt(0);
        r.transition=c.value("meshSizeRatioSpin").toDouble(r.transition);
        const int mode=c.value("meshTerrainModeCombo").toInt(0), ref=c.value("meshTerrainReferenceCombo").toInt(0);
        r.terrain=mode==2?Terrain::Off:mode==1?Terrain::Legacy:Terrain::Adaptive;
        r.reference=ref==2?Reference::Direct:ref==1?Reference::Full:Reference::Averaged;
        // Legacy zero explicitly disabled refinement, unlike adaptive auto.
        if (r.terrain==Terrain::Legacy && !r.toleranceM) r.terrain=Terrain::Off;
        return r;
    }
};
}
#endif
