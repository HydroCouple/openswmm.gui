/*!
 * \file   openswmmvissession.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  A single-window editing session bound to one OpenSWMM workspace.
 *
 * \details OpenSWMMVisSession binds an OpenSWMMVisWorkspace (project data) to
 *          an OpenSWMMVisGraphicsView (interactive map overlay). Each project
 *          window in the MDI host owns exactly one session; creating or
 *          importing a second SWMM model opens a new session rather than
 *          reusing the existing one.
 *
 *          OpenSWMMVisSession derives from OpenSWMMVisLayer so it can be tracked
 *          inside the workspace's layer list and referenced polymorphically by
 *          the canvas.
 */

#ifndef SWMMVISSUBPROJECT_H
#define SWMMVISSUBPROJECT_H

#include "layers/openswmmvislayer.h"

class OpenSWMMVisWorkspace;
class OpenSWMMVisGraphicsView;

/*!
 * \class OpenSWMMVisSession
 * \brief Couples an OpenSWMMVisWorkspace (project data) with an
 *        OpenSWMMVisGraphicsView (map overlay) for one MDI project window.
 *
 * \details The session acts as the glue layer between:
 *  - The project/data side (OpenSWMMVisWorkspace — layers, SWMM model, undo).
 *  - The rendering side (OpenSWMMVisGraphicsView — scene, pan/zoom, tools).
 *
 *  populateScene() is intentionally delegated to child layers; the session
 *  itself does not add any scene items of its own.
 */
class OpenSWMMVisSession : public OpenSWMMVisLayer
{
    Q_OBJECT

public:

    /*!
     * \brief Constructs a session bound to \p parent workspace.
     * \param parent        Owning workspace; must not be nullptr when creating
     *                      a session as part of a project-open workflow.
     * \param swmmGraphicsView  Optional pre-created graphics view to bind
     *                          immediately; may be set later via setGraphicsView().
     */
    OpenSWMMVisSession(OpenSWMMVisWorkspace *parent,
                       OpenSWMMVisGraphicsView *swmmGraphicsView = nullptr);

    /*!
     * \brief Destructor.
     */
    virtual ~OpenSWMMVisSession();

    // ----- populateScene implementation (no-op — child layers populate) ------

    /*!
     * \brief No-op override — scene population is handled by the child layers
     *        registered with the workspace.
     */
    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override {}

    // ----- Graphics view binding ---------------------------------------------

    /*!
     * \brief Replaces (or clears) the bound graphics view.
     * \details Disconnects all signals from the old view before binding the
     *          new one.  Pass nullptr to detach without rebinding.
     * \param graphicsView  New graphics view to bind, or nullptr.
     */
    void setGraphicsView(OpenSWMMVisGraphicsView *graphicsView);

    /*!
     * \brief Returns the currently bound graphics view, or nullptr if none.
     */
    [[nodiscard]] OpenSWMMVisGraphicsView *graphicsView() const;

    // ----- Workspace access --------------------------------------------------

    /*!
     * \brief Returns the owning workspace for this session.
     * \note The returned pointer is non-owning; the workspace object outlives
     *       the session in the normal MDI project-window lifecycle.
     */
    [[nodiscard]] OpenSWMMVisWorkspace *project() const;

private:
    OpenSWMMVisGraphicsView *mGraphicsView;
    OpenSWMMVisWorkspace    *mProject;
};

Q_DECLARE_METATYPE(OpenSWMMVisSession *)

#endif // SWMMVISSUBPROJECT_H
