/**
 * @file      plotvariablepickerdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Modal picker for time-series plot variables — the Plot Time
 *         Series toolbar action's dialog.
 *
 * Lists the 14 system-wide variables plus one checkable group per selected
 * map feature (node / link / subcatchment) with that kind's plottable
 * attributes. OK returns every checked (ref, attribute) pair; the caller
 * adds them to the Comparison Plot dialog in bulk. Right-click entry points
 * keep their quick single-attribute menus (AttributePickerMenu) — this
 * dialog only replaces the toolbar / Ctrl+T flow.
 *
 * Engine-free by construction: depends only on plot/ headers, takes
 * pre-mapped plot::ObjectRef features and an optional IRunLayer for
 * per-attribute availability gating, so tests drive it with a stub layer.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_PLOTVARIABLEPICKERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_PLOTVARIABLEPICKERDIALOG_H

#include "plot/irunlayer.h"
#include "plot/plotattribute.h"

#include <QDialog>
#include <QVector>

class QDialogButtonBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace openswmmvis::ui
{

class PlotVariablePickerDialog : public QDialog
{
    Q_OBJECT

public:
    /*! One checked leaf: what to plot, on which object. System entries
     *  carry `ObjectRef::forSystem()` (name unused). Species leaves
     *  (Y2b-2, amendment D-Y4) carry the species NAME with attribute
     *  Unknown — the same union `ResultDescriptor` models. */
    struct Entry {
        openswmmvis::plot::ObjectRef ref;
        openswmmvis::plot::PlotAttribute attribute = openswmmvis::plot::PlotAttribute::Unknown;
        QString species;

        /**
         * @return Value descriptor for nonempty species name, otherwise fixed attribute (possibly
         *   Unknown/invalid). Species takes precedence without trimming/resolving name; no run
         *   availability lookup or source ownership.
         */
        openswmmvis::plot::ResultDescriptor descriptor() const
        {
            return species.isEmpty() ? openswmmvis::plot::ResultDescriptor::forAttribute(attribute)
                                     : openswmmvis::plot::ResultDescriptor::forSpecies(species);
        }
    };

    /*!
     * \param features     Plottable selected features (Node / Link /
     *                     Subcatch), already filtered and sorted by the
     *                     caller. May be empty → the dialog shows the
     *                     System group only.
     * \param availability Optional run layer used to gate
     *                     supportsAttribute(); non-owning, may be null
     *                     (everything enabled).
     * \param units        Unit system for the attribute labels.
     * @param[in] parent Optional QWidget owner, caller manages parentless dialog.
     * @brief Build unchecked system/feature variable tree and fresh selection state from supplied
     *   values/availability.
     * @note Availability is inspected synchronously during construction and NOT retained; no live
     *   run/catalog refresh. Copies feature kind/name payload, not source QObject or full
     *   specialized ObjectRef fields. Modal behavior is supplied by caller exec; no plotting/data
     *   reads/model mutation.
     */
    explicit PlotVariablePickerDialog(const QVector<openswmmvis::plot::ObjectRef> &features,
                                      const openswmmvis::plot::IRunLayer *availability,
                                      openswmmvis::plot::UnitSystem units,
                                      QWidget *parent = nullptr);

    /*! \brief All checked leaf entries, in tree order.
     * @return Value entries for every exactly Checked child leaf in tree group order, including
     *   hidden or programmatically checked disabled leaves. No acceptance check, deduplication or
     *   availability revalidation.
     * @note System ref reconstructed forSystem; other refs reconstructed from kind/name only, so
     *   caller should supply intended Node/Link/Subcatch refs rather than expecting mesh
     *   index/other specialized fields retained. Species has Unknown attribute and nonempty species
     *   token.
 */
    [[nodiscard]] QVector<Entry> checkedEntries() const;

private:
    /**
     * @param[in] features Borrowed feature values inspected in order without sorting/dedup; skip
     *   kinds whose descriptor catalog is empty.
     * @param[in] availability Optional borrowed run used only now for kind descriptors and
     *   fixed-attribute supports checks; null uses default fixed descriptors without species.
     * @param[in] units Plot unit enum for labels only, no result value conversion.
     * @brief Append system group first and one group per supported feature with unchecked
     *   descriptor leaves, while tree signals blocked.
     * @note Does NOT clear prior items, update OK or expand inside helper; constructor does
     *   afterward. Groups are auto-tristate/checkable, so user group toggles follow Qt propagation
     *   independent of visible-only toolbar actions. No engine/source ownership.
     */
    void buildTree(const QVector<openswmmvis::plot::ObjectRef> &features,
                   const openswmmvis::plot::IRunLayer *availability,
                   openswmmvis::plot::UnitSystem units);
    /**
     * @param[in] group Required existing tree group; owns newly allocated child.
     * @param[in] ref Copied kind/name payload only; no full object identity/lifetime pin.
     * @param[in] attr Raw fixed plot attribute label/key stored, Unknown not independently
     *   rejected.
     * @param[in] availability Optional borrowed run, supportsAttribute false disables leaf and
     *   gives tooltip.
     * @param[in] units Units used by labelWithUnits, no sample conversion.
     * @brief Append unchecked fixed-attribute leaf, enabled unless explicitly unsupported. No
     *   plotting or result extraction; tooltip does not encode per-object availability.
     */
    void addAttributeLeaf(QTreeWidgetItem *group, const openswmmvis::plot::ObjectRef &ref,
                          openswmmvis::plot::PlotAttribute attr,
                          const openswmmvis::plot::IRunLayer *availability,
                          openswmmvis::plot::UnitSystem units);
    /**
     * @param[in] group Required owning parent group.
     * @param[in] ref Copied kind/name payload, specialized fields not stored.
     * @param[in] d Descriptor inspected synchronously; fixed delegates to addAttributeLeaf, species
     *   keeps exact name.
     * @param[in] availability Borrowed run forwarded for FIXED attribute gating only; species leaf
     *   not independently supports-checked.
     * @param[in] units Label-only units, species label authorities determine native semantics such
     *   as age hours.
     * @brief Append unchecked leaf for descriptor; species uses Unknown attribute and separate name
     *   role.
     * @note Species entries always enabled when offered by descriptor catalog, no additional
     *   availability check or duplicate filtering. No run ownership/value reads.
     */
    void addDescriptorLeaf(QTreeWidgetItem *group, const openswmmvis::plot::ObjectRef &ref,
                           const openswmmvis::plot::ResultDescriptor &d,
                           const openswmmvis::plot::IRunLayer *availability,
                           openswmmvis::plot::UnitSystem units);
    /**
     * @param[in] checked True checks, false unchecks enabled checkable leaves whose own hidden flag
     *   is false.
     * @brief Apply bulk state to visible eligible leaves with tree signals blocked, then explicitly
     *   recompute OK.
     * @note Hidden/disabled selections retained; unlike direct Qt group check propagation this
     *   action skips hidden children. No plot/source changes.
     */
    void setAllChecked(bool checked);
    /**
     * @brief Toggle visible enabled checkable child leaves (Checked→Unchecked, all other→Checked),
     *   preserving hidden/disabled values.
     * @note Blocks tree signals during operation then recomputes OK once; no result reads or model
     *   mutation.
     */
    void invertChecked();
    /**
     * @param[in] text Trimmed literal case-insensitive substring, empty matches every group.
     * @brief Show all children when group title matches, otherwise only matching leaf labels; hide
     *   groups without visible children.
     * @note Retains all check states, including hidden ones, and does not recompute OK because
     *   selection unchanged. Searches displayed labels/units only, no regex/source query.
     */
    void applyFilter(const QString &text);
    /**
     * @brief Enable OK if ANY child has exact Checked state, regardless of hidden/enabled/checkable
     *   status; disable otherwise.
     * @note No-op without OK button. Does not fetch results, validate descriptor or enforce
     *   acceptance in checkedEntries.
     */
    void updateOkEnabled();

    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_filter = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_PLOTVARIABLEPICKERDIALOG_H
