/*!
 * \file   newfeaturelayerdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Create an editable feature layer
 * (workplans/MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN_2026-09-07.md §5.2).
 *
 * Everything this dialog collects is immutable-ish afterwards, which is why it
 * is a dialog rather than an inline row:
 *   - geometry type decides the GeoPackage table's OGR geometry type;
 *   - 2D / 3D decides whether that type carries the 25D flag;
 *   - the CRS is the canvas CRS and is shown read-only.
 * The schema and the Z SOURCE can be changed later from the Features dock; the
 * dimension and geometry type cannot, and the dialog says so.
 */

#ifndef NEWFEATURELAYERDIALOG_H
#define NEWFEATURELAYERDIALOG_H

#include "feature/featuregeometry.h"
#include "feature/featuretypes.h"
#include "layers/featurelayer.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class MapCanvas;

namespace openswmmvis::ui {

class NewFeatureLayerDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param canvas  Supplies the canvas CRS (the table is created in it) and
     *                the list of raster / mesh layers offered as Z sources.
     */
    explicit NewFeatureLayerDialog(MapCanvas *canvas, QWidget *parent = nullptr);

    [[nodiscard]] QString layerName() const;
    [[nodiscard]] openswmmvis::feature::GeometryType geometryType() const;
    [[nodiscard]] openswmmvis::feature::Schema schema() const;
    [[nodiscard]] ZPolicy zPolicy() const;
    [[nodiscard]] FeatureLayerRole role() const;
    /*! \brief WKT of the canvas CRS, or empty when the canvas has none. */
    [[nodiscard]] QString srsWkt() const;

private slots:
    void onRoleChanged(int index);
    void onZSourceChanged(int index);
    void onAddField();
    void onRemoveField();
    void validateAndAccept();

private:
    void buildUi();
    void populateZSources();
    /*! Replace the schema table's contents with the role's template. */
    void applyRoleTemplate(FeatureLayerRole r);
    [[nodiscard]] openswmmvis::feature::Schema readSchemaTable() const;
    void writeSchemaTable(const openswmmvis::feature::Schema &s);

    MapCanvas        *m_canvas = nullptr;

    QLineEdit        *m_nameEdit      = nullptr;
    QComboBox        *m_roleCombo     = nullptr;
    QComboBox        *m_geomCombo     = nullptr;
    QLabel           *m_crsLabel      = nullptr;

    QComboBox        *m_zSourceCombo  = nullptr;   ///< None / Constant / Raster / Mesh
    QComboBox        *m_zLayerCombo   = nullptr;   ///< raster or mesh layers
    QSpinBox         *m_zBandSpin     = nullptr;
    QDoubleSpinBox   *m_zConstantSpin = nullptr;
    QDoubleSpinBox   *m_zScaleSpin    = nullptr;
    QDoubleSpinBox   *m_zDensifySpin  = nullptr;
    QCheckBox        *m_zResampleBox  = nullptr;
    QLabel           *m_zHint         = nullptr;

    QTableWidget     *m_fieldTable    = nullptr;
    QPushButton      *m_addFieldBtn   = nullptr;
    QPushButton      *m_removeFieldBtn = nullptr;

    QDialogButtonBox *m_buttons       = nullptr;
};

}   // namespace openswmmvis::ui

#endif // NEWFEATURELAYERDIALOG_H
