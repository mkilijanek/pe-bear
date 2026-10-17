#!/bin/bash
# End-to-end, with the real binaries: the staging root is made of the
# installation's parent, which can be a directory other users write to
# (/tmp, a shared tools folder). Somebody else got there first and left
# ".PE-bear-staging" as a link into a directory of theirs. The helper must not
# unpack the package through it: the update is refused, the installation is
# exactly as it was, and nothing was written into the other directory.
#
# Usage: e2e_linux_hostile_staging.sh <PE-bear> <pe-bear-updater>
set -u
PEBEAR=${1:?PE-bear binary}
HELPER=${2:?pe-bear-updater binary}
[ -x "$PEBEAR" ] && [ -x "$HELPER" ] || { echo "binaries missing: $PEBEAR $HELPER"; exit 2; }
for tool in tar xz sha256sum stat; do command -v $tool >/dev/null || { echo "$tool not available"; exit 2; }; done

E=$(mktemp -d "${TMPDIR:-/tmp}/pe-bear-e2e-hostile.XXXXXX")
trap 'rm -rf "$E"' EXIT
INSTALL=$E/parent/install
mkdir -p "$INSTALL" "$E/pkgsrc/PE-bear-new" "$E/xdg" "$E/theirs"
cp "$PEBEAR" "$HELPER" "$INSTALL/"; echo old > "$INSTALL/readme.txt"
cp "$PEBEAR" "$HELPER" "$E/pkgsrc/PE-bear-new/"; echo new > "$E/pkgsrc/PE-bear-new/readme.txt"
XZ_OPT="-T0 -1" tar -C "$E/pkgsrc" -cJf "$E/pkg.tar.xz" PE-bear-new

# What the other user left behind.
ln -s "$E/theirs" "$E/parent/.PE-bear-staging"
BEFORE=$(stat -c %y "$E/theirs")

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
OUT=$(timeout 180 "$INSTALL/pe-bear-updater" --handoff "$ROOT/handoff.json" 2>&1 </dev/null); X=$?
[ "$X" -eq 0 ] || fail "original helper exit $X"
for i in $(seq 1 60); do [ -f "$ROOT/last-result.json" ] && break; sleep 1; done
[ -f "$ROOT/last-result.json" ] || fail "no last-result.json after the copy ran"

grep -q '"result": *"RolledBack"' "$ROOT/last-result.json" \
	|| fail "the update was not refused: $(cat "$ROOT/last-result.json")"
grep -q 'not private' "$ROOT/last-result.json" || fail "refused, but not for the right reason: $(cat "$ROOT/last-result.json")"
[ "$(cat "$INSTALL/readme.txt")" = old ] || fail "the installation was changed"
[ "$(stat -c %y "$E/theirs")" = "$BEFORE" ] || fail "something was written into the other user's directory"
[ -z "$(ls -A "$E/theirs")" ] || fail "the other user's directory is not empty: $(ls -A "$E/theirs")"
LEFT=$(ls -A "$E/parent" | grep -v -e '^install$' -e '^\.PE-bear-staging$' || true)
[ -z "$LEFT" ] || fail "left beside the installation: $LEFT"
pgrep -f "$E/" >/dev/null && fail "a process from the test is still running: $(pgrep -af "$E/")"
echo "PASS: a staging root that is a link into somebody else's directory was refused; installation untouched, nothing written through the link"
