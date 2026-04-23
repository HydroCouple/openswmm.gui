/*!
 * \file   openswmmvissession.h
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

#ifndef SWMMVISSUBPROJECT_H
#define SWMMVISSUBPROJECT_H


#include "layers/openswmmvislayer.h"

class OpenSWMMVisWorkspace;
class OpenSWMMVisGraphicsView;

class OpenSWMMVisSession : public OpenSWMMVisLayer
{

	Q_OBJECT

public:

	OpenSWMMVisSession(OpenSWMMVisWorkspace* parent, OpenSWMMVisGraphicsView *swmmGraphicsView = nullptr);

	virtual ~OpenSWMMVisSession();

	void setGraphicsView(OpenSWMMVisGraphicsView *graphicsView);

	OpenSWMMVisGraphicsView *graphicsView() const;

	OpenSWMMVisWorkspace *project() const;


private:
	OpenSWMMVisGraphicsView *mGraphicsView;
	OpenSWMMVisWorkspace *mProject;

};

Q_DECLARE_METATYPE(OpenSWMMVisSession*)

#endif // SWMMVISSUBPROJECT_H
