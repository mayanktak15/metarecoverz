# MetaRecoverX – USB, Disk Image, and Demo Usage

This document explains how to run MetaRecoverX in an offline Ubuntu / TTY environment against:

- A real USB pendrive (forensic image + full pipeline)
- An existing raw disk image
- A dummy image file (quick demo / sanity check)

All commands are intended to be run from the project root.

---

## 1. Build the C++ tools

Recommended first step (offline‑friendly):

```bash
chmod +x scripts/*.sh
./scripts/setup.sh
```

This compiles the C++17 binaries into `build/` (via CMake when available, falling back to `g++`):

- `build/xfs_scan`
- `build/btrfs_scan`
- `build/reconstruct_csv`
- `build/recover_csv`

You can also build explicitly via the wrapper:

```bash
./scripts/metarecoverx.sh build
```

---

## 2. Quick sanity‑check (dummy image / demo)

Use the demo pipeline to verify that the toolchain, wrapper script, and offline TTY workflow all work end‑to‑end without touching a real device.

### 2.1. Run the demo

```bash
./scripts/metarecoverx.sh demo
```

or equivalently (wrapper script around the same flow):

```bash
./scripts/run_demo.sh
```

What the demo does:

1. Creates a small zero‑filled `dummy.img` if it does not already exist.
2. Runs acquisition on `dummy.img` (writes `artifacts/acquire.json`).
3. Runs XFS and Btrfs scanners on the dummy image (`artifacts/xfs_scan.csv`, `artifacts/btrfs_scan.csv`).
4. Runs metadata reconstruction to JSON/CSV (`artifacts/metadata.json`, `artifacts/metadata.csv`).
5. Runs recovery to exercise the carving logic (recovered files contain only zeros).
6. Copies `artifacts/metadata.csv` to `report.csv` at the project root.

Use this mode to confirm that:

- Binaries build and execute correctly.
- The Bash wrapper works in your TTY environment.
- The expected artifact files and directories are created.

To quickly inspect the resulting CSV in a TTY:

```bash
column -t -s, report.csv | head
```

---

## 3. Full pipeline on an existing disk image

If you already have a raw disk image, you can run the complete pipeline without imaging from USB.

### 3.1. Prepare the image

Place or reference your image (for example `disk.img`) and note its path.

### 3.2. Run the pipeline

```bash
./scripts/metarecoverx.sh pipeline --image /path/to/disk.img
```

This performs, in order:

1. Build (if needed).
2. Acquire metadata for the image (writes `artifacts/acquire.json`).
3. Run XFS and Btrfs scans, producing CSVs in `artifacts/`.
4. Reconstruct unified metadata to `artifacts/metadata.csv` and `artifacts/metadata.json`.
5. Recover file data into `recovered/` based on the reconstructed metadata.
6. Copy `artifacts/metadata.csv` to `report.csv` at the project root.

The output layout matches the USB workflow described below.

---

## 4. Forensic pipeline on a USB pendrive

This workflow acquires a forensic image from a USB device, verifies its hash, scans supported filesystems, reconstructs metadata, and recovers files.

### 4.1. Identify the USB device

You **must** be root (or have equivalent privileges) to access block devices safely.

List attached USB storage devices via either:

```bash
./scripts/detect_usb.sh
```

or, through the wrapper:

```bash
sudo ./scripts/metarecoverx.sh detect-usb
```

This prints a list of USB block devices (for example `/dev/sdb`, `/dev/sdc`).
**Double‑check** that you have identified the correct source device; choosing the wrong one may destroy data.

### 4.2. Run the end‑to‑end USB pipeline

Once you know the correct device (for example `/dev/sdb`), run:

```bash
sudo ./scripts/metarecoverx.sh pipeline-usb --device /dev/sdb
```

Internally this performs:

1. Builds the C++ tools (if not already built).
2. Uses `dd` to acquire a forensic image into `artifacts/`, e.g. `artifacts/usb_sdb_YYYYMMDD_HHMMSS.img`.
3. Computes a SHA‑256 hash of the image and writes it to `artifacts/hash.txt`.
4. Writes acquisition metadata (path, size, hash) to `artifacts/acquire.json`.
5. Scans the image as XFS and/or Btrfs, producing:
   - `artifacts/xfs_scan.csv`
   - `artifacts/btrfs_scan.csv`
6. Reconstructs combined metadata into:
   - `artifacts/metadata.csv`
   - `artifacts/metadata.json`
7. Recovers file data into:
   - `recovered/`
8. Copies `artifacts/metadata.csv` to `report.csv` at the project root.

You can use the same `column` command as in the demo to inspect the summary CSV.

---

## 5. Key outputs and directory layout

After any successful pipeline run (demo, disk image, or USB), you should see:

- `artifacts/acquire.json`  – acquisition metadata (image path, size, hash)
- `artifacts/hash.txt`      – SHA‑256 of the acquired image (USB pipeline)
- `artifacts/xfs_scan.csv`  – XFS filesystem metadata (if XFS is detected)
- `artifacts/btrfs_scan.csv`– Btrfs filesystem metadata (if Btrfs is detected)
- `artifacts/metadata.csv`  – unified metadata (global_id, fs, path, size, uid, gid, mode, extents, ...)
- `artifacts/metadata.json` – same metadata in JSON format
- `recovered/`              – carved files based on recorded extents
- `report.csv`              – copy of `artifacts/metadata.csv` for quick inspection

These files can be safely copied off the offline system for further analysis.

---

## 6. Direct scanner and recovery usage (advanced)

Advanced users can call the binaries directly for low‑level testing or custom workflows.

### 6.1. XFS scanner

```bash
build/xfs_scan --image /path/to/disk.img --format csv > artifacts/xfs_scan.csv
```

### 6.2. Btrfs scanner

```bash
build/btrfs_scan --image /path/to/disk.img --format csv > artifacts/btrfs_scan.csv
```

### 6.3. Reconstruct metadata from CSVs

```bash
build/reconstruct_csv \
  --out artifacts/metadata.json \
  --csv-out artifacts/metadata.csv \
  artifacts/xfs_scan.csv artifacts/btrfs_scan.csv
```

### 6.4. Recover files from reconstructed metadata

```bash
build/recover_csv \
  --image /path/to/disk.img \
  --plan artifacts/metadata.csv \
  --out recovered
```

---

## 7. Interactive TTY menu (simple key‑based usage)

On bare‑metal Ubuntu TTYs it can be convenient to avoid long commands and instead use a small key‑driven menu.

```bash
chmod +x scripts/*.sh
./scripts/menu.sh
```

Menu options:

- `1` – Detect USB storage devices (wraps `scripts/metarecoverx.sh detect-usb`)
- `2` – Run the demo pipeline on a small `dummy.img` (wraps `scripts/metarecoverx.sh demo`)
- `q` – Quit back to the shell

You can still run all advanced and USB workflows manually as shown above.

---

## 8. Safety and forensic practice

- **Never** run `pipeline-usb`, `dd`, or any destructive command against a device unless you are absolutely certain it is the correct source device.
- Prefer working on read‑only clones or images of evidence media rather than original devices.
- Keep MetaRecoverX machines offline when used in forensic workflows (no network connectivity is required for any of the flows described here).
- Verify hashes (`artifacts/hash.txt`) before and after copying images between systems.

This README is intended to serve as a compact GitHub‑friendly quick start for MetaRecoverX in offline environments.
