/*!
 * \file   swmmproject.h
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
#ifndef SWMMVISPROJECT_H
#define SWMMVISPROJECT_H

#include <QObject>

class OpenSWMMVisSession;
class SWMMModelLayer;

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
	* \brief loadProject method to load project file
	* \param filepath QString file path
	* \param warnings QList<QString> list of warnings
	* \param errors QList<QString> list of errors
	* \return bool true if project file is loaded successfully, false otherwise
	* \sa saveProject
	*/
	bool saveProject(
		const QString& filepath,
		QList<QString>& warnings,
		QList<QString>& errors
	);

	/*!
	* \brief loadProject method to load project file
	* \param filepath QString file path
	* \param warnings QList<QString> list of warnings
	* \param errors QList<QString> list of errors
	* \return bool true if project file is loaded successfully, false otherwise
	* \sa loadProject
	*/
	bool saveSWMMProject(
		const QString& filepath,
		QList<QString>& warnings,
		QList<QString>& errors
	);

	bool openSWMMModel(
		const QString& filepath,
		QList<QString>& warnings,
		QList<QString>& errors
	);

	static OpenSWMMVisWorkspace* newInstance(
		const QString& projectFilepath = "", 
		QObject* parent = nullptr
	);


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
