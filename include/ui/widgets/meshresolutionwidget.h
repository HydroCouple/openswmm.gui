// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshresolutionwidget.h
 * @brief     Shared mesh resolution editor for dialogs and preferences.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_MESH_RESOLUTION_WIDGET_H
#define OPENSWMM_MESH_RESOLUTION_WIDGET_H
#include "mesh/meshresolution.h"
#include <QWidget>
class QDoubleSpinBox;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class QTreeWidget;
class QStackedWidget;
class QDockWidget;

// Shared editor: dialog and Preferences use identical meanings and units.
class MeshResolutionWidget : public QWidget
{
public:
    /**
     * @brief Create shared categorized mesh-resolution controls seeded from default ResolutionSettings.
     * @param[in] parent Optional QWidget owner; controls/pages are widget-owned and operate on the GUI thread.
     */
    explicit MeshResolutionWidget(QWidget *parent = nullptr);
    /**
     * @brief Collect the current controls into SI mesh-resolution settings.
     * @return Value with physical distances multiplied by metresPerUnit, optional modes mapped to nullopt, transition dimensionless and disabled coarsening represented by zero passes.
     * @note Disabling terrain availability only gates controls; stored values/flags remain in this result. Full semantic validation is the caller's responsibility.
     */
    mesh::ResolutionSettings settings() const;
    /**
     * @brief Hydrate all controls from SI resolution settings and update visibility/enablement.
     * @param[in] settings Value whose distances are divided by the current display-unit scale. Qt ranges/precision can clamp displayed values; no complete settings validation is performed.
     * @note Preserves Direct reference as the advanced full-resolution flag. Child-control signals are not uniformly blocked, and this widget defines no aggregate change signal.
     */
    void setSettings(const mesh::ResolutionSettings &settings);
    /**
     * @brief Re-express physical controls in a new display length unit while preserving collected SI settings.
     * @param[in] metresPerUnit Required positive finite scale to metres; no explicit validation occurs before conversion/division.
     * @param[in] label Unit suffix appended to distance controls; dimensionless transition is unaffected.
     * @note Values may be rounded/clamped by control ranges during rehydration.
     */
    void setUnits(double metresPerUnit, const QString &label);
    /**
     * @brief Gate terrain-dependent controls and update guidance without discarding values.
     * @param[in] available True enables supported terrain settings; false disables their controls.
     */
    void setTerrainAvailable(bool available);
    /**
     * @brief Add content to an existing exact-title category or create a new scrollable settings page.
     * @param[in] title Category display text; exact match reuses an existing page.
     * @param[in] content Required widget reparented into the page layout, transferring QWidget ownership to this editor hierarchy.
     */
    void addSettingsPage(const QString &title, QWidget *content);
    /**
     * @brief Reveal the page containing a control, scroll it into view and focus it.
     * @param[in] control Required descendant control; a control outside all pages is ignored.
     */
    void showSettingsPage(QWidget *control);

    QDoubleSpinBox *minimum, *maximum, *feature, *tolerance, *breaklineTolerance, *averaging,
        *transition, *minimumBreakline;
    QComboBox *maximumMode, *featureMode, *toleranceMode, *breaklineMode, *averagingMode,
        *terrainMode, *reference, *minimumBreaklineMode;
    QCheckBox *refineFeatures, *alignTerrain;
    QSpinBox *maxBreaklines, *coarsenPasses;
    QCheckBox *coarsenEnabled;
    QLabel *report;

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void updateNavigationWidth();
    /**
     * @brief Recompute control visibility, terrain gates and reference-resolution guidance from current draft choices.
     * @note Does not clear disabled values or validate relationships such as maximum >= minimum.
     */
    void sync();
    double m_toSI = 1.0;
    bool m_hasTerrain = true;
    QCheckBox *m_refineTerrain;
    QWidget *m_featureRow;
    QLabel *m_referenceNote;
    QTreeWidget *m_categories = nullptr;
    QDockWidget *m_categoryDock = nullptr;
    QStackedWidget *m_pages;
};
#endif
