/**
 * @file extensionnetwork.h
 * @brief Private HTTP manager enforcing extension offline and destination policies.
 * @details Settings are read at request time. Refused requests yield an asynchronous error reply
 * and perform no network IO. Automatic redirects are disabled to prevent destination-policy bypass.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/INetworkPolicy.hpp"
#include <QNetworkAccessManager>
#include <QSettings>
namespace openswmmvis
{
/** @brief GUI-thread HTTP policy adapter; settings and parent must outlive requests. */
class ExtensionNetwork final : public QNetworkAccessManager, public OpenSWMM::Sdk::INetworkPolicy
{
public:
    /** @brief Construct a policy client.
     * @param[in] settings Required borrowed host settings, read under extensions/network/.
     * @param[in] parent Optional QObject owner. */
    explicit ExtensionNetwork(QSettings *settings, QObject *parent = nullptr);
    bool allowed(const QUrl &url) const override;
    QNetworkAccessManager *shared() override { return this; }

protected:
    /** @brief Validate and normalize an outgoing Qt request before any IO.
     * @param[in] operation Qt request verb.
     * @param[in] request Requested URL and headers, copied before redirect policy is changed.
     * @param[in] outgoingData Optional body device borrowed by Qt for the request lifetime.
     * @return Qt-owned normal reply or an asynchronous policy-refusal reply. */
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override;

private:
    QSettings *mSettings; ///< Borrowed GUI-thread host settings.
};
}
