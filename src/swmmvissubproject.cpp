/*!
 * \file   swmmvissubproject.cpp
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


#include "swmmvissubproject.h"

SWMMVisSubProject::SWMMVisSubProject(SWMMVisProject* parent, SWMMVisGraphicsView *swmmGraphicsView)
	: mProject(parent),
      mGraphicsView(swmmGraphicsView)
{

}


SWMMVisSubProject::~SWMMVisSubProject()
{

}

void SWMMVisSubProject::setGraphicsView(SWMMVisGraphicsView* graphicsView)
{
	mGraphicsView = graphicsView;
}


SWMMVisGraphicsView *SWMMVisSubProject::graphicsView() const
{
	return mGraphicsView;
}

SWMMVisProject *SWMMVisSubProject::project() const
{
	return mProject;
}