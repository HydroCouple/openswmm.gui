/*!
 * \file   swmmmodellayer.h
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

#ifndef SWMMMODELLAYER_H
#define SWMMMODELLAYER_H

#include "layers/openswmmvislayer.h"

#include <QColor>

#ifdef HAVE_OPENSWMMCORE
#include <openswmm/engine/openswmm_callbacks.h>  // SWMM_Engine typedef
#else
typedef void* SWMM_Engine;
#endif
#include <QFont>
#include <QPen>
#include <QBrush>
#include <QVariantMap>

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;

/*!
 * \struct SWMMElementSymbol
 * \brief Rendering style for a class of SWMM network elements.
 */
struct SWMMElementSymbol
{
    QColor  fillColor    = Qt::blue;
    QColor  outlineColor = Qt::darkBlue;
    double  outlineWidth = 1.0;
    double  size         = 8.0;    /*!< Marker diameter / line width in pixels. */
    bool    showLabel    = false;
    QFont   labelFont;
    QColor  labelColor   = Qt::black;
};

/*!
 * \class SWMMModelLayer
 * \brief Renders the SWMM network elements (nodes, links, subcatchments, rain gages)
 *        for one OpenSWMMCore model.
 * \details The layer uses the coordinate frame of the OpenSWMMCore model as its
 *          native CRS.  When the canvas CRS differs, coordinates are reprojected
 *          using GDAL's OGRCoordinateTransformation.
 *
 *          The layer also provides:
 *          - Selection tracking (highlighted with a secondary colour).
 *          - Identify-by-point returning element attributes as QVariantMap.
 *          - Label display driven by element names.
 */
class SWMMModelLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

    Q_PROPERTY(QString modelFilePath  READ modelFilePath  NOTIFY modelFilePathChanged)
    Q_PROPERTY(bool    showNodes      READ showNodes      WRITE setShowNodes
               NOTIFY showNodesChanged)
    Q_PROPERTY(bool    showLinks      READ showLinks      WRITE setShowLinks
               NOTIFY showLinksChanged)
    Q_PROPERTY(bool    showSubcatchments READ showSubcatchments
               WRITE setShowSubcatchments NOTIFY showSubcatchmentsChanged)
    Q_PROPERTY(bool    showRainGages  READ showRainGages  WRITE setShowRainGages
               NOTIFY showRainGagesChanged)
    Q_PROPERTY(bool    showLabels     READ showLabels     WRITE setShowLabels
               NOTIFY showLabelsChanged)

public:

    explicit SWMMModelLayer(const QString &modelFilePath,
                            OpenSWMMVisWorkspace *parent = nullptr);

    ~SWMMModelLayer() override;

    // ----- Model file -----------------------------------------------------

    [[nodiscard]] QString modelFilePath() const;
    void setModelFilePath(const QString &path);

    /** Raw engine handle — valid only after a successful loadModel(). */
    [[nodiscard]] SWMM_Engine engine() const;

    /*!
     * \brief Loads (or reloads) the SWMM input file and rebuilds geometry caches.
     * \returns true on success.
     */
    bool loadModel(QList<QString> &warnings, QList<QString> &errors);

    /** Close and destroy the engine, clearing all geometry caches. */
    void closeEngine();

    // ----- Element visibility toggles -------------------------------------

    [[nodiscard]] bool showNodes()        const;
    void setShowNodes(bool show);

    [[nodiscard]] bool showLinks()        const;
    void setShowLinks(bool show);

    [[nodiscard]] bool showSubcatchments() const;
    void setShowSubcatchments(bool show);

    [[nodiscard]] bool showRainGages()    const;
    void setShowRainGages(bool show);

    [[nodiscard]] bool showLabels()       const;
    void setShowLabels(bool show);

    // ----- Symbology ------------------------------------------------------

    [[nodiscard]] SWMMElementSymbol junctionSymbol()   const;
    void setJunctionSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol outfallSymbol()    const;
    void setOutfallSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol storageSymbol()    const;
    void setStorageSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol dividerSymbol()    const;
    void setDividerSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol conduitSymbol()    const;
    void setConduitSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol pumpSymbol()       const;
    void setPumpSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol orificeSymbol()    const;
    void setOrificeSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol weirSymbol()       const;
    void setWeirSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol subcatchmentSymbol() const;
    void setSubcatchmentSymbol(const SWMMElementSymbol &s);

    [[nodiscard]] SWMMElementSymbol rainGageSymbol()   const;
    void setRainGageSymbol(const SWMMElementSymbol &s);

    // ----- Selection ------------------------------------------------------

    /*!
     * \brief Returns the names of currently selected network elements.
     */
    [[nodiscard]] QStringList selectedElementNames() const;

    /*!
     * \brief Selects network elements by name (replaces prior selection).
     */
    void setSelectedElementNames(const QStringList &names);

    void clearSelection();

    // ----- Identify -------------------------------------------------------

    /*!
     * \brief Returns attributes of the network element closest to the map point.
     * \param mapX        X in canvas CRS.
     * \param mapY        Y in canvas CRS.
     * \param canvasSRS   Canvas CRS (may be nullptr).
     * \param tolerance   Search radius in map units.
     * \returns           Attribute map, or an empty map if nothing was found.
     *                    Includes "elementType", "elementName", and all SWMM properties.
     */
    [[nodiscard]] QVariantMap identifyAt(double mapX, double mapY,
                                         const SpatialReferenceSystem *canvasSRS,
                                         double tolerance = 1e-6) const;

    /*!
     * \brief Convenience overload — no CRS needed.
     */
    [[nodiscard]] QVariantMap identifyAt(double mapX, double mapY,
                                         double tolerance = 1e-6) const;

    /*!
     * \brief Identify an object by name (instead of by map coordinates).
     * \details Used by panels that already know the object reference and just
     *          need its attribute map (Object Browser → AttributePanel).
     *          Returns an empty map if the name doesn't match any cached
     *          node / link / subcatchment / gage.
     */
    [[nodiscard]] QVariantMap identifyByName(const QString &name) const;

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    /*!
     * \brief Re-read every node / link / subcatchment coordinate from the
     *        engine and rebuild the geometry cache + extent.
     * \details Use after a coordinate-mutating operation (CRS reproject, bulk
     *          coordinate edit) so cached vertices match engine state.
     */
    void reloadGeometry();

    /*!
     * \brief Classify a name into its SWMM object class by looking it up in
     *        the geometry cache.
     * \details Used by the SelectionManager bridge to translate the layer's
     *          name-only selection set into typed SWMMObjectRefs.
     * \return  0=Unknown, 1=Node, 2=Link, 3=Subcatchment, 4=RainGage —
     *          matches the SWMMObjectRef::ObjectType enum values.
     */
    [[nodiscard]] int objectTypeFor(const QString &name) const;

signals:
    void modelFilePathChanged(const QString &path);
    void showNodesChanged(bool show);
    void showLinksChanged(bool show);
    void showSubcatchmentsChanged(bool show);
    void showRainGagesChanged(bool show);
    void showLabelsChanged(bool show);
    void selectionChanged(const QStringList &selectedNames);
    void modelLoaded();
    void modelLoadError(const QString &errorMessage);

private:
    struct NodeGeom    { double x, y; int objectType; int nodeType; QString name; };
    struct LinkGeom    { QVector<QPointF> vertices; int linkType; QString name; };
    struct CatchGeom   { QVector<QPointF> vertices; QString name; };

    void buildGeometryCache();
    void rebuildTransform(const SpatialReferenceSystem *canvasSRS);

    SWMM_Engine                  m_engine          = nullptr;

    QString                      m_modelFilePath;
    bool                         m_showNodes       = true;
    bool                         m_showLinks       = true;
    bool                         m_showSubcatchments = true;
    bool                         m_showRainGages   = true;
    bool                         m_showLabels      = false;

    QVector<NodeGeom>            m_nodes;
    QVector<LinkGeom>            m_links;
    QVector<CatchGeom>           m_catchments;
    QVector<NodeGeom>            m_gages;

    SWMMElementSymbol            m_junctionSym;
    SWMMElementSymbol            m_outfallSym;
    SWMMElementSymbol            m_storageSym;
    SWMMElementSymbol            m_dividerSym;
    SWMMElementSymbol            m_conduitSym;
    SWMMElementSymbol            m_pumpSym;
    SWMMElementSymbol            m_orificeSym;
    SWMMElementSymbol            m_weirSym;
    SWMMElementSymbol            m_subcatchSym;
    SWMMElementSymbol            m_gageSym;

    QStringList                  m_selectedNames;

    // GDAL transform (layer CRS → canvas CRS)
    class OGRCoordinateTransformation *m_transform = nullptr;

    // Dirty flag — skip scene rebuild when only the view extent changed
    bool                         m_needsRebuild = true;
};

Q_DECLARE_METATYPE(SWMMModelLayer *)
Q_DECLARE_METATYPE(SWMMElementSymbol)

#endif // SWMMMODELLAYER_H
