/*!
 * \file   swmmproject.h
 * \author Caleb Buahin <buahin.caleb@epa.gov>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2024
 * \pre
 * \bug
 * \warning
 * \todo
 */
#ifndef SWMMVISPROJECT_H
#define SWMMVISPROJECT_H

#include <QObject>

class SWMMVisSubProject;

class SWMMVisProject : public QObject
{

	Q_OBJECT
	Q_PROPERTY(QString ProjectFilePath READ projectPath NOTIFY projectFilePathChanged)
	Q_PROPERTY(QString Title READ title NOTIFY projectTileChanged)

public:
	/*!
	* \brief SWMMVisProject constructor with parent object as parameter (default is nullptr)
	* \param parent QObject parent object
	* \return SWMMVisProject object with parent as parent object
	*/
	SWMMVisProject(QObject* parent = nullptr);

	/*!
	* \brief SWMMVisProject constructor with project file path and parent object as parameters
	* \param projectFilepath QString project file path
	* \param parent QObject parent object
	* \return SWMMVisProject object with project file path and parent as parent object
	*/
	SWMMVisProject(
		const QString& projectFilepath = "",
		QObject* parent = nullptr
	);

	/*! SWMMVisProject destructor */
	virtual ~SWMMVisProject();

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
	* \brief layers getter method
	* \return QVector<SWMMVisLayer*> vector of SWMMVisLayer pointers
	* \sa layer
	* \sa addLayer
	* \sa removeLayer
	* \sa layer
	*/
	QVector<SWMMVisSubProject*> layers() const;

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

	static SWMMVisProject* newInstance(
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
	QVector<SWMMVisSubProject*> mLayers;
	QString mProjectPath;
	QString mSWMMProjectPath;
	QString mTitle;
	bool mHasChanges;

};


Q_DECLARE_METATYPE(SWMMVisProject*)
Q_DECLARE_METATYPE(QVector<SWMMVisProject*>)

#endif // SWMMVISPROJECT_H
