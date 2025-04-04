/*!
 * \file  swmmvislogmessage.cpp
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

#include <QDateTime>

#include "swmmvislogmessage.h"


SWMMVisLogMessage::SWMMVisLogMessage(LogMessageType type, QString message, QObject* parent)
    : QObject(parent), mType(type), mMessage(message)
{
    this->mTimestamp = QDateTime::currentDateTime();
}

SWMMVisLogMessage::~SWMMVisLogMessage()
{
}

SWMMVisLogMessage::LogMessageType SWMMVisLogMessage::type() const
{
    return mType;
}

QString SWMMVisLogMessage::message() const
{
    return mMessage;
}

QDateTime SWMMVisLogMessage::timestamp() const
{
    return mTimestamp;
}
