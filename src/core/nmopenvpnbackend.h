#pragma once

#include "nmbackendbase.h"

#include <QTemporaryDir>

namespace vypr {

// OpenVPN over NetworkManager: imports Vypr's bundled .ovpn set as NM
// connections (with the Phase-0 field rules applied) and activates them.
class NmOpenVpnBackend : public NmBackendBase
{
    Q_OBJECT
public:
    explicit NmOpenVpnBackend(ServerModel *servers, QObject *parent = nullptr);

    QString backendName() const override { return QStringLiteral("OpenVPN"); }
    void connectToServer(const QString &serverName) override;
    void disconnect() override;

private:
    void ensureProfileAndConnect(const vypr::Server &server);
    void finishProfileSetup(const QString &id, const QString &importedName,
                            int code, const QString &out, QTemporaryDir *tmpDir);
};

} // namespace vypr
