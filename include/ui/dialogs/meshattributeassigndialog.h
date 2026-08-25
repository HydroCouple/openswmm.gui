/*!
 * \file   meshattributeassigndialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Assign per-cell 2D mesh parameters from a GIS source: a raster band or a
 * vector layer's attribute field, mapped onto the mesh cells in one undoable
 * step.
 *
 * Complements the mesh-editing toolbar's cell editor — that prescribes one
 * value to a hand-picked selection; this prescribes a spatially varying field
 * to the whole mesh (or the current selection).
 *
 * Four mapping modes (GUI plan §3.4, phase GG0e):
 *
 *  - **Single numeric target** — the original behaviour: one band / one field
 *    → one mesh::cellParamSpecs() target, sampled at the cell centroid.
 *  - **Multiple numeric targets** — N bands / N fields → N parameters in one
 *    pass (Horton's f0 / fmin / decay together), still one undo entry.
 *  - **Classified infiltration lookup** — a categorical key field (landuse
 *    code, hydrologic soil group), optionally a second key (Curve Number is
 *    assigned by landuse × HSG), mapped through an editable, CSV-round-tripped
 *    mesh::InfilLookupTable to a whole mesh::InfilRow.
 *  - **Natural-neighbour interpolation** — for scattered point sources rather
 *    than coverages; a *sampling* choice rather than a target choice, so it
 *    composes with the numeric modes. Same mesh::NaturalNeighbourInterpolator
 *    and the same wording the mesh generation dialog uses for elevation.
 *
 * Sampling is orthogonal to the mode: cell centroid (point sample),
 * area-weighted mean, majority (largest covering share), or natural
 * neighbour. Overlay sampling is bound to the source's declared type —
 * majority for categorical, area-weighted mean for continuous — with an
 * explicit user override, because offering the wrong one silently produces
 * plausible nonsense.
 *
 * Numeric writes go through mesh::pushCellParamEdits and infiltration writes
 * through mesh::pushCellInfilEdit, so the result lands on the same undo stack
 * as every other cell edit and every view refreshes through the layer's
 * `attributeChanged` signal. Multi-target and multi-row assignments are
 * wrapped in a single QUndoStack macro so a whole assignment is one Ctrl+Z.
 *
 * Sampling runs on a QtConcurrent worker guarded by a QFutureWatcher, exactly
 * as MeshGenerationDialog does: the Job carries plain data only (no widgets,
 * no layers, no GDAL handles) and the worker RE-OPENS every raster / vector
 * source by path on its own thread, because GDAL/OGR handles must not cross
 * threads. Nothing is written to the mesh until the worker has finished
 * successfully, so cancelling mid-run leaves the mesh untouched.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H

#include "mesh/meshinfil.h"
#include "mesh/naturalnbinterpolator.h"

#include <QByteArray>
#include <QDialog>
#include <QFutureWatcher>
#include <QPointF>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class MapCanvas;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QCheckBox;
class QTableWidget;
class SelectionManager;
class SWMM2DMeshLayer;

namespace openswmmvis::ui {

class MeshAttributeAssignDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \brief Which source the dialog opens on. */
    enum class Source { Raster, Vector };

    /*! \brief What the assignment writes. */
    enum class Mode {
        SingleNumeric,     ///< One source value → one numeric cell parameter.
        MultiNumeric,      ///< N source values → N numeric cell parameters.
        ClassifiedInfil    ///< Key (or key pair) → a whole mesh::InfilRow.
    };

    /*! \brief How one cell's source value is obtained. */
    enum class Sampling {
        Centroid,          ///< Point sample at the cell centroid (original).
        OverlayAuto,       ///< Majority or area-weighted mean, by source type.
        AreaWeightedMean,  ///< Mean over the cell's footprint (continuous).
        Majority,          ///< Largest covering share wins (categorical).
        NaturalNeighbour   ///< Sibson / Laplace over scattered points.
    };

    /*! \brief Where the assignment lands (GUI plan §3.4 "Write as", engine D-I3). */
    enum class WriteTarget {
        CellOverrides,     ///< Per-cell [2D_INFILTRATION] override rows.
        RegionDefaults     ///< One [2D_INFILTRATION_DEFAULTS] row per region tag.
    };

    // -----------------------------------------------------------------------
    // Data exchanged with the sampling worker.
    //
    // Both structs are plain data: no QObject, no layer pointer, no GDAL
    // handle. collectJob() fills the Job on the GUI thread; the worker
    // re-opens every source by path on its own thread.
    // -----------------------------------------------------------------------

    /*! \brief Everything one sampling pass needs. */
    struct Job
    {
        Mode     mode     = Mode::SingleNumeric;
        Source   source   = Source::Raster;
        Sampling sampling = Sampling::Centroid;

        /*! Cells in scope. \ref centroids is parallel to it; \ref triVerts
         *  holds 3 vertices per entry (only filled for overlay sampling). */
        QVector<int>     triangles;
        QVector<QPointF> centroids;
        QVector<QPointF> triVerts;

        QString meshCrsWkt;          ///< Mesh layer CRS; "" = assume source CRS.

        // ---- Raster source ----
        QString      rasterPath;
        QVector<int> bands;          ///< Parallel to \ref targetKeys (numeric modes).
        int          keyBand1 = 1;   ///< Classified mode: band supplying key 1.
        int          keyBand2 = 0;   ///< Classified mode: 0 = single key.
        double       scale    = 1.0;
        double       offset   = 0.0;

        // ---- Vector source ----
        QString         vectorPath;
        QString         vectorLayerName;
        QString         vectorFilterExpr;
        QStringList     fields;      ///< Parallel to \ref targetKeys (numeric modes).
        QString         keyField1;   ///< Classified mode.
        QString         keyField2;   ///< Classified mode; "" = single key.
        bool            filterBySelection = false;
        QSet<long long> selectedIds;

        // ---- Numeric targets ----
        QVector<QByteArray> targetKeys;
        QVector<double>     targetMin;   ///< Parallel to \ref targetKeys.
        QVector<double>     targetMax;   ///< Parallel to \ref targetKeys.

        // ---- Classified infiltration ----
        mesh::InfilLookupTable table;

        // ---- Natural neighbour ----
        mesh::NaturalNeighbourInterpolator::Variant nnVariant =
            mesh::NaturalNeighbourInterpolator::Variant::Sibson;
    };

    /*! \brief Outcome of one sampling pass over the in-scope cells. */
    struct SampleResult
    {
        QVector<int> triangles;             ///< Cells that received a value.
        /*! Numeric modes: one row per target key, each parallel to
         *  \ref triangles. NaN = that target got nothing for that cell. */
        QVector<QVector<double>> values;
        /*! Classified mode: parallel to \ref triangles. */
        QVector<mesh::InfilRow> rows;
        /*! Classified mode: the source key text each cell resolved to,
         *  parallel to \ref triangles. Drives the tag-correspondence check. */
        QStringList keys;

        int  skippedNoData     = 0;  ///< NoData / outside the source.
        int  skippedNonNumeric = 0;  ///< Field value not a number.
        int  skippedRange      = 0;  ///< Outside the parameter's range.
        int  unmatchedKeys     = 0;  ///< Classified: fell through to the fallback row.
        int  scanned           = 0;  ///< Cells considered.
        bool cancelled         = false;
        /*! What Sampling::OverlayAuto resolved to, and why — reported so the
         *  user can see that a categorical source got majority rather than a
         *  meaningless average. Empty for the explicit choices. */
        QString resolvedSampling;
        QString error;               ///< Non-empty aborts the pass.
    };

    MeshAttributeAssignDialog(SWMM2DMeshLayer  *meshLayer,
                              MapCanvas        *canvas,
                              SelectionManager *selection,
                              Source            initialSource,
                              const QString    &depthUnitLabel,
                              QWidget          *parent = nullptr);

public slots:
    /*! \brief Esc / Close / the window button. While a sampling pass is
     *         running this cancels it instead of closing, so the dialog never
     *         disappears with a worker still writing into its watcher. */
    void reject() override;

private slots:
    void onSourceChanged();
    void onTargetChanged();
    void onModeChanged();
    void onSamplingChanged();
    void onPreview();
    void onApply();
    void onBrowseRaster();
    void onCancelOrClose();
    void onSampleFinished();

    // Multi-target table
    void onAddTargetRow();
    void onRemoveTargetRow();

    // Classified lookup table
    void onAddLookupRow();
    void onRemoveLookupRow();
    void onLoadLookupCsv();
    void onSaveLookupCsv();
    void onTwoKeyToggled();

private:
    void buildUi(Source initialSource, const QString &depthUnitLabel);
    void buildTargetGroup(const QString &depthUnitLabel);
    void buildLookupGroup();
    void populateLayerCombos();
    void refreshVectorFields();
    void updateButtons();
    void setRunning(bool running);

    /*! \brief Fill a numeric-parameter combo with the cellParamSpecs() entries
     *         (disabled ones greyed, as the original target combo does). */
    void fillParamCombo(QComboBox *combo, const QString &depthUnitLabel) const;

    [[nodiscard]] Mode     currentMode() const;
    [[nodiscard]] Sampling currentSampling() const;

    /*! \brief Triangle indices in scope (all cells, or the selected ones). */
    [[nodiscard]] QVector<int> scopeTriangles() const;

    /*! \brief Cell centroids, in the mesh layer's own CRS, parallel to \p tris. */
    [[nodiscard]] QVector<QPointF> centroidsFor(const QVector<int> &tris) const;

    /*! \brief Collect the whole sampling configuration into a worker-safe Job.
     *  \returns false with \p err set when the configuration is incomplete. */
    [[nodiscard]] bool collectJob(Job *job, QString *err) const;

    /*! \brief Start the worker for a preview (\p apply false) or an apply. */
    void startSampling(bool apply);

    /*! \brief Read the numeric-target rows (key + band/field) off the table. */
    [[nodiscard]] QVector<QByteArray> collectTargetKeys() const;

    /*! \brief Install the column-1 editor for a multi-target row: a band spin
     *         box for a raster source, a field combo for a vector one. */
    void setTargetRowSourceWidget(int row);

    /*! \brief Read the lookup-table editor into a mesh::InfilLookupTable.
     *         Row 0 is the "unmatched keys" fallback. */
    [[nodiscard]] mesh::InfilLookupTable collectLookupTable() const;
    void                                 applyLookupTable(const mesh::InfilLookupTable &t);
    void                                 rebuildLookupColumns();
    /*! \brief Grey out the parameter cells the row's method does not use, the
     *         same masking mesh::infilUsesParam() drives elsewhere. */
    void maskLookupRow(int row);

    /*! \brief Human-readable "n of m cells …" summary of \p r. */
    [[nodiscard]] QString summarise(const SampleResult &r) const;

    /*! \brief Write \p r to the mesh as one undo entry. */
    void applyResult(const SampleResult &r);
    void applyNumericResult(const SampleResult &r);
    void applyInfilResult(const SampleResult &r);
    /*! \brief WriteTarget::RegionDefaults — one [2D_INFILTRATION_DEFAULTS] row
     *  per source key naming an existing region tag, through
     *  mesh::pushInfilDefaultsEdit. Keys matching no tag are skipped and
     *  reported: a row for an unknown tag reaches no cell. */
    void applyInfilDefaultsResult(const SampleResult &r);

    QPointer<SWMM2DMeshLayer>  m_mesh;
    QPointer<MapCanvas>        m_canvas;
    QPointer<SelectionManager> m_selection;

    QComboBox      *m_modeCombo     = nullptr;
    QComboBox      *m_targetCombo   = nullptr;   // SingleNumeric
    QGroupBox      *m_singleGroup   = nullptr;
    QGroupBox      *m_multiGroup    = nullptr;
    QTableWidget   *m_targetTable   = nullptr;   // MultiNumeric
    QPushButton    *m_addTargetBtn  = nullptr;
    QPushButton    *m_delTargetBtn  = nullptr;
    QGroupBox      *m_lookupGroup   = nullptr;   // ClassifiedInfil
    QTableWidget   *m_lookupTable   = nullptr;
    QComboBox      *m_key1Combo     = nullptr;
    QComboBox      *m_key2Combo     = nullptr;
    QSpinBox       *m_key1BandSpin  = nullptr;
    QSpinBox       *m_key2BandSpin  = nullptr;
    QCheckBox      *m_twoKeyCheck   = nullptr;
    QLabel         *m_key1Label     = nullptr;
    QLabel         *m_key2Label     = nullptr;

    QRadioButton   *m_srcRaster     = nullptr;
    QRadioButton   *m_srcVector     = nullptr;
    QComboBox      *m_rasterCombo   = nullptr;
    QPushButton    *m_browseBtn     = nullptr;
    QSpinBox       *m_bandSpin      = nullptr;
    QDoubleSpinBox *m_scaleSpin     = nullptr;
    QDoubleSpinBox *m_offsetSpin    = nullptr;
    QComboBox      *m_vectorCombo   = nullptr;
    QComboBox      *m_fieldCombo    = nullptr;
    QCheckBox      *m_selectedOnly  = nullptr;

    QComboBox      *m_samplingCombo = nullptr;
    QComboBox      *m_nnVariantCombo = nullptr;
    QLabel         *m_nnVariantLbl  = nullptr;

    QRadioButton   *m_writeOverrides = nullptr;
    QRadioButton   *m_writeDefaults  = nullptr;
    QCheckBox      *m_keepInherited  = nullptr;
    QGroupBox      *m_writeGroup     = nullptr;

    QRadioButton   *m_scopeAll      = nullptr;
    QRadioButton   *m_scopeSelected = nullptr;
    QLabel         *m_statusLbl     = nullptr;
    QProgressBar   *m_progress      = nullptr;
    QPushButton    *m_previewBtn    = nullptr;
    QPushButton    *m_applyBtn      = nullptr;
    QPushButton    *m_closeBtn      = nullptr;

    /*! Path typed via Browse… (empty when a canvas layer is chosen). */
    QString m_browsedRasterPath;
    /*! UnitSystem::depthLabel(), kept so the multi-target rows label their
     *  length parameters the same way the single-target combo does. */
    QString m_depthUnitLabel;

    /*! Live sampling worker; null when idle. Owned via QObject parenting and
     *  cleared in onSampleFinished(). */
    QFutureWatcher<SampleResult> *m_watcher = nullptr;
    /*! Configuration the running / last-finished pass used. The result carries
     *  values, not the target keys they belong to, so the completion handler
     *  reads them back from here. */
    Job m_runJob;
    /*! True when the running pass should write its result to the mesh. */
    bool m_applyPending = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H
