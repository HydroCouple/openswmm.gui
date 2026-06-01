/*!
 * \file   kindrendererpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer-mode chooser + classification controls (Slice S2+S5).
 *
 *         Embedded in each FeatureStyleEditor below the basic single-symbol
 *         controls. Lets the user override the per-kind paint with a
 *         GraduatedRenderer or CategorizedRenderer:
 *
 *           ┌─ Classified rendering ──────────────────────┐
 *           │ Mode:    [ None      ▾ ]                    │
 *           │ Attribute: ___________                       │
 *           │ Ramp:    [ ▒▒▒▒▒▒ Viridis ▾ ]               │
 *           │ Method:  [ Equal interval ▾ ]  Classes: 5    │
 *           │ ┌────────────────────────────────────┐       │
 *           │ │ Lower   Upper   Colour   Label     │       │
 *           │ │ 0.0     1.2     ▒▒▒      Class 1   │       │
 *           │ │ 1.2     2.4     ▒▒▒      Class 2   │       │
 *           │ │ ...                                │       │
 *           │ └────────────────────────────────────┘       │
 *           │  [Auto-classify from data]                  │
 *           └──────────────────────────────────────────────┘
 *
 *         The widget owns no model state — it reads / writes
 *         hostLayer->setKindRenderer(category, ...) directly so every
 *         edit fires the layer's repaintRequested. Cancel rollback is
 *         handled at the dialog level via the ILayerStyleSubject snapshot
 *         (the renderer JSON round-trip preserves the full state).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H

#include "layers/swmm_category.h"

#include <QWidget>

class OpenSWMMVisLayer;
class SWMMModelLayer;
class SWMMResultsLayer;

class ColorRampComboBox;

class QComboBox;
class QPushButton;
class QSpinBox;
class QStandardItem;
class QStandardItemModel;
class QTableView;
class QToolButton;

namespace OpenSWMM::Render {
class IFeatureRenderer;
class Rule;          // Slice B.6a — Rule-aware ctor below.
}

namespace openswmmvis::ui {

class KindRendererPanel : public QWidget
{
    Q_OBJECT
public:
    /*! \param hostLayer  Either a SWMMModelLayer or a SWMMResultsLayer.
     *                    The panel uses the layer's kindRenderer /
     *                    setKindRenderer / resetKindRendererToDefaults
     *                    API to read and write per-kind renderers. */
    KindRendererPanel(OpenSWMMVisLayer *hostLayer,
                      OpenSWMMVis::SwmmCategory category,
                      QWidget *parent = nullptr);

    /*! Slice B.6a — Rule-aware constructor. The panel reads/writes the
     *  Rule's owned IFeatureRenderer directly. \p rule must outlive
     *  the panel; the panel does not take ownership. */
    explicit KindRendererPanel(OpenSWMM::Render::Rule *rule,
                                QWidget *parent = nullptr);

    /*! Re-read the current renderer from the host layer and update the
     *  panel's controls. Called by the dialog after a Cancel rollback. */
    void refreshFromModel();

private slots:
    void onModeChanged(int comboRow);
    void onRampChanged();
    void onBinMethodChanged(int comboRow);
    void onBinCountChanged(int n);
    void onAutoClassify();
    void onBreakEdited(class QStandardItem *item);
    // Slice DM.2 — attribute picker. Populated from IAttributeProvider
    // when the host layer (or the Rule's owning layer) implements it;
    // hidden otherwise.
    void onAttributeChanged(int comboRow);
    // O1-3 — animated range mode (Fixed over run / Per-frame auto-stretch /
    // Fixed user). Only shown when the host is a results (animated) layer.
    void onRangeModeChanged(int comboRow);

private:
    void rebuildBreaksTable();
    OpenSWMM::Render::IFeatureRenderer *currentRenderer() const;

    OpenSWMMVisLayer       *m_hostLayer = nullptr;
    SWMMModelLayer         *m_modelLayer = nullptr;     // either m_modelLayer
    SWMMResultsLayer       *m_resultsLayer = nullptr;   // or m_resultsLayer is set
    OpenSWMMVis::SwmmCategory m_category = OpenSWMMVis::CatJunctions;

    // Slice B.6a — Rule-aware mode. When non-null, m_modelLayer /
    // m_resultsLayer are both null and read/write paths route through
    // Rule::renderer / Rule::setRenderer.
    OpenSWMM::Render::Rule *m_rule = nullptr;

    // Controls
    QComboBox          *m_modeCombo    = nullptr;
    QComboBox          *m_attrCombo    = nullptr;   // Slice DM.2
    QWidget            *m_attrRow      = nullptr;   // Slice DM.2 — hidden when no provider
    QWidget            *m_graduatedBox = nullptr;
    ColorRampComboBox  *m_rampCombo    = nullptr;
    QComboBox          *m_methodCombo  = nullptr;
    QComboBox          *m_rangeCombo   = nullptr;   // O1-3 — animated range mode
    QWidget            *m_rangeRow     = nullptr;   // O1-3 — hidden for static layers
    QSpinBox           *m_countSpin    = nullptr;
    QTableView         *m_breaksTable  = nullptr;
    QStandardItemModel *m_breaksModel  = nullptr;
    QToolButton        *m_autoBtn      = nullptr;

    // Re-entrancy guard while rebuilding the table from the renderer.
    bool m_suppressEdits = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_KINDRENDERERPANEL_H
