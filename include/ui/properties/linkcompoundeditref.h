/*!
 * \file   linkcompoundeditref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice SC.1 — Compound-attribute cell value for the link Property
 * Browser. Mirror of `NodeCompoundEditRef` (Slice DB.2) for link-side
 * compound concepts: cross section (shape + geoms + barrels), culvert
 * code (FHWA chart number), and inlet usage (`[INLETS]` per-conduit
 * rows). Each of these legacy SWMM concepts has its own dedicated
 * sub-dialog page hosted by `LinkCompoundEditDialog`.
 *
 * Registered as a Qt metatype so QPropertyModel can store it via
 * `QVariant::fromValue(...)` and the matching custom editor creator
 * (`LinkCompoundEditButton`) is instantiated by the delegate when the
 * cell goes into edit mode. A `QMetaType` string converter is also
 * installed so the read-only display state shows the `summary`
 * string rather than `QVariant<LinkCompoundEditRef>`.
 */

#ifndef LINKCOMPOUNDEDITREF_H
#define LINKCOMPOUNDEDITREF_H

#include <QMetaType>
#include <QString>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

class SWMMModelLayer;

/*! Identifies one editable compound attribute on a single link. */
struct LinkCompoundEditRef
{
    /*! Which legacy SWMM concept this cell wraps. Order matches the
     *  visual layout of `ConduitProps` rows 5/19/20 in
     *  `SWMM-GUI/Epaswmm5/objprops.txt` (Shape, Culvert Code, Inlets). */
    enum Kind {
        XSection = 0,  ///< `[XSECTIONS]` per-link shape + geom1..4 + barrels
        CulvertCode,   ///< `[CULVERT]` per-link FHWA chart number
        InletUsage,    ///< `[INLETS]` per-conduit usage rows (placeholder)
    };

    SWMM_Engine     engine   = nullptr;  ///< Engine handle (borrow, not owned)
    QString         linkName;            ///< Owning link id
    Kind            kind     = XSection;
    QString         summary;             ///< Short text shown in the cell
    /*! Owning model layer (borrow). Slice SC.1 — needed so the dialog's
     *  pages can route writes through `applyLinkXsect` /
     *  `applyLinkCulvertCode` so the Map + Attribute Table refresh via
     *  `attributeChanged(linkName)` without a second source of truth. */
    SWMMModelLayer *layer    = nullptr;

    bool operator==(const LinkCompoundEditRef &other) const noexcept
    {
        return engine == other.engine && linkName == other.linkName
               && kind == other.kind && summary == other.summary
               && layer == other.layer;
    }
};

Q_DECLARE_METATYPE(LinkCompoundEditRef)

/*! Install the `LinkCompoundEditRef → QString` metatype converter so
 *  the read-only DisplayRole path on QPropertyModel renders `summary`
 *  in the cell when the user isn't actively editing. Idempotent — safe
 *  to call multiple times from different initialisers. */
void registerLinkCompoundEditRefConverter();

#endif // LINKCOMPOUNDEDITREF_H
