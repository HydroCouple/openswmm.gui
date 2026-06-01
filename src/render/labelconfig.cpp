/*!
 * \file   labelconfig.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "render/labelconfig.h"

#include <QJsonArray>

namespace OpenSWMM::Render {

QJsonObject LabelConfig::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("enabled")]      = enabled;
    if (!fieldName.isEmpty())
        j[QStringLiteral("field")]    = fieldName;
    j[QStringLiteral("fontFamily")]   = font.family();
    j[QStringLiteral("fontBold")]     = font.bold();
    j[QStringLiteral("fontItalic")]   = font.italic();
    j[QStringLiteral("fontSizePt")]   = fontSizePt;
    j[QStringLiteral("color")]        = color.name(QColor::HexArgb);
    j[QStringLiteral("haloEnabled")]  = haloEnabled;
    j[QStringLiteral("haloColor")]    = haloColor.name(QColor::HexArgb);
    j[QStringLiteral("haloRadiusPx")] = haloRadiusPx;
    j[QStringLiteral("placement")]    = int(placement);
    if (minScale > 0.0) j[QStringLiteral("minScale")] = minScale;
    if (maxScale > 0.0) j[QStringLiteral("maxScale")] = maxScale;
    // X.24 — background + priority
    if (backgroundEnabled)
        j[QStringLiteral("backgroundEnabled")] = true;
    if (backgroundColor   != QColor(255, 255, 255, 200))
        j[QStringLiteral("backgroundColor")]   = backgroundColor.name(QColor::HexArgb);
    if (!qFuzzyCompare(backgroundPaddingPx, 2.0))
        j[QStringLiteral("backgroundPaddingPx")] = backgroundPaddingPx;
    if (!qFuzzyCompare(backgroundRadiusPx, 3.0))
        j[QStringLiteral("backgroundRadiusPx")]  = backgroundRadiusPx;
    if (!priorityField.isEmpty())
        j[QStringLiteral("priorityField")]    = priorityField;
    return j;
}

void LabelConfig::fromJson(const QJsonObject &j)
{
    if (j.contains(QStringLiteral("enabled")))
        enabled = j.value(QStringLiteral("enabled")).toBool(false);
    if (j.contains(QStringLiteral("field")))
        fieldName = j.value(QStringLiteral("field")).toString();
    if (j.contains(QStringLiteral("fontFamily")))
        font.setFamily(j.value(QStringLiteral("fontFamily")).toString());
    if (j.contains(QStringLiteral("fontBold")))
        font.setBold(j.value(QStringLiteral("fontBold")).toBool(false));
    if (j.contains(QStringLiteral("fontItalic")))
        font.setItalic(j.value(QStringLiteral("fontItalic")).toBool(false));
    if (j.contains(QStringLiteral("fontSizePt")))
        fontSizePt = j.value(QStringLiteral("fontSizePt")).toDouble(9.0);
    if (j.contains(QStringLiteral("color"))) {
        const QColor c(j.value(QStringLiteral("color")).toString());
        if (c.isValid()) color = c;
    }
    if (j.contains(QStringLiteral("haloEnabled")))
        haloEnabled = j.value(QStringLiteral("haloEnabled")).toBool(false);
    if (j.contains(QStringLiteral("haloColor"))) {
        const QColor c(j.value(QStringLiteral("haloColor")).toString());
        if (c.isValid()) haloColor = c;
    }
    if (j.contains(QStringLiteral("haloRadiusPx")))
        haloRadiusPx = j.value(QStringLiteral("haloRadiusPx")).toDouble(1.5);
    if (j.contains(QStringLiteral("placement")))
        placement = static_cast<Placement>(j.value(QStringLiteral("placement")).toInt(0));
    minScale = j.value(QStringLiteral("minScale")).toDouble(0.0);
    maxScale = j.value(QStringLiteral("maxScale")).toDouble(0.0);

    if (j.contains(QStringLiteral("backgroundEnabled")))
        backgroundEnabled = j.value(QStringLiteral("backgroundEnabled")).toBool(false);
    if (j.contains(QStringLiteral("backgroundColor"))) {
        const QColor c(j.value(QStringLiteral("backgroundColor")).toString());
        if (c.isValid()) backgroundColor = c;
    }
    if (j.contains(QStringLiteral("backgroundPaddingPx")))
        backgroundPaddingPx = j.value(QStringLiteral("backgroundPaddingPx")).toDouble(2.0);
    if (j.contains(QStringLiteral("backgroundRadiusPx")))
        backgroundRadiusPx  = j.value(QStringLiteral("backgroundRadiusPx")).toDouble(3.0);
    if (j.contains(QStringLiteral("priorityField")))
        priorityField = j.value(QStringLiteral("priorityField")).toString();
}

} // namespace OpenSWMM::Render
