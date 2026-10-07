#!/bin/bash
# End-to-end, with the real binaries: a portable Linux installation (a
# directory holding PE-bear and pe-bear-updater) is replaced by a tar.xz
# package through the helper, started from inside that directory as PE-bear
# would start it. Everything the unit suite fakes is real here: the
# filesystem, the archive, the process boundary, the startup handshake with
# the new build, the journal. Asserts the outcome, not just the exit code.
#
# Usage: e2e_linux_tarxz.sh <PE-bear> <pe-bear-updater>
set -u
PEBEAR=${1:?PE-bear binary}
HELPER=${2:?pe-bear-updater binary}
[ -x "$PEBEAR" ] && [ -x "$HELPER" ] || { echo "binaries missing: $PEBEAR $HELPER"; exit 2; }
for tool in tar xz sha256sum; do command -v $tool >/dev/null || { echo "$tool not available"; exit 2; }; done

E=$(mktemp -d "${TMPDIR:-/tmp}/pe-bear-e2e.XXXXXX")
trap 'rm -rf "$E"' EXIT
INSTALL=$E/parent/install
mkdir -p "$INSTALL" "$E/pkgsrc/PE-bear-new" "$E/xdg"
cp "$PEBEAR" "$HELPER" "$INSTALL/"; echo old > "$INSTALL/readme.txt"
cp "$PEBEAR" "$HELPER" "$E/pkgsrc/PE-bear-new/"; echo new > "$E/pkgsrc/PE-bear-new/readme.txt"
# Fast and multi-threaded: a debug PE-bear is large and the ratio is beside the point.
XZ_OPT="-T0 -1" tar -C "$E/pkgsrc" -cJf "$E/pkg.tar.xz" PE-bear-new

# The updater's private directory is derived from XDG_DATA_HOME, so the test
# never touches the real one; the handshake instance needs a QPA platform.
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

# Started from inside the installation, as PE-bear starts it. The copy it
# steps out into inherits this stdout, so the capture also waits for it.
cd /
OUT=$(timeout 180 "$INSTALL/pe-bear-updater" --handoff "$ROOT/handoff.json" 2>&1 </dev/null); X=$?
[ "$X" -eq 0 ] || fail "original helper exit $X"
echo "$OUT" | grep -q "stepping out of the installation" || fail "the helper did not step out of the installation"
for i in $(seq 1 60); do [ -f "$ROOT/last-result.json" ] && break; sleep 1; done
[ -f "$ROOT/last-result.json" ] || fail "no last-result.json after the copy ran"

grep -q '"result": *"Succeeded"' "$ROOT/last-result.json" || fail "result is not Succeeded: $(cat "$ROOT/last-result.json")"
[ "$(cat "$INSTALL/readme.txt")" = new ] || fail "the installation was not replaced"
[ -x "$INSTALL/PE-bear" ] && [ -x "$INSTALL/pe-bear-updater" ] || fail "executables missing or not executable after the swap"
LEFT=$(ls -A "$E/parent" | grep -v '^install$' || true)
[ -z "$LEFT" ] || fail "left beside the installation: $LEFT"
REC=$(ls "$ROOT"/transactions/*.json 2>/dev/null | head -1)
[ -n "$REC" ] || fail "no journal record"
grep -q '"state": *"Committed"' "$REC" || fail "journal not Committed: $(grep '"state"' "$REC")"
grep -q '"error"' "$REC" && fail "journal carries an error: $(grep '"error"' "$REC")"
[ -d "$ROOT/helper/$RUNID" ] || fail "the relocated copy's directory is missing"
[ ! -f "$ROOT/handoff.json" ] || fail "handoff.json was not consumed"
pgrep -f "$E/" >/dev/null && fail "a process from the test is still running: $(pgrep -af "$E/")"
echo "PASS: tar.xz installed through the relocated helper, handshake answered, journal Committed, nothing left beside the installation"
