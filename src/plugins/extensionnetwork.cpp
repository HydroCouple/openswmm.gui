/**
 * @file extensionnetwork.cpp
 * @brief Offline/allowlist enforcement and asynchronous refused HTTP replies.
 * @details Only HTTPS destinations without embedded credentials are permitted. Host patterns
 * support exact DNS names and explicit *.domain suffixes; redirects must be followed manually.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionnetwork.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace openswmmvis
{
namespace
{
/** @brief A complete asynchronous Qt reply representing a policy refusal without IO. */
class RefusedReply final : public QNetworkReply
{
public:
    /** @brief Create a refusal compatible with normal QNetworkReply completion handling.
     * @param[in] request Original destination/headers copied into the reply.
     * @param[in] parent Owning network manager. */
    RefusedReply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setError(QNetworkReply::ContentAccessDenied,
                 tr("Request refused by extension network policy."));
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            emit errorOccurred(error());
            emit finished();
        });
    }
    /** @brief Refused replies have no active transfer to abort. */
    void abort() override {}

protected:
    /** @brief Report end-of-stream without producing response bytes.
     * @param[out] data Unused caller buffer.
     * @param[in] maximum Unused requested capacity.
     * @return Minus one, indicating no payload. */
    qint64 readData(char *data, qint64 maximum) override
    {
        Q_UNUSED(data);
        Q_UNUSED(maximum);
        return -1;
    }
};
}
ExtensionNetwork::ExtensionNetwork(QSettings *settings, QObject *parent)
    : QNetworkAccessManager(parent), mSettings(settings)
{
}
bool ExtensionNetwork::allowed(const QUrl &url) const
{
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || mSettings->value("extensions/network/offline", false).toBool())
        return false;
    const auto hosts = mSettings->value("extensions/network/allowedHosts").toStringList();
    if (hosts.isEmpty())
        return true;
    const auto host = url.host().toLower();
    for (auto pattern : hosts) {
        pattern = pattern.toLower();
        if (pattern == host || (pattern.startsWith("*.") && host.endsWith(pattern.mid(1))))
            return true;
    }
    return false;
}
QNetworkReply *ExtensionNetwork::createRequest(Operation operation, const QNetworkRequest &request,
                                               QIODevice *outgoingData)
{
    if (!allowed(request.url()))
        return new RefusedReply(request, this);
    auto checked = request;
    checked.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    return QNetworkAccessManager::createRequest(operation, checked, outgoingData);
}
}
