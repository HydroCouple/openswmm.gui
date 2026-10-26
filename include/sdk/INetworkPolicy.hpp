/**
 * @file INetworkPolicy.hpp
 * @brief Public network policy and shared HTTP client for GUI extensions.
 * @details The client honors the host offline switch and optional host allowlist. Redirects
 * are manual so each destination passes the same policy. Qt supplies normal TLS validation.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QUrl>
class QNetworkAccessManager;
namespace OpenSWMM::Sdk
{
/** @brief Borrowed GUI-thread network service used for catalogue previews and data downloads. */
class INetworkPolicy
{
public:
    /** @brief Destroy an implementation; extensions never delete the host-owned service. */
    virtual ~INetworkPolicy() = default;
    /** @brief Test a destination without making a request.
     * @param[in] url HTTPS destination; credentials embedded in URLs are refused.
     * @return False in offline mode, for unsupported schemes or outside an explicit host allowlist. */
    virtual bool allowed(const QUrl &url) const = 0;
    /** @brief Obtain the policy-enforcing network manager.
     * @return Borrowed GUI-thread manager; replies are caller-owned under Qt conventions.
     * @note Requests and manually followed redirects are checked before network IO. */
    virtual QNetworkAccessManager *shared() = 0;
};
}
