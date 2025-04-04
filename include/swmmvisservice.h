/*!
 * \file   swmmvisservice.h
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
#ifdef SWMMVISSERVICE_H
#define SWMMVISSERVICE_H


class SWMMVisServiceRequest;
class SWMMVisServiceResponse;

 /*!
  * \brief The SWMMVisService class
  * \details This class is an abstract class that provides a base class for all SWMMVis services.
  * This class provides methods for executing server requests and handling responses.
  * They are registered at runtime for a given service name.
  */
class SWMMVisService
{
public:

	/*!
	* \brief Constructor for SWMMVisService class
	* \details
	*/
	SWMMVisService() = default;

	/*!
	* \brief Destructor for SWMMVisService class
	*/
	virtual ~SWMMVisService() = default;

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
	* \param request - SWMMVisServiceRequest object
	* \param response - QStringSWMMVisServiceResponse object
	*/
	virtual void executeRequest(
		const SWMMVisServiceRequest &request, 
		QStringSWMMVisServiceResponse &response
	) = 0;

};

#endif // SWMMVISSERVICE_H