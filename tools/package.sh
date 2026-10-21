#!/bin/sh
# Source distributions contain no build products or Python implementation.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUTPUT=${1:-"$ROOT/.."}
mkdir -p -- "$OUTPUT"
OUTPUT=$(CDPATH= cd -- "$OUTPUT" && pwd)
VERSION=$(cat "$ROOT/VERSION")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-dist.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir "$WORK/kremen"
tar -C "$ROOT" --exclude='./build' --exclude='./selfhost/build' \
    --exclude='./.git' --exclude='./.venv' --exclude='__pycache__' \
    --exclude='*.py' --exclude='*.pyc' --exclude='*.pyo' \
    --exclude='*.zip' --exclude='*.tar.gz' -cf "$WORK/source.tar" .
tar -C "$WORK/kremen" -xf "$WORK/source.tar"
(cd "$WORK/kremen" && find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$WORK/checksums"
mv "$WORK/checksums" "$WORK/kremen/SHA256SUMS"
cp "$WORK/kremen/SHA256SUMS" "$ROOT/SHA256SUMS"
(cd "$WORK/kremen" && sha256sum -c SHA256SUMS > /dev/null)
(cd "$WORK" && zip -q -X -r "$WORK/kremen-v$VERSION.zip" kremen)
tar -C "$WORK" -czf "$WORK/kremen-v$VERSION.tar.gz" kremen
mv "$WORK/kremen-v$VERSION.zip" "$OUTPUT/kremen-v$VERSION.zip"
mv "$WORK/kremen-v$VERSION.tar.gz" "$OUTPUT/kremen-v$VERSION.tar.gz"
cp "$OUTPUT/kremen-v$VERSION.zip" "$OUTPUT/kremen.zip"
(cd "$OUTPUT" && sha256sum "kremen-v$VERSION.zip" "kremen-v$VERSION.tar.gz") > "$OUTPUT/kremen-v$VERSION.sha256"
printf 'Created %s/kremen-v%s.zip and .tar.gz\n' "$OUTPUT" "$VERSION"
