#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART_DIR="$ROOT_DIR/artifacts"
REC_DIR="$ROOT_DIR/recovered"
BUILD_DIR="$ROOT_DIR/build"

ensure_dirs() {
  mkdir -p "$ART_DIR" "$REC_DIR" "$BUILD_DIR"
}

print_usage() {
  cat <<'USAGE'
MetaRecoverX (offline, Bash wrapper)

Usage:
  scripts/metarecoverx.sh build
  scripts/metarecoverx.sh acquire --image <path>
  scripts/metarecoverx.sh scan xfs    --image <path> [--format csv|json]
  scripts/metarecoverx.sh scan btrfs  --image <path> [--format csv|json]
  scripts/metarecoverx.sh reconstruct [--out artifacts/metadata.json] [--csv-out artifacts/metadata.csv] [inputs.csv ...]
  scripts/metarecoverx.sh recover --image <path> [--plan artifacts/metadata.csv] [--out recovered]
  scripts/metarecoverx.sh pipeline --image <path>
  scripts/metarecoverx.sh detect-usb
  scripts/metarecoverx.sh pipeline-usb --device /dev/sdX
  scripts/metarecoverx.sh demo [--image dummy.img]

Commands:
  build        Build all C++ binaries (uses CMake if available, else g++)
  acquire      Write acquire.json with size + sha256 for the image
  scan         Run xfs_scan or btrfs_scan producing CSV or JSON
  reconstruct  Merge scan CSVs into metadata.json + metadata.csv
  recover      Carve extents from image into recovered/*.bin
  pipeline     Build + acquire + scan (xfs,btrfs) + reconstruct + recover (Ubuntu TTY, offline)
  detect-usb   List connected USB storage devices
  pipeline-usb Forensically image a USB/removable device then run the full pipeline
  demo         Full pipeline on a small image (creates one if missing)
USAGE
}

resolve_path() {
  local p="$1"
  if command -v readlink >/dev/null 2>&1; then
    readlink -f "$p" 2>/dev/null || printf '%s' "$p"
  elif command -v realpath >/dev/null 2>&1; then
    realpath "$p" 2>/dev/null || printf '%s' "$p"
  else
    printf '%s' "$p"
  fi
}

cmd_build() {
  ( cd "$ROOT_DIR" && ./scripts/build.sh )
}

cmd_acquire() {
  local image=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2;;
      --help|-h) print_usage; return 0;;
      *) echo "unknown arg: $1" >&2; return 2;;
    esac
  done
  [[ -n "$image" ]] || { echo "--image is required" >&2; return 2; }
  [[ -f "$image" ]] || { echo "image not found: $image" >&2; return 1; }
  ensure_dirs
  local size hash abs
  size=$(stat -c%s "$image")
  if command -v sha256sum >/dev/null 2>&1; then
    hash=$(sha256sum "$image" | awk '{print $1}')
  else
    echo "sha256sum not found" >&2; return 1
  fi
  abs=$(resolve_path "$image")
  cat > "$ART_DIR/acquire.json" <<JSON
{
  "schema": "metarecoverx.acquire.v1",
  "image": "$abs",
  "generated_at": "now",
  "size": $size,
  "hash": { "algo": "sha256", "value": "$hash" },
  "notes": ""
}
JSON
  echo "wrote $ART_DIR/acquire.json"
}

cmd_scan() {
  local fs="" image="" format="csv"
  [[ $# -ge 1 ]] || { print_usage; return 2; }
  fs="$1"; shift || true
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2;;
      --format) format="$2"; shift 2;;
      --help|-h) print_usage; return 0;;
      *) echo "unknown arg: $1" >&2; return 2;;
    esac
  done
  [[ -n "$image" ]] || { echo "--image is required" >&2; return 2; }
  [[ -f "$image" ]] || { echo "image not found: $image" >&2; return 1; }
  ensure_dirs
  cmd_build >/dev/null
  case "$fs" in
    xfs)
      if [[ "$format" == "csv" ]]; then
        "$BUILD_DIR/xfs_scan" --image "$image" --format csv > "$ART_DIR/xfs_scan.csv"
        echo "wrote $ART_DIR/xfs_scan.csv"
      else
        "$BUILD_DIR/xfs_scan" --image "$image" > "$ART_DIR/xfs_scan.json"
        echo "wrote $ART_DIR/xfs_scan.json"
      fi
      ;;
    btrfs)
      if [[ "$format" == "csv" ]]; then
        "$BUILD_DIR/btrfs_scan" --image "$image" --format csv > "$ART_DIR/btrfs_scan.csv"
        echo "wrote $ART_DIR/btrfs_scan.csv"
      else
        "$BUILD_DIR/btrfs_scan" --image "$image" > "$ART_DIR/btrfs_scan.json"
        echo "wrote $ART_DIR/btrfs_scan.json"
      fi
      ;;
    *) echo "unknown fs: $fs (use xfs|btrfs)" >&2; return 2;;
  esac
}

cmd_reconstruct() {
  local out_json="$ART_DIR/metadata.json" out_csv="$ART_DIR/metadata.csv"
  local inputs=()
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --out) out_json="$2"; shift 2;;
      --csv-out) out_csv="$2"; shift 2;;
      --help|-h) print_usage; return 0;;
      *) inputs+=("$1"); shift;;
    esac
  done
  ensure_dirs
  if [[ ${#inputs[@]} -eq 0 ]]; then
    [[ -f "$ART_DIR/xfs_scan.csv" ]] && inputs+=("$ART_DIR/xfs_scan.csv")
    [[ -f "$ART_DIR/btrfs_scan.csv" ]] && inputs+=("$ART_DIR/btrfs_scan.csv")
  fi
  [[ ${#inputs[@]} -gt 0 ]] || { echo "no input CSVs provided" >&2; return 2; }
  cmd_build >/dev/null
  "$BUILD_DIR/reconstruct_csv" --out "$out_json" --csv-out "$out_csv" "${inputs[@]}"
  echo "wrote $out_json"
  echo "wrote $out_csv"
}

cmd_recover() {
  local image="" plan="$ART_DIR/metadata.csv" out="$REC_DIR"
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2;;
      --plan) plan="$2"; shift 2;;
      --out) out="$2"; shift 2;;
      --help|-h) print_usage; return 0;;
      *) echo "unknown arg: $1" >&2; return 2;;
    esac
  done
  [[ -n "$image" ]] || { echo "--image is required" >&2; return 2; }
  [[ -f "$image" ]] || { echo "image not found: $image" >&2; return 1; }
  [[ -f "$plan" ]] || { echo "plan not found: $plan" >&2; return 1; }
  ensure_dirs
  cmd_build >/dev/null
  "$BUILD_DIR/recover_csv" --image "$image" --plan "$plan" --out "$out"
}

cmd_pipeline() {
  local image=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2;;
      --help|-h) print_usage; return 0;;
      *) echo "unknown arg: $1" >&2; return 2;;
    esac
  done
  [[ -n "$image" ]] || { echo "--image is required" >&2; return 2; }
  [[ -f "$image" ]] || { echo "image not found: $image" >&2; return 1; }

  ensure_dirs
  echo "[1/5] Building binaries..."; cmd_build
  echo "[2/5] Acquiring image info..."; cmd_acquire --image "$image"
  echo "[3/5] Scanning filesystems (XFS, Btrfs) to CSV..."
  cmd_scan xfs --image "$image" --format csv
  cmd_scan btrfs --image "$image" --format csv
  echo "[4/5] Reconstructing metadata..."
  cmd_reconstruct
  echo "[5/5] Recovering file data..."
  cmd_recover --image "$image"
  cp "$ART_DIR/metadata.csv" "$ROOT_DIR/report.csv" 2>/dev/null || true
  echo "Pipeline complete. See artifacts/, recovered/, and report.csv"
}

# ---------------------------------------------------------------------------
# detect-usb: print USB storage devices (wraps detect_usb.sh)
# ---------------------------------------------------------------------------
cmd_detect_usb() {
  local script="$ROOT_DIR/scripts/detect_usb.sh"
  if [[ -f "$script" ]]; then
    bash "$script"
  else
    # inline fallback if the helper script is missing
    if ! command -v lsblk >/dev/null 2>&1; then
      echo "error: lsblk not found (install util-linux)" >&2; return 1
    fi
    echo "NAME  TRAN  SIZE  MOUNTPOINT"
    echo "----  ----  ----  ----------"
    lsblk -o NAME,TRAN,SIZE,MOUNTPOINT -n 2>/dev/null \
      | awk '$2 == "usb" { printf "%-6s %-5s %-6s %s\n", $1, $2, $3, ($4==""?"":$4) }'
  fi
}

# ---------------------------------------------------------------------------
# detect_fs: use blkid to auto-detect filesystem on an image file
#   prints "xfs", "btrfs", or "unknown"
# ---------------------------------------------------------------------------
detect_fs() {
  local image="$1"
  if command -v blkid >/dev/null 2>&1; then
    local fstype
    fstype=$(blkid -o value -s TYPE "$image" 2>/dev/null || true)
    case "$fstype" in
      xfs)   echo "xfs";   return;;
      btrfs) echo "btrfs"; return;;
    esac
  fi
  echo "unknown"
}

# ---------------------------------------------------------------------------
# pipeline-usb: forensic image from a USB/removable device → full pipeline
# ---------------------------------------------------------------------------
cmd_pipeline_usb() {
  local device=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --device) device="$2"; shift 2;;
      --help|-h) print_usage; return 0;;
      *) echo "unknown arg: $1" >&2; return 2;;
    esac
  done

  [[ -n "$device" ]] || { echo "--device is required (e.g. --device /dev/sdb)" >&2; return 2; }
  [[ -e "$device" ]]  || { echo "device not found: $device" >&2; return 1; }

  ensure_dirs

  local img_name
  img_name="usb_$(basename "$device")_$(date +%Y%m%d_%H%M%S).img"
  local img_path="$ART_DIR/$img_name"

  # Step 1 — list USB devices for reference
  echo "[pipeline-usb] Connected USB devices:"
  cmd_detect_usb || true
  echo ""

  # Step 2 — build C++ tools
  echo "[1/6] Building binaries..."
  cmd_build

  # Step 3 — acquire forensic image with dd
  echo "[2/6] Acquiring forensic image from $device ..."
  echo "      destination: $img_path"
  if ! command -v dd >/dev/null 2>&1; then
    echo "error: dd not found" >&2; return 1
  fi
  dd if="$device" of="$img_path" bs=4M status=progress conv=noerror,sync
  echo "dd complete."

  # Step 4 — SHA256 hash
  echo "[3/6] Generating SHA256 hash..."
  if ! command -v sha256sum >/dev/null 2>&1; then
    echo "error: sha256sum not found" >&2; return 1
  fi
  sha256sum "$img_path" > "$ART_DIR/hash.txt"
  echo "      written: $ART_DIR/hash.txt"
  cat "$ART_DIR/hash.txt"

  # Step 5 — acquire (timestamp + hash JSON)
  echo "[4/6] Recording acquisition metadata..."
  cmd_acquire --image "$img_path"

  # Step 6 — detect filesystem type and scan
  echo "[5/6] Detecting filesystem and scanning..."
  local fstype
  fstype=$(detect_fs "$img_path")
  echo "      detected filesystem: $fstype"

  case "$fstype" in
    xfs)
      cmd_scan xfs   --image "$img_path" --format csv
      ;;
    btrfs)
      cmd_scan btrfs --image "$img_path" --format csv
      ;;
    *)
      echo "      filesystem unknown or undetectable; scanning as both XFS and Btrfs..."
      cmd_scan xfs   --image "$img_path" --format csv
      cmd_scan btrfs --image "$img_path" --format csv
      ;;
  esac

  # Step 7 — reconstruct + recover
  echo "[6/6] Reconstructing metadata and recovering files..."
  cmd_reconstruct
  cmd_recover --image "$img_path"

  cp "$ART_DIR/metadata.csv" "$ROOT_DIR/report.csv" 2>/dev/null || true

  echo ""
  echo "pipeline-usb complete."
  echo "  Forensic image : $img_path"
  echo "  SHA256 hash    : $ART_DIR/hash.txt"
  echo "  Artifacts      : $ART_DIR/"
  echo "  Recovered files: $REC_DIR/"
  echo "  Report         : $ROOT_DIR/report.csv"
}

cmd_demo() {
  local image="dummy.img"
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --image) image="$2"; shift 2;;
      --help|-h) print_usage; return 0;;
      *) echo "unknown arg: $1" >&2; return 2;;
    esac
  done
  ensure_dirs
  cmd_build
  if [[ ! -f "$image" ]]; then
    dd if=/dev/zero of="$image" bs=1M count=1 status=none
  fi
  "$0" acquire --image "$image"
  "$0" scan xfs --image "$image" --format csv
  "$0" scan btrfs --image "$image" --format csv
  "$0" reconstruct
  "$0" recover --image "$image"
  cp "$ART_DIR/metadata.csv" "$ROOT_DIR/report.csv"
  echo "Demo complete. See artifacts/, recovered/, and report.csv"
}

main() {
  local cmd="${1:-}"; shift || true
  case "$cmd" in
    build) cmd_build "$@";;
    acquire) cmd_acquire "$@";;
    scan) cmd_scan "$@";;
    reconstruct) cmd_reconstruct "$@";;
    recover) cmd_recover "$@";;
    pipeline) cmd_pipeline "$@";;
    detect-usb) cmd_detect_usb "$@";;
    pipeline-usb) cmd_pipeline_usb "$@";;
    demo) cmd_demo "$@";;
    --help|-h|help|"") print_usage;;
    *) echo "Unknown command: $cmd" >&2; print_usage; exit 2;;
  esac
}

main "$@"
