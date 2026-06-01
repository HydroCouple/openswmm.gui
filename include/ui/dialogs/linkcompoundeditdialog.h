/*!
 * \file   linkcompoundeditdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice SC.1 — One modal dialog with a stacked-widget body, one page
 * per compound link attribute (XSection / Culvert Code / Inlet Usage).
 *
 * Apply-as-you-go: each page commits changes to the engine via the
 * model layer's `applyLink*` helpers as the user edits. The dialog's
 * `updatedSummary()` reflects the final state on close so the cell
 * widget can refresh its label without re-querying the engine.
 *
 * Slice BN Phase 6.4.4 replaces the XSection page's inner body with
 * the rich multi-pane 26-shape live-preview editor (per §S.11); the
 * `LinkCompoundEditRef` plumbing + dialog host stay unchanged so this
 * is a drop-in widget swap.
 */

#ifndef LINKCOMPOUNDEDITDIALOG_H
#define LINKCOMPOUNDEDITDIALOG_H

#include <QDialog>

#include "ui/properties/linkcompoundeditref.h"

class QStackedWidget;
class QDialogButtonBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QListWidget;
class QSplitter;
class LabeledPickerCombo;

class LinkCompoundEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LinkCompoundEditDialog(LinkCompoundEditRef ref,
                                      QWidget *parent = nullptr);

    /*! Refreshed summary string after the dialog closes. Equal to the
     *  constructor's `ref.summary` if no mutation succeeded. */
    [[nodiscard]] QString updatedSummary() const { return m_ref.summary; }

private:
    void buildXSectionPage();
    void buildCulvertCodePage();
    void buildInletUsagePage();

    /*! Look up the bound link's index on the engine. Returns -1 if the
     *  link has been removed since the dialog opened. */
    [[nodiscard]] int linkIdx() const;

    /*! Build the human-readable summary string for the current xsection
     *  state (e.g. "CIRCULAR (3.0 ft)"). Used both for the dialog title
     *  hint and for `m_ref.summary` after every apply. */
    QString computeXsectSummary() const;

    /*! Refresh visibility of geom2..geom4 + transect/curve picker rows
     *  based on the currently-selected shape. */
    void updateXsectFieldVisibility();

    /*! Apply the current shape+geom1..4 to the engine via
     *  `SWMMModelLayer::applyLinkXsect` and refresh the summary. */
    void applyXsect();

    /*! Refresh the transect picker's items from the layer's
     *  TransectRegistry and select \p selected if non-empty. No-op when
     *  the picker hasn't been built or no layer is bound. */
    void refreshTransectPickerItems(const QString &selected = {});

    /*! Open `TransectEditorDialog::pickTransect` modally and, on a
     *  non-empty return, update the picker selection + commit the new
     *  transect index to the engine via `applyXsect`. Triggered by the
     *  picker's "..." button. */
    void onTransectPickerClicked();

    LinkCompoundEditRef m_ref;

    // XSection page widgets — §S.SC.1.a (2026-05-25) reworked: the
    // single QComboBox shape picker is replaced by a horizontal
    // QSplitter whose left pane is a QListWidget in IconMode (one item
    // per allowed shape, colored placeholder icon + label below) and
    // whose right pane is the existing per-shape params form. The
    // splitter + list expose the same `currentXsectShapeId()` selection
    // signal the old combo did so the suppress-apply / round-trip
    // contract is unchanged.
    QSplitter      *m_xsSplitter     = nullptr;
    QListWidget    *m_xsShapeList    = nullptr;
    QDoubleSpinBox *m_xsGeom1Spin    = nullptr;
    QDoubleSpinBox *m_xsGeom2Spin    = nullptr;
    QDoubleSpinBox *m_xsGeom3Spin    = nullptr;
    QDoubleSpinBox *m_xsGeom4Spin    = nullptr;
    QSpinBox       *m_xsBarrelsSpin  = nullptr;
    QLabel         *m_xsSummaryLabel = nullptr;
    QLabel         *m_xsGeom1Label   = nullptr;
    QLabel         *m_xsGeom2Label   = nullptr;
    QLabel         *m_xsGeom3Label   = nullptr;
    QLabel         *m_xsGeom4Label   = nullptr;

    // §S.SC.1.b (2026-05-25) — When the user picks IRREGULAR the engine
    // expects geom1 to be a transect *index*. Bare numeric input is
    // hostile (the indices aren't stable across rename / reorder) so we
    // hide the geom1 spin and surface a LabeledPickerCombo whose
    // "..." button opens the TransectEditorDialog CRUD modal
    // (`pickTransect`). On commit we translate the picked name → engine
    // transect index → geom1.
    QLabel             *m_xsTransectLabel  = nullptr;
    LabeledPickerCombo *m_xsTransectPicker = nullptr;

    // Re-entrancy guard: when we populate widgets from engine state
    // on dialog open / page activation, the QSignals fire and would
    // otherwise bounce right back through `applyXsect()`. Set true
    // while populating; the slot bails when set.
    bool m_xsSuppressApply = false;

    // Culvert page widgets.
    QComboBox *m_cvCodeCombo = nullptr;
    bool       m_cvSuppressApply = false;

    QStackedWidget   *m_stack   = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // LINKCOMPOUNDEDITDIALOG_H
