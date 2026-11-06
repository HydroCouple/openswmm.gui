/**
 * @file      openswmmvissession.cpp
 * @brief     OpenSWMMVisSession binds an OpenSWMMVisWorkspace (project data) to an
 *            OpenSWMMVisGraphicsView (interactive map overlay).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/openswmmvissession.h"

OpenSWMMVisSession::OpenSWMMVisSession(OpenSWMMVisWorkspace *parent,
                                       OpenSWMMVisGraphicsView *swmmGraphicsView)
    : mProject(parent), mGraphicsView(swmmGraphicsView)
{
}

OpenSWMMVisSession::~OpenSWMMVisSession() {}

void OpenSWMMVisSession::setGraphicsView(OpenSWMMVisGraphicsView *graphicsView)
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