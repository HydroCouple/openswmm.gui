/*!
 * \file   openswmmvisgraphicsview.h
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
#ifndef SWMMVISGRAPHICSVIEW_H
#define SWMMVISGRAPHICSVIEW_H

#include <QGraphicsView>

class OpenSWMMVisScene;

class OpenSWMMVisGraphicsView: public QGraphicsView
{

    Q_OBJECT

public:

    OpenSWMMVisGraphicsView(OpenSWMMVisScene*scene, QWidget *parent = nullptr);

    OpenSWMMVisGraphicsView(QWidget *parent = nullptr);

    virtual ~OpenSWMMVisGraphicsView();

};

#endif // SWMMVISGRAPHICSVIEW_H
