/*!
 * \file   lidlayerdiagram.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  LID control layer-stack diagram.
 *
 * Slice SP.6 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * Takes a plain input struct rather than a LidControlProvider* so the drawing
 * code carries no dependency on lid/ — the dialog fills the struct from the
 * provider. Keeps the test link chain to sectiondiagram.cpp alone.
 *
 * Which layers exist is a function of the LID type (a rain barrel has no soil,
 * a green roof has a drainmat instead of storage); see lidLayersFor().
 *
 * The engine exposes LID setters but no getters, so a control loaded from a
 * file has no readable layer values. Layers whose thickness is unknown are
 * drawn hatched with an em-dash label rather than silently shown as zero.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_LIDLAYERDIAGRAM_H
#define OPENSWMMVIS_SECTIONVIEW_LIDLAYERDIAGRAM_H

#include <QString>
#include <QVector>

#include "ui/sectionview/sectiondiagram.h"

namespace openswmmvis::sectionview {

/*! Which physical layer a row of the stack represents. */
enum class LidLayer { Surface, Pavement, Soil, Storage, Drainmat };

/*! LID type codes, matching [LID_CONTROLS] / LidControlProvider::type(). */
enum class LidType {
    BioCell        = 0,
    RainGarden     = 1,
    GreenRoof      = 2,
    InfilTrench    = 3,
    PermPavement   = 4,
    RainBarrel     = 5,
    RooftopDisconn = 6,
    VegSwale       = 7
};

/*! Layer stack for \p type, top to bottom. */
[[nodiscard]] QVector<LidLayer> lidLayersFor(LidType type);

/*! True when \p type drains through an underdrain the user can configure. */
[[nodiscard]] bool lidHasDrain(LidType type);

/*! Display name for a layer ("Surface", "Soil", …). */
[[nodiscard]] QString lidLayerName(LidLayer layer);

/*! Everything the diagram needs about the control being edited. */
struct LidDiagramInput
{
    QString name;
    LidType type = LidType::BioCell;

    // Layer values as currently entered. Thicknesses are in project length
    // units; a non-positive thickness renders as an unknown layer.
    double surfaceStorage   = 0.0;   //!< Berm / ponding depth.
    double surfaceRoughness = 0.0;
    double surfaceSlope     = 0.0;
    double soilThickness    = 0.0;
    double soilPorosity     = 0.0;
    double soilConductivity = 0.0;
    double storageThickness = 0.0;
    double storageVoidFrac  = 0.0;
    double storageSeepage   = 0.0;
    double drainCoeff       = 0.0;
    double drainExponent    = 0.0;
    double drainOffset      = 0.0;

    /*! Pavement + drainmat have engine setters but no editor fields yet, so
     *  their thickness is always unknown today. Wired when those tabs land. */
    double pavementThickness = 0.0;
    double drainmatThickness = 0.0;

    QString lengthLabel = QStringLiteral("m");

    /*! Layer to outline as the active one (mirrors the editor's current tab).
     *  Pass std::nullopt-equivalent by leaving `hasActiveLayer` false. */
    LidLayer activeLayer    = LidLayer::Surface;
    bool     hasActiveLayer = false;
};

/*!
 * \brief Build the layer-stack diagram for one LID control.
 *
 * Layer boxes are proportional to their thickness where known, with a floor so
 * a thin layer stays visible and an unknown layer still occupies a row.
 */
[[nodiscard]] SectionDiagramModel buildLidLayerDiagram(const LidDiagramInput &in);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_LIDLAYERDIAGRAM_H
