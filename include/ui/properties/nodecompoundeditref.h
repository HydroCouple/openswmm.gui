/*!
 * \file   nodecompoundeditref.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice DB.2 — Compound-attribute cell value for the node Property
 * Browser. Carries everything the per-cell button widget needs to
 * (a) display a short summary and (b) open the right dialog for the
 * underlying SWMM concept (Inflows / DWF / RDII / Treatment).
 *
 * Registered as a Qt metatype so QPropertyModel can store it via
 * `QVariant::fromValue(...)` and the matching custom editor creator
 * (`NodeCompoundEditButton`) is instantiated by the delegate when the
 * cell goes into edit mode. A `QMetaType` string converter is also
 * installed so the read-only display state shows the `summary`
 * string rather than `QVariant<NodeCompoundEditRef>`.
 */

#ifndef NODECOMPOUNDEDITREF_H
#define NODECOMPOUNDEDITREF_H

#include <QMetaType>
#include <QString>

#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef

/*! Identifies one editable compound attribute on a single node. */
struct NodeCompoundEditRef
{
    /*! Which legacy SWMM concept this cell wraps. The order mirrors the
     *  layout in legacy `Fproped.pas` so the Property Browser rows read
     *  top-to-bottom in the same order users have seen for decades. */
    enum Kind {
        Inflows = 0,   ///< `[INFLOWS]` per-node entries (per constituent)
        Dwf,           ///< `[DWF]` per-node entries (per constituent)
        Rdii,          ///< `[RDII]` per-node UH-group assignment + area
        Treatment,     ///< `[TREATMENT]` per-node per-pollutant expression
    };

    SWMM_Engine engine   = nullptr;  ///< Engine handle (borrow, not owned)
    QString     nodeName;            ///< Owning node id
    Kind        kind     = Inflows;
    QString     summary;             ///< Short text shown in the cell

    bool operator==(const NodeCompoundEditRef &other) const noexcept
    {
        return engine == other.engine && nodeName == other.nodeName
               && kind == other.kind && summary == other.summary;
    }
};

Q_DECLARE_METATYPE(NodeCompoundEditRef)

/*! Install the `NodeCompoundEditRef → QString` metatype converter so
 *  the read-only DisplayRole path on QPropertyModel renders `summary`
 *  in the cell when the user isn't actively editing. Idempotent — safe
 *  to call multiple times from different initialisers. */
void registerNodeCompoundEditRefConverter();

#endif // NODECOMPOUNDEDITREF_H
