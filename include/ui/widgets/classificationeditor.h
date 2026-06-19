/*!
 * \file   classificationeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.1 (UNIFIED_STYLING plan S1) — the one classification
 *         editor shared by every classified visual.
 *
 *         Extracted from KindRendererPanel's graduated box so the 1D
 *         per-kind GraduatedRenderer, the 2D depth fill / contour bands /
 *         isolines, and the mesh elevation fill all expose the SAME
 *         ArcGIS-Pro-style controls:
 *
 *           ┌─ Classification ─────────────────────────────┐
 *           │ [○ Continuous  ● Classified]   (optional)    │
 *           │ Attribute: [ depth ▾ ]          (optional)   │
 *           │ Colour ramp: [ ▒▒▒ Viridis ▾ ]  [ ] Invert   │
 *           │ Method: [ Equal interval ▾ ]   Classes: 5    │
 *           │ Range:  [ Fixed over run ▾ ]    (optional)   │
 *           │ [ ] Custom range  Min ____  Max ____ (opt.)  │
 *           │ ┌─────────────────────────────────────────┐ │
 *           │ │ Lower   Upper   Colour   Label          │ │
 *           │ └─────────────────────────────────────────┘ │
 *           │  [Auto-classify from data]                  │
 *           └──────────────────────────────────────────────┘
 *
 *         The editor owns no model state — it talks to an
 *         IClassificationBinding. Every edit calls binding->setScheme(),
 *         which routes through the model's normal change channel so all
 *         views repaint. The host re-shows the editor against the live
 *         model via refresh() (e.g. after a Cancel rollback).
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONEDITOR_H

#include "render/classificationscheme.h"

#include <QWidget>

#include <memory>

class ColorRampComboBox;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QToolButton;

namespace openswmmvis::ui {

class IClassificationBinding;

class ClassificationEditor : public QWidget
{
    Q_OBJECT
public:
    /*! \param binding  Adapter onto the classified model. The editor does
     *                  NOT take ownership unless \p ownBinding is true. */
    explicit ClassificationEditor(IClassificationBinding *binding,
                                  bool ownBinding = false,
                                  QWidget *parent = nullptr);
    ~ClassificationEditor() override;

    /*! Swap the bound model (e.g. when the host re-targets the editor).
     *  Takes ownership when \p ownBinding is true. */
    void setBinding(IClassificationBinding *binding, bool ownBinding = false);

    /*! Re-read every control from the binding's current scheme. Call after
     *  an external change / Cancel rollback. */
    void refresh();

signals:
    /*! Emitted after any edit has been pushed to the binding — hosts that
     *  show a live legend swatch or derived UI can refresh on this. */
    void edited();

private slots:
    void onModeChanged();
    void onAttributeChanged(int row);
    void onRampChanged();
    void onInvertToggled(bool on);
    void onMethodChanged(int row);
    void onClassCountChanged(int n);
    void onRangeModeChanged(int row);
    void onCustomRangeToggled(bool on);
    void onCustomRangeEdited();
    void onAutoClassify();
    void onTableItemChanged(QStandardItem *item);

private:
    void buildUi();
    void rebuildTable();
    void applyVisibility();
    /*! Read the editor's binding scheme, mutate via \p fn, push back. */
    template <class Fn> void mutateScheme(Fn fn);

    IClassificationBinding *m_binding = nullptr;
    bool                    m_ownBinding = false;

    // Controls
    QWidget            *m_modeRow      = nullptr;
    QComboBox          *m_modeCombo    = nullptr;
    QWidget            *m_attrRow      = nullptr;
    QComboBox          *m_attrCombo    = nullptr;
    ColorRampComboBox  *m_rampCombo    = nullptr;
    QCheckBox          *m_invertCheck  = nullptr;
    QComboBox          *m_methodCombo  = nullptr;
    QSpinBox           *m_countSpin    = nullptr;
    QLabel             *m_methodLabel  = nullptr;
    QLabel             *m_countLabel   = nullptr;
    QWidget            *m_rangeModeRow = nullptr;
    QComboBox          *m_rangeModeCombo = nullptr;
    QWidget            *m_customRangeRow = nullptr;
    QCheckBox          *m_customRangeCheck = nullptr;
    QDoubleSpinBox     *m_rangeMinSpin = nullptr;
    QDoubleSpinBox     *m_rangeMaxSpin = nullptr;
    QToolButton        *m_autoBtn      = nullptr;
    QTableView         *m_table        = nullptr;
    QStandardItemModel *m_tableModel   = nullptr;

    bool m_suppress = false;   // re-entrancy guard during refresh()
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONEDITOR_H
