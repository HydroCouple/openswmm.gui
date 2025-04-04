/*!
 * \file   swmmvissubproject.h
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

#ifndef SWMMVISSUBPROJECT_H
#define SWMMVISSUBPROJECT_H


#include "swmmvislayer.h"

class SWMMVisProject;
class SWMMVisGraphicsView;

class SWMMVisSubProject : public SWMMVisLayer
{

	Q_OBJECT

public:

	SWMMVisSubProject(SWMMVisProject* parent, SWMMVisGraphicsView *swmmGraphicsView = nullptr);

	virtual ~SWMMVisSubProject();

	void setGraphicsView(SWMMVisGraphicsView *graphicsView);

	SWMMVisGraphicsView *graphicsView() const;

	SWMMVisProject *project() const;


private:
	SWMMVisGraphicsView *mGraphicsView;
	SWMMVisProject *mProject;

};

Q_DECLARE_METATYPE(SWMMVisSubProject*)

#endif // SWMMVISSUBPROJECT_H
