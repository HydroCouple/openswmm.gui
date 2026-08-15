/*!
 * \file   meshattributeassigndialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Assign a per-cell 2D mesh parameter (Manning's n, initial depth, …) from a
 * GIS source: a raster sampled at each cell centroid, or a polygon layer's
 * attribute field joined by containment.
 *
 * Complements the mesh-editing toolbar's cell editor — that prescribes one
 * value to a hand-picked selection; this prescribes a spatially varying field
 * to the whole mesh (or the current selection) in one undoable step.
 *
 * Writes go through mesh::pushCellParamEdits, so the result lands on the same
 * undo stack as every other cell edit and every view refreshes through the
 * layer's `attributeChanged` signal.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QVector>

class MapCanvas;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QCheckBox;
class SelectionManager;
class SWMM2DMeshLayer;

namespace openswmmvis::ui {

class MeshAttributeAssignDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \brief Which source the dialog opens on. */
    enum class Source { Raster, Vector };

    MeshAttributeAssignDialog(SWMM2DMeshLayer  *meshLayer,
                              MapCanvas        *canvas,
                              SelectionManager *selection,
                              Source            initialSource,
                              const QString    &depthUnitLabel,
                              QWidget          *parent = nullptr);

private slots:
    void onSourceChanged();
    void onTargetChanged();
    void onPreview();
    void onApply();
    void onBrowseRaster();

private:
    /*! \brief Outcome of one sampling pass over the in-scope cells. */
    struct SampleResult
    {
        QVector<int>    triangles;   ///< cells that received a value
        QVector<double> values;      ///< parallel to \ref triangles
        int             skippedNoData   = 0;  ///< NoData / outside the source
        int             skippedNonNumeric = 0;///< field value not a number
        int             skippedRange    = 0;  ///< outside the parameter's range
        int             scanned         = 0;  ///< cells considered
        QString         error;                ///< non-empty aborts the pass
    };

    void buildUi(Source initialSource, const QString &depthUnitLabel);
    void populateLayerCombos();
    void refreshVectorFields();
    void updateButtons();

    /*! \brief Triangle indices in scope (all cells, or the selected ones). */
    [[nodiscard]] QVector<int> scopeTriangles() const;

    /*! \brief Cell centroids, in the mesh layer's own CRS, parallel to \p tris. */
    [[nodiscard]] QVector<QPointF> centroidsFor(const QVector<int> &tris) const;

    /*! \brief Run the configured sampling over \p tris. Blocking, but bounded
     *         by the mesh size and reported through the status label. */
    [[nodiscard]] SampleResult sample(const QVector<int> &tris);
    [[nodiscard]] SampleResult sampleRaster(const QVector<int> &tris);
    [[nodiscard]] SampleResult sampleVector(const QVector<int> &tris);

    QPointer<SWMM2DMeshLayer>  m_mesh;
    QPointer<MapCanvas>        m_canvas;
    QPointer<SelectionManager> m_selection;

    QComboBox      *m_targetCombo   = nullptr;
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
    QRadioButton   *m_scopeAll      = nullptr;
    QRadioButton   *m_scopeSelected = nullptr;
    QLabel         *m_statusLbl     = nullptr;
    QPushButton    *m_previewBtn    = nullptr;
    QPushButton    *m_applyBtn      = nullptr;

    /*! Path typed via Browse… (empty when a canvas layer is chosen). */
    QString m_browsedRasterPath;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESHATTRIBUTEASSIGNDIALOG_H
