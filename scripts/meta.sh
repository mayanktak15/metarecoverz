#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART_DIR="$ROOT_DIR/artifacts"
REC_DIR="$ROOT_DIR/recovered"
BUILD_DIR="$ROOT_DIR/build"

ensure_dirs() {
  mkdir -p "$ART_DIR" "$REC_DIR" "$BUILD_DIR"
}

timestamp() {
  date -u +"%Y-%m-%dT%H:%M:%SZ"
}

print_usage() {
cat <<'USAGE'
MetaRecoverX

Usage:
  scripts/metarecoverx.sh build
  scripts/metarecoverx.sh acquire --image <path>
  scripts/metarecoverx.sh scan xfs|btrfs --image <path> [--format csv|json]
  scripts/metarecoverx.sh reconstruct
  scripts/metarecoverx.sh recover --image <path>
  scripts/metarecoverx.sh pipeline --image <path>
  scripts/metarecoverx.sh detect-usb
  scripts/metarecoverx.sh pipeline-usb --device /dev/sdX
  scripts/metarecoverx.sh demo

Commands:
  build        Compile C++ binaries
  acquire      Generate acquisition metadata (hash + size)
  scan         Scan filesystem metadata
  reconstruct  Combine scan CSV into metadata
  recover      Recover file extents
  pipeline     Run full pipeline
  detect-usb   List USB storage devices
  pipeline-usb Image USB device then run pipeline
  demo         Run pipeline on dummy image
USAGE
}

resolve_path() {
  command -v realpath >/dev/null && realpath "$1" || readlink -f "$1"
}

cmd_build() {

  if [[ -x "$BUILD_DIR/xfs_scan" ]]; then
      echo "Binaries already built"
      return
  fi

  echo "Building binaries..."
  ( cd "$ROOT_DIR" && ./scripts/build.sh )
}

cmd_acquire() {

  local image=""

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2 ;;
      *) echo "Unknown arg $1"; exit 1 ;;
    esac
  done

  [[ -f "$image" ]] || { echo "Image not found"; exit 1; }

  ensure_dirs

  local size hash abs
  size=$(stat -c%s "$image")
  hash=$(sha256sum "$image" | awk '{print $1}')
  abs=$(resolve_path "$image")

cat > "$ART_DIR/acquire.json" <<JSON
{
  "schema": "metarecoverx.acquire.v1",
  "image": "$abs",
  "generated_at": "$(timestamp)",
  "size": $size,
  "hash": {
    "algo": "sha256",
    "value": "$hash"
  }
}
JSON

echo "acquire.json written"
}

cmd_scan() {

  local fs="$1"
  shift

  local image=""
  local format="csv"

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2 ;;
      --format) format="$2"; shift 2 ;;
      *) echo "Unknown arg $1"; exit 1 ;;
    esac
  done

  [[ -f "$image" ]] || { echo "Image missing"; exit 1; }

  ensure_dirs
  cmd_build

  case "$fs" in

    xfs)

      if [[ "$format" == "csv" ]]; then
        "$BUILD_DIR/xfs_scan" --image "$image" --format csv > "$ART_DIR/xfs_scan.csv"
      else
        "$BUILD_DIR/xfs_scan" --image "$image" > "$ART_DIR/xfs_scan.json"
      fi
      ;;

    btrfs)

      if [[ "$format" == "csv" ]]; then
        "$BUILD_DIR/btrfs_scan" --image "$image" --format csv > "$ART_DIR/btrfs_scan.csv"
      else
        "$BUILD_DIR/btrfs_scan" --image "$image" > "$ART_DIR/btrfs_scan.json"
      fi
      ;;

    *)
      echo "Unknown filesystem"
      exit 1
      ;;

  esac
}

cmd_reconstruct() {

  ensure_dirs
  cmd_build

  "$BUILD_DIR/reconstruct_csv" \
      --out "$ART_DIR/metadata.json" \
      --csv-out "$ART_DIR/metadata.csv" \
      "$ART_DIR/"*_scan.csv

echo "Metadata reconstructed"
}

cmd_recover() {

  local image=""

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2 ;;
      *) echo "Unknown arg"; exit 1 ;;
    esac
  done

  [[ -f "$image" ]] || { echo "Image missing"; exit 1; }

  cmd_build

  "$BUILD_DIR/recover_csv" \
      --image "$image" \
      --plan "$ART_DIR/metadata.csv" \
      --out "$REC_DIR"
}

cmd_pipeline() {

  local image="$1"

  echo "[1/5] build"
  cmd_build

  echo "[2/5] acquire"
  cmd_acquire --image "$image"

  echo "[3/5] scan"
  cmd_scan xfs --image "$image" --format csv
  cmd_scan btrfs --image "$image" --format csv

  echo "[4/5] reconstruct"
  cmd_reconstruct

  echo "[5/5] recover"
  cmd_recover --image "$image"

  cp "$ART_DIR/metadata.csv" "$ROOT_DIR/report.csv"

  echo "Pipeline complete"
}

cmd_detect_usb() {

lsblk -o NAME,TRAN,SIZE,MOUNTPOINT | awk '
NR==1{print;next}
$2=="usb"{print}
'

}

cmd_pipeline_usb() {

  local device="$1"

  ensure_dirs
  cmd_build

  local img="$ART_DIR/usb_image.img"

  echo "Imaging device $device"

  sudo dd if="$device" of="$img" bs=16M status=progress conv=noerror,sync

  sha256sum "$img" > "$ART_DIR/hash.txt"

  cmd_acquire --image "$img"

  cmd_scan xfs --image "$img" --format csv
  cmd_scan btrfs --image "$img" --format csv

  cmd_reconstruct
  cmd_recover --image "$img"

echo "USB pipeline finished"
}

cmd_demo() {

  ensure_dirs
  cmd_build

  local img="dummy.img"

  if [[ ! -f "$img" ]]; then
    dd if=/dev/zero of="$img" bs=1M count=5
  fi

  cmd_pipeline "$img"
}

main() {

  case "${1:-}" in

    build) shift; cmd_build "$@" ;;
    acquire) shift; cmd_acquire "$@" ;;
    scan) shift; cmd_scan "$@" ;;
    reconstruct) shift; cmd_reconstruct "$@" ;;
    recover) shift; cmd_recover "$@" ;;
    pipeline) shift; cmd_pipeline "$@" ;;
    detect-usb) cmd_detect_usb ;;
    pipeline-usb) shift; cmd_pipeline_usb "$@" ;;
    demo) cmd_demo ;;
    *) print_usage ;;

  esac

}

main "$@"