/*!
 * \file   newfeaturelayerdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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

namespace openswmmvis::ui {

class NewFeatureLayerDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param canvas  Supplies the canvas CRS (the table is created in it) and
     *                the list of raster / mesh layers offered as Z sources.
     *                May be null (tests): no CRS, no Z source layers.
     */
    explicit NewFeatureLayerDialog(MapCanvas *canvas, QWidget *parent = nullptr);

    [[nodiscard]] QString layerName() const;
    [[nodiscard]] openswmmvis::feature::GeometryType geometryType() const;
    /*! The ticked role fields (with the defaults as edited) followed by the
     *  ticked custom fields. */
    [[nodiscard]] openswmmvis::feature::Schema schema() const;
    [[nodiscard]] ZPolicy zPolicy() const;
    [[nodiscard]] FeatureLayerRole role() const;
    /*! \brief WKT of the canvas CRS, or empty when the canvas has none. */
    [[nodiscard]] QString srsWkt() const;

    // ----- Test seams ----------------------------------------------------
    /*! Select \p r, as picking it in the role combo does. */
    void setRole(FeatureLayerRole r);
    /*! The geometry kinds currently offered. */
    [[nodiscard]] QVector<openswmmvis::feature::GeometryType> offeredGeometries() const;
    void setGeometryType(openswmmvis::feature::GeometryType t);
    /*! Tick / untick the field row named \p name; a required field ignores
     *  an untick. Returns whether the row is ticked afterwards. */
    bool setFieldIncluded(const QString &name, bool on);
    /*! Append a custom field, as "Add custom field…" does once its editor is
     *  accepted. Returns false for an invalid or duplicate field. */
    bool addCustomField(const openswmmvis::feature::FieldDef &f);
    /*! Whether the Z section is expanded. */
    [[nodiscard]] bool zSectionExpanded() const;

private slots:
    void onRoleChanged(int index);
    void onZSourceChanged(int index);
    void onAddCustomField();
    void onRemoveCustomField();
    void validateAndAccept();

private:
    void buildUi();
    void populateZSources();
    void rebuildGeometryChoices();
    /*! Replace the field rows with the role's fields (custom rows are kept). */
    void rebuildFieldRows();
    void appendFieldRow(const openswmmvis::feature::FieldDef &f, bool custom);
    [[nodiscard]] QStringList fieldNames() const;
    void setZExpanded(bool on);
    void refreshZSummary();
    [[nodiscard]] int rowOf(const QString &name) const;

    MapCanvas        *m_canvas = nullptr;

    QComboBox        *m_roleCombo     = nullptr;
    QLabel           *m_roleSummary   = nullptr;
    QLabel           *m_roleNote      = nullptr;
    QLineEdit        *m_nameEdit      = nullptr;
    QWidget          *m_geomBox       = nullptr;
    QButtonGroup     *m_geomGroup     = nullptr;
    QLabel           *m_crsLabel      = nullptr;

    QTableWidget     *m_fieldTable    = nullptr;
    QPushButton      *m_addFieldBtn   = nullptr;
    QPushButton      *m_removeFieldBtn = nullptr;

    QToolButton      *m_zToggle       = nullptr;
    QWidget          *m_zBody         = nullptr;
    QComboBox        *m_zSourceCombo  = nullptr;   ///< None / Constant / Raster / Mesh
    QComboBox        *m_zLayerCombo   = nullptr;   ///< raster or mesh layers
    QSpinBox         *m_zBandSpin     = nullptr;
    QDoubleSpinBox   *m_zConstantSpin = nullptr;
    QDoubleSpinBox   *m_zScaleSpin    = nullptr;
    QDoubleSpinBox   *m_zDensifySpin  = nullptr;
    QCheckBox        *m_zResampleBox  = nullptr;
    QLabel           *m_zHint         = nullptr;

    QDialogButtonBox *m_buttons       = nullptr;
};

}   // namespace openswmmvis::ui

#endif // NEWFEATURELAYERDIALOG_H
