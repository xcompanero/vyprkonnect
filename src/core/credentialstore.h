#pragma once

#include <QObject>
#include <QString>

namespace vypr {

// Stores VyprVPN account credentials.
//
// Two implementations:
//  - HAVE_KWALLET: KWallet (preferred on KDE; enable via -DHAVE_KWALLET=ON
//    once libkf6wallet-dev is installed).
//  - Fallback: QSettings file chmod 0600. Dev convenience only — it is NOT
//    an encrypted store; the UI should surface that when this path is active.
class CredentialStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString username READ username WRITE setUsername NOTIFY credentialsChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY credentialsChanged)
    Q_PROPERTY(bool secure READ secure CONSTANT)
public:
    static CredentialStore &instance();

    QString username() const;
    void setUsername(const QString &u);
    QString password() const;
    void setPassword(const QString &p);
    // True when backed by an encrypted store (KWallet).
    bool secure() const;

signals:
    void credentialsChanged();

private:
    explicit CredentialStore(QObject *parent = nullptr);
    void load();
    void save();

    QString m_username;
    QString m_password;
};

} // namespace vypr
