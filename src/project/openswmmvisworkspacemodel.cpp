/**
 * @file      openswmmvisworkspacemodel.cpp
 * @brief     model-view framework.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/openswmmvisworkspacemodel.h"
#include "project/openswmmvisworkspace.h"

OpenSWMMVisWorkspaceModel::OpenSWMMVisWorkspaceModel(OpenSWMMVisWorkspace *parent)
    : QAbstractItemModel(parent)
{
}

OpenSWMMVisWorkspaceModel::~OpenSWMMVisWorkspaceModel() {}
