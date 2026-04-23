/*!
 * \file   openswmmvissession.cpp
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


#include "project/openswmmvissession.h"

OpenSWMMVisSession::OpenSWMMVisSession(OpenSWMMVisWorkspace* parent, OpenSWMMVisGraphicsView *swmmGraphicsView)
	: mProject(parent),
      mGraphicsView(swmmGraphicsView)
{

}


OpenSWMMVisSession::~OpenSWMMVisSession()
{

}

void OpenSWMMVisSession::setGraphicsView(OpenSWMMVisGraphicsView* graphicsView)
{
	mGraphicsView = graphicsView;
}


OpenSWMMVisGraphicsView *OpenSWMMVisSession::graphicsView() const
{
	return mGraphicsView;
}

OpenSWMMVisWorkspace *OpenSWMMVisSession::project() const
{
	return mProject;
}