/**
 * @file      openswmmvislogmessage.cpp
 * @brief     wall-clock time.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QDateTime>

#include "core/openswmmvislogmessage.h"

OpenSWMMVisLogMessage::OpenSWMMVisLogMessage(LogMessageType type, QString message, QObject *parent)
    : QObject(parent), mType(type), mMessage(message)
{
    this->mTimestamp = QDateTime::currentDateTime();
}

OpenSWMMVisLogMessage::~OpenSWMMVisLogMessage() {}

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
