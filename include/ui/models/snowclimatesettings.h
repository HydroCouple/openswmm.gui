// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <openswmm/engine/openswmm_snow.h>
#include <QMap>
#include <QString>
#include <QStringList>
#include <array>

namespace openswmmvis::ui {
struct UebFieldSpec { const char* key; const char* label; const char* group; double minimum, maximum; };
inline constexpr UebFieldSpec uebFields[] = {
    {"Z0", "Roughness length (m)", "Turbulence", .000001, 10},
    {"ZM", "Measurement height (m)", "Turbulence", .000001, 1000},
    {"FSTAB", "Stability weighting", "Turbulence", 0, 1},
    {"RHO_S", "Snow density (kg/m³)", "Snow and soil", 1, 916.999},
    {"RHO_G", "Soil density (kg/m³)", "Snow and soil", 1, 10000},
    {"CG", "Soil heat capacity (kJ/kg/K)", "Snow and soil", .000001, 100},
    {"DE", "Active soil depth (m)", "Snow and soil", .000001, 100},
    {"LC", "Liquid retention fraction", "Snow and soil", 0, 1},
    {"KSAT", "Saturated snow drainage (m/h)", "Snow and soil", 0, 1e6},
    {"KS", "Equilibrium conductance (m/h)", "Temperature scheme", .000001, 100},
    {"LAMBDA_S", "Snow conductivity (kJ/m/K/h)", "Temperature scheme", .000001, 1000},
    {"LAMBDA_G", "Soil conductivity (kJ/m/K/h)", "Temperature scheme", .000001, 1000},
    {"OMEGA_LF", "Low frequency (rad/h)", "Temperature scheme", .000001, 100},
    {"R", "Damping amplitude", "Temperature scheme", .000001, 100},
    {"TR", "All-rain temperature (°C)", "Precipitation", -99, 70},
    {"TB", "All-snow temperature (°C)", "Precipitation", -100, 69},
    {"EPSILON", "Snow emissivity", "Albedo and radiation", 0, 1},
    {"ABG", "Ground albedo", "Albedo and radiation", 0, 1},
    {"ALPHA_VO", "Fresh visible albedo", "Albedo and radiation", 0, 1},
    {"ALPHA_IRO", "Fresh infrared albedo", "Albedo and radiation", 0, 1},
    {"DNEWS", "Fresh-snow reset depth (m)", "Albedo and radiation", 0, 10},
    {"R3", "Dust ageing coefficient", "Albedo and radiation", 0, 100},
    {"CV", "Visible albedo ageing coefficient", "Albedo and radiation", 0, 1},
    {"CIR", "Infrared albedo ageing coefficient", "Albedo and radiation", 0, 1},
    {"BC_A", "Bristow–Campbell maximum transmissivity", "Albedo and radiation", .000001, 1},
    {"BC_C", "Bristow–Campbell exponent", "Albedo and radiation", .000001, 100}
};

// Staged model shared by the climatology form and its UEB parameter editor.
// Neither view writes the engine until the parent dialog accepts the draft.
struct SnowClimateSettings {
    int model = 0;
    double step = 3600;
    SWMM_RadiationConfig radiation{};
    SWMM_ClimateCloudConfig cloud{};
    QString cloudSeries;
    std::array<QString, 3> sources;
    QMap<QString, QString> options;
    std::array<double, 12> ranges{};

    bool load(SWMM_Engine engine) {
        if (swmm_snow_get_model(engine, &model) != SWMM_OK || swmm_snow_get_step(engine, &step) != SWMM_OK ||
            swmm_climate_get_radiation(engine, &radiation) != SWMM_OK ||
            swmm_ueb_get_monthly_ranges(engine, ranges.data()) != SWMM_OK) return false;
        char value[8192];
        if (swmm_climate_get_cloud(engine,&cloud,value,sizeof(value)) != SWMM_OK) return false;
        cloudSeries=QString::fromUtf8(value);
        for (int i = 0; i < 3; ++i) {
            if (swmm_climate_get_radiation_source(engine, i, value, sizeof(value)) != SWMM_OK) return false;
            sources[i] = QString::fromUtf8(value);
        }
        QStringList keys{QStringLiteral("SURFACE_TEMP"), QStringLiteral("PRECIP_SPLIT")};
        for (const auto& f : uebFields) keys << QString::fromLatin1(f.key);
        for (const auto& key : keys) {
            if (swmm_ueb_option_get(engine, key.toUtf8().constData(), value, sizeof(value)) != SWMM_OK) return false;
            options[key] = QString::fromUtf8(value);
        }
        return true;
    }
    QString temperatureRows() const {
        QStringList rows{QStringLiteral("SNOWMELT_MODEL ") + (model == 1 ? QStringLiteral("UEB") : QStringLiteral("DEGREE_DAY")),
                         QStringLiteral("SNOW_STEP ") + QString::number(step, 'g', 17)};
        if (radiation.explicit_site)
            rows << QStringLiteral("SITE %1 %2 %3 %4").arg(radiation.latitude, 0, 'g', 17).arg(radiation.longitude, 0, 'g', 17)
                .arg(radiation.timezone, 0, 'g', 17).arg(radiation.elevation_m, 0, 'g', 17);
        for (int i = 0; i < 2; ++i) {
            if (!(i ? radiation.longwave_authored : radiation.shortwave_authored)) continue;
            const int mode = i ? radiation.longwave_mode : radiation.shortwave_mode;
            rows << (i ? QStringLiteral("LONGWAVE ") : QStringLiteral("SHORTWAVE ")) +
                (mode == 1 ? QStringLiteral("TIMESERIES \"") + sources[i] + '"' : mode == 2 ? QStringLiteral("FILE") : QStringLiteral("COMPUTED"));
        }
        if (!sources[2].isEmpty()) rows << QStringLiteral("MET_FILE \"") + sources[2] + '"';
        return rows.join('\n');
    }
    QString optionRows() const {
        QStringList rows;
        for (auto it = options.cbegin(); it != options.cend(); ++it) rows << it.key() + ' ' + it.value();
        for (int i = 0; i < 12; ++i) if (ranges[i] > 0)
            rows << QStringLiteral("BC_MONTHLY %1 %2").arg(i+1).arg(ranges[i], 0, 'g', 17);
        return rows.join('\n');
    }
    QString signature() const {
        return temperatureRows() + '\n' + optionRows() + QStringLiteral("\nCLOUD %1 %2 %3 %4 %5 %6")
            .arg(cloud.mode).arg(cloud.fraction,0,'g',17).arg(cloud.shortwave_k,0,'g',17)
            .arg(cloud.shortwave_n,0,'g',17).arg(cloud.longwave_k,0,'g',17).arg(cloudSeries);
    }
    QString apply(SWMM_Engine engine) const {
        for (const auto& source : sources) if (source.contains('"') || source.contains('\n') || source.contains('\r'))
            return QStringLiteral("Source names and paths cannot contain quotes or line breaks.");
        char error[1024]{};
        if (swmm_snow_apply_configuration_with_cloud(engine, temperatureRows().toUtf8().constData(),
                optionRows().toUtf8().constData(), &cloud, cloudSeries.toUtf8().constData(), error, sizeof(error)) != SWMM_OK)
            return *error ? QString::fromUtf8(error) : QStringLiteral("The snow configuration could not be changed in the current model state.");
        return {};
    }
};
} // namespace openswmmvis::ui
