# MetaRecoverX

**Recovery of Deleted Data and Associated Metadata from XFS and Btrfs Filesystems**

MetaRecoverX is a Linux digital forensics and data recovery tool designed to recover deleted files and reconstruct their associated metadata from XFS and Btrfs filesystems. It scans low-level filesystem structures — including inodes, allocation groups, metadata logs, and filesystem trees — to accurately restore lost data along with critical metadata such as filenames, directory paths, file sizes, timestamps, UID/GID, permissions, and file extents.

The tool runs **fully offline** in Ubuntu / Linux TTY environments, making it suitable for digital forensics, incident response, and system administration workflows.

---

## Project Information

- **Institute:** Swami Keshvanand Institute of Technology, Management & Gramothan (SKIT), Jaipur
- **Department:** Information Technology
- **Academic Session:** 2025–2026
- **Team Name:** Tech-Aizen

### Team Members

| Member | Roll No. | Role |
|---|---|---|
| Mayank Tak | 22ESKIT093 | Lead Developer — XFS module, recovery engine, reporting |
| Lucky Panchal | 22ESKIT088 | Btrfs module, testing, and integration |
| Milan Kumar | 22ESKIT094 | Metadata reconstruction pipeline, QA, and tooling |

### Faculty Mentors

- Mr. Jagendra Singh Chaudhary — Assistant Professor (II)
- Ms. Richa Rawal — Assistant Professor (I), Lab Coordinator

---

## Key Features

- Recovery of deleted files from **XFS** filesystems
- Recovery of deleted files from **Btrfs** filesystems
- **Metadata reconstruction** — filenames, directory paths, timestamps, UID/GID, permissions
- **Extent-based file recovery** — reassemble files from disk extents and verify integrity
- **Disk image analysis** — full pipeline on raw disk images (`.img`)
- **USB / pendrive forensic acquisition** — image USB drives with `dd`, hash with SHA-256
- **Filesystem auto-detection** — uses `blkid` to determine XFS vs Btrfs
- **SHA-256 hashing** for evidence integrity verification
- Support for **large disk images** (>100 GB) using `uint64_t` offsets
- **Fully offline execution** — no internet, no external libraries
- **Reporting pipeline** — CSV and JSON metadata exports

---

## Supported Filesystems and Structures

### XFS

The XFS scanner detects and parses:

- XFS superblock
- Allocation groups (AGs)
- Inode structures (including deleted inodes)
- Directory entries (`xfs_dir2_data_entry`)
- B+Trees and metadata journals/logs
- Extent records (freed extents for carving)

Recovered metadata: inode, filename, path, size, uid, gid, mode, extents.

### Btrfs

The Btrfs scanner parses:

- Superblock
- Chunk tree, Extent tree, Inode tree
- FS tree (ROOT_TREE, FS_TREE)
- INODE_ITEM, INODE_REF, DIR_ITEM
- Snapshot discovery and deleted item traversal

Recovered metadata: inode, filename, path, size, uid, gid, mode.

---

## Technology Stack

| Technology | Version | Purpose |
|---|---|---|
| C++17 | GCC 9+ | Core binaries — filesystem parsing, low-level recovery |
| Bash | System | Orchestration scripts (build, pipeline, demo, USB) |
| POSIX APIs | — | Low-level I/O and system calls |
| Linux coreutils | — | `dd`, `stat`, `sha256sum`, `readlink`/`realpath`, `blkid`, `lsblk` |
| CMake (optional) | 3.10+ | Build system (falls back to direct `g++` if missing) |
| Sample disk images | — | Validation and QA |

**No external libraries, Python, Rust, or internet downloads are required.**

---

## Architecture and Modules

```
┌─────────────────────────────────────────────────────────┐
│                    MetaRecoverX Pipeline                │
├──────────────┬──────────────┬───────────────┬───────────┤
│  1. Acquire  │  2. Scan     │ 3. Reconstruct│ 4. Recover│
│  Disk Image  │  Filesystem  │   Metadata    │   Files   │
│  (dd + hash) │  Structures  │   (CSV/JSON)  │ (extents) │
└──────────────┴──────────────┴───────────────┴───────────┘
```

| Module | Binary | Description |
|---|---|---|
| Disk Image Acquisition | `dd` / `sha256sum` | Acquire disk images from devices, verify integrity with hashing |
| XFS Scanner | `build/xfs_scan` | Parse XFS superblock, AGs, enumerate inodes & freed extents |
| Btrfs Scanner | `build/btrfs_scan` | Traverse chunk/extent/inode trees, locate deleted items & snapshots |
| Metadata Reconstruction | `build/reconstruct_csv` | Combine scan CSVs, rebuild timestamps, UID/GID, permissions, paths |
| Recovery Engine | `build/recover_csv` | Reassemble files from extents, verify integrity, write to `recovered/` |

---

## Repository Structure

```text
MetaRecoverX/
├── core/                           # C++ source code
│   ├── CMakeLists.txt              # CMake build configuration
│   └── src/
│       ├── xfs_scan.cpp            # XFS filesystem scanner
│       ├── btrfs_scan.cpp          # Btrfs filesystem scanner
│       ├── reconstruct_csv.cpp     # Metadata reconstruction engine
│       └── recover_csv.cpp         # File recovery engine
│
├── scripts/                        # Bash orchestration scripts
│   ├── setup.sh                    # Build script (CMake → g++ fallback)
│   ├── build.sh                    # Direct build wrapper
│   ├── metarecoverx.sh            # Main pipeline script
│   ├── detect_usb.sh              # USB device detection
│   ├── run_demo.sh                # Quick demo runner
│   └── menu.sh                    # Interactive TTY menu
│
├── build/                          # Compiled binaries (generated)
│   ├── xfs_scan
│   ├── btrfs_scan
│   ├── reconstruct_csv
│   └── recover_csv
│
├── artifacts/                      # Pipeline outputs (generated)
│   ├── acquire.json               # Acquisition metadata (path, size, hash)
│   ├── hash.txt                   # SHA-256 hash of disk image
│   ├── xfs_scan.csv              # XFS scan results
│   ├── btrfs_scan.csv            # Btrfs scan results
│   ├── metadata.csv              # Unified metadata
│   └── metadata.json             # Unified metadata (JSON)
│
├── recovered/                      # Recovered files (generated)
│   ├── file.txt                   # (filename preserved when possible)
│   ├── photo.jpg
│   └── file_0001.bin              # (fallback name if unknown)
│
├── docs/                           # Additional technical documentation
├── tests/                          # Test images, fixtures, automated tests
├── dummy.img                       # 1 MiB test image for demo
├── report.csv                      # Copy of metadata.csv for quick viewing
└── Makefile                        # Top-level Makefile
```

---

## Getting Started

### Prerequisites (Ubuntu, offline)

- Ubuntu (native or WSL) with a C++17 compiler (`g++`)
- Optional: CMake 3.10+ (for nicer builds; script falls back to `g++` if missing)
- Optional: `libxfs` development headers (future XFS integration)

### Clone the Repository

```bash
git clone https://github.com/YOUR_USERNAME/MetaRecoverX.git
cd MetaRecoverX
```

### Build the Project

```bash
chmod +x scripts/*.sh
./scripts/setup.sh
```

This compiles C++17 binaries into `build/` (via CMake when available, falling back to direct `g++`):

- `build/xfs_scan`
- `build/btrfs_scan`
- `build/reconstruct_csv`
- `build/recover_csv`

You can also build explicitly via:

```bash
./scripts/metarecoverx.sh build
```

---

## How to Run

MetaRecoverX supports three modes of operation:

### 1. Quick Demo (Dummy Image — Sanity Check)

Verify that the toolchain, wrapper script, and pipeline work end-to-end without touching a real device:

```bash
./scripts/metarecoverx.sh demo
# or equivalently:
./scripts/run_demo.sh
```

**What the demo does:**

1. Creates a small zero-filled `dummy.img` if it does not already exist
2. Runs acquisition on `dummy.img` (writes `artifacts/acquire.json`)
3. Runs XFS and Btrfs scanners (`artifacts/xfs_scan.csv`, `artifacts/btrfs_scan.csv`)
4. Reconstructs metadata (`artifacts/metadata.json`, `artifacts/metadata.csv`)
5. Runs recovery engine (recovered files contain only zeros)
6. Copies `artifacts/metadata.csv` to `report.csv`

Inspect the result:

```bash
column -t -s, report.csv | head
```

### 2. Full Pipeline on an Existing Disk Image

If you already have a raw disk image:

```bash
./scripts/metarecoverx.sh pipeline --image /path/to/disk.img
```

This performs, in order:

1. Build tools (if needed)
2. Acquire metadata for the image (`artifacts/acquire.json`)
3. Run XFS and Btrfs scans → CSVs in `artifacts/`
4. Reconstruct unified metadata → `artifacts/metadata.csv` + `artifacts/metadata.json`
5. Recover file data → `recovered/`
6. Copy `artifacts/metadata.csv` → `report.csv`

### 3. Forensic Pipeline on a USB Pendrive

This workflow acquires a forensic image from a USB device, verifies its hash, scans supported filesystems, reconstructs metadata, and recovers files.

#### Step 1: Detect USB Devices

```bash
./scripts/detect_usb.sh
# or through the wrapper:
sudo ./scripts/metarecoverx.sh detect-usb
```

Example output:

```text
NAME  TRAN  SIZE
sdb   usb   32G
sdc   usb   64G
```

**⚠️ Double-check that you have identified the correct source device; choosing the wrong one may destroy data.**

#### Step 2: Run USB Forensic Recovery

```bash
sudo ./scripts/metarecoverx.sh pipeline-usb --device /dev/sdb
```

**USB recovery workflow (automatic):**

1. Build C++ tools (if not already built)
2. Acquire forensic image via `dd`:
   - `dd if=/dev/sdb of=artifacts/usb_sdb_YYYYMMDD_HHMMSS.img bs=4M status=progress conv=sync`
3. Compute SHA-256 hash → `artifacts/hash.txt`
4. Write acquisition metadata → `artifacts/acquire.json`
5. Detect filesystem type via `blkid`:
   - `xfs` → run `xfs_scan`
   - `btrfs` → run `btrfs_scan`
   - Unknown → run both scanners
6. Scan filesystem metadata → `artifacts/xfs_scan.csv`, `artifacts/btrfs_scan.csv`
7. Reconstruct combined metadata → `artifacts/metadata.csv`, `artifacts/metadata.json`
8. Recover files → `recovered/`
9. Copy `artifacts/metadata.csv` → `report.csv`

### 4. Interactive TTY Menu

For bare-metal Ubuntu TTYs, use the key-driven menu:

```bash
./scripts/menu.sh
```

Menu options:

- `1` — Detect USB storage devices
- `2` — Run the demo pipeline on `dummy.img`
- `q` — Quit

---

## Direct Scanner and Recovery Usage (Advanced)

Advanced users can call the binaries directly for low-level testing or custom workflows.

### XFS Scanner

```bash
build/xfs_scan --image /path/to/disk.img --format csv > artifacts/xfs_scan.csv
```

### Btrfs Scanner

```bash
build/btrfs_scan --image /path/to/disk.img --format csv > artifacts/btrfs_scan.csv
```

### Reconstruct Metadata from CSVs

```bash
build/reconstruct_csv \
  --out artifacts/metadata.json \
  --csv-out artifacts/metadata.csv \
  artifacts/xfs_scan.csv artifacts/btrfs_scan.csv
```

### Recover Files from Reconstructed Metadata

```bash
build/recover_csv \
  --image /path/to/disk.img \
  --plan artifacts/metadata.csv \
  --out recovered
```

---

## CSV Metadata Format

### Scanner Output Format

```text
fs,inode,path,size,uid,gid,mode,extents
```

Example:

```text
xfs,10231,/home/user/file.txt,20480,1000,1000,644,8192:20480
btrfs,256,/documents/report.pdf,51200,1000,1000,644,16384:51200
```

### Unified Metadata (after `reconstruct_csv`)

```text
global_id,fs,path,size,uid,gid,mode,extents,recovery_plan
xfs-10231,xfs,/home/user/file.txt,20480,1000,1000,644,8192:20480,carve
btrfs-256,btrfs,/documents/report.pdf,51200,1000,1000,644,16384:51200,carve
```

---

## Output Structure

After a successful pipeline run (demo, disk image, or USB):

```text
artifacts/
  acquire.json        — acquisition metadata (image path, size, hash)
  hash.txt            — SHA-256 of the acquired image (USB pipeline)
  xfs_scan.csv        — XFS filesystem metadata
  btrfs_scan.csv      — Btrfs filesystem metadata
  metadata.csv        — unified metadata (all filesystems)
  metadata.json       — same metadata in JSON format

recovered/
  file.txt            — recovered file (filename preserved)
  photo.jpg           — recovered file (filename preserved)
  file_0001.bin       — recovered file (filename unknown)

report.csv            — copy of metadata.csv for quick inspection
```

---

## Scanner Implementation Details

### Scanner Requirements

All scanners must:

- Read raw disk images directly (no mount required)
- Support large images (>100 GB) using `uint64_t` offsets
- Scan blocks sequentially
- Use only C++17 standard library + POSIX APIs

Example read pattern:

```cpp
uint64_t offset = 0;
const uint64_t block_size = 4096;

while (offset < image_size) {
    image.seekg(offset);
    char buffer[4096];
    image.read(buffer, 4096);
    // scan metadata structures
    offset += block_size;
}
```

### XFS Scanner Internals

`xfs_scan.cpp` parses:

- XFS inode structures
- Directory entries:

```cpp
struct xfs_dir2_data_entry {
    uint64_t inode;
    uint8_t  namelen;
    char     name[];
};
```

Extracts: inode number, filename, path, size, uid, gid, mode, extents.

### Btrfs Scanner Internals

`btrfs_scan.cpp` traverses:

- ROOT_TREE, FS_TREE
- INODE_ITEM, INODE_REF, DIR_ITEM

Extracts: inode number, filename, path, size, uid, gid, mode.

---

## Windows Users (WSL Recommended)

The scripts are POSIX shell-based and rely on coreutils. The simplest way to run on Windows is via WSL (Ubuntu):

1. Install WSL with Ubuntu from the Microsoft Store (one-time).
2. Open an Ubuntu terminal and navigate to the project folder:

```bash
cd /mnt/c/Users/YourName/Desktop/MetaRecoverX
```

3. Run the same offline steps:

```bash
chmod +x scripts/*.sh
./scripts/setup.sh
./scripts/metarecoverx.sh pipeline --image /mnt/c/Path/To/disk.img
```

Native Windows (without WSL) is not officially supported by the shell scripts. The C++ tools can be built with CMake + MSVC, but you'll need to run the binaries directly.

---

## Safety and Forensic Best Practices

- **Never** run `pipeline-usb`, `dd`, or any destructive command against a device unless you are absolutely certain it is the correct source device.
- Prefer working on **read-only clones or images** of evidence media rather than original devices.
- Keep MetaRecoverX machines **offline** when used in forensic workflows (no network connectivity required).
- **Verify hashes** (`artifacts/hash.txt`) before and after copying images between systems.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| `realpath`/`readlink` not found | Script tries both and falls back gracefully |
| CMake not found | Build falls back to direct `g++` compilation automatically |
| Permission denied | Ensure scripts are executable (`chmod +x scripts/*.sh`) and directory is writable |
| Binary not found in `build/` | Some CMake generators create `build/Release/`; the wrapper accounts for this |
| USB device not listed | Ensure device is connected; run `lsblk` manually to verify |

---

## Project Milestones (2025)

| Date Range | Milestone |
|---|---|
| Sep 10–20 | Btrfs tree traversal prototype; metadata pipeline outline |
| Sep 20–25 | Requirements finalized; select sample datasets |
| Sep 25–30 | Disk image acquisition process finalized |
| Oct 1–2 | XFS superblock and AG parser drafted and validated |
| Oct 5–10 | Deleted inode detection and carving (XFS) |
| Oct 10–18 | Integrate metadata pipeline; timestamps/UID/GID recovery |
| Oct 15–20 | Btrfs deleted file and snapshot detection |
| Oct 20–25 | Integrate Btrfs with recovery engine; QA on multiple images |
| Oct 31 | Integrate XFS modules; optimize carving and scanning performance |
| Nov 7–20 | Reporting (HTML/CSV), performance tuning, packaging, and final QA |

---

## Future Improvements

- Snapshot recovery (Btrfs)
- Advanced carving heuristics
- HTML forensic reports
- Performance optimizations
- Support for additional filesystems

---

## License

License will be added after academic evaluation. Planned: MIT License or Apache 2.0.

---

## Acknowledgements

Special thanks to the Information Technology Department at SKIT Jaipur for academic guidance, infrastructure support, and project mentorship.
