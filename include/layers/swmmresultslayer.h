/*!
 * \file   swmmresultslayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */

#ifndef SWMMRESULTSLAYER_H
#define SWMMRESULTSLAYER_H

#include "layers/openswmmvislayer.h"
#include "layers/gisrasterlayer.h"   // RasterColorRamp
#include "layers/swmmmodellayer.h"

#include <QColor>
#include <QDateTime>
#include <QString>
#include <QVariantMap>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;

/*!
 * \enum SWMMResultVariable
 * \brief Selectable result output variables for colour-mapped display.
 */
enum class SWMMResultVariable
{
    // Node results
    NodeDepth           = 0,
    NodeHead            = 1,
    NodeVolume          = 2,
    NodeInflow          = 3,
    NodeOverflow        = 4,
    NodeLateralInflow   = 5,
    // Link results
    LinkFlow            = 10,
    LinkDepth           = 11,
    LinkVelocity        = 12,
    LinkCapacity        = 13,
    // Subcatchment results
    SubcatchRunoff      = 20,
    SubcatchInfiltration = 21,
    SubcatchEvaporation = 22,
    SubcatchSnowDepth   = 23,
};

/*!
 * \class SWMMResultsLayer
 * \brief Overlays time-stepped simulation results on the SWMM network geometry.
 * \details Reads output from an OpenSWMMCore binary results file (.out), maps
 *          a selected variable at the current simulation time step to a colour
 *          ramp, and paints the network elements with those colours.
 *
 *          Supports:
 *          - Animation: step forward/backward through the time series.
 *          - Any SWMMResultVariable (node depth/head, link flow/velocity, etc.).
 *          - A configurable RasterColorRamp for consistent colour mapping.
 *          - Legend rendering.
 *
 *          The layer delegates geometry loading to an associated SWMMModelLayer
 *          and only manages the colour-mapping / time-stepped rendering.
 */
class SWMMResultsLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

    Q_PROPERTY(QString   resultsFilePath  READ resultsFilePath   NOTIFY resultsFilePathChanged)
    Q_PROPERTY(int       currentTimeStep  READ currentTimeStep   WRITE setCurrentTimeStep
               NOTIFY currentTimeStepChanged)
    Q_PROPERTY(QDateTime currentDateTime  READ currentDateTime   NOTIFY currentDateTimeChanged)
    Q_PROPERTY(int       totalTimeSteps   READ totalTimeSteps    NOTIFY totalTimeStepsChanged)
    Q_PROPERTY(SWMMResultVariable variable READ variable         WRITE setVariable
               NOTIFY variableChanged)
    Q_PROPERTY(bool      showLegend       READ showLegend        WRITE setShowLegend
               NOTIFY showLegendChanged)

public:

    explicit SWMMResultsLayer(const QString &resultsFilePath,
                              class SWMMModelLayer *modelLayer,
                              OpenSWMMVisWorkspace *parent = nullptr);

    ~SWMMResultsLayer() override;

    // ----- Results file ---------------------------------------------------

    [[nodiscard]] QString resultsFilePath() const;

    /*!
     * \brief Opens the binary results file.
     * \returns true if the file was opened and indexed successfully.
     */
    bool openResults(QList<QString> &warnings, QList<QString> &errors);

    void closeResults();

    // ----- Animation ------------------------------------------------------

    [[nodiscard]] int       currentTimeStep() const;
    [[nodiscard]] QDateTime currentDateTime() const;
    [[nodiscard]] int       totalTimeSteps()  const;
    [[nodiscard]] QDateTime startDateTime()   const;
    [[nodiscard]] QDateTime endDateTime()     const;

    /*!
     * \brief Seeks to the given 0-based time step.
     */
    void setCurrentTimeStep(int step);

    /*!
     * \brief Advances to the next time step (wraps at end if \p loop is true).
     */
    void stepForward(bool loop = false);

    /*!
     * \brief Goes back one time step (wraps at start if \p loop is true).
     */
    void stepBackward(bool loop = false);

    // ----- Variable & colour mapping --------------------------------------

    [[nodiscard]] SWMMResultVariable variable() const;
    void setVariable(SWMMResultVariable var);

    [[nodiscard]] RasterColorRamp colorRamp() const;
    void setColorRamp(const RasterColorRamp &ramp);

    /*!
     * \brief Automatically sets the colour ramp range from the data min/max
     *        across all time steps for the current variable.
     */
    void autoStretchColorRamp();

    // ----- Legend ---------------------------------------------------------

    [[nodiscard]] bool showLegend() const;
    void setShowLegend(bool show);

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

signals:
    void resultsFilePathChanged(const QString &path);
    void currentTimeStepChanged(int step);
    void currentDateTimeChanged(const QDateTime &dt);
    void totalTimeStepsChanged(int count);
    void variableChanged(SWMMResultVariable var);
    void showLegendChanged(bool show);
    void resultsOpened();
    void resultsError(const QString &message);

private:
    QString              m_resultsFilePath;
    class SWMMModelLayer      *m_modelLayer     = nullptr;  /*!< Non-owning reference. */
    int                  m_currentStep    = 0;
    int                  m_totalSteps     = 0;
    QDateTime            m_startDateTime;
    QDateTime            m_endDateTime;
    SWMMResultVariable   m_variable       = SWMMResultVariable::NodeDepth;
    RasterColorRamp      m_colorRamp;
    bool                 m_showLegend     = true;

    // GDAL coordinate transform
    class OGRCoordinateTransformation *m_transform = nullptr;
};

Q_DECLARE_METATYPE(SWMMResultsLayer *)
Q_DECLARE_METATYPE(SWMMResultVariable)

#endif // SWMMRESULTSLAYER_H
