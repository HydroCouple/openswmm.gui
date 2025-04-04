/*!
 * \file   spatialreferencesystem.cpp
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

#include "spatialreferencesystem.h"

SpatialReferenceSystem::SpatialReferenceSystem(const QString &authName, int code)
	: m_authName(authName), m_code(code)
{
}

SpatialReferenceSystem::~SpatialReferenceSystem()
{
}

QString SpatialReferenceSystem::authName() const
{
	return m_authName;
}

int SpatialReferenceSystem::code() const
{
	return m_code;
}

QString SpatialReferenceSystem::description() const
{
	return m_description;
}

void SpatialReferenceSystem::setDescription(const QString &description)
{
	m_description = description;
	emit propertyChanged("description")
}