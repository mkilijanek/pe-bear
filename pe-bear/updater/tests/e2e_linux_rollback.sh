#!/bin/bash
# End-to-end rollback, with the real binaries: the package's PE-bear is a
# program that cannot answer the startup handshake, so the new build is put in
# place, found wanting, and the old one restored -- byte for byte, with the
# journal saying so and nothing left beside the installation.
#
# Usage: e2e_linux_rollback.sh <PE-bear> <pe-bear-updater>
set -u
PEBEAR=${1:?PE-bear binary}
HELPER=${2:?pe-bear-updater binary}
[ -x "$PEBEAR" ] && [ -x "$HELPER" ] || { echo "binaries missing: $PEBEAR $HELPER"; exit 2; }
for tool in tar xz sha256sum; do command -v $tool >/dev/null || { echo "$tool not available"; exit 2; }; done

E=$(mktemp -d "${TMPDIR:-/tmp}/pe-bear-e2e-rb.XXXXXX")
trap 'rm -rf "$E"' EXIT
INSTALL=$E/parent/install
mkdir -p "$INSTALL" "$E/pkgsrc/PE-bear-bad" "$E/xdg"
cp "$PEBEAR" "$HELPER" "$INSTALL/"; echo old > "$INSTALL/readme.txt"
BEFORE=$(cd "$INSTALL" && sha256sum PE-bear pe-bear-updater readme.txt)
# The "new build": a PE-bear that starts and exits without answering.
cp "$HELPER" "$E/pkgsrc/PE-bear-bad/"
printf '#!/bin/sh\nexit 3\n' > "$E/pkgsrc/PE-bear-bad/PE-bear"; chmod +x "$E/pkgsrc/PE-bear-bad/PE-bear"
echo bad > "$E/pkgsrc/PE-bear-bad/readme.txt"
XZ_OPT="-T0 -1" tar -C "$E/pkgsrc" -cJf "$E/pkg.tar.xz" PE-bear-bad

export XDG_DATA_HOME=$E/xdg QT_QPA_PLATFORM=offscreen
ROOT=$E/xdg/PE-bear/PE-bear-updates
mkdir -p "$ROOT/downloads/manual"; cp "$E/pkg.tar.xz" "$ROOT/downloads/manual/"
PKG=$ROOT/downloads/manual/pkg.tar.xz
VER=$("$INSTALL/pe-bear-updater" --version) || { echo "helper --version failed"; exit 1; }
SHA=$(sha256sum "$PKG" | cut -c1-64)
SIZE=$(stat -c %s "$PKG")
RUNID=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
NOW=$(date -u +%Y-%m-%dT%H:%M:%SZ)
cat > "$ROOT/handoff.json" <<JSON
{"version":1,"runId":"$RUNID","packagePath":"$PKG","packageSha256":"$SHA","packageSize":$SIZE,
 "targetDir":"$INSTALL","expectedVersion":"$VER","parentPid":2147483646,"relaunch":false,
 "createdAtUtc":"$NOW","assetUrl":"https://example.invalid/pkg.tar.xz","releaseTag":"v$VER-e2e","assetName":"pkg.tar.xz"}
JSON
chmod 600 "$ROOT/handoff.json"

fail() { echo "FAIL: $*"; echo "--- helper output:"; echo "$OUT"; echo "--- log:"; cat "$ROOT"/helper-*.log 2>/dev/null; exit 1; }

cd /
OUT=$(timeout 240 "$INSTALL/pe-bear-updater" --handoff "$ROOT/handoff.json" 2>&1 </dev/null); X=$?
[ "$X" -eq 0 ] || fail "original helper exit $X (it only steps out)"
for i in $(seq 1 180); do [ -f "$ROOT/last-result.json" ] && break; sleep 1; done
[ -f "$ROOT/last-result.json" ] || fail "no last-result.json"

grep -q '"result": *"RolledBack"' "$ROOT/last-result.json" || fail "result is not RolledBack: $(cat "$ROOT/last-result.json")"
grep -q '"exitCode": *20' "$ROOT/last-result.json" || fail "exit code is not 20: $(cat "$ROOT/last-result.json")"
grep -q '"leftUntouched": *false' "$ROOT/last-result.json" || fail "a rollback is not 'left untouched'"
AFTER=$(cd "$INSTALL" && sha256sum PE-bear pe-bear-updater readme.txt)
[ "$BEFORE" = "$AFTER" ] || fail "the installation differs from before:
$BEFORE
--
$AFTER"
LEFT=$(ls -A "$E/parent" | grep -v '^install$' || true)
[ -z "$LEFT" ] || fail "left beside the installation: $LEFT"
REC=$(ls "$ROOT"/transactions/*.json 2>/dev/null | head -1)
[ -n "$REC" ] || fail "no journal record"
grep -q '"state": *"RolledBack"' "$REC" || fail "journal not RolledBack: $(grep '"state"' "$REC")"
[ ! -f "$ROOT/handoff.json" ] || fail "handoff.json was not consumed"
pgrep -f "$E/" >/dev/null && fail "a process from the test is still running: $(pgrep -af "$E/")"
echo "PASS: a build that does not answer the handshake is rolled back; the installation is byte-for-byte what it was"
