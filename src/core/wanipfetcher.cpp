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
    // A fresh QNetworkAccessManager per refresh is deliberate: a shared manager
    // reuses its keep-alive connection, which may have been established over the
    // pre-tunnel route — the fetched IP would then be stale after connecting.
    auto *nam = new QNetworkAccessManager(this);

    QNetworkRequest req{QUrl(u"https://ifconfig.io/ip"_s)};
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(10000);
    req.setHeader(QNetworkRequest::UserAgentHeader, u"vyprkonnect/0.1"_s);
    QNetworkReply *reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, nam, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QString addr = QString::fromUtf8(reply->readAll()).trimmed();
            if (!addr.isEmpty() && addr != m_address) {
                m_address = addr;
                emit addressChanged();
            }
        }
        nam->deleteLater();
    });
}

} // namespace vypr
