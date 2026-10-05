# PE-bear updater

Native update discovery for PE-bear, built on the GitHub Releases API.

This directory contains milestones **M1** (discovery, download, verification)
and **M2** (the transactional installer, the helper process, recovery, and the
hand-off from the GUI -- validated on Windows only to the extent recorded in
#5 and #27). Remaining:
M3 -- platform coverage for Linux and macOS packaging shapes -- and M4, which
wires `PEBEAR_PACKAGE_TYPE` and the `pe-bear-build.json` manifest into the
published packages. Until M4 does that, a release build still degrades to
notify-only, because it cannot prove which package would replace it.

## What it does today

1. Once a day at most, in the background, asks the GitHub API for the latest
   **stable** release of `hasherezade/pe-bear`. Drafts and prereleases are
   rejected.
2. Picks the one asset that is a drop-in replacement for the running build,
   matching operating system, architecture, package format, Qt major version
   and toolchain runtime. If two assets match, or none does, it says so instead
   of guessing.
3. Downloads it, but only after the user opted in or asked.
4. Confirms the size and the SHA-256 published by the API, streaming the hash so
   the window stays responsive. A package that does not match is deleted.
5. Stops at `ReadyToInstall` and offers the user the choice.
6. On the user's explicit confirmation -- asked every time, with a count of
   unsaved changes that would be lost -- writes an owner-only instruction file
   into its private directory and starts `pe-bear-updater` detached, then
   closes. The helper waits for that, bounded; it never forces it.
7. At the next start, reads the helper's result once, tells the user how it
   went -- including, after a refusal, that nothing was changed -- and forgets
   it. Before that, it acts on anything an earlier update left unfinished.

Between steps 6 and 7 nothing here runs. The helper re-checks the package
digest and the paths in its own process, replaces the installation inside a
transaction, commits only after the new build answers a one-time handshake,
and restarts PE-bear. `UpdateManager` has one state for this, `Installing`,
and it is the last one this process ever sees.

## How installing works

PE-bear cannot replace PE-bear. On Windows a running executable's image cannot
be renamed or deleted, so something that is not the application has to do it,
from outside the directory being replaced. That is the whole reason
`pe-bear-updater` exists as a separate program.

```
PE-bear                          pe-bear-updater
-------                          ---------------
verify package
write handoff.json  ----------->  re-verify digest, canonicalise paths
start helper                      check the target is updatable
quit                              wait for PE-bear to exit (never kills it)
                                  open a transaction
                                  stage the package
                                  check the staged tree is PE-bear
                                  move the installation aside
                                  activate
     <-- --update-handshake ----   start the new build
write response, exit  ---------->  read the response: right nonce? right
                                   version? started?
                                  yes -> commit, discard the backup
                                  no  -> roll back, restore the previous build
```

Three properties of that exchange are the point of it:

- **Elapsed time is not evidence.** A build that starts and crashes a second
  later has validated nothing. The new build has to say something specific,
  echoing a one-time nonce, and it says it only after `QApplication` and the
  main window have been constructed -- so a missing Qt library, an unloadable
  platform plugin or unreadable settings all show up as no answer, and no
  answer rolls back.
- **Nothing is checked twice in the same process.** The helper recomputes the
  SHA-256 itself. PE-bear hashed the package when it arrived, which was a
  statement about the past, not a property of the file.
- **Staging and the layout check happen before the backup.** A package that
  will not unpack, or unpacks into something that is not PE-bear, has to be
  discovered while the working installation is still in place. A digest proves
  the bytes; it does not prove they are the right program.

Every filesystem step is recorded in a journal before the state advances, so an
interruption leaves something that can be replayed backwards. The helper's exit
code says what happened, and five of its nine outcomes guarantee the
installation is exactly as it was.

## What it deliberately does not do

- Terminate PE-bear. The helper waits, bounded, and refuses when the wait runs
  out. There is deliberately no code in it capable of ending another process:
  the alternative discards unsaved work that belongs to the user.
- Touch a managed installation. Anything under `/usr`, inside a Flatpak or Snap,
  or in a directory the user cannot write to is reported and left alone.
- Elevate. No `sudo`, no `pkexec`, no UAC, and no package manager is ever run —
  not even to query one.
- Migrate between variants. A Qt5 build is never replaced by a Qt6 package, an
  x86 build never by an x64 one, an AppImage never by a tarball.
- Resume a partial download. Bytes from an earlier, unverified session are
  discarded rather than reused.
- Send anything about the user. The only outgoing detail is `User-Agent:
  PE-bear/<version>`; names, paths and hashes of analysed samples never leave
  the machine.

## Trust model

Integrity rests on the SHA-256 that the GitHub Releases API publishes for each
asset, fetched over a fully validated TLS connection. **This is not a publisher
signature.** It proves the bytes match what the API says, which means trusting
GitHub's metadata. A signed manifest is a v2 item.

TLS errors are fatal and never ignored. Redirects are followed only to an
explicit host allowlist. Every process boundary re-validates: the installer
helper recomputes the digest itself rather than trusting this result.

**`pe-bear-updater` is not a privilege boundary**, and nothing about it pretends
to be one. It runs as whoever started it and never elevates, so it can do
nothing its invoker could not do by hand. There is therefore no secret on its
command line and no attempt to authenticate its caller; that would be theatre.
The instruction file is owner-only and lives in the updater's private directory
for a narrower reason: a command line is readable by every user on the machine,
and the package path and expected digest should not be published that way. The
checks that carry weight are the ones the helper performs itself against the
filesystem -- the digest, the canonical paths, that the package is inside the
updater's own directory, that the target is a real updatable installation, and
that the instruction is not stale.

Only *whether* an archive entry is executable is taken from the package, never
its stored mode: carrying the mode across would carry setuid and setgid bits out
of a file that arrived over the network.

## Layout

| Path | Role |
|---|---|
| `Version.*` | strict numeric release versions, plus the fork's `-pNNN`; the number itself lives only in `rebear_ver_short.h` |
| `BuildProfile.*` | what this build is: platform, architecture, package type, Qt major, runtime |
| `UpdateTypes.*` | states, errors and the release/asset/candidate structures |
| `ReleaseClient.*` | the bounded GitHub API exchange, plus pure parsing |
| `AssetSelector.*` | asset-name grammar and exact-match selection |
| `InstallationDetector.*` | portable vs. system vs. managed, and writability |
| `UpdatePaths.*` | the private `PE-bear-updates` directory tree |
| `DownloadManager.*` | streaming download to `.part`, one at a time |
| `PackageVerifier.*` | size and SHA-256, synchronous and event-loop-friendly |
| `UpdateSettings.*` | preferences, stored with the rest of the configuration |
| `UpdateManager.*` | the state machine |
| `FileSystem.*` | `IFileSystem` and the real implementation; every destructive step goes through it |
| `TransactionTypes.*`, `TransactionJournal.*`, `Transaction.*` | the install transaction, its on-disk record, and reverse-order rollback |
| `ArchiveTypes.*` | `IArchiveReader` and the entry description the policy judges |
| `ExtractionPolicy.*` | every reason an archive entry is refused, as a pure function |
| `PackageExtractor.*` | unpacking under the policy, nothing written until the whole list passes |
| `LibArchiveReader.*` | libarchive with only zip, tar, xz and gzip enabled |
| `PlatformInstaller.h` | the per-OS steps, and nothing else |
| `DirectoryInstaller.*` | the one concrete installer: an installation that is a single directory |
| `Installer.*` | the platform-independent ordering and failure handling |
| `HelperHandoff.*` | the instruction file PE-bear writes and the helper refuses to trust |
| `ProcessControl.*` | waiting for a process and starting one, behind interfaces |
| `StartupHandshake.*` | how a new build proves it works |
| `UpdateHelper.*` | the helper's orchestration: the order of the refusals |
| `helper/main.cpp` | the `pe-bear-updater` executable |
| `gui/` | dialog and application glue — the only part that uses QtWidgets |
| `tests/` | the unit tests, including a checked-in real API payload |

`pebear_update_core` links Qt Core and Qt Network only. The QtWidgets boundary
is enforced by the `tst_no_widgets_dependency` test, not merely documented.

## Build options

| Option | Default | Meaning |
|---|---|---|
| `PEBEAR_ENABLE_UPDATER` | `ON` | build the updater at all |
| `PEBEAR_ENABLE_LEGACY_UPDATER` | `OFF` | allow it on Qt4 builds; unsupported |
| `PEBEAR_BUILD_UPDATER_TESTS` | `OFF` | build and register the unit tests |
| `PEBEAR_WITH_LIBARCHIVE` | `ON` | the zip/tar.xz reader, and with it `pe-bear-updater` |
| `PEBEAR_PACKAGE_TYPE` | *(empty)* | `windows-zip`, `linux-tar-xz`, `linux-appimage`, `macos-app-zip` |
| `PEBEAR_BUILD_RUNTIME` | *(empty)* | toolchain tag, e.g. `vs17`; inferred from `_MSC_VER` when unset |
| `PEBEAR_MIN_OS_VERSION` | *(empty)* | lowest OS version this build supports |
| `PEBEAR_FORK_PATCH` | `0` | the fork's number on top of the release: `3` makes this build `0.7.2-p003` |

The fork numbers its own builds on top of the upstream release: `0.7.2-p001`,
`0.7.2-p002`, ... They sort after the release they are built on and before
whatever upstream publishes next (`0.7.2 < 0.7.2-p001 < 0.7.2-p002 < 0.7.2.1 <
0.7.3`), so a fork release tagged `v0.7.2-p002` is an update for a `-p001`
build and the upstream `v0.7.3` is an update for both. Any other dash or plus
suffix is still a prerelease to this code and is never offered. The number is
one more field in `rebear_ver_short.h`, set from CMake; the About box and the
window caption show it through the same macro.

A build that does not declare `PEBEAR_PACKAGE_TYPE` cannot prove which package
would replace it, so it degrades to notify-only. Release packaging must set it;
wiring that into the published packages, together with the `pe-bear-build.json`
manifest, is M4.

Without libarchive the build still succeeds, `pe-bear-updater` is not produced,
and the updater degrades to notify-only in the same way — CMake says so at
configure time rather than leaving it to be discovered at runtime. Install
`libarchive-dev` (Linux), `brew install libarchive` (macOS) or
`vcpkg install libarchive` (Windows).

## Settings

Stored in the `Updates` group of PE-bear's existing `QSettings`.

| Key | Default | Meaning |
|---|---|---|
| `AutoCheck` | `true` | check in the background |
| `AutoDownload` | `false` | download without asking |
| `CheckIntervalHours` | `24` | minimum gap between automatic checks |
| `LastCheck` | *(unset)* | ISO-8601 timestamp of the last check |
| `SkippedVersion` | *(unset)* | version the user asked not to be reminded about |

Installing is not a setting. It is an explicit action, confirmed every time, and
the user is told first if unsaved analysis would be lost.

## Tests

```sh
cmake -S . -B build -DUSE_QT5=ON -DPEBEAR_BUILD_UPDATER_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Everything runs offline; the only socket is a loopback server the download test
starts itself. `tests/data/release_v0.7.2.json` is the real API response for
v0.7.2, reduced to the fields the updater reads — if the upstream asset naming
convention drifts, `tst_liverelease` fails instead of a user discovering that no
package matches their build.
