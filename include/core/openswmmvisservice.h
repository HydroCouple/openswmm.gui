/*!
 * \file   openswmmvisservice.h
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
#ifdef SWMMVISSERVICE_H
#define SWMMVISSERVICE_H


class OpenSWMMVisServiceRequest;
class OpenSWMMVisServiceResponse;

 /*!
  * \brief The OpenSWMMVisService class
  * \details This class is an abstract class that provides a base class for all OpenSWMMVis services.
  * This class provides methods for executing server requests and handling responses.
  * They are registered at runtime for a given service name.
  */
class OpenSWMMVisService
{
public:

	/*!
	* \brief Constructor for OpenSWMMVisService class
	* \details
	*/
	OpenSWMMVisService() = default;

	/*!
	* \brief Destructor for OpenSWMMVisService class
	*/
	virtual ~OpenSWMMVisService() = default;

	/*!
	* \brief name returns the name of the service
	*/
	virtual QString name() const = 0;


	/*!
	* \brief version returns the version of the service
	* \return QString version
	*/
	virtual QString version() const = 0;


	/*!
	* \brief executeRequest executes a request and returns a response
	* \param request - OpenSWMMVisServiceRequest object
	* \param response - QStringOpenSWMMVisServiceResponse object
	*/
	virtual void executeRequest(
		const OpenSWMMVisServiceRequest &request, 
		QStringOpenSWMMVisServiceResponse &response
	) = 0;

};

#endif // SWMMVISSERVICE_H