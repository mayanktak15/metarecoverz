#!/usr/bin/env bash
set -euo pipefail

# Resolve repo root relative to this script
cd "$(dirname "$0")/.."

# Build low-level binaries only (no Python)
./scripts/build.sh

echo "\n[OK] MetaRecoverX low-level binaries built."
echo "    xfs_scan        => build/xfs_scan"
echo "    btrfs_scan      => build/btrfs_scan"
echo "    reconstruct_csv => build/reconstruct_csv"
echo "    recover_csv     => build/recover_csv"
echo "    Try the offline pipeline: scripts/metarecoverx.sh pipeline --image <disk.img>"
