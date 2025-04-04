/*!
 * \file   spatialreferencesystem.h
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

#ifndef SPATIALREFERENCESYSTEM_H
#define SPATIALREFERENCESYSTEM_H

#include <QObject>

/*!
 * \class SpatialReferenceSystem
 * \brief The SpatialReferenceSystem class
 */
class SpatialReferenceSystem: public QObject 
{		
		Q_OBJECT
		Q_PROPERTY(QString AuthorityName READ authName NOTIFY notifyPropertyChanged)
		Q_PROPERTY(int Code READ code NOTIFY propertyChanged)
		Q_PROPERTY(QString Description READ description WRITE setDescription NOTIFY propertyChanged)


	public:

		/*!
		 * \brief SpatialReferenceSystem constructor
		 * \param authName: Authority name of the spatial reference system
		 * \param code: Code of the spatial reference system
		 */
		SpatialReferenceSystem(const QString &authName = "EPSG", int code = 4326);
		
		/*!
		 * \brief SpatialReferenceSystem destructor
		 */
		virtual ~SpatialReferenceSystem();

		/*!
		 * \brief authName returns the authority name of the spatial reference system
		 * \return QString authority name
		 */
		QString authName() const;

		/*!
		 * \brief code returns the code of the spatial reference system
		 * \return int code
		 */
		QString code() const;
		
		/*!
		 * \brief description returns the description of the spatial reference system
		 * \return QString description
		 */
		QString description() const;

		/*!
		 * \brief setAuthName sets the authority name of the spatial reference system
		 * \param authName: Authority name of the spatial reference system
		 * \return QString authority name
		 */
		void setDescription(const QString &description);

signals:
		void propertyChanged(const QString &propertyName);

private:
		QString mAuthName;
		int mCode;
		QString m_description;


};

#endif // SPATIALREFERENCESYSTEM_H
