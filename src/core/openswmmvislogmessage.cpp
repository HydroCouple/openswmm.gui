/*!
 * \file  openswmmvislogmessage.cpp
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

#include <QDateTime>

#include "core/openswmmvislogmessage.h"


OpenSWMMVisLogMessage::OpenSWMMVisLogMessage(LogMessageType type, QString message, QObject* parent)
    : QObject(parent), mType(type), mMessage(message)
{
    this->mTimestamp = QDateTime::currentDateTime();
}

OpenSWMMVisLogMessage::~OpenSWMMVisLogMessage()
{
}

OpenSWMMVisLogMessage::LogMessageType OpenSWMMVisLogMessage::type() const
{
    return mType;
}

QString OpenSWMMVisLogMessage::message() const
{
    return mMessage;
}

QDateTime OpenSWMMVisLogMessage::timestamp() const
{
    return mTimestamp;
}
