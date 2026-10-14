// SPDX-License-Identifier: GPL-3.0-or-later
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

// Shared editor: dialog and Preferences use identical meanings and units.
class MeshResolutionWidget : public QWidget
{
public:
    explicit MeshResolutionWidget(QWidget *parent=nullptr);
    mesh::ResolutionSettings settings() const;
    void setSettings(const mesh::ResolutionSettings &settings);
    void setUnits(double metresPerUnit, const QString &label);
    void setTerrainAvailable(bool available);
    void addSettingsPage(const QString &title, QWidget *content);
    void showSettingsPage(QWidget *control);

    QDoubleSpinBox *minimum, *maximum, *feature, *tolerance, *breaklineTolerance, *averaging, *transition, *minimumBreakline;
    QComboBox *maximumMode, *featureMode, *toleranceMode, *breaklineMode, *averagingMode, *terrainMode, *reference, *minimumBreaklineMode;
    QCheckBox *refineFeatures, *alignTerrain;
    QSpinBox *maxBreaklines, *coarsenPasses;
    QCheckBox *coarsenEnabled;
    QLabel *report;
private:
    void sync();
    double m_toSI=1.0;
    bool m_hasTerrain=true;
    QCheckBox *m_refineTerrain;
    QWidget *m_featureRow;
    QLabel *m_referenceNote;
    QTreeWidget *m_categories;
    QStackedWidget *m_pages;
};
#endif
