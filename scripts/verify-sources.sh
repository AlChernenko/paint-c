#!/usr/bin/env bash
# verify-sources.sh — project integrity checker.
#
#   ./scripts/verify-sources.sh           report only
#   ./scripts/verify-sources.sh --clean   also remove backup_* dirs
set -euo pipefail
cd "$(cd "$(dirname "$0")/.." && pwd)"

MESON=src/meson.build
[ -f "$MESON" ] || { echo "!! src/meson.build missing"; exit 1; }

SOURCES=$(sed -n '/sources = files(/,/)/p' "$MESON" | grep -oE "'[^']+\.c'" | tr -d "'")
N=$(printf '%s\n' "$SOURCES" | wc -l)

echo "=== Checking $N source files listed in meson.build ==="
fail=0
for f in $SOURCES; do
    if [ -f "src/$f" ]; then
        printf '  ✓ %s\n' "src/$f"
    else
        printf '  ✗ MISSING src/%s\n' "$f"
        fail=1
    fi
done

echo ""
echo "=== Orphan .c/.h in src/ (not listed in meson.build) ==="
orphans=0
while IFS= read -r f; do
    rel="${f#src/}"
    case "$f" in *.h) continue ;; esac
    if ! printf '%s\n' "$SOURCES" | grep -qxF "$rel"; then
        printf '  ? %s\n' "$f"
        orphans=$((orphans + 1))
    fi
done < <(find src -type f -name '*.c' | sort)
[ "$orphans" -eq 0 ] && echo "  (none)"

echo ""
echo "=== Backup directories ==="
found_backups=0
for d in backup_*; do
    [ -d "$d" ] && { printf '  %s (%s)\n' "$d" "$(du -sh "$d" | cut -f1)"; found_backups=1; }
done
[ "$found_backups" -eq 0 ] && echo "  (none)"

if [ "${1:-}" = "--clean" ]; then
    echo ""
    echo "=== --clean: removing backup_* ==="
    rm -rf backup_* && echo "  done"
fi

echo ""
if [ "$fail" -eq 0 ]; then
    echo "INTEGRITY OK"
    exit 0
else
    echo "INTEGRITY FAILED — files above marked ✗" >&2
    exit 1
fi
