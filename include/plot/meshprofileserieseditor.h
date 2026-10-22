/**
 * @file      meshprofileserieseditor.h
 * @brief     Mesh Profile Series Editor interface and supporting types.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MESH_PROFILE_SERIES_EDITOR_H
#define MESH_PROFILE_SERIES_EDITOR_H
#include "plot/profilesectionseries.h"
#include <QWidget>
class QComboBox;
class QTableWidget;
class QLabel;
class MeshProfileSeriesEditor : public QWidget
{
    Q_OBJECT
public:
    struct SourceChoice {
        QString id, label;
        QVector<openswmmvis::io::Mesh2DResultVariable> variables;
    };
    /**
     * @brief Build an owned table editor for a copied section definition and independent series styles.
     * @param[in] parent Optional QWidget owner. Child controls/layouts belong to this widget; no result sources are bound or owned.
     * @note User edits change the draft immediately and emit definitionEdited() even when validation reports an error. Width controls constrain [0.1,32] pixels, opacity [0,1], and scalar range controls [-1e12,1e12]. The host decides when/how to resample or save.
     */
    explicit MeshProfileSeriesEditor(QWidget *parent = nullptr);
    /**
     * @brief Replace the draft and rebuild controls without announcing a user edit.
     * @param[in] definition Copied verbatim without validation or source resolution. Existing draft/row widgets are replaced; definitionEdited() is not emitted and the prior error-label text is not recomputed.
     */
    void setDefinition(const ProfileSection::Definition &definition);
    /**
     * @brief Inspect the current editor draft.
     * @return Borrowed immutable reference, potentially invalid under validateDefinition(). Later setters/user edits mutate or replace it; copy for a stable snapshot.
     */
    const ProfileSection::Definition &definition() const { return m_definition; }
    /**
     * @brief Refresh copied source/catalog choices while retaining unavailable draft series.
     * @param[in] sources Source IDs/display labels/variable metadata copied without holding live sources.
     * @note Restores the selected source by exact ID when available, otherwise uses the combo default. Rebuilds quantity choices/rows but does not edit the draft or emit definitionEdited(); source IDs should be unique.
     */
    void setSources(const QVector<SourceChoice> &sources);
signals:
    /**
     * @brief Notify a user-mutated draft, including invalid configurations.
     * @note Emitted after validation text is updated. The host must read definition() and perform any acceptance/resampling/persistence; this signal is not evidence of successful validation.
     */
    void definitionEdited();

private:
    /**
     * @brief Replace row controls from the complete draft while suppressing edit announcements.
     * @note Retains unavailable source/quantity identities with a visible unavailable label. Widget initialization clamps displayed numeric controls without normalizing the underlying draft. Rebuilding does not update the validation label or emit definitionEdited().
     */
    void rebuildRows();
    /**
     * @brief Replace quantity choices from the currently selected exact source ID.
     * @note All listed variables are offered with metadata units or units unknown; no role/time-policy compatibility filtering or previous variable selection restoration occurs. Does not mutate the draft.
     */
    void refreshVariables();
    /**
     * @brief Append a default draft series for the selected source/quantity and announce the edit.
     * @note Missing selections show a message/focus without mutation. Success creates a UUID, defaults to scalar/Hold, and selects elevation/green styling for groundwater-table/aquifer-base datasets. Duplicate source/quantity series are allowed. Rebuilds rows then validates/announces even if the complete definition remains invalid.
     */
    void addSeries();
    /**
     * @brief Validate the current draft for display and emit its edit notification regardless of validity.
     * @note No-op during rebuildRows(). Otherwise updates the plain-text error label, then emits definitionEdited(); never rolls back a failed validation.
     */
    void announceEdit();
    ProfileSection::Definition m_definition;
    QVector<SourceChoice> m_sources;
    QComboBox *m_source = nullptr, *m_variable = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_error = nullptr;
    bool m_refreshing = false;
};
#endif
