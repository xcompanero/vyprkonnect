#include "credentialstore.h"

#include <QFile>
#include <QSettings>

#ifdef HAVE_KWALLET
#include <KWallet>
#endif

namespace vypr {

using namespace Qt::Literals::StringLiterals;

CredentialStore &CredentialStore::instance()
{
    static CredentialStore s;
    return s;
}

CredentialStore::CredentialStore(QObject *parent)
    : QObject(parent)
{
#ifdef HAVE_KWALLET
    load();
#else
    load();
#endif
}

#ifdef HAVE_KWALLET

static QString fallbackSettingsPath()
{
    return QSettings().fileName();
}

// Synchronous wallet access; the kwalletd daemon lives in the user session.
// If the wallet is locked, kwalletd will prompt on the desktop.
static KWallet::Wallet *openWallet()
{
    auto *w = KWallet::Wallet::openWallet(
        KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!w)
        return nullptr;
    if (!w->hasFolder(u"vyprkonnect"_s))
        w->createFolder(u"vyprkonnect"_s);
    w->setFolder(u"vyprkonnect"_s);
    return w;
}

// One-time migration: if the wallet is empty but the legacy plaintext fallback
// file holds credentials, import them and delete the file.
static void migrateFromFallback(KWallet::Wallet *w, QString *username, QString *password)
{
    if (!w || w->entryList().contains(u"username"_s))
        return;
    QSettings s(fallbackSettingsPath(), QSettings::IniFormat);
    const QString u = s.value(u"account/username"_s).toString();
    const QString p = s.value(u"account/password"_s).toString();
    if (u.isEmpty() || p.isEmpty())
        return;
    w->writeEntry(u"username"_s, u.toUtf8());
    w->writePassword(u"password"_s, p);
    *username = u;
    *password = p;
    QFile::remove(fallbackSettingsPath());
    qInfo("Migrated credentials from plaintext store to KWallet; plaintext file removed");
}

QString CredentialStore::username() const
{
    return m_username;
}

void CredentialStore::setUsername(const QString &u)
{
    if (KWallet::Wallet *w = openWallet())
        w->writeEntry(u"username"_s, u.toUtf8());
    m_username = u;
    emit credentialsChanged();
}

QString CredentialStore::password() const
{
    return m_password;
}

void CredentialStore::setPassword(const QString &p)
{
    if (KWallet::Wallet *w = openWallet())
        w->writePassword(u"password"_s, p);
    m_password = p;
    emit credentialsChanged();
}

bool CredentialStore::secure() const { return true; }

void CredentialStore::load()
{
    KWallet::Wallet *w = openWallet();
    if (!w)
        return;
    QByteArray ub;
    QString p;
    if (w->readEntry(u"username"_s, ub) == 0)
        m_username = QString::fromUtf8(ub);
    if (w->readPassword(u"password"_s, p) == 0)
        m_password = p;
    migrateFromFallback(w, &m_username, &m_password);
}

void CredentialStore::save() {}

#else
// --- Fallback implementation (QSettings, mode 0600; NOT encrypted) ---

QString CredentialStore::username() const
{
    return m_username;
}

void CredentialStore::setUsername(const QString &u)
{
    m_username = u;
    save();
    emit credentialsChanged();
}

QString CredentialStore::password() const
{
    return m_password;
}

void CredentialStore::setPassword(const QString &p)
{
    m_password = p;
    save();
    emit credentialsChanged();
}

bool CredentialStore::secure() const { return false; }

void CredentialStore::load()
{
    QSettings s(QSettings().fileName(), QSettings::IniFormat);
    m_username = s.value(u"account/username"_s).toString();
    m_password = s.value(u"account/password"_s).toString();
    // Best-effort hardening of the fallback store.
    if (QFile::exists(QSettings().fileName()))
        QFile::setPermissions(QSettings().fileName(),
                              QFile::ReadOwner | QFile::WriteOwner);
}

void CredentialStore::save()
{
    QSettings s(QSettings().fileName(), QSettings::IniFormat);
    s.setValue(u"account/username"_s, m_username);
    s.setValue(u"account/password"_s, m_password);
}

#endif // HAVE_KWALLET

} // namespace vypr
