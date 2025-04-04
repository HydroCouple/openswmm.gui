/*!
 * \file  swmmvislogmessage.h
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
#ifndef SWMMVISLOGMESSAGE_H
#define SWMMVISLOGMESSAGE_H

#include <QDateTime>

class SWMMVisLogMessage : public QObject
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

    SWMMVisLogMessage(LogMessageType type, QString message, QObject* parent = nullptr);

	~SWMMVisLogMessage();

	LogMessageType type() const;

	QString message() const; 
	
	QDateTime timestamp() const;

private:
	LogMessageType mType;
	QString mMessage;
	QDateTime mTimestamp;

};

Q_DECLARE_METATYPE(SWMMVisLogMessage::LogMessageType)
Q_DECLARE_METATYPE(SWMMVisLogMessage)

#endif // SWMMVISLOGMESSAGE_H




