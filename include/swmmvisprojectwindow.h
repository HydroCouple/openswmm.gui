/*!
 * \file   swmmvisprojectwindow.h
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
#ifndef SWMMVISPROJECTWINDOW_H
#define SWMMVISPROJECTWINDOW_H

#include <QMdiSubWindow>

class SWMMVisProject;
class SWMMVisGraphicsView;

/*! \class SwmmVisProjectWindow
 * \brief
 * \details
*/
class SWMMVisProjectWindow : public QMdiSubWindow
{
	Q_OBJECT

public:
	/*! \brief Constructor for SwmmVisProjectWindow class
	 * \details
	 * \param project - SWMMVisProject object to be displayed in the window
	 * \param parent - parent widget of the window
	 * \return
	 * \todo
	*/
	explicit SWMMVisProjectWindow(SWMMVisProject* project, QWidget* parent = nullptr);

	/*! \brief Destructor for SwmmVisProjectWindow class
	 * \details
	 * \return
	 * \todo
	*/
	virtual ~SWMMVisProjectWindow();


private:
	SWMMVisProject *mProject;
	SWMMVisGraphicsView *mGraphicsView;
};


#endif // SWMMVISPROJECTWINDOW_H