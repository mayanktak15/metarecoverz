#!/usr/bin/env bash
# detect_usb.sh — List connected USB storage devices (offline, no internet needed)
# Usage: ./scripts/detect_usb.sh
# Output (one line per device): <name> usb <size>
# Example:
#   sdb usb 32G
#   sdc usb 64G

set -euo pipefail

if ! command -v lsblk >/dev/null 2>&1; then
    echo "error: lsblk not found (install util-linux)" >&2
    exit 1
fi

# Print header
echo "NAME  TRAN  SIZE  MOUNTPOINT"
echo "----  ----  ----  ----------"

# Parse lsblk output; select only rows where TRAN == usb
lsblk -o NAME,TRAN,SIZE,MOUNTPOINT -n 2>/dev/null \
    | awk '$2 == "usb" { printf "%-6s %-5s %-6s %s\n", $1, $2, $3, ($4 == "" ? "" : $4) }'

# Warn if nothing found
FOUND=$(lsblk -o NAME,TRAN -n 2>/dev/null | awk '$2 == "usb"' | wc -l)
if [[ "$FOUND" -eq 0 ]]; then
    echo "(no USB storage devices detected)"
fi
