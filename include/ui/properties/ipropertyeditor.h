/*!
 * \file   ipropertyeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BM Phase 6.3.1 — abstract `IPropertyEditor` interface.
 *
 * Substrate for every concrete object editor (Slice BN node/link editors,
 * BO subcatchment/raingage/infiltration/LID, BP pollutant/landuse/treatment/
 * aquifer/snowpack/groundwater, BQ curves/transect/xsection/culvert/patterns
 * /timeseries/UH, BR controls, BS climatology+RDII, BT defaults). The
 * `PropertyEditorRegistry` maps a (kind) string to a factory that materialises
 * the right concrete editor when `PropertiesPanel` resolves a selection.
 *
 * Lifecycle on a typical right-side panel binding:
 *   1. User selects an object in Object Browser / Attribute Table / Map.
 *   2. PropertiesPanel translates the SWMMObjectRef to an "editor kind"
 *      string (e.g. "junction", "outfall", "curve", "timeseries").
 *   3. PropertiesPanel asks the registry for a factory; if missing, the
 *      legacy property-tree path is used (unchanged from Slice AG.3).
 *   4. The returned editor builds its widgets, attaches to the engine,
 *      and is shown.  apply() pushes user-entered values back through
 *      engine setters.
 *
 * Engine ownership: editors hold (engine, name) just like the existing
 * property adapters in `swmm{node,link,subcatch}propertyadapter.h`. The
 * registry does NOT own the engine; editors look it up through whatever
 * adapter their concrete BN/BO/BP slice introduces.
 */
#ifndef IPROPERTYEDITOR_H
#define IPROPERTYEDITOR_H

#include <QString>

#include "selection/selectionmanager.h"

class QWidget;

/*!
 * \class IPropertyEditor
 * \brief Abstract interface every concrete editor implements.
 *
 * Editors are not QObjects directly so concrete subclasses can choose
 * their own parent / lifetime model. The returned `QWidget*` from
 * `editorForObject` is owned by the caller (typically PropertiesPanel),
 * which reparents it into the right-side dock.
 */
class IPropertyEditor
{
public:
    virtual ~IPropertyEditor() = default;

    /*!
     * \brief Stable identifier for the object kind this editor handles.
     *
     * Lowercase, no spaces, matches the BM.0 data-objects table or the
     * legacy SWMM-GUI form unit name (e.g. "junction", "outfall",
     * "storage", "divider", "conduit", "pump", "orifice", "weir",
     * "outlet", "subcatchment", "raingage", "curve", "timeseries",
     * "pattern", "lid_control", "pollutant", "landuse", "aquifer",
     * "snowpack", "control_rule", "transect", "hydrograph", "street",
     * "inlet", "inlet_usage").
     *
     * Used as the registry key — must remain stable across releases.
     */
    [[nodiscard]] virtual QString objectKind() const = 0;

    /*!
     * \brief Build (or reuse) the editor's QWidget bound to \p ref.
     *
     * Returning nullptr means "this editor can't handle that ref" — the
     * registry will fall back to the next match or to the legacy
     * property-tree path. Implementations may cache the widget across
     * calls when the ref's name is unchanged; PropertiesPanel does not
     * assume widget identity.
     */
    [[nodiscard]] virtual QWidget *editorForObject(const SWMMObjectRef &ref) = 0;

    /*!
     * \brief Push the editor's current values back through the engine
     *        setters for \p ref. Called when the user clicks Apply / OK
     *        in a modal editor, or on focus-loss for inline editors.
     *
     * Implementations are responsible for engine-state guards
     * (BUILDING / OPENED checks) — the registry imposes no policy.
     */
    virtual void apply(const SWMMObjectRef &ref) = 0;
};

#endif // IPROPERTYEDITOR_H
