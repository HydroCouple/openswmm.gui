/*!
 * \file  openswmmvislogmessage.h
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
#ifndef SWMMVISLOGMESSAGE_H
#define SWMMVISLOGMESSAGE_H

#include <QDateTime>

class OpenSWMMVisLogMessage : public QObject
{
	Q_OBJECT	
	Q_PROPERTY(LogMessageType Type READ type)
	Q_PROPERTY(QString Message READ message)
	Q_PROPERTY(QDateTime Timestamp READ timestamp)

public:
	
	enum LogMessageType
	{
		Information,
		Warning,
		Error,

    };

    Q_ENUM(LogMessageType)

    OpenSWMMVisLogMessage(LogMessageType type, QString message, QObject* parent = nullptr);

	~OpenSWMMVisLogMessage();

	LogMessageType type() const;

	QString message() const; 
	
	QDateTime timestamp() const;

private:
	LogMessageType mType;
	QString mMessage;
	QDateTime mTimestamp;

};

Q_DECLARE_METATYPE(OpenSWMMVisLogMessage::LogMessageType)
Q_DECLARE_METATYPE(OpenSWMMVisLogMessage)

#endif // SWMMVISLOGMESSAGE_H




