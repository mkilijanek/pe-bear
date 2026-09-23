# PE-bear updater

Native update discovery for PE-bear, built on the GitHub Releases API.

This directory contains milestone **M1** of the updater: everything up to a
verified package sitting on disk. Performing the installation is M2 (Windows),
M3 (Linux and macOS) and M4 (packaging, CI and the release manifest).

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

## What it deliberately does not do

- Install anything. `Install and Restart` is enabled only in `ReadyToInstall`
  and currently reports that the installer is not part of this build.
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
helper (M2) recomputes the digest itself rather than trusting this result.

## Layout

| Path | Role |
|---|---|
| `Version.*` | strict numeric release versions; the number itself lives only in `rebear_ver_short.h` |
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
| `PEBEAR_PACKAGE_TYPE` | *(empty)* | `windows-zip`, `linux-tar-xz`, `linux-appimage`, `macos-app-zip` |
| `PEBEAR_BUILD_RUNTIME` | *(empty)* | toolchain tag, e.g. `vs17`; inferred from `_MSC_VER` when unset |
| `PEBEAR_MIN_OS_VERSION` | *(empty)* | lowest OS version this build supports |

A build that does not declare `PEBEAR_PACKAGE_TYPE` cannot prove which package
would replace it, so it degrades to notify-only. Release packaging must set it;
wiring that into the published packages, together with the `pe-bear-build.json`
manifest, is M4.

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
