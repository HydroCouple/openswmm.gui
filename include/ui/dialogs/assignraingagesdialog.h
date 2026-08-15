/*!
 * \file   assignraingagesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Bind rain gages to subcatchments spatially, in one undoable step.
 *
 * Two methods:
 *
 *   Proximity — assign the gage whose Thiessen (Voronoi) cell covers the
 *     largest share of the subcatchment's area. Uses the real watershed
 *     boundary rather than a representative point, which matters as soon as a
 *     subcatchment is large relative to the gage spacing.
 *
 *   Interpolated — area-average natural-neighbour weights over the gage
 *     network for each subcatchment, group subcatchments whose weight vectors
 *     agree, and materialise each group as a generated gage backed by a
 *     generated series. SWMM binds exactly one gage per subcatchment, so an
 *     interpolated field cannot be expressed any other way.
 *
 * Everything is computed before any mutation: the volume-conservation gate can
 * therefore abort with nothing written. The whole assignment is one undo macro.
 *
 * Complements the per-object gage field in the Properties panel and the
 * attribute table — those set one subcatchment at a time.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H

#include "core/gageblend.h"

#include <QDialog>
#include <QPointF>
#include <QPointer>
#include <QStringList>
#include <QVector>

class MapCanvas;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTableWidget;
class SelectionManager;
class SWMMModelLayer;

namespace openswmmvis::ui {

class AssignRainGagesDialog : public QDialog
{
    Q_OBJECT

public:
    AssignRainGagesDialog(SWMMModelLayer   *layer,
                          MapCanvas        *canvas,
                          SelectionManager *selection,
                          QWidget          *parent = nullptr);

private slots:
    void onMethodChanged();
    void onPreview();
    void onApply();

private:
    /*! \brief A rain gage that is usable as an interpolation site. */
    struct GageSite
    {
        int     index = -1;   ///< Engine gage index.
        QString name;
        QPointF pos;          ///< Engine coordinate, never the display cache.
    };

    /*! \brief One subcatchment's outcome. */
    struct RowPlan
    {
        QString subcatch;
        QString oldGage;
        QString newGage;
        QString detail;            ///< Area share, or the weight vector.
        bool    changed = false;
    };

    /*! \brief One synthesised gage + series, shared by a cluster. */
    struct GeneratedGage
    {
        QString                          gageName;
        QString                          seriesName;
        QString                          key;          ///< Canonical weight key.
        QVector<GageBlend::SeriesPoint>  points;
        qint64                           intervalSec = 0;
        double                           snowFactor  = 1.0;
        double                           relError    = 0.0;
        QStringList                      members;      ///< Subcatchment names.
        bool                             isNew    = false;
        bool                             isUpdate = false;
    };

    /*! \brief The complete, still-unapplied outcome of one computation. */
    struct Plan
    {
        QVector<RowPlan>       rows;
        QVector<GeneratedGage> generated;
        QStringList            staleGages;   ///< Generated gages now unused.
        QStringList            warnings;
        QString                error;        ///< Non-empty aborts everything.
        int                    scanned   = 0;
        int                    skipped   = 0;
        int                    changed   = 0;
    };

    void buildUi();
    void updateButtons();

    /*! \brief Gages with a real `[SYMBOLS]` coordinate, coincidences removed. */
    [[nodiscard]] QVector<GageSite> eligibleGages(QStringList *warnings,
                                                  QStringList *unlocated) const;

    /*! \brief Subcatchment indices in scope (all, or the current selection). */
    [[nodiscard]] QVector<int> scopeSubcatchments() const;

    [[nodiscard]] Plan buildPlan();
    [[nodiscard]] Plan buildProximityPlan(const QVector<GageSite> &gages,
                                          const QVector<int> &scope,
                                          QStringList warnings);
    [[nodiscard]] Plan buildInterpolatedPlan(const QVector<GageSite> &gages,
                                             const QVector<int> &scope,
                                             QStringList warnings,
                                             const QStringList &unlocated);

    /*! \brief Read a gage's series from the ENGINE (authoritative — every
     *         editor flushes to it immediately after mutating). */
    [[nodiscard]] bool readSourceGage(const GageSite &site,
                                      GageBlend::SourceGage *out,
                                      QString *error) const;

    void showPlan(const Plan &plan, bool applied);

    QPointer<SWMMModelLayer>   m_layer;
    QPointer<MapCanvas>        m_canvas;
    QPointer<SelectionManager> m_selection;

    QRadioButton   *m_methodProximity = nullptr;
    QRadioButton   *m_methodInterp    = nullptr;
    QComboBox      *m_variantCombo    = nullptr;
    QDoubleSpinBox *m_tolSpin         = nullptr;
    QRadioButton   *m_scopeAll        = nullptr;
    QRadioButton   *m_scopeSelected   = nullptr;
    QLabel         *m_gageCountLbl    = nullptr;
    QTableWidget   *m_preview         = nullptr;
    QLabel         *m_statusLbl       = nullptr;
    QPushButton    *m_previewBtn      = nullptr;
    QPushButton    *m_applyBtn        = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H
