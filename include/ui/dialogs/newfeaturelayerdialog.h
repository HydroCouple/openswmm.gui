/**
 * @file      newfeaturelayerdialog.h
 * @brief     Create an editable feature layer — role first
 *            (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §5, R4; supersedes
 *            MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §5.2).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Create an editable feature layer — role first
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §5, R4;
 * supersedes MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §5.2).
 *
 *   - The role comes first; one line from the registry says what it is for,
 *     and the geometry choices are the role's (General offers all six).
 *   - The role's fields are listed with checkboxes. Name and type are fixed so
 *     a user cannot break what the mesher reads; only optional fields can be
 *     unticked. The default is edited with the field's own editor.
 *   - "Add custom field…" opens FeatureFieldEditor (Choice fields included).
 *   - The Z section collapses to a one-line summary unless the role is
 *     Breaklines or General.
 *
 * Everything this dialog collects is immutable-ish afterwards: geometry type
 * and 2D / 3D decide the GeoPackage table's OGR geometry type; the CRS is the
 * canvas CRS. The schema and the Z SOURCE can be changed later from the
 * Features dock.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef NEWFEATURELAYERDIALOG_H
#define NEWFEATURELAYERDIALOG_H

#include "feature/featuregeometry.h"
#include "feature/featuretypes.h"
#include "layers/featurelayer.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QToolButton;
class QWidget;
class MapCanvas;

namespace openswmmvis::ui
{

class NewFeatureLayerDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param canvas  Supplies the canvas CRS (the table is created in it) and
     *                the list of raster / mesh layers offered as Z sources.
     *                May be null (tests): no CRS, no Z source layers.
     * @param[in] parent Optional QWidget owner; dialog owns controls/default editors, does not own
     *   canvas/layers.
     * @note Store canvas as RAW pointer, must outlive dialog including getters/modal callbacks.
     *   Build registry role choices, initialize role geometry/fields/Z UI and source list. No
     *   layer/table created, GeoPackage/file opened or undo command; caller creates layer from
     *   accepted getters. GUI-thread only.
     */
    explicit NewFeatureLayerDialog(MapCanvas *canvas, QWidget *parent = nullptr);

    /**
     * @return Trimmed typed name, translated Features fallback if blank, even before acceptance.
     *   validateAndAccept rejects blank instead of using fallback. No table-name
     *   sanitization/uniqueness/project check or control mutation.
     */
    [[nodiscard]] QString layerName() const;
    /**
     * @return Checked offered geometry enum, None if no group/checked radio; no layer creation, 3D
     *   flag or role validation here.
     */
    [[nodiscard]] openswmmvis::feature::GeometryType geometryType() const;
    /*! The ticked role fields (with the defaults as edited) followed by the
     *  ticked custom fields.
     * @return New schema from checked rows in table order: custom FieldDef decoded from stored
     *   JSON, role field copied from CURRENT role spec with live default editor value.
     *   Missing/unknown rows skipped. Required fields are UI-locked checked, no independent
     *   force-include in getter. Numeric role editors materialize zero for missing default, custom
     *   metadata remains stored snapshot. No final validation/source write.
     */
    [[nodiscard]] openswmmvis::feature::Schema schema() const;
    /**
     * @return Widget snapshot including currently disabled fields: source/selected layer ID, band
     *   1..512, constant+/-1e9, scale+/-1e6 default 1, densify 0..1e9, resample checkbox default
     *   true. Native policy units, no conversion other than eventual explicit scale; zero densify
     *   means off. Missing sample layer ID allowed; no sampling or layer creation.
     */
    [[nodiscard]] ZPolicy zPolicy() const;
    /**
     * @return Current combo payload cast to FeatureLayerRole; missing selection conversion yields
     *   underlying 0, no explicit validity guard or source mutation.
     */
    [[nodiscard]] FeatureLayerRole role() const;
    /*! \brief WKT of the canvas CRS, or empty when the canvas has none.
     * @return CURRENT raw canvas SRS toWkt copy, empty absent canvas/SRS; not constructor snapshot
     *   or guaranteed valid/normalized WKT. Host preserves canvas lifetime; no CRS
     *   transformation/write.
     */
    [[nodiscard]] QString srsWkt() const;

    // ----- Test seams ----------------------------------------------------
    /*! Select \p r, as picking it in the role combo does.
     * @param[in] r Requested role enum, only selects existing combo item; unknown no-op.
     * @note Changed selection triggers full role field/geometry rebuild and Z
     *   expansion/default-source nudge. Equal selection does not retrigger slot. Does not create
     *   layer or validate custom fields against external storage.
     */
    void setRole(FeatureLayerRole r);
    /*! The geometry kinds currently offered.
     * @return Value list of current radio-button IDs in button-group order; role-allowed
     *   geometries, no independent sorting or source query.
     */
    [[nodiscard]] QVector<openswmmvis::feature::GeometryType> offeredGeometries() const;
    /**
     * @param[in] t Requested geometry, ignored if no current offered radio matches.
     * @note Check matching radio, no role change, schema mutation or layer creation. Selection
     *   applies before final validation.
     */
    void setGeometryType(openswmmvis::feature::GeometryType t);
    /*! Tick / untick the field row named \p name; a required field ignores
     *  an untick. Returns whether the row is ticked afterwards.
     * @param[in] name Case-insensitive row name, no trim, first match.
     * @param[in] on Requested checked state; noncheckable required role row ignores change.
     * @return Actual checked state afterward, false missing row. Custom required=true rows are
     *   still checkable because custom flag overrides requirement UI. No row/default deletion or
     *   source mutation.
     */
    bool setFieldIncluded(const QString &name, bool on);
    /*! Append a custom field, as "Add custom field…" does once its editor is
     *  accepted. Returns false for an invalid or duplicate field.
     * @param[in] f Already-built custom definition; static FeatureFieldEditor::validate checks
     *   name/list against ALL row names but not numeric default/type/metadata.
     * @return False validation error without dialog or change, true after append. Does not sanitize
     *   f.name itself or exempt unticked names. Captures JSON snapshot, checked by default, no
     *   source/undo write.
     */
    bool addCustomField(const openswmmvis::feature::FieldDef &f);
    /*! Whether the Z section is expanded.
     * @return Toggle checked state controlling expanded body, independent of parent widget
     *   visibility or chosen source. No mutation.
     */
    [[nodiscard]] bool zSectionExpanded() const;

private slots:
    /**
     * @param[in] index Ignored signal argument, implementation unnamed; CURRENT role combo drives
     *   behavior.
     * @brief Set registry description/note, rebuild geometry/default role fields preserving custom
     *   snapshots, and expand Z per wantsZ.
     * @note Breakline with current None source nudges to Raster but allows later 2D; source can
     *   remain missing. Other roles preserve existing Z source/settings. No new layer or project
     *   write.
     */
    void onRoleChanged(int index);
    /**
     * @param[in] index Ignored signal argument, implementation unnamed; CURRENT source combo drives
     *   behavior.
     * @brief Enable source-specific controls, rebuild source-layer list, set availability hint and
     *   summary.
     * @note None disables 3D controls but retains values; Raster/Mesh includes all matching canvas
     *   layers, can remain no-source and still accepted 3D. Rebuilding resets selected source
     *   rather than preserving ID. No sampling, unit conversion or project write.
     */
    void onZSourceChanged(int index);
    /**
     * @brief Open child FeatureFieldEditor with current field names; append returned field only
     *   after accepted validation.
     * @note Cancel no-op, addCustomField result ignored because editor already validated against
     *   earlier snapshot; host preserves dialog/canvas state through modal callback. No source/undo
     *   write.
     */
    void onAddCustomField();
    /**
     * @brief Remove current row only if stored custom flag true, no confirmation or undo.
     * @note Missing selection/role row no-op. Required role rows remain; removed custom
     *   metadata/default discarded from unsaved dialog only.
     */
    void onRemoveCustomField();
    /**
     * @note Reject blank trimmed name with warning/focus, then None geometry with warning,
     *   otherwise call inherited accept. Does NOT validate duplicate/external table name,
     *   schema/defaults, canvas CRS, role 3D necessity or sample-source existence. Acceptance
     *   collects settings only, caller owns creation/errors/undo; direct inherited accept can
     *   bypass this button handler.
     */
    void validateAndAccept();

private:
    /**
     * @brief Constructor-only owned scroll/form/role/schema/Z widgets and signals, not idempotent.
     * @note Role field default editors built later; CRS caption queried once but srsWkt getter
     *   stays live. No store/table/file creation, canvas ownership or source-change subscription.
     *   Numeric Z ranges bound UI presentation; names remain raw until getter trim.
     */
    void buildUi();
    /**
     * @brief Clear source combo then append GISRaster or SWMM2DMesh canvas layers matching CURRENT
     *   source, names displayed/layer IDs stored.
     * @note Null canvas or None/Constant leaves empty. Visibility/readiness ignored, previous
     *   selected ID not restored, no layer ownership/sampling or future canvas layer-change
     *   subscription. Raw canvas must outlive call.
     */
    void populateZSources();
    /**
     * @note Delete previous radio widgets/layout items and rebuild CURRENT role geometry choices.
     *   Prefer role default when allowed, otherwise previous allowed choice, otherwise Polygon then
     *   first/None. No source geometry/type mutation, signal-level validation or user-change
     *   persistence beyond fallback.
     */
    void rebuildGeometryChoices();
    /*! Replace the field rows with the role's fields (custom rows are kept).
     * @note Save custom JSON definitions/check state, clear table and append fresh CURRENT role
     *   fields/default editors, then noncolliding custom rows preserving check state. Custom name
     *   collision case-insensitive silently drops it; edited role defaults and inclusion choices
     *   reset on role change. No source/engine write; resize columns afterward.
     */
    void rebuildFieldRows();
    /**
     * @param[in] f Borrowed role/custom definition, copied into items and custom JSON when
     *   requested.
     * @param[in] custom True creates removable/checkable JSON snapshot with read-only default text;
     *   false uses live typed role default editor and locks required checkbox.
     * @note Append checked row, name/type/description not inline-editable. No
     *   uniqueness/sanitization/default validation or source mutation. Custom required flag does
     *   not remove user-checkability; no schema append until schema getter.
     */
    void appendFieldRow(const openswmmvis::feature::FieldDef &f, bool custom);
    /**
     * @return EVERY existing row name in table order, unticked/required/custom included; missing
     *   name item skipped. No deduplication, trim or source query.
     */
    [[nodiscard]] QStringList fieldNames() const;
    /**
     * @param[in] on Requested toggle/body expansion, no source 3D semantics.
     * @note Set checked state with QSignalBlocker when necessary, arrow/body visibility, refresh
     *   summary. No reset of hidden Z values, sampling or source mutation.
     */
    void setZExpanded(bool on);
    /**
     * @brief Update toggle text with source-derived 2D/3D summary only; no value conversion,
     *   validation, expansion change or source write. Unknown enum leaves empty detail.
     */
    void refreshZSummary();
    /**
     * @param[in] name Raw name compared case-insensitively, no trim/canonicalization.
     * @return First current field-table row matching, -1 absent. Unticked rows still match; no
     *   source/schema mutation.
     */
    [[nodiscard]] int rowOf(const QString &name) const;

    MapCanvas *m_canvas = nullptr;

    QComboBox *m_roleCombo = nullptr;
    QLabel *m_roleSummary = nullptr;
    QLabel *m_roleNote = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QWidget *m_geomBox = nullptr;
    QButtonGroup *m_geomGroup = nullptr;
    QLabel *m_crsLabel = nullptr;

    QTableWidget *m_fieldTable = nullptr;
    QPushButton *m_addFieldBtn = nullptr;
    QPushButton *m_removeFieldBtn = nullptr;

    QToolButton *m_zToggle = nullptr;
    QWidget *m_zBody = nullptr;
    QComboBox *m_zSourceCombo = nullptr; ///< None / Constant / Raster / Mesh
    QComboBox *m_zLayerCombo = nullptr;  ///< raster or mesh layers
    QSpinBox *m_zBandSpin = nullptr;
    QDoubleSpinBox *m_zConstantSpin = nullptr;
    QDoubleSpinBox *m_zScaleSpin = nullptr;
    QDoubleSpinBox *m_zDensifySpin = nullptr;
    QCheckBox *m_zResampleBox = nullptr;
    QLabel *m_zHint = nullptr;

    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace openswmmvis::ui

#endif // NEWFEATURELAYERDIALOG_H
