/*!
 * \file   swmm_category.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Standalone definition of the SWMM-element Category enum used by
 *         per-Category APIs across layers, sublayers, dialogs, and tests.
 *
 *         Carved out of swmmmodellayer.h so leaf tests can depend on the
 *         enum alone without dragging in the full layer's engine and Qt
 *         GUI dependencies. swmmmodellayer.h aliases the enum into the
 *         SWMMModelLayer class via `using` so existing call-sites that
 *         write SWMMModelLayer::Category continue to compile unchanged.
 */
#ifndef OPENSWMMVIS_LAYERS_SWMM_CATEGORY_H
#define OPENSWMMVIS_LAYERS_SWMM_CATEGORY_H

namespace OpenSWMMVis {

/*!
 * \enum SwmmCategory
 * \brief Stable ordinal categorising every SWMM-element kind that has
 *        per-kind styling, per-kind tree rows, or per-kind result paint.
 *
 *        Order is preserved across releases so JSON files round-trip and
 *        tree-view expansion state survives schema updates.
 */
enum SwmmCategory {
    CatJunctions     = 0,
    CatOutfalls,
    CatStorage,
    CatDividers,
    CatConduits,
    CatPumps,
    CatOrifices,
    CatWeirs,
    CatOutlets,
    CatSubcatchments,
    CatRainGages,
    NumCategories
};

} // namespace OpenSWMMVis

#endif // OPENSWMMVIS_LAYERS_SWMM_CATEGORY_H
