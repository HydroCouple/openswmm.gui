/*!
 * \file   plotvariablepickerdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Modal picker for time-series plot variables — the Plot Time
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

namespace openswmmvis::ui {

class PlotVariablePickerDialog : public QDialog
{
    Q_OBJECT

public:
    /*! One checked leaf: what to plot, on which object. System entries
     *  carry `ObjectRef::forSystem()` (name unused). Species leaves
     *  (Y2b-2, amendment D-Y4) carry the species NAME with attribute
     *  Unknown — the same union `ResultDescriptor` models. */
    struct Entry {
        openswmmvis::plot::ObjectRef     ref;
        openswmmvis::plot::PlotAttribute attribute =
            openswmmvis::plot::PlotAttribute::Unknown;
        QString                          species;

        openswmmvis::plot::ResultDescriptor descriptor() const
        {
            return species.isEmpty()
                ? openswmmvis::plot::ResultDescriptor::forAttribute(attribute)
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
     */
    explicit PlotVariablePickerDialog(
        const QVector<openswmmvis::plot::ObjectRef> &features,
        const openswmmvis::plot::IRunLayer *availability,
        openswmmvis::plot::UnitSystem units,
        QWidget *parent = nullptr);

    /*! \brief All checked leaf entries, in tree order. */
    [[nodiscard]] QVector<Entry> checkedEntries() const;

private:
    void buildTree(const QVector<openswmmvis::plot::ObjectRef> &features,
                   const openswmmvis::plot::IRunLayer *availability,
                   openswmmvis::plot::UnitSystem units);
    void addAttributeLeaf(QTreeWidgetItem *group,
                          const openswmmvis::plot::ObjectRef &ref,
                          openswmmvis::plot::PlotAttribute attr,
                          const openswmmvis::plot::IRunLayer *availability,
                          openswmmvis::plot::UnitSystem units);
    void addDescriptorLeaf(QTreeWidgetItem *group,
                           const openswmmvis::plot::ObjectRef &ref,
                           const openswmmvis::plot::ResultDescriptor &d,
                           const openswmmvis::plot::IRunLayer *availability,
                           openswmmvis::plot::UnitSystem units);
    void setAllChecked(bool checked);
    void invertChecked();
    void applyFilter(const QString &text);
    void updateOkEnabled();

    QTreeWidget      *m_tree    = nullptr;
    QLineEdit        *m_filter  = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_PLOTVARIABLEPICKERDIALOG_H
