/*!
 * \file   openswmmvislogmessage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Immutable plain-data log message stamped with a severity level and
 *         wall-clock time.
 *
 * \details OpenSWMMVisLogMessage objects are created by subsystems and posted
 *          through the application's log bus (typically via SWMMVis::onLogMessage).
 *          They are stored in a QStandardItemModel that backs the Message Log
 *          dock panel.
 */
#ifndef SWMMVISLOGMESSAGE_H
#define SWMMVISLOGMESSAGE_H

#include <QDateTime>
#include <QObject>

/*!
 * \class OpenSWMMVisLogMessage
 * \brief Immutable application log record carrying a severity level, message
 *        text, and a wall-clock timestamp.
 *
 * \details Instances are created at emission time; the timestamp is set to
 *          QDateTime::currentDateTime() in the constructor and is never
 *          changed afterwards.  The class derives from QObject so it can be
 *          used as a Q_PROPERTY value and stored in a QVariant.
 */
class OpenSWMMVisLogMessage : public QObject
{
    Q_OBJECT
    Q_PROPERTY(LogMessageType Type      READ type      CONSTANT)
    Q_PROPERTY(QString        Message   READ message   CONSTANT)
    Q_PROPERTY(QDateTime      Timestamp READ timestamp CONSTANT)

public:

    /*!
     * \enum LogMessageType
     * \brief Severity classification for a log record.
     */
    enum LogMessageType
    {
        Information, /*!< Routine status update — no user action required. */
        Warning,     /*!< Non-fatal condition that may affect results. */
        Error,       /*!< Operation failed; user should review before proceeding. */
    };

    Q_ENUM(LogMessageType)

    /*!
     * \brief Constructs a log message and captures the current wall-clock time.
     * \param type     Severity of the message.
     * \param message  Human-readable description of the event.
     * \param parent   Optional QObject parent.
     */
    OpenSWMMVisLogMessage(LogMessageType type, QString message, QObject *parent = nullptr);

    /*!
     * \brief Destructor.
     */
    ~OpenSWMMVisLogMessage();

    /*!
     * \brief Returns the severity level of this message.
     */
    [[nodiscard]] LogMessageType type() const;

    /*!
     * \brief Returns the human-readable message text.
     */
    [[nodiscard]] QString message() const;

    /*!
     * \brief Returns the wall-clock timestamp when this message was created.
     */
    [[nodiscard]] QDateTime timestamp() const;

private:
    LogMessageType mType;
    QString        mMessage;
    QDateTime      mTimestamp;
};

Q_DECLARE_METATYPE(OpenSWMMVisLogMessage::LogMessageType)
Q_DECLARE_METATYPE(OpenSWMMVisLogMessage)

#endif // SWMMVISLOGMESSAGE_H




