/*!
 * \file   openswmmvisworkspace.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Top-level project container — owns sessions, the SWMM model layer,
 *         and project-level persistence paths.
 *
 * \details OpenSWMMVisWorkspace corresponds to a single `.oswp` / `.inp` pair
 *          on disk.  It holds the collection of OpenSWMMVisSession objects
 *          (one per MDI window), a reference to the shared SWMMModelLayer,
 *          and the paths required for loading and saving the project.
 *
 *          Instances are normally created via the static factory method
 *          newInstance() rather than directly, so that the constructor's
 *          two-phase initialisation (construct then initializeFromProjectFile)
 *          is always completed correctly.
 */
#ifndef SWMMVISPROJECT_H
#define SWMMVISPROJECT_H

#include <QObject>

class OpenSWMMVisSession;
class SWMMModelLayer;

/*!
 * \class OpenSWMMVisWorkspace
 * \brief Top-level project container managing sessions, the SWMM model layer,
 *        and persistence paths for one OpenSWMM project.
 *
 * \details The workspace is the owner of:
 *  - The project file path (`.oswp` or a path to an `.inp`).
 *  - The (optional) separate SWMM input file path when an `.oswp` sidecar is used.
 *  - The list of open sessions (one per MDI sub-window).
 *  - The single SWMMModelLayer that is bound to the project.
 *
 *  Use newInstance() to create a workspace; the constructor alone does not
 *  open any files.
 */
class OpenSWMMVisWorkspace : public QObject
{

	Q_OBJECT
	Q_PROPERTY(QString ProjectFilePath READ projectPath NOTIFY projectFilePathChanged)
	Q_PROPERTY(QString Title READ title NOTIFY projectTileChanged)

public:
	/*!
	* \brief OpenSWMMVisWorkspace constructor with parent object as parameter (default is nullptr)
	* \param parent QObject parent object
	* \return OpenSWMMVisWorkspace object with parent as parent object
	*/
	OpenSWMMVisWorkspace(QObject* parent = nullptr);

	/*!
	* \brief OpenSWMMVisWorkspace constructor with project file path and parent object as parameters
	* \param projectFilepath QString project file path
	* \param parent QObject parent object
	* \return OpenSWMMVisWorkspace object with project file path and parent as parent object
	*/
	OpenSWMMVisWorkspace(
		const QString& projectFilepath = "",
		QObject* parent = nullptr
	);

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
	*/
	QString swmmProjectPath() const;

	/*! 
	* \brief Returns the SWMM model layer bound to this project.
	*/
	SWMMModelLayer* swmmModelLayer() const;

	/*!
	* \brief Binds a SWMM model layer to the project.
	* \details Exactly one model layer can be bound to each project.
	*/
	bool setSWMMModelLayer(
		SWMMModelLayer* modelLayer,
		QList<QString>& warnings,
		QList<QString>& errors
	);

	/*!
	* \brief layers getter method
	* \return QVector<OpenSWMMVisLayer*> vector of OpenSWMMVisLayer pointers
	* \sa layer
	* \sa addLayer
	* \sa removeLayer
	* \sa layer
	*/
	QVector<OpenSWMMVisSession*> layers() const;

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
	bool saveProject(
		const QString& filepath,
		QList<QString>& warnings,
		QList<QString>& errors
	);

	/*!
	* \brief Saves the SWMM model file companion (.inp) to the given path.
	* \param filepath  Destination `.inp` path.
	* \param warnings  Populated with non-fatal save warnings.
	* \param errors    Populated with error messages on failure.
	* \return true on success; false otherwise.
	* \note  Currently unimplemented; see saveProject() for the same routing
	*        contract.
	*/
	bool saveSWMMProject(
		const QString& filepath,
		QList<QString>& warnings,
		QList<QString>& errors
	);

    /*!
     * \brief Opens a SWMM input file and loads the model into this workspace.
     * \param filepath   Absolute path to the `.inp` file.
     * \param warnings   Populated with non-fatal parser warnings.
     * \param errors     Populated with error messages on failure.
     * \return true on success; false if the file could not be opened or parsed.
     */
    bool openSWMMModel(
        const QString &filepath,
        QList<QString> &warnings,
        QList<QString> &errors
    );

    /*!
     * \brief Factory: create and optionally initialise a workspace from a
     *        project file path.
     * \details If \p projectFilepath is non-empty the two-phase initialisation
     *          is run immediately so the returned workspace is ready for use.
     * \param projectFilepath  Path to an `.oswp` or `.inp` file, or empty for a
     *                         new untitled project.
     * \param parent           Qt parent object.
     * \return Newly allocated workspace; ownership passes to the caller.
     */
    static OpenSWMMVisWorkspace *newInstance(
        const QString &projectFilepath = "",
        QObject       *parent          = nullptr
    );

    /*!
     * \brief Returns true when the workspace has unsaved changes.
     * \details The flag is set whenever the model layer is mutated and cleared
     *          after a successful save.
     */
    bool hasChanges() const;


signals:

	void layerChanged();

	void projectFilePathChanged();

	void projectTileChanged();

private:

	bool initializeFromProjectFile(
		const QString& projectFilepath,
		QList<QString>& warnings,
		QList<QString>& errors
	);

private:
	QVector<OpenSWMMVisSession*> mLayers;
	QString mProjectPath;
	QString mSWMMProjectPath;
	QString mTitle;
	SWMMModelLayer* mSWMMModelLayer;
	bool mHasChanges;

};


Q_DECLARE_METATYPE(OpenSWMMVisWorkspace*)
Q_DECLARE_METATYPE(QVector<OpenSWMMVisWorkspace*>)

#endif // SWMMVISPROJECT_H
