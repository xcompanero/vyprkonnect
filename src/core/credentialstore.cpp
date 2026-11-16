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
    load();
}

#ifdef HAVE_KWALLET
// --- KWallet implementation (encrypted, integrates with Plasma) ---

static KWallet::Wallet *openWallet()
{
    KWallet::Wallet *w = KWallet::Wallet::openWallet(
        KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Asynchronous);
    // openWallet with Asynchronous returns null until callback; use Synchronous for simplicity.
    if (!w)
        w = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0);
    if (w && !w->hasFolder(u"vyprkonnect"_s))
        w->createFolder(u"vyprkonnect"_s);
    if (w)
        w->setFolder(u"vyprkonnect"_s);
    return w;
}

QString CredentialStore::username() const
{
    KWallet::Wallet *w = openWallet();
    QString v;
    if (w)
        w->readEntry(u"username"_s, v);
    return v;
}

void CredentialStore::setUsername(const QString &u)
{
    if (KWallet::Wallet *w = openWallet())
        w->writeEntry(u"username"_s, u);
    m_username = u;
    emit credentialsChanged();
}

QString CredentialStore::password() const
{
    KWallet::Wallet *w = openWallet();
    QString v;
    if (w)
        w->readPassword(u"password"_s, v);
    return v;
}

void CredentialStore::setPassword(const QString &p)
{
    if (KWallet::Wallet *w = openWallet())
        w->writePassword(u"password"_s, p);
    m_password = p;
    emit credentialsChanged();
}

bool CredentialStore::secure() const { return true; }

void CredentialStore::load() {}

void CredentialStore::save() {}

#else
// --- Fallback implementation (QSettings, mode 0600; NOT encrypted) ---

static QString settingsPath()
{
    return QSettings().fileName();
}

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
    QSettings s(settingsPath(), QSettings::IniFormat);
    m_username = s.value(u"account/username"_s).toString();
    m_password = s.value(u"account/password"_s).toString();
    // Best-effort hardening of the fallback store.
    if (QFile::exists(settingsPath()))
        QFile::setPermissions(settingsPath(), QFile::ReadOwner | QFile::WriteOwner);
}

void CredentialStore::save()
{
    QSettings s(settingsPath(), QSettings::IniFormat);
    s.setValue(u"account/username"_s, m_username);
    s.setValue(u"account/password"_s, m_password);
}

#endif // HAVE_KWALLET

} // namespace vypr
