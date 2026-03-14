#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

# Try CMake build first; if unavailable, fallback to plain g++
set +e
cmake --version >/dev/null 2>&1
HASCMAKE=$?
set -e

if [ "$HASCMAKE" -eq 0 ]; then
	mkdir -p build
	cd build
	cmake ../core >/dev/null
	cmake --build . --config Release
	cd ..
else
	echo "[WARN] CMake not found. Falling back to g++ direct build"
	mkdir -p build
	g++ -std=c++17 -O2 -o build/xfs_scan core/src/xfs_scan.cpp
	g++ -std=c++17 -O2 -o build/btrfs_scan core/src/btrfs_scan.cpp
	g++ -std=c++17 -O2 -o build/reconstruct_csv core/src/reconstruct_csv.cpp
	g++ -std=c++17 -O2 -o build/recover_csv core/src/recover_csv.cpp
fi

echo "\n[OK] Built binaries:"
echo "  build/xfs_scan"
echo "  build/btrfs_scan"
echo "  build/reconstruct_csv"
echo "  build/recover_csv"
