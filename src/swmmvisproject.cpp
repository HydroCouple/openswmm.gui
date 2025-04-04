/*!
 * \file   swmmproject.cpp
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

#include "swmmvisproject.h"
#include "swmmvislayer.h"


SWMMVisProject::SWMMVisProject(QObject *parent):
	QObject(parent),
	mProjectPath("Untitled Project.svz"),
	mSWMMProjectPath(""),
	mTitle("Untitled Project"),
    mHasChanges(false)
{

}

SWMMVisProject::SWMMVisProject(const QString &projectFilepath,	QObject* parent):
    QObject(parent),
    mProjectPath(projectFilepath)
{
}

SWMMVisProject::~SWMMVisProject()
{

}

QString SWMMVisProject::projectPath() const
{
	return this->mProjectPath;
}

QString SWMMVisProject::title() const
{
	return this->mTitle;
}

QString SWMMVisProject::swmmProjectPath() const
{
	return this->mSWMMProjectPath;
}

QVector<SWMMVisSubProject*> SWMMVisProject::layers() const
{
	return this->mLayers;
}


bool SWMMVisProject::saveProject(
	const QString &filepath,
	QList<QString> &warnings,
	QList<QString> &errors
	)
{
	return false;
}

bool SWMMVisProject::saveSWMMProject(
	const QString &filepath,
	QList<QString> &warnings,
	QList<QString> &errors
	)
{
	return false;
}

bool SWMMVisProject::openSWMMModel(
	const QString &filepath,
	QList<QString> &warnings,
	QList<QString> &errors
	)
{
	return false;
}


SWMMVisProject *SWMMVisProject::newInstance(const QString& projectFilepath, QObject *parent)
{
	return new SWMMVisProject(projectFilepath, parent);
}

bool SWMMVisProject::hasChanges() const
{
	return this->mHasChanges;
}
