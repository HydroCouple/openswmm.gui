/*!
 * \file   culvertcodes.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 0 of docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md.
 * Codes/groups/labels transcribed 1:1 from the legacy selector tree
 * (`SWMM-GUI/Epaswmm5/Dculvert.dfm`, TreeView1 NodeData). Two
 * deviations from the legacy text, both deliberate:
 *   - code 11 reads "0 deg. wingwall flares" (the legacy string lost
 *     the leading "0"; HDS-5 chart 11 is the straight-sides 0-deg case);
 *   - em-dashes/quotes normalised to ASCII (encoding hygiene rule).
 */

#include "ui/properties/culvertcodes.h"

#include <QCoreApplication>

const std::vector<CulvertCodeInfo> &culvertCodes()
{
    static const std::vector<CulvertCodeInfo> s_codes = {
        { 1, "Circular Concrete", "Square edge with headwall" },
        { 2, "Circular Concrete", "Groove end with headwall" },
        { 3, "Circular Concrete", "Groove end projecting" },
        { 4, "Circular Corrugated Metal Pipe", "Headwall" },
        { 5, "Circular Corrugated Metal Pipe", "Mitered to slope" },
        { 6, "Circular Corrugated Metal Pipe", "Projecting" },
        { 7, "Circular, Beveled Ring Entrance", "45 deg. bevels" },
        { 8, "Circular, Beveled Ring Entrance", "33.7 deg. bevels" },
        { 9, "Rectangular Box; Flared Wingwalls",
             "30-75 deg. wingwall flares" },
        {10, "Rectangular Box; Flared Wingwalls",
             "90 or 15 deg. wingwall flares" },
        {11, "Rectangular Box; Flared Wingwalls",
             "0 deg. wingwall flares (straight sides)" },
        {12, "Rectangular Box; Flared Wingwalls and Top Edge Bevel",
             "45 deg flare; 0.43D top edge bevel" },
        {13, "Rectangular Box; Flared Wingwalls and Top Edge Bevel",
             "18-33.7 deg. flare; 0.083D top edge bevel" },
        {14, "Rectangular Box, 90-deg Headwall, Chamfered / Beveled Inlet Edges",
             "Chamfered 3/4-in." },
        {15, "Rectangular Box, 90-deg Headwall, Chamfered / Beveled Inlet Edges",
             "Beveled 1/2-in/ft at 45 deg (1:1)" },
        {16, "Rectangular Box, 90-deg Headwall, Chamfered / Beveled Inlet Edges",
             "Beveled 1-in/ft at 33.7 deg (1:1.5)" },
        {17, "Rectangular Box, Skewed Headwall, Chamfered / Beveled Inlet Edges",
             "3/4\" chamfered edge, 45 deg skewed headwall" },
        {18, "Rectangular Box, Skewed Headwall, Chamfered / Beveled Inlet Edges",
             "3/4\" chamfered edge, 30 deg skewed headwall" },
        {19, "Rectangular Box, Skewed Headwall, Chamfered / Beveled Inlet Edges",
             "3/4\" chamfered edge, 15 deg skewed headwall" },
        {20, "Rectangular Box, Skewed Headwall, Chamfered / Beveled Inlet Edges",
             "45 deg beveled edge, 10-45 deg skewed headwall" },
        {21, "Rectangular Box, Non-offset Flared Wingwalls, 3/4\" Chamfer at Top of Inlet",
             "45 deg (1:1) wingwall flare" },
        {22, "Rectangular Box, Non-offset Flared Wingwalls, 3/4\" Chamfer at Top of Inlet",
             "8.4 deg (3:1) wingwall flare" },
        {23, "Rectangular Box, Non-offset Flared Wingwalls, 3/4\" Chamfer at Top of Inlet",
             "18.4 deg (3:1) wingwall flare, 30 deg inlet skew" },
        {24, "Rectangular Box, Offset Flared Wingwalls, Beveled Edge at Inlet Top",
             "45 deg (1:1) flare, 0.042D top edge bevel" },
        {25, "Rectangular Box, Offset Flared Wingwalls, Beveled Edge at Inlet Top",
             "33.7 deg (1.5:1) flare, 0.083D top edge bevel" },
        {26, "Rectangular Box, Offset Flared Wingwalls, Beveled Edge at Inlet Top",
             "18.4 deg (3:1) flare, 0.083D top edge bevel" },
        {27, "Corrugated Metal Box", "90 deg headwall" },
        {28, "Corrugated Metal Box", "Thick wall projecting" },
        {29, "Corrugated Metal Box", "Thin wall projecting" },
        {30, "Horizontal Ellipse Concrete", "Square edge with headwall" },
        {31, "Horizontal Ellipse Concrete", "Grooved end with headwall" },
        {32, "Horizontal Ellipse Concrete", "Grooved end projecting" },
        {33, "Vertical Ellipse Concrete", "Square edge with headwall" },
        {34, "Vertical Ellipse Concrete", "Grooved end with headwall" },
        {35, "Vertical Ellipse Concrete", "Grooved end projecting" },
        {36, "Pipe Arch, 18\" Corner Radius, Corrugated Metal",
             "90 deg headwall" },
        {37, "Pipe Arch, 18\" Corner Radius, Corrugated Metal",
             "Mitered to slope" },
        {38, "Pipe Arch, 18\" Corner Radius, Corrugated Metal",
             "Projecting" },
        {39, "Pipe Arch, 18\" Corner Radius, Corrugated Metal",
             "Projecting" },
        {40, "Pipe Arch, 18\" Corner Radius, Corrugated Metal",
             "No bevels" },
        {41, "Pipe Arch, 18\" Corner Radius, Corrugated Metal",
             "33.7 deg bevels" },
        {42, "Pipe Arch, 31\" Corner Radius, Corrugated Metal",
             "Projecting" },
        {43, "Pipe Arch, 31\" Corner Radius, Corrugated Metal",
             "No bevels" },
        {44, "Pipe Arch, 31\" Corner Radius, Corrugated Metal",
             "33.7 deg. bevels" },
        {45, "Arch, Corrugated Metal", "90 deg headwall" },
        {46, "Arch, Corrugated Metal", "Mitered to slope" },
        {47, "Arch, Corrugated Metal", "Thin wall projecting" },
        {48, "Circular Culvert", "Smooth tapered inlet throat" },
        {49, "Circular Culvert", "Rough tapered inlet throat" },
        {50, "Elliptical Inlet Face", "Tapered inlet, beveled edges" },
        {51, "Elliptical Inlet Face", "Tapered inlet, square edges" },
        {52, "Elliptical Inlet Face", "Tapered inlet, thin edge projecting" },
        {53, "Rectangular Concrete", "Tapered inlet throat" },
        {54, "Rectangular Concrete", "Side tapered, less favorable edges" },
        {55, "Rectangular Concrete", "Side tapered, more favorable edges" },
        {56, "Rectangular Concrete", "Slope tapered, less favorable edges" },
        {57, "Rectangular Concrete", "Slope tapered, more favorable edges" },
    };
    return s_codes;
}

QString culvertCodeLabel(int code)
{
    if (code <= 0)
        return QCoreApplication::translate("CulvertCodes", "(none)");
    for (const CulvertCodeInfo &c : culvertCodes()) {
        if (c.code == code)
            return QStringLiteral("%1) %2: %3")
                       .arg(code)
                       .arg(QString::fromUtf8(c.group),
                            QString::fromUtf8(c.label));
    }
    return QCoreApplication::translate("CulvertCodes", "Code %1").arg(code);
}
