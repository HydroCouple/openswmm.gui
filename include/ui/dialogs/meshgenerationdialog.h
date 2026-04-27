/*!
 * \file   meshgenerationdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice AU.4 — Tools → Generate Mesh… and the editing-toolbar's
 * `actionGenerateMesh`. Single-page form that:
 *
 *  1. Pulls the active SWMM model's junctions / outfalls / storage as
 *     Steiner points (TAG = node id) and conduits as constraint segments
 *     (segment marker → conduit id), so the resulting mesh aligns with
 *     1D coupling points by construction (Slice AU.3 tagging rules).
 *  2. Samples a user-picked DTM raster (loaded `GISRasterLayer`) at
 *     each output vertex via `mesh::DTMSampler`.
 *  3. Builds a CouplingMap (vertex/triangle → SWMM node ids) so the
 *     engine reads `[2D_VERTEX_NODE_MAP]` / `[2D_TRIANGLE_NODE_MAP]`
 *     without post-processing.
 *  4. Writes a sibling `.2dm` and patches `[2D_MESH_FILE]` (default), or
 *     inlines the four sections in the `.inp`.
 *
 * Manning's source extension (categorical raster / shapefile field) is
 * stubbed — first cut ships with a constant value the user enters.
 */
#ifndef MESHGENERATIONDIALOG_H
#define MESHGENERATIONDIALOG_H

#include <QDialog>
#include <QString>

class SWMMVisProjectWindow;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;

class MeshGenerationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MeshGenerationDialog(SWMMVisProjectWindow *pw,
                                  QWidget *parent = nullptr);
    ~MeshGenerationDialog() override = default;

private slots:
    void onBrowseMeshPath();
    void onApply();
    void onAccept();

private:
    void buildUi();
    void seedDefaults();
    void populateRasterCombo();

    /*! Run the full pipeline:
     *  - assemble PSLG inputs from the SWMM model + dialog choices
     *  - call mesh::MeshGenerator
     *  - sample DTM elevations
     *  - build CouplingMap
     *  - write via mesh::InpMeshWriter (External or Inline)
     *  Returns true on success; false sets *errorOut.
     */
    bool runPipeline(QString *errorOut);

    SWMMVisProjectWindow *m_pw = nullptr;

    // ── Sources ─────────────────────────────────────────────────────
    QComboBox     *m_dtmCombo        = nullptr;  ///< Raster layer picker.
    QLabel        *m_domainLabel     = nullptr;  ///< Read-only summary of model extent.

    // ── Auxiliary feature-layer constraints (all optional) ──────────
    QComboBox     *m_boundaryLayerCombo = nullptr;   ///< Polygon layer = mesh boundary (single).
    QListWidget   *m_pointLayersList    = nullptr;   ///< Point layers = extra Steiner points (multi-select via checkboxes).
    QListWidget   *m_lineLayersList     = nullptr;   ///< Line layers = extra constraint segments (multi-select via checkboxes).

    // ── Constraints (SWMM-aware tagging) ────────────────────────────
    QCheckBox     *m_includeJunctions = nullptr;
    QCheckBox     *m_includeConduits  = nullptr;
    QCheckBox     *m_includeSubcatch  = nullptr;

    // ── Quality ─────────────────────────────────────────────────────
    QDoubleSpinBox *m_maxAreaSpin    = nullptr;
    QDoubleSpinBox *m_minAngleSpin   = nullptr;
    QSpinBox       *m_maxSteinerSpin = nullptr;
    QCheckBox      *m_allowSteiner   = nullptr;

    // ── Roughness / Manning's ───────────────────────────────────────
    QRadioButton  *m_manningsConstant     = nullptr;  ///< default selected
    QRadioButton  *m_manningsCategorical  = nullptr;  ///< stub for AU follow-up
    QRadioButton  *m_manningsField        = nullptr;  ///< stub for AU follow-up
    QDoubleSpinBox *m_manningsValueSpin   = nullptr;

    // ── Output ──────────────────────────────────────────────────────
    QRadioButton  *m_outputExternal  = nullptr;  ///< default; .2dm sibling
    QRadioButton  *m_outputInline    = nullptr;
    QLineEdit     *m_meshPathEdit    = nullptr;
    QPushButton   *m_browseMeshBtn   = nullptr;

    // ── Progress ────────────────────────────────────────────────────
    QProgressBar  *m_progressBar     = nullptr;  ///< Hidden until Generate clicked.
    QLabel        *m_progressLabel   = nullptr;
};

#endif // MESHGENERATIONDIALOG_H
