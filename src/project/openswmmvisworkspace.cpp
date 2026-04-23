/*!
 * \file   swmmproject.cpp
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

#include "project/openswmmvisworkspace.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"


OpenSWMMVisWorkspace::OpenSWMMVisWorkspace(QObject *parent):
	QObject(parent),
	mProjectPath("Untitled Project.svz"),
	mSWMMProjectPath(""),
	mTitle("Untitled Project"),
	mSWMMModelLayer(nullptr),
    mHasChanges(false)
{

}

OpenSWMMVisWorkspace::OpenSWMMVisWorkspace(const QString &projectFilepath,	QObject* parent):
    QObject(parent),
    mProjectPath(projectFilepath),
	mSWMMProjectPath(""),
	mTitle("Untitled Project"),
	mSWMMModelLayer(nullptr),
	mHasChanges(false)
{
}

OpenSWMMVisWorkspace::~OpenSWMMVisWorkspace()
{

}

QString OpenSWMMVisWorkspace::projectPath() const
{
	return this->mProjectPath;
}

QString OpenSWMMVisWorkspace::title() const
{
	return this->mTitle;
}

QString OpenSWMMVisWorkspace::swmmProjectPath() const
{
	return this->mSWMMProjectPath;
}

SWMMModelLayer* OpenSWMMVisWorkspace::swmmModelLayer() const
{
	return this->mSWMMModelLayer;
}

bool OpenSWMMVisWorkspace::setSWMMModelLayer(
	SWMMModelLayer *modelLayer,
	QList<QString> &warnings,
	QList<QString> &errors
	)
{
	if (!modelLayer)
	{
		errors.append("Cannot bind a null SWMM model layer.");
		return false;
	}

	if (mSWMMModelLayer && mSWMMModelLayer != modelLayer)
	{
		errors.append("A SWMM model layer is already bound to this project.");
		return false;
	}

	if (mSWMMModelLayer == modelLayer)
	{
		warnings.append("Requested SWMM model layer is already bound to this project.");
		return true;
	}

	mSWMMModelLayer = modelLayer;
	mHasChanges = true;

	if (!modelLayer->modelFilePath().isEmpty())
		mSWMMProjectPath = modelLayer->modelFilePath();

	emit layerChanged();
	return true;
}

QVector<OpenSWMMVisSession*> OpenSWMMVisWorkspace::layers() const
{
	return this->mLayers;
}


bool OpenSWMMVisWorkspace::saveProject(
	const QString &filepath,
	QList<QString> &warnings,
	QList<QString> &errors
	)
{
	return false;
}

bool OpenSWMMVisWorkspace::saveSWMMProject(
	const QString &filepath,
	QList<QString> &warnings,
	QList<QString> &errors
	)
{
	return false;
}

bool OpenSWMMVisWorkspace::openSWMMModel(
	const QString &filepath,
	QList<QString> &warnings,
	QList<QString> &errors
	)
{
	return false;
}


OpenSWMMVisWorkspace *OpenSWMMVisWorkspace::newInstance(const QString& projectFilepath, QObject *parent)
{
	return new OpenSWMMVisWorkspace(projectFilepath, parent);
}

bool OpenSWMMVisWorkspace::hasChanges() const
{
	return this->mHasChanges;
}
