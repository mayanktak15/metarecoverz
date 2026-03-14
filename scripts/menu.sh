#!/usr/bin/env bash
set -euo pipefail

# Simple TTY menu for MetaRecoverX helpers
# 1 => USB detection (wraps scripts/metarecoverx.sh detect-usb)
# 2 => Demo pipeline on dummy image
# q => Quit

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

while true; do
  clear || true
  echo "======================"
  echo "  MetaRecoverX Menu"
  echo "======================"
  echo "[1] Detect USB devices"
  echo "[2] Run demo pipeline (dummy.img)"
  echo "[q] Quit"
  echo
  read -rp "Select an option: " choice

  case "${choice}" in
    1)
      echo
      echo "[Detecting USB storage devices]"
      echo
      bash "${ROOT_DIR}/scripts/metarecoverx.sh" detect-usb || true
      echo
      read -rp "Press Enter to return to menu..." _pause
      ;;
    2)
      echo
      echo "[Running demo pipeline on dummy.img]"
      echo
      bash "${ROOT_DIR}/scripts/metarecoverx.sh" demo || true
      echo
      read -rp "Press Enter to return to menu..." _pause
      ;;
    q|Q)
      echo "Exiting menu."
      exit 0
      ;;
    *)
      echo "Unknown option: ${choice}";
      sleep 1
      ;;
  esac

done
