/*!
 * \file   swmmvisprojectwindow.cpp
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

 #include "swmmvisprojectwindow.h"

SWMMVisProjectWindow::SWMMVisProjectWindow(SWMMVisProject* project, QWidget *parent)
 : QMdiSubWindow(parent)
 {
	 setWindowTitle("SWMM Visualizer");
	 setMinimumSize(800, 600);

 }


SWMMVisProjectWindow::~SWMMVisProjectWindow()
{

}