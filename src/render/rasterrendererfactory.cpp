/**
 * @file      rasterrendererfactory.cpp
 * @brief     of RendererFactory::makeRenderer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/rasterrendererfactory.h"

#include "render/irasterrenderer.h"
#include "render/renderers/graduatedrasterrenderer.h"
#include "render/renderers/multibandcolorrenderer.h"
#include "render/renderers/palettedrasterrenderer.h"
#include "render/renderers/singlebandpseudocolorrenderer.h"

namespace OpenSWMM::Render
{

std::unique_ptr<IRasterRenderer> makeRasterRenderer(const QJsonObject &j)
{
    const QString id = j.value(QStringLiteral("id")).toString();
    std::unique_ptr<IRasterRenderer> r;
    if (id == QLatin1String("graduatedraster"))
        r = std::make_unique<GraduatedRasterRenderer>();
    else if (id == QLatin1String("palettedraster"))
        r = std::make_unique<PalettedRasterRenderer>();
    else if (id == QLatin1String("multibandcolor"))
        r = std::make_unique<MultiBandColorRenderer>();
    else if (id == QLatin1String("singlebandpseudocolor"))
        r = std::make_unique<SingleBandPseudoColorRenderer>();
    else
        return nullptr;
    r->fromJson(j);
    return r;
}

} // namespace OpenSWMM::Render
