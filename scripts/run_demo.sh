#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# Ensure built (pure C++)
./scripts/build.sh >/dev/null

IMG=${IMG:-dummy.img}

# Create a 1MiB zero-filled dummy image for demo
if [[ ! -f "$IMG" ]]; then
  dd if=/dev/zero of="$IMG" bs=1M count=1 status=none
fi

mkdir -p artifacts recovered

# Acquire (size + sha256) using coreutils only
SIZE=$(stat -c%s "$IMG")
HASH=$(sha256sum "$IMG" | awk '{print $1}')
cat > artifacts/acquire.json <<JSON
{
  "schema": "metarecoverx.acquire.v1",
  "image": "$(realpath "$IMG")",
  "generated_at": "now",
  "size": $SIZE,
  "hash": { "algo": "sha256", "value": "$HASH" },
  "notes": ""
}
JSON

# Scan to CSV (offline friendly)
./build/xfs_scan --image "$IMG" --format csv > artifacts/xfs_scan.csv
./build/btrfs_scan --image "$IMG" --format csv > artifacts/btrfs_scan.csv

# Reconstruct (C++ tool)
./build/reconstruct_csv --out artifacts/metadata.json --csv-out artifacts/metadata.csv artifacts/xfs_scan.csv artifacts/btrfs_scan.csv

"./build/recover_csv" --image "$IMG" --plan artifacts/metadata.csv --out recovered

# Report (CSV already generated)
cp artifacts/metadata.csv report.csv

echo "\n[DONE] Demo complete. Outputs:"
echo "  artifacts/ (JSON outputs)"
echo "  recovered/ (carved files)"
echo "  report.csv (open in a spreadsheet)"
