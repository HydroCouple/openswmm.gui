/*!
 * \file   openswmmvisservice.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Abstract base interface for all plug-in services registered with the
 *         OpenSWMM visualiser at runtime.
 *
 * \details Services are registered by name and version using a static service
 *          registry. Callers retrieve a service by name, pass a request object,
 *          and receive a response object.  The interface is intentionally thin
 *          so that new services can be added without modifying the host
 *          application.
 */
#ifndef SWMMVISSERVICE_H
#define SWMMVISSERVICE_H

class OpenSWMMVisServiceRequest;
class OpenSWMMVisServiceResponse;

/*!
 * \class OpenSWMMVisService
 * \brief Abstract base class for all OpenSWMMVis plug-in services.
 *
 * \details Concrete services must override name(), version(), and
 *          executeRequest().  They are registered with the host application at
 *          runtime so that UI panels, scripts, and automation clients can
 *          invoke them by name without a compile-time dependency on the
 *          concrete implementation.
 */
class OpenSWMMVisService
{
public:

    /*!
     * \brief Default constructor.
     */
    OpenSWMMVisService() = default;

    /*!
     * \brief Virtual destructor — required for polymorphic deletion.
     */
    virtual ~OpenSWMMVisService() = default;

    /*!
     * \brief Returns the unique string identifier of this service.
     * \details The name is used as the registry key; it must be unique across
     *          all registered services and must not change between versions.
     * \return Service name string (e.g. "SWMM.Simulation.Run").
     */
    virtual QString name() const = 0;

    /*!
     * \brief Returns the version string of this service implementation.
     * \details Uses semantic versioning (MAJOR.MINOR.PATCH).  Callers can
     *          compare against a minimum required version to guard against
     *          incompatible service implementations.
     * \return Version string (e.g. "1.0.0").
     */
    virtual QString version() const = 0;

    /*!
     * \brief Execute a service request and populate a response.
     * \details Implementations must be re-entrant: the host may call this
     *          method from multiple threads concurrently.  Implementations
     *          are responsible for any internal locking.
     *
     * \param[in]  request   Typed request object carrying the input parameters.
     * \param[out] response  Response object that will be populated by this call.
     */
    virtual void executeRequest(
        const OpenSWMMVisServiceRequest &request,
        OpenSWMMVisServiceResponse &response
    ) = 0;
};

#endif // SWMMVISSERVICE_H