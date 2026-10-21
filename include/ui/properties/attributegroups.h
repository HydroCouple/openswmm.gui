#ifndef OPENSWMM_ATTRIBUTE_GROUPS_H
#define OPENSWMM_ATTRIBUTE_GROUPS_H

#include <QCoreApplication>
#include <QString>

namespace openswmmvis {
inline constexpr int AttributeGroupRole = Qt::UserRole + 65;

// Accept either a Q_PROPERTY name or the table's engine-access tag.
inline QString attributeGroup(QString key)
{
    for (const auto &prefix : {QStringLiteral("node_"), QStringLiteral("link_"),
                               QStringLiteral("subcatch_")})
        if (key.startsWith(prefix)) { key.remove(0, prefix.size()); break; }
    key.remove(QLatin1Char('_'));
    key = key.toLower();
    auto label = [](const char *text) {
        return QCoreApplication::translate("AttributeGroups", text);
    };
    if (key == "statmaxdepth") return label("Output Summary / Node Depth");
    if (key == "statmaxoverflow" || key == "statvolflooded" || key == "stattimeflooded")
        return label("Output Summary / Node Flooding");
    if (key.startsWith("statpump")) return label("Output Summary / Pumping");
    if (key == "statsurchargetime") return label("Output Summary / Surcharge");
    if (key == "statprecip" || key == "stattotalprecip" || key == "statrunoffvol"
        || key == "statrunoffvolume" || key == "statmaxrunoff" || key == "statpeakrunoff")
        return label("Output Summary / Runoff");
    if (key == "statmaxflow" || key == "statmaxvelocity" || key == "statmaxfilling"
        || key == "statvolflow") return label("Output Summary / Link Flow");
    if (key == "coupleable" || key == "couplingtype" || key == "2dcouplingtype")
        return label("2D Coupling / Setup");
    if (key == "couplingspread" || key == "couplingtailwaterrelax")
        return label("2D Coupling / Outfall");
    if (key == "couplinglaw" || key == "couplingcoeffs" || key == "couplingcw"
        || key == "couplingcsw" || key == "couplingcoi" || key == "couplingcoo")
        return label("2D Coupling / Exchange");
    if (key.startsWith("coupling")) return label("2D Coupling / Geometry");
    return {};
}
} // namespace openswmmvis
#endif
