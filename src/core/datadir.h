#pragma once

#include <QString>
#include <QtGlobal>

namespace vypr {

// Bundled data directory resolution order:
//   1. $VYPRKONNECT_DATA_DIR   (explicit override)
//   2. compile-time VYPR_DATA_DIR (only defined for dev builds:
//      -DVYPR_DEV_DATA_DIR=ON; release builds must NOT define it so the
//      installed location is used)
//   3. installed location (/usr/share/vyprkonnect/data)
//
// Single source of truth: main.cpp (server list) and the OpenVPN backend
// (.ovpn imports) must both resolve through here — never re-derive from the
// raw macro.
inline QString resolveDataDir()
{
    if (const QString env = qEnvironmentVariable("VYPRKONNECT_DATA_DIR"); !env.isEmpty())
        return env;
#ifdef VYPR_DATA_DIR
    return QStringLiteral(VYPR_DATA_DIR);
#endif
    return QStringLiteral("/usr/share/vyprkonnect/data");
}

} // namespace vypr
