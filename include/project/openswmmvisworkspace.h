/**
 * @file      openswmmvisworkspace.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Top-level project container — owns sessions, the SWMM model layer,
 *         and project-level persistence paths.
 *
 * @details OpenSWMMVisWorkspace corresponds to a single `.oswp` / `.inp` pair
 *          on disk.  It holds the collection of OpenSWMMVisSession objects
 *          (one per MDI window), a reference to the shared SWMMModelLayer,
 *          and the paths required for loading and saving the project.
 *
 *          newInstance() allocates the workspace and records the supplied path;
 *          loading and persistence remain delegated to the application windows.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMVISPROJECT_H
#define SWMMVISPROJECT_H

#include <QObject>

class OpenSWMMVisSession;
class SWMMModelLayer;

/*!
 * \class OpenSWMMVisWorkspace
 * \brief Top-level project container managing sessions, the SWMM model layer,
 *        and persistence paths for one SWMMVis project.
 *
 * \details The workspace records:
 *  - The project file path (`.oswp` or a path to an `.inp`).
 *  - The (optional) separate SWMM input file path when an `.oswp` sidecar is used.
 *  - The list of open sessions (one per MDI sub-window).
 *  - The single SWMMModelLayer that is bound to the project.
 *
 *  Neither constructor nor newInstance() opens files. Binding a model layer
 *  stores a borrowed pointer without changing its QObject parent.
 */
class OpenSWMMVisWorkspace : public QObject
{

    Q_OBJECT
    Q_PROPERTY(QString ProjectFilePath READ projectPath NOTIFY projectFilePathChanged)
    Q_PROPERTY(QString Title READ title NOTIFY projectTileChanged)

public:
    /**
     * @brief Construct an empty workspace with the legacy Untitled Project.svz path.
     * @param[in] parent Optional QObject owner. No model is opened.
     */
    OpenSWMMVisWorkspace(QObject *parent = nullptr);

    /**
     * @brief Record an initial project path without reading it.
     * @param[in] projectFilepath Path as supplied; may be empty.
     * @param[in] parent Optional QObject owner.
     */
    OpenSWMMVisWorkspace(const QString &projectFilepath = "", QObject *parent = nullptr);

    /*! OpenSWMMVisWorkspace destructor */
    virtual ~OpenSWMMVisWorkspace();

    /*!
     * \brief projectPath getter method
     * \return QString project file path
     */
    QString projectPath() const;

    /*!
     * \brief projectName getter method
     * \return QString project name
     * \sa setProjectName
     */
    QString title() const;

    /*!
     * \brief Get the path to the SWMM project file
     * \sa projectName
     * @return Bound model file path recorded at binding, or empty when no nonempty model path was supplied.
     */
    QString swmmProjectPath() const;

    /*!
     * \brief Returns the SWMM model layer bound to this project.
     * @return Borrowed bound layer pointer, initially null. Binding does not reparent or delete it.
     */
    SWMMModelLayer *swmmModelLayer() const;

    /*!
     * \brief Binds a SWMM model layer to the project.
     * \details Exactly one model layer can be bound to each project.
     * @param[in] modelLayer Non-null borrowed layer; only the first distinct layer can be bound.
     * @param[in,out] warnings Appends a warning when the same pointer is already bound.
     * @param[in,out] errors Appends a diagnostic for null or a conflicting existing layer.
     * @return True for a new or repeated binding; false leaves the binding unchanged.
     * @note A new binding sets hasChanges(), copies a nonempty model path, and emits layerChanged().
     */
    bool setSWMMModelLayer(SWMMModelLayer *modelLayer, QList<QString> &warnings,
                           QList<QString> &errors);

    /*!
     * \brief layers getter method
     * \return Snapshot of the session pointers currently recorded in the workspace.
     * 	* \sa addLayer
     * \sa removeLayer
     * 	*/
    QVector<OpenSWMMVisSession *> layers() const;

    /*!
     * \brief Saves the workspace project to the given file path.
     * \param filepath  Destination path (`.oswp` or `.inp`).
     * \param warnings  Populated with non-fatal save warnings.
     * \param errors    Populated with error messages on failure.
     * \return true on success; false otherwise.
     * \note  Currently unimplemented at the workspace level; per-window save
     *        is handled by SWMMVisProjectWindow::saveAs(). Calls log a
     *        warning and append an error explaining the limitation.
     */
    bool saveProject(const QString &filepath, QList<QString> &warnings, QList<QString> &errors);

    /*!
     * \brief Saves the SWMM model file companion (.inp) to the given path.
     * \param filepath  Destination `.inp` path.
     * \param warnings  Populated with non-fatal save warnings.
     * \param errors    Populated with error messages on failure.
     * \return true on success; false otherwise.
     * \note  Currently unimplemented; see saveProject() for the same routing
     *        contract.
     */
    bool saveSWMMProject(const QString &filepath, QList<QString> &warnings, QList<QString> &errors);

    /**
     * @brief Report that workspace-level model opening is currently unavailable.
     * @param[in] filepath Unused requested input path.
     * @param[in,out] warnings Unchanged by this stub.
     * @param[in,out] errors Receives an appended routing diagnostic.
     * @return Always false; also logs a warning. Open through the application window instead.
     */
    bool openSWMMModel(const QString &filepath, QList<QString> &warnings, QList<QString> &errors);

    /**
     * @brief Allocate a workspace containing the requested path, without opening any files.
     * @param[in] projectFilepath Initial project path; empty creates an untitled path value.
     * @param[in] parent Optional QObject owner of the allocation.
     * @return Newly allocated workspace; caller manages its lifetime when parent is null.
     */
    static OpenSWMMVisWorkspace *newInstance(const QString &projectFilepath = "",
                                             QObject *parent = nullptr);

    /**
     * @brief Inspect the workspace dirty flag set by the first successful model binding.
     * @return Stored flag; current workspace save stubs never clear it, and model edits are not observed here.
     */
    bool hasChanges() const;

signals:

    /**
     * @brief Announce the first accepted model-layer binding.
     */
    void layerChanged();

    /**
     * @brief Reserved notification for project-path changes; current implementation never emits it.
     */
    void projectFilePathChanged();

    /**
     * @brief Reserved title notification; current implementation never emits it.
     */
    void projectTileChanged();

private:
    /**
     * @brief Reserved project-file initialization interface; no implementation is currently provided.
     * @param[in] projectFilepath Requested source path.
     * @param[in,out] warnings Reserved nonfatal diagnostic collection.
     * @param[in,out] errors Reserved failure diagnostic collection.
     * @return Reserved initialization result; callers must not rely on this undefined interface.
     */
    bool initializeFromProjectFile(const QString &projectFilepath, QList<QString> &warnings,
                                   QList<QString> &errors);

private:
    QVector<OpenSWMMVisSession *> mLayers;
    QString mProjectPath;
    QString mSWMMProjectPath;
    QString mTitle;
    SWMMModelLayer *mSWMMModelLayer;
    bool mHasChanges;
};

Q_DECLARE_METATYPE(OpenSWMMVisWorkspace *)
Q_DECLARE_METATYPE(QVector<OpenSWMMVisWorkspace *>)

#endif // SWMMVISPROJECT_H
