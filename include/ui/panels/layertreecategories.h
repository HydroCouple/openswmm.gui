/*!
 * \file   layertreecategories.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Pure category-bucketing helpers extracted from layertreepanel.cpp
 *         (Slice LTR-2026-05-30) so the mapping can be unit-tested headlessly
 *         without standing up a MapCanvas / OpenSWMMVisLayer object graph.
 *
 *         The header has no Qt dependency and no link dependency on
 *         layers/openswmmvislayer.h. Layer types are passed as plain int
 *         (matching the underlying OpenSWMMVisLayer::OpenSWMMVisLayerType
 *         enum values, which are stable across releases per
 *         openswmmvislayer.h's serialization contract). The mirror enum
 *         LayerTypeOrdinal below lets test code reference the same ordinals
 *         without pulling Qt — a static_assert in the .cpp checks that the
 *         mirror stays in lockstep with the source.
 */
#ifndef OPENSWMMVIS_UI_PANELS_LAYERTREECATEGORIES_H
#define OPENSWMMVIS_UI_PANELS_LAYERTREECATEGORIES_H

namespace openswmmvis::ui {

/*!
 * \enum CategoryId
 * \brief Top-level layer-tree groups.
 *
 *        Order is the default display order. Categories with no layers
 *        are hidden by rebuildCategories() in layertreepanel.cpp. The
 *        user can permute the order via the right-click "Move Category
 *        Up/Down" menu — that permutation is stored in
 *        LayerTreeModel::m_categoryDisplayOrder, not here.
 */
enum CategoryId {
    CatSwmm = 0,
    CatMeshes,
    CatSwmm1DOutputs,
    CatSwmm2DOutputs,
    CatFeatureLayers,
    CatRasterLayers,
    CatBasemaps,
    CatTables,
    CatCount
};

/*!
 * \struct CategoryInfo
 * \brief User-visible label + Qt resource alias for a category.
 *        Returned by categoryInfo(); the strings are static (resource
 *        aliases under :/swmmvis/ are compile-time constants).
 */
struct CategoryInfo {
    const char *name;
    const char *iconAlias;
};

/*!
 * \enum LayerTypeOrdinal
 * \brief Mirror of OpenSWMMVisLayer::OpenSWMMVisLayerType — same ordinals,
 *        no Qt include cost. Used by tests so they can call
 *        categoryForLayerType() without pulling openswmmvislayer.h.
 *
 *        The .cpp static_asserts this stays in lockstep with the source
 *        enum, so adding a new layer type without updating both will fail
 *        the build instead of silently mis-bucketing.
 */
enum class LayerTypeOrdinal : int {
    SWMMDefaultLayer            = 0,
    SWMMModelLayer              = 1,
    SWMMResultsLayer            = 2,
    SWMMGISLayer                = 3,
    SWMMVectorLayer             = 4,
    SWMMRasterLayer             = 5,
    SWMMImageryLayer            = 6,
    SWMMTabularDataLayer        = 7,
    SWMMTabularyTimeSeriesLayer = 8,
    SWMMSubProjectLayer         = 9,
    SWMMWMSLayer                = 10,
    SWMMWMTSLayer               = 11,
    SWMM2DMeshLayer             = 12,
    SWMM2DResultsLayer          = 13,
    SWMMAnnotationLayer         = 14,
};

/*!
 * \brief Maps a layer-type ordinal to its tree category.
 *
 *        Unrecognised ordinals return CatFeatureLayers (matches the
 *        runtime fall-back behaviour, but without the qWarning() that the
 *        full layertreepanel.cpp version emits — a headless helper has
 *        nowhere sensible to log).
 */
[[nodiscard]] CategoryId categoryForLayerType(int layerTypeOrdinal);

/*! \brief Convenience overload taking the typed ordinal directly. */
[[nodiscard]] inline CategoryId categoryForLayerType(LayerTypeOrdinal t)
{
    return categoryForLayerType(static_cast<int>(t));
}

/*! \brief Returns the display label + icon alias for a category. */
[[nodiscard]] CategoryInfo categoryInfo(CategoryId id);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_LAYERTREECATEGORIES_H
