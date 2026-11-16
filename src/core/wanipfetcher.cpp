#include "wanipfetcher.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace vypr {

using namespace Qt::Literals::StringLiterals;

WanIpFetcher::WanIpFetcher(QObject *parent)
    : QObject(parent)
{
}

void WanIpFetcher::refresh()
{
    QNetworkRequest req{QUrl(u"https://ifconfig.io/ip"_s)};
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(10000);
    req.setHeader(QNetworkRequest::UserAgentHeader, u"vyprkonnect/0.1"_s);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return; // keep last known address; UI can ignore stale values
        const QString addr = QString::fromUtf8(reply->readAll()).trimmed();
        if (!addr.isEmpty() && addr != m_address) {
            m_address = addr;
            emit addressChanged();
        }
    });
}

} // namespace vypr
