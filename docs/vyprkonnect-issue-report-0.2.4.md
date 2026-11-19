# VyprKonnect 0.2.4 — three release-blocking bugs found in end-user testing

**Package tested:** `vyprkonnect_0.2.4_amd64.deb` (built from this project's dev machine)
**Test environment:** Kubuntu 26.04.1 LTS, KDE Plasma 6.6.6, Qt 6.10.2, NetworkManager 1.54, KWallet available
**Test methodology:** the package was installed on a clean machine that is *not* the build machine, and exercised strictly as an end user would experience it (menu launch, GUI interaction). Findings below are backed by reproduction steps with actual observed output.

**TL;DR — three related bugs, all stemming from or exposed by the same packaging flaw:**

| # | Symptom an end user sees | Root cause | User-visible severity |
|---|--------------------------|------------|----------------------|
| 1 | App dies silently on launch, exit code 3 | `QLockFile` parent directory never created; every lock failure misreported as "already running" | **App is dead on arrival** on a fresh install |
| 2 | "Failed to load bundled server list from /home/…/Race-Cloud/Projects/vypr/vyprkonnect/data" | `VYPR_DATA_DIR` compile definition baked the build machine's source path into the released binary | Server list empty in all packaged builds |
| 3 | "Missing bundled config for \<server\>" on every OpenVPN connect | OpenVPN backend builds its `.ovpn` path from the raw compile-time macro, ignoring the app's own data-dir resolution | **OpenVPN cannot work at all** in packaged builds; WireGuard unaffected |

Each bug individually would have been caught by a single clean-machine install test; together they mean the 0.2.4 package only *appears* to work on the machine where it was built.

---

## Bug 1 — App fails to start silently on a fresh install

### Symptom

Launching from the application menu appears to start, then nothing happens (stderr from a `.desktop` launcher is invisible). From a terminal:

```
$ vyprkonnect; echo $?
VyprKonnect is already running — refusing to start a second instance.
3
```

At the time of that run: no `vyprkonnect` process existed (`ps aux`), and no
lock file existed anywhere (`find ~ /tmp /var/tmp -name "vyprkonnect.lock"` and
broader searches all came up empty).

### Diagnosis trail

1. The binary runs fine from a terminal — libraries all resolve (`ldd` clean) — but exits 3 claiming a second instance.
2. Redirecting `XDG_DATA_HOME` to a **completely empty scratch directory** reproduced the identical failure, and no lock file was created — proving the failure is not contention and not anything pre-existing in `$HOME`.
3. Pre-creating the lock file's parent directory made the app start and run normally. Root cause confirmed.

### Root cause (two compounding defects)

**Defect 1a — the lock directory is never created.** `src/main.cpp` (as of `master`):

```cpp
QApplication app(argc, argv);
app.setApplicationName("vyprkonnect");
app.setOrganizationName("vyprkonnect");

// Single instance: two instances racing the same NM profiles made NM 1.54
// crash during WireGuard profile churn. (Must come AFTER the app identity
// is set — QStandardPaths derives the lock path from it.)
QLockFile instanceLock(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                       + u"/vyprkonnect.lock"_s);
instanceLock.removeStaleLockFile();
if (!instanceLock.tryLock(100)) {
    qWarning("VyprKonnect is already running — refusing to start a second instance.");
    return 3;
}
```

`AppLocalDataLocation` resolves to `~/.local/share/vyprkonnect/vyprkonnect/`,
which does not exist on a fresh install. Nothing in the app creates it before
this point. On a KDE system KWallet is available, so the credential-store
fallback (the only other thing that might plausibly have created the
directory) is never exercised either. Qt's `QLockFile` does **not** create
parent directories.

**Defect 1b — every lock failure is reported as "already running".** In Qt's
`qlockfile_unix.cpp`, when `open(O_WRONLY|O_CREAT|O_EXCL)` fails with `ENOENT`
(directory missing) the result is `UnknownError`; in `qlockfile.cpp`:

```cpp
switch (d->lockError) {
case NoError:
    d->isLocked = true;
    return true;
case PermissionError:
case UnknownError:
    return false;          // <-- NOT "someone else holds the lock"
case LockFailedError:
    ... (stale-lock handling, retry loop)
}
```

So `tryLock()` returning `false` covers both genuine contention *and* "the
file could not be created at all". The app's handler conflates the two, emits
a misleading warning, and exits. The single-instance guard thus becomes a
kill-switch on every machine where the directory is absent.

### Suggested fix for `src/main.cpp`

Add `#include <QDir>` to the include block, then replace the lock block with:

```cpp
// Single instance: two instances racing the same NM profiles made NM 1.54
// crash during WireGuard profile churn. (Must come AFTER the app identity
// is set — QStandardPaths derives the lock path from it.)
const QString appDataDir =
    QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
QDir().mkpath(appDataDir);   // QLockFile does NOT create parent directories

QLockFile instanceLock(appDataDir + u"/vyprkonnect.lock"_s);
instanceLock.removeStaleLockFile();
if (!instanceLock.tryLock(100)) {
    if (instanceLock.error() == QLockFile::LockFailedError)
        qWarning("VyprKonnect is already running — refusing to start a second instance.");
    else
        qCritical("Cannot acquire single-instance lock at %s (QLockFile error %d) — aborting.",
                  qPrintable(instanceLock.fileName()), int(instanceLock.error()));
    return 3;
}
```

**Why this shape:**
- `QDir().mkpath()` is idempotent and cheap; calling it unconditionally (rather
  than checking `exists()` first) is the standard Qt idiom.
- Branching on `QLockFile::error()` keeps the "already running" message for the
  genuine case and turns silent misdiagnosis into a loud, accurate error for
  everything else. If this class of bug ever recurs, the log will say why
  instead of lying.

---

## Bug 2 — Packaged binary looks for bundled data on the build machine

### Symptom

After Bug 1 is worked around, the app starts but logs:

```
Failed to load bundled server list from /home/aidan/Race-Cloud/Projects/vypr/vyprkonnect/data
```

The server list is empty in the GUI. The package *does* install the data
correctly at `/usr/share/vyprkonnect/data` (confirmed: `dpkg -L vyprkonnect`
shows `servers.json` and 70+ `.ovpn` files there) — the binary just never looks
there.

### Root cause

`CMakeLists.txt` (tail section, as of `master`):

```cmake
# Bundled server data location (dev builds point at the source tree).
target_compile_definitions(vyprkonnect PRIVATE
    VYPR_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
```

This is **unconditional**, so every build — including `cpack`-produced release
`.deb`s — freezes the build machine's source-tree path into the binary. In
`src/main.cpp`, the resolution order is:

```cpp
// Data directory resolution order:
//   1. $VYPRKONNECT_DATA_DIR   (explicit override)
//   2. compile-time VYPR_DATA_DIR (dev builds: source tree)
//   3. installed location (/usr/share/vyprkonnect/data)
QString resolveDataDir()
{
    if (const QString env = qEnvironmentVariable("VYPRKONNECT_DATA_DIR"); !env.isEmpty())
        return env;
#ifdef VYPR_DATA_DIR
    return QStringLiteral(VYPR_DATA_DIR);
#endif
    return u"/usr/share/vyprkonnect/data"_s;
}
```

Because `VYPR_DATA_DIR` is always defined, step 3 (the fallback that matches
the package layout) is unreachable in any build that used the default
`CMAKE_CURRENT_SOURCE_DIR`. The comment "dev builds point at the source tree"
describes intent, but nothing enforces the "dev builds" condition.

### Suggested fix for `CMakeLists.txt`

Replace the unconditional definition with an opt-in for real dev builds:

```cmake
# Bundled server data location. OFF (the default) for packaged/release builds:
# the binary then falls back to the installed location
# (${CMAKE_INSTALL_FULL_DATAROOTDIR}/vyprkonnect/data) at runtime. Turn ON only
# for in-tree development runs.
option(VYPR_DEV_DATA_DIR "Point the binary at the source-tree data directory (dev builds only)" OFF)
if(VYPR_DEV_DATA_DIR)
    target_compile_definitions(vyprkonnect PRIVATE
        VYPR_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()
```

**Why this shape:**
- Default OFF means the next `.deb` **cannot regress** by accident — the failing
  configuration becomes something you must deliberately ask for.
- It preserves the convenience the original comment intended: dev runs from the
  build tree still work with `-DVYPR_DEV_DATA_DIR=ON` (or by setting
  `$VYPRKONNECT_DATA_DIR`, which needs no rebuild at all).
- Alternative considered: define `VYPR_DATA_DIR` as
  `"${CMAKE_INSTALL_FULL_DATAROOTDIR}/vyprkonnect/data"` so the macro always
  matches the install layout. This also works, but hard-codes an assumption
  about packaging layout into dev builds; the opt-in option is safer.

---

## Bug 3 — OpenVPN connect always fails: "Missing bundled config for …"

### Symptom

With the server list loading, WireGuard connections work end-to-end, but every
**OpenVPN** connect attempt fails immediately in the GUI:

```
Missing bundled config for <server name>
```

### Root cause

`src/core/nmopenvpnbackend.cpp` resolves the `.ovpn` file from the **compile-time
macro directly**, bypassing the app's own data-dir resolution:

```cpp
void NmOpenVpnBackend::ensureProfileAndConnect(const vypr::Server &server)
{
    const QString id = profileId(server);
    const QString ovpnPath = u"%1/ovpn/%2.ovpn"_s.arg(QStringLiteral(VYPR_DATA_DIR), server.name);

    // Rule 1: NM's openvpn plugin rejects `compress stub-v2` in imports.
    QFile src(ovpnPath);
    if (!src.open(QIODevice::ReadOnly)) {
        setMessage(u"Missing bundled config for %1"_s.arg(server.name));
        setState(Failed);
        return;
    }
```

So even when `resolveDataDir()` in `main.cpp` correctly honours
`$VYPRKONNECT_DATA_DIR` (which is how the server list was made to load on the
test machine), the OpenVPN backend still looks in the nonexistent build-machine
path and fails. WireGuard works because `nmwireguardbackend.cpp` creates NM
profiles via the D-Bus API from the in-memory server list and never touches a
config file.

Structurally, this is the same packaging flaw as Bug 2, but it also reveals a
code-health issue: **the data-dir resolution logic exists in exactly one place
(`main.cpp`) while a second consumer independently re-derives the path from the
raw macro.** Any future consumer (e.g. the planned server-list refresh tooling)
would hit the same trap.

### Suggested fix

**Preferred (structural):** move `resolveDataDir()` out of `main.cpp` into a
small shared header (e.g. `src/core/datadir.h`) or onto `NmBackendBase` /
`ServerModel`, have `main.cpp` and `NmOpenVpnBackend` both call it, and pass the
resolved dir into the backend constructor so the dependency is explicit.

**Minimal diff (if the structural move should wait):** in
`nmopenvpnbackend.cpp`, add the same resolution helper and use it:

```cpp
namespace {
// Keep in sync with resolveDataDir() in main.cpp — or better, merge into a
// shared helper so they cannot drift.
QString resolveDataDir()
{
    if (const QString env = qEnvironmentVariable("VYPRKONNECT_DATA_DIR"); !env.isEmpty())
        return env;
#ifdef VYPR_DATA_DIR
    return QStringLiteral(VYPR_DATA_DIR);
#endif
    return u"/usr/share/vyprkonnect/data"_s;
}
} // namespace
```

and change one line in `ensureProfileAndConnect()`:

```cpp
const QString ovpnPath = u"%1/ovpn/%2.ovpn"_s.arg(resolveDataDir(), server.name);
```

**Why:** with Bug 2 fixed (macro undefined in release builds), this change alone
makes OpenVPN work in packaged builds; with the macro still defined (dev
builds), behaviour is unchanged. Either way the env override starts working for
the backend too, matching what `main.cpp` already promises.

---

## End-user reproduction & evidence summary

All commands run on the test machine (Kubuntu 26.04.1, Qt 6.10.2):

```bash
# Bug 1: nothing running, no lock file, yet "already running"
$ ps aux | grep vyprkonnect        # no output
$ find ~ -name "vyprkonnect.lock"  # no output
$ vyprkonnect; echo $?
VyprKonnect is already running — refusing to start a second instance.
3

# Bug 1: identical failure with an EMPTY data dir → not contention, not $HOME state
$ mkdir -p /tmp/vdata && XDG_DATA_HOME=/tmp/vdata vyprkonnect; echo $?
VyprKonnect is already running — refusing to start a second instance.
3

# Bug 1: pre-creating the parent directory fixes startup
$ mkdir -p /tmp/vdata/vyprkonnect/vyprkonnect
$ XDG_DATA_HOME=/tmp/vdata QT_QPA_PLATFORM=offscreen timeout 6 vyprkonnect
alive after 1.5s                   # ran until the timeout killed it

# Bug 2: with startup fixed, the server list fails at the baked-in path
Failed to load bundled server list from /home/aidan/Race-Cloud/Projects/vypr/vyprkonnect/data

# Bug 2: the env override (resolution order step 1) fixes the server list...
$ VYPRKONNECT_DATA_DIR=/usr/share/vyprkonnect/data QT_QPA_PLATFORM=offscreen timeout 8 vyprkonnect
alive after 1.5s                   # no "Failed to load" warning

# Bug 3: ...but the OpenVPN backend ignores the override and fails anyway
# (GUI message: "Missing bundled config for <server>"; WireGuard connects fine)
```

## Workaround state on the test machine (for context; not part of the fix)

- `~/.local/share/vyprkonnect/vyprkonnect/` created manually → fixes Bug 1.
- User-level `~/.local/share/applications/vyprkonnect.desktop` override setting
  `Exec=env VYPRKONNECT_DATA_DIR=/usr/share/vyprkonnect/data vyprkonnect` → fixes Bug 2.
- A symlink shadowing the build-machine path was tried for Bug 3 and then
  **removed** at the tester's request; **OpenVPN is currently non-functional on
  the test machine** pending a fixed build. This is deliberate: the shadow path
  is not an acceptable end-user workaround, which is precisely why Bug 3 is
  release-blocking.

---

## Dev-machine verification checklist (after applying all three fixes)

```bash
# 1. Rebuild with default (release/packaging) options
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build && cpack

# 2. The binary must no longer contain the source-tree path:
strings build/vyprkonnect | grep -F "$PWD/data"        # expect: no output

# 3. Simulate a fresh machine: no app state, no env override
mv ~/.local/share/vyprkonnect /tmp/vypr-backup 2>/dev/null
QT_QPA_PLATFORM=offscreen timeout 6 ./build/vyprkonnect
#   expect: starts, no "already running", no "Failed to load bundled server list"

# 4. Install the new .deb on a clean machine and verify from the menu:
#    - app launches
#    - server list populated
#    - OpenVPN connect succeeds (config found, tunnel up)
#    - WireGuard connect still succeeds
#    - double-launch from menu + terminal → second instance exits with
#      "already running" (i.e. the guard still works for its real purpose)

# 5. Restore state on the dev machine if needed:
#    mv /tmp/vypr-backup ~/.local/share/vyprkonnect
```

## Minor note (cosmetic, non-fatal)

On Plasma 6.6 / Qt 6.10 the app logs at startup:

```
qt.core.qobject.connect: QObject::connect: No such signal QPlatformNativeInterface::systemTrayWindowChanged(QScreen*)
```

This comes from `QSystemTrayIcon` internals on this Qt/Plasma combination and
appears harmless, but it fires during tray initialisation and may be worth a
look.

## Suggested release notes for the fix version

> Fixes three defects found in clean-machine testing of 0.2.4: the app could
> fail to start on machines without existing app state (single-instance lock
> directory not created, all lock errors misreported as "already running");
> packaged builds looked for the bundled server data on the build machine's
> source path; and OpenVPN connections failed with "Missing bundled config"
> because the OpenVPN backend did not use the app's data-dir resolution.
> WireGuard was unaffected.
