/*!
 * \file   swmmpatternpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [PATTERNS].
 *
 * Scalar surface is name-only today: the engine ships `swmm_pattern_add`
 * (write-only on creation), `swmm_pattern_set_factors` (write-only), and
 * `swmm_pattern_count`/`_index`/`_id`. There is no `swmm_pattern_get_type`
 * or `swmm_pattern_get_factors` accessor yet — file engine gap
 * `DA-ENG-05`. Until then the adapter exposes only the name; the BQ
 * PatternEditor lives behind an "Edit…" button that ships in DA.3.
 */

#ifndef SWMMPATTERNPROPERTYADAPTER_H
#define SWMMPATTERNPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMPatternPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;
};

#endif // SWMMPATTERNPROPERTYADAPTER_H
