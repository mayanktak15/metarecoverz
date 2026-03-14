# MetaRecoverX

Recovery of Deleted Data and Associated Metadata from XFS and Btrfs Filesystems

## Overview

MetaRecoverX is a Linux-focused data recovery and analysis tool designed to retrieve deleted files and reconstruct their associated metadata from XFS and Btrfs filesystems. It scans low-level filesystem structures (e.g., superblocks, allocation groups, inodes, metadata logs/trees) to accurately recover file content along with timestamps, ownership, and original directory hierarchy. The tool supports both raw disk images and live USB / removable drives, and is intended to assist digital forensics and system administration workflows.

Institute: Swami Keshvanand Institute of Technology, Management & Gramothan (SKIT), Jaipur  
Department: Information Technology  
Academic Session: 2025–2026

## Key Features

- Low-level sequential block scanning of XFS and Btrfs internals for robust recovery
- **Real XFS inode parsing** — detects inode magic (0x494E), extracts mode/uid/gid/size, decodes 128-bit packed extent records
- **Real Btrfs leaf-node parsing** — reads `INODE_ITEM` (type 1), `INODE_REF` (type 12), and `DIR_ITEM` (type 84) keys for filename and metadata recovery
- **Directory entry reconstruction** — correlates inode numbers with filenames from XFS dir-data blocks and Btrfs DIR_ITEM / INODE_REF records
- **Filename-preserving recovery** — `recover_csv` writes `recovered/<original_filename>` when known; falls back to `file_NNNN.bin`
- CSV output: `fs,inode,path,size,uid,gid,mode,extents` per recovered entry
- USB / removable-drive support: detect, forensically image, hash, and recover in one command
- Automatic filesystem detection via `blkid` (XFS, Btrfs, or both)
- 64-bit file-offset support (`fseeko` + `_FILE_OFFSET_BITS=64`) for images and devices larger than 2 GB
- `uint64_t` offsets throughout the scan loop — safe for images > 100 GB
- Fully offline — no internet connection required at any stage
- Validation against sample disk images

## Filesystems and Structures Scanned

- XFS
  - Superblock, Allocation Groups (AGs)
  - Inodes, B+Trees, metadata journals/logs
  - Deleted inode/extent enumeration and carving

- Btrfs
  - Chunk, Extent, and Inode trees
  - Snapshot discovery and deleted item traversal
  - Extent mapping for recovery

## Architecture and Modules

1. Disk Image Acquisition
	- Acquire disk images and verify integrity (SHA-256 hashing)

2. USB / Removable Device Scanner (`scripts/detect_usb.sh`)
	- Detect connected USB drives via `lsblk`
	- Forensically image the selected device with `dd`
	- Auto-detect filesystem type with `blkid`

3. XFS Scanner (`xfs_scan`)
	- Sequential 4096-byte block scan with `uint64_t` offsets — safe for >100 GB images
	- Detects XFS superblock (magic `XFSB`) to extract block size and inode size
	- Detects inode magic (`0x494E` = "IN") at every inode-size boundary
	- Parses `xfs_dinode_core`: mode, uid, gid, di_size, di_nextents (v2 and v3 inodes)
	- Decodes 128-bit packed extent records (startblock, blockcount, unwritten flag)
	- Scans XFS directory data blocks (magic `XD2D` / `XD3B`) for `xfs_dir2_data_entry` records
	- Correlates inode numbers with filenames; outputs `fs,inode,path,size,uid,gid,mode,extents`
	- Accepts raw image files and block devices (`/dev/sdX`)

4. Btrfs Scanner (`btrfs_scan`)
	- Sequential 4096-byte block scan with `uint64_t` offsets — safe for >100 GB images
	- Reads Btrfs superblock at offset 65536 to extract FSID, nodesize, sectorsize
	- Identifies leaf nodes (`level == 0`) by header inspection; verifies FSID when known
	- Parses `INODE_ITEM` (key type 1): extracts size, uid, gid, mode from 160-byte structure
	- Parses `INODE_REF` (key type 12): extracts filename and parent directory mapping
	- Parses `DIR_ITEM` / `DIR_INDEX` (key types 84/96): maps child inode → filename
	- Correlates filenames with inode records; outputs `fs,inode,path,size,uid,gid,mode,extents`
	- Accepts raw image files and block devices (`/dev/sdX`)

5. Metadata Reconstruction (`reconstruct_csv`)
	- Rebuild timestamps, UID/GID, permissions, and original directory paths
	- Merges XFS and Btrfs scan CSVs into a unified `metadata.json` / `metadata.csv`

6. Recovery Engine (`recover_csv`)
	- Reassembles files from extents using 64-bit-safe `fseeko` I/O
	- **Preserves original filenames**: writes `recovered/<basename>` when path is known
	- Falls back to sequential naming (`file_0001.bin`, `file_0002.bin`, …) when path is unknown
	- Accepts both disk image files and block devices as the image source

## Technology Stack

- C++17 (standalone binaries; planned: optional libxfs linking for XFS) — Linux
- Bash + coreutils for orchestration (no internet required)
- Test media: sample disk images for validation and QA

## Getting Started

This repository currently contains documentation. Implementation is in progress and will be published incrementally.

### Quick start (Ubuntu TTY, offline)

You only need a C++17 compiler and coreutils. No internet required.

```
chmod +x scripts/*.sh
# Build all C++ tools (uses CMake if present; falls back to g++)
./scripts/setup.sh

# Run the full pipeline on a disk image
./scripts/metarecoverx.sh pipeline --image /path/to/disk.img

# List connected USB drives
./scripts/detect_usb.sh

# Forensically image a USB drive and run the full recovery pipeline
sudo ./scripts/metarecoverx.sh pipeline-usb --device /dev/sdb

# Or run a tiny self-contained demo (creates a 1MiB dummy image)
./scripts/run_demo.sh
```

Outputs:

- `artifacts/` — scan CSVs and combined metadata JSON/CSV
- `recovered/` — carved file blobs (`*.bin`)
- `report.csv` — a copy of `artifacts/metadata.csv` for quick viewing

### Prerequisites (Ubuntu, offline)

- Ubuntu (native or WSL) with a C++ compiler (g++)
- Optional: CMake (for nicer builds); script falls back to g++ if missing
- Optional: libxfs development headers (future XFS integration)

### Repository Layout (offline-friendly)

```
core/               # C++ tools: xfs_scan, btrfs_scan, reconstruct_csv, recover_csv
scripts/
    build.sh        # Build all binaries (CMake or g++ fallback)
    setup.sh        # One-step build helper
    metarecoverx.sh # Main wrapper: pipeline, pipeline-usb, detect-usb, …
    detect_usb.sh   # List connected USB storage devices
    run_demo.sh     # Self-contained demo
tests/              # Test images, fixtures, and automated tests (future)
docs/               # Additional technical docs
```

### High-Level Workflow (Ubuntu TTY, no internet)

1. Acquire or reference a disk image (dd/ewf, etc.)
2. Build low-level binaries: `scripts/setup.sh` (uses CMake if present; otherwise direct g++)
3. Scan filesystem structures (CSV output):
  - XFS: `./build/xfs_scan --image path/to/disk.img --format csv > artifacts/xfs_scan.csv`
  - Btrfs: `./build/btrfs_scan --image path/to/disk.img --format csv > artifacts/btrfs_scan.csv`
4. Reconstruct metadata (no jq required):
  - `./build/reconstruct_csv --out artifacts/metadata.json --csv-out artifacts/metadata.csv artifacts/xfs_scan.csv artifacts/btrfs_scan.csv`
5. Recover file contents: `./build/recover_csv --image path/to/disk.img --plan artifacts/metadata.csv --out recovered/`
6. Export a report (CSV): `cp artifacts/metadata.csv report.csv`

Try it quickly:

```
chmod +x scripts/*.sh
# Build all tools (offline)
./scripts/setup.sh

# Option A: Full offline pipeline on your disk image
./scripts/metarecoverx.sh pipeline --image /path/to/disk.img

# Option B: Quick demo (creates a tiny dummy image)
./scripts/run_demo.sh
```

Notes for TTY/no-internet environments:

- Only coreutils and a C++17 compiler are required. The scripts use: `dd`, `stat`, `sha256sum`, `readlink -f`/`realpath`.
- If `realpath` is not present, the scripts fall back to `readlink -f`.
- If CMake is missing, the build script falls back to direct `g++` compilation.

### Windows users (WSL recommended)

The scripts are POSIX shell-based and rely on coreutils. The simplest way to run end-to-end on Windows is via WSL (Ubuntu):

1) Install WSL with Ubuntu from the Microsoft Store (one-time).

2) Open an Ubuntu terminal and navigate to this project folder. If the repo lives on C:, it will be under `/mnt/c/...`, for example:

```
cd /mnt/c/Users/Mayank\ Tak/Desktop/btrfs
```

3) Run the same offline steps:

```
chmod +x scripts/*.sh
./scripts/setup.sh
./scripts/metarecoverx.sh pipeline --image /mnt/c/Path/To/disk.img
```

Native Windows (without WSL) is not officially supported by the shell scripts. The C++ tools can still be built with CMake + MSVC, but you’ll need to run the binaries directly and perform hashing (`sha256`) yourself or via PowerShell.

### Troubleshooting

- “realpath/readlink not found”: The script will try both and fall back gracefully. You can proceed.
- “CMake not found”: The build falls back to direct `g++` compilation automatically.
- Permission denied: Ensure you’re running from a writable directory and that the `scripts/*.sh` files are executable.
- Binary locations: Binaries are written to `build/`. Some generators (CMake/MSVC) may create `build/Release/` — the wrapper accounts for that by calling `scripts/build.sh` and then referencing `build/` outputs.

---

## USB / Pendrive Recovery Support

MetaRecoverX can forensically image and recover deleted data from USB pendrives and removable drives in addition to existing disk image files.

### How it works

1. **Detect** connected USB drives using `lsblk`.
2. **Acquire** a forensic raw image from the selected device using `dd`.
3. **Hash** the image with SHA-256 to preserve chain of custody.
4. **Auto-detect** the filesystem type (`xfs` / `btrfs`) via `blkid`.
5. **Scan → Reconstruct → Recover** through the standard pipeline.

### Listing connected USB drives

```bash
./scripts/detect_usb.sh
```

Example output:

```
NAME  TRAN  SIZE  MOUNTPOINT
----  ----  ----  ----------
sdb   usb   32G
sdc   usb   64G
```

Or through the main wrapper:

```bash
./scripts/metarecoverx.sh detect-usb
```

### Running the USB pipeline (requires root for dd)

```bash
# Identify your device first
./scripts/detect_usb.sh

# Run the full forensic pipeline on /dev/sdb
sudo ./scripts/metarecoverx.sh pipeline-usb --device /dev/sdb
```

What happens internally:

```
[1/6] Build C++ binaries
[2/6] dd if=/dev/sdb of=artifacts/usb_sdb_<timestamp>.img bs=4M status=progress
[3/6] sha256sum  →  artifacts/hash.txt
[4/6] Write artifacts/acquire.json
[5/6] blkid detect FS → run xfs_scan or btrfs_scan (or both)
[6/6] reconstruct_csv + recover_csv  →  recovered/
```

### Output layout (USB pipeline)

```
artifacts/
    usb_sdb_<timestamp>.img   ← forensic image
    hash.txt                  ← SHA-256 of the image
    acquire.json              ← acquisition metadata
    xfs_scan.csv              ← (if XFS or unknown FS)
    btrfs_scan.csv            ← (if Btrfs or unknown FS)
    metadata.json
    metadata.csv
recovered/
    file_1.bin
    file_2.bin
report.csv
```

### Notes

- Running `dd` on a block device requires **root** (`sudo`).
- The `blkid` utility (part of `util-linux`) is used for filesystem auto-detection; if unavailable, both XFS and Btrfs scanners are run.
- `xfs_scan` and `btrfs_scan` now accept block devices (`/dev/sdX`) directly in addition to image files, using `ioctl(BLKGETSIZE64)` to query the device size.
- `recover_csv` uses `fseeko` with 64-bit `off_t` to handle images larger than 2 GB.
- All steps are **offline** — no internet connection required.

---

## Project Plan and Milestones (2025)

Dates reflect the academic plan and are subject to refinement during implementation.

- Sep 10–20: Btrfs tree traversal prototype; metadata pipeline outline
- Sep 20–25: Requirements finalized; select sample datasets
- Sep 25–30: Disk image acquisition process finalized
- Oct 1–2: XFS superblock and AG parser drafted and validated
- Oct 5–10: Deleted inode detection and carving (XFS)
- Oct 10–18: Integrate metadata pipeline; timestamps/UID/GID recovery
- Oct 15–20: Btrfs deleted file and snapshot detection
- Oct 20–25: Integrate Btrfs with recovery engine; QA on multiple images
- Oct 31: Integrate XFS modules; optimize carving and scanning performance
- Nov 7–20: Reporting (HTML/CSV), performance tuning, packaging, and final QA

## Team

- Mayank Tak (22ESKIT093) — Lead developer; XFS module and reporting
- Lucky Panchal (22ESKIT088) — Btrfs module; testing and integration
- Milan Kumar (22ESKIT094) — Metadata reconstruction; QA and tooling

Mentors / Faculty:

- Mr. Jagendra Singh Chaudhary — Assistant Professor (II)
- Ms. Richa Rawal — Assistant Professor (I)

## Status

- As of 2025-11-03: Offline C++ stubs for `xfs_scan` and `btrfs_scan` plus `reconstruct_csv` and `recover_csv` are available. Python/Rust/jq dependencies removed to support no-internet TTY operation.
- As of 2026-03-12 (USB support): USB / removable-drive support added (`detect_usb.sh`, `pipeline-usb`, `detect-usb` commands; `blkid`-based FS detection; `ioctl(BLKGETSIZE64)` for block devices; 64-bit `fseeko` in `recover_csv`).
- As of 2026-03-12 (real scanners): `xfs_scan` and `btrfs_scan` upgraded from stubs to real block-level scanners:
  - **XFS**: parses inode magic, `xfs_dinode_core` (mode/uid/gid/size/extents), and `xfs_dir2_data_entry` directory records.
  - **Btrfs**: reads superblock at offset 65536, identifies leaf nodes, parses `INODE_ITEM`, `INODE_REF`, `DIR_ITEM`, and `DIR_INDEX` keys.
  - **recover_csv**: writes recovered files using their original basename; falls back to `file_NNNN.bin`.
  - CSV column header updated to `fs,inode,path,size,uid,gid,mode,extents`.

## Contributing

For academic evaluation and internal collaboration within SKIT. External contributions will be considered once the core modules are open-sourced. Please open issues for clarifications or to propose enhancements.

## License

TBD. A permissive open-source license (e.g., MIT/Apache-2.0) is planned post-initial evaluation.

## Acknowledgements

Thanks to SKIT Jaipur IT Department for guidance and infrastructure support. Sample disk images will be credited as sources upon publication.

Here is the full text from the document:

--- PAGE 1 ---

SKIT आवले समय Swami Keshvanand Institute Of Technology, Management & Gramothan, Jaipur Department of Information Technology, Session 2025-2026 Meta RecoverX "Recovery of Deleted Data and Associate Metadata from XFS and Btrfs Filesystems" Abstract This project introduces MetaRecoverX, a data recovery and analysis system developed to retrieve deleted files and their associated metadata from XFS and Btrfs filesystems. The system focuses on the recovery of lost information by scanning low-level filesystem structures, including inodes, allocation groups, and metadata logs. MetaRecoverX ensures accurate reconstruction of deleted data along with critical metadata such as timestamps, ownership details, and directory hierarchy. The tool is designed to assist digital forensic experts and system administrators in recovering essential data for investigative and administrative purposes. By implementing efficient scanning algorithms and metadata analysis, the project enhances reliability and precision in the recovery of data from modern Linux-based storage systems. Key Words Data Recovery Metadata Reconstruction XFS and Btrfs Filesystems Deleted File Analysis Linux-Based Storage Systems Team Name: Tech-Aizen Team Members: Mentor: Lab Coordinator:

Mayank Tak (22ESKIT093)

Lucky Panchal (22ESKIT088)

Milan Kumar (22ESKIT094) (Assistant Professor-2) Mr. Jagendra Singh Chaudhary Ms. Richa Rawal (Assistant Professor-1)

--- PAGE 2 ---

MAJOR / MINOR PROJECT ABSTRACT [Form - 1] (YEAR - 2025-26) NAME OF LAB COORDINATOR: TITLE OF PROJECT: Ms. Richa Rawal (Assistant Professor-1) Meta Recover X: Recovery of Deleted data 4 associated metadata from PROJECT TRACK: (Tick the appropriate one / ones)

The following table: "☑

R&D (Innovation) ","2. CONSULTANCY 3 STARTUP

(Fetched from Industry)

(Self-Business Initiative) ","4. PROJECT POOL

(From IBM/INFOSYS) ","5. HARDWARE

/EMBEDDED "

XFS 4 Btrfs filesysterms. BRIEF INTRODUCTION OF PROJECT: Meta Recoverx is a Linux-based tool designed to recover deleted files 4 reconstruct their metadata prom xrs 4 Btrfs filesystems. It scams low level structures like lodes, allocation groups, and metadata lags to restore lost data along with timestamps, ownership, and directory TOOLS/TECHNOLOGIES TO BE USED: Letails.

The following table: "NAME OF TOOL/TECHNOLOGY ","VERSION ","SOFTWARE/ HARDWARE ","PURPOSE OF USE " " c++ (libxfs)

Python

Rust ","GLE

3.12+

Latest ","Linux

Linux

Linux ","Filesystem pansing & low-leurd recovery Metadata reconstruction & reporting Performance critical recovery modules " "Sample disk images ","NA ","Test disks ","validation and QA "

PROPOSED PROJECT MODULES:

The following table: "NAME OF MODULE ","PROPOSED FUNCTIONALITY IN PROJECT " "Dick Image Acquisition " "XFS Scanner

Btrfs Scammer

Metadata Reconstruction

Recovery

Engine ","Acquire disk lisk images, hosh verification Scan XFS intemnals enumerate Leleted extents Siam Btrfs tree structures, find deleted items Restore timestamps, directory path, /G/O Reassemble files from extents, verify integrity "

TEAM MEMBER DETAILS:

The following table: "STUDENT NAME ","CLASS & GROUP ","MOBILE No. ","EXPERTISE AREA ","ROLE IN PROJECT " "Mayank

Tak

Lucky Panchal

Milan

Kumar ","B.Tech IT B.Tech IT B.Tech IT

","9214364918 Python

8619276148 ","7597286684 Filesystem internals tooling

Btrfs concepts, QA ","Lead developer, XFS module , reporting

metadata

Blrfs module testing "

NOTE: 1. This form is to be submitted by a team of maximum 4 students in the starting of semester to lab coordinator. 2. Students must keep a Xerox copy of this form as reference for project work and attach it to final report.

--- PAGE 3 ---

ROLE SPECIFICATION OF TEAM MEMBERS [Form - 2]

The following table: "Mayank MEMBER 1

NAME OF ACTIVITY ","Tak

SOFT DEADLINE DATE ","HANDLING

HARD DEADLINE DATE ","XFS scamnon, Recovery Engine MODULE DETAILS OF ACTIVITY (STORY) " "Resign XFS superblock, AG parsea ","20 Sept 2025 ","05 Oct 2025 ","Develop parser for XFS superblocks and allocation groups, enumerate structures. " "150ct 2025 Iomplemented deleted inode recovery & carving ",,"31 Oct 2025 ","Recover deleted inodes, carve files extends verify file integrity " ,,,"Metadata Instruction "

. MEMBER 2 Panchal HANDLING MODULE

The following table: "Lucky

NAME OF ACTIVITY ","DETAILS OF ACTIVITY (STORY)

HARD DEADLINE DATE

SOFT DEADLINE DATE " "Metadata reconstruction pipdine ","Extract timestamps, UID / GID directory structure from recovered files

os bit 2025

20 Oct 2025 " "Develop reporting

interface ","ment report export to Design & implement HTML / CSV & structured evidence. package. , QA

1025

20 Nov 2025

Nov 2025 "

HANDLING MODULE Btrfs Scanner

The following table: "NAME OF ACTIVITY ","SOFT DEADLINE DATE ","HARD DEADLINE DATE ","DETAILS OF ACTIVITY (STORY) " "Implement Btrfs tree traversal ","10 Sep 2025 10 Oct 2025 ","20 Sep 2025 ","Traverse Btrfs chunk, extent, inode tree locate deleted files 4 snapshots. " "head quality assurance unit testing

",,"25 Oct 2025 ","Test recovery on multiple disk. images, document anomalies, provide feedback. "

MEMBER 3 Milan Kumar

The following table: "MEMBER 4 ",,"HANDLING MODULE ", "NAME OF ACTIVITY ","SOFT DEADLINE DATE ","HARD DEADLINE DATE ","DETAILS OF ACTIVITY (STORY) "

MENTOR'S NAME & SIGNATURE NOTE: 1. This form is to be submitted by a team of maximum 4 students in the starting of semester to lab coordinator. 2. Every member student must keep a Xerox copy of this form as reference for his/her part in project work. 3. Students must provide the detailed list of planned activities along with their completion deadline dates. 4. The lab coordinator will check the weekly progress of student against the information provided in this form.

--- PAGE 4 ---

Form-3 PROJECT WEEKLY STATUS MATRIX (FOR PROJECT MENTORS) Mayank Tak NAME OF STUDENT - 1 NAME OF PROJECT Meta Recover-X

The following table: "OTHER TEAM

MEMBERS

WORKING ON MODULE

WEEK (TO-FROM)

20 Sept 2025-Requirement 25 sept 2025 gathering ","2. Lucky Panchal

PROGRESS ACHIEVED

finalized requirements, selected sample dataset ","3. milan Kumar.

COMMENTS

Good st ","4.

MARKS

(X/10) "

"25 Sept 2025- Disk image Developed disk innage 30 Sept 2025 acquisition acquisition process. Oct 2025-XFS superblock Drafted & validated 2 Oct 2025 4 AG parser superblock /AG parser ",,, ,,, "30ct 2025-Deleted inode detection 4 carving ","Implemented carving of deleted extents ",, "4 Oct 2025-Integration 5 2015 of XFS modules 060c4 2025-lerformance 10 Oct tuning & 4 15 Oct 2025 2017 31 Oct 2025 handling ","Tested integrated Scanning & recovery

on images

Improved carving Speed with optimized optimized algorithms ","

","

"

"MODULE

TOTAL ","OVERALL PROGRESS ","OVERALL COMMENT ","PERCENTAGE " "COMPLETED (YES/NO)

WEEKS ","(POOR/AVG/GOOD) ","(POOR/AVG/GOOD) ","ESTIMATE

MARKS "

LAB COORDINATOR's remarks & Signature NOTE:1. This form is to be maintained in a file by lab coordinators for student - 1 of the team to track his/her progress. 2. Lab coordinators must cross check and evaluate the PROGRESS ACHIEVED + it's DOCUMENTATION by student against the work done by student and note their own comments about student's performance. 3. The lab coordinator must evaluate student's work for every lab from a score of 10 points. 4. The lab coordinator must compute average of these points at the end of semester to draw an estimate of the PERCENT MARKS to be awarded to the student for his/her performance. 5. The lab coordinator must IMMEDIATELY CONTACT MENTOR FACULTY of student in case of POOR PERFORMANCE or 2 CONTINUOUS ABSENCE from lab, 6. In case of absence, 00/10 MARKS will be awarded if the mentioned work is not presented in next lab by student

--- PAGE 5 ---

Form-3 PROJECT WEEKLY STATUS MATRIX (FOR PROJECT MENTORS) NAME OF STUDENT - 2 Lucky Panchal NAME OF PROJECT Meta Recoverx

The following table: "OTHER TEAM ","MEMBERS ","1. Mayank Tak ",,"Milan Kumar 3. ","4. " "WEEK (TO-FROM) ","WORKING ON MODULE ","PROGRESS ACHIEVED ",,"COMMENTS ","MARKS (X/10) " "05 Out 2025. Blofs tree 10 Out 2025 structure 10 Oct-15 Oct Study 2025 Btrfs 15 Oct-

20 Oct 2025

INOV 07 Nov 2025

8 Nov- 15 Nov 2015

16 Nov-

20 Nov 2025 ","scanning module setup

Deleted file detection

Integration with recovery

Performance Optimization ","Understood Btrfs chunk extent, inode trees.

Developed initial Btrfs tree walker prototype

Retected deleted Snapshots file extents

Integrated recovered dete dete with main recovery engine

Optimized tree traversal reduced scanning time ",",

","

","

"

"TOTAL WEEKS ","MODULE

COMPLETED (YES/NO) ","OVERALL PROGRESS (POOR/AVG/GOOD) ",,"OVERALL COMMENT

(POOR/AVG/GOOD) ","PERCENTAGE

MARKS ESTIMATE "

LAB COORDINATOR's remarks & Signature NOTE:1. This form is to be maintained in a file by lab coordinators for student - 2 of the team to track his/her progress. 2. Lab coordinators must cross check and evaluate the PROGRESS ACHIEVED + it's DOCUMENTATION by student against the work done by student and note their own comments about student's performance. 3. The lab coordinator must evaluate student's work for every lab from a score of 10 points. 4. The lab coordinator must compute average of these points at the end of semester to draw an estimate of the PERCENT MARKS to be awarded to the student for his / her performance. 5. The lab coordinator must IMMEDIATELY CONTACT MENTOR FACULTY of student in case of POOR PERFORMANCE or 2 CONTINUOUS ABSENCE from lab. 6. In case of absence, 00 / 10 MARKS will be awarded if the mentioned work is not presented in next lab by student

--- PAGE 6 ---

Form-3 PROJECT WEEKLY STATUS MATRIX (FOR PROJECT MENTORS) NAME OF STUDENT - 3 Milan Kumar

The following table: "NAME OF PROJECT ",,"Meta Recoverx ",, ,"OTHER TEAM MEMBERS ","1. Mayank Tak ","Lucky Panchal 2. ","4. " "WEEK (TO-FROM) ","WORKING ON MODULE ","PROGRESS ACHIEVED ","COMMENTS ","MARKS (X/10) " "10 Sep- 2025 12 sep

12 Sep 2025- 15 sep 2025

16 Sep- 20 sep 2025 ","Metadata pipeline ","Project planning set up Python and tooling outline reconstruction logic Implemented basic extermotion parsing of file metadata Timestamps Restored timestamps A VID/GIO and ownership from logs restore

",, "10 Oct- 18 Oct 2025

Oct- 25 oct 2025 TOTAL WEEKS

","Integration with recovery engines

inal testingd feedback

MODULE

COMPLETED (YES/NO) ","Connected metadata pipeline with file recovery routines Tested on diverse datasets, implemented user feedback.

OVERALL PROGRESS

(POOR/AVG/GOOD) ","OVERALL COMMENT

(POOR/AVG/GOOD) ","PERCENTAGE

MARKS ESTIMATE "

LAB COORDINATOR's remarks & Signature NOTE:1. This form is to be maintained in a file by lab coordinators for student - 3 of the team to track his / her progress. 2. Lab coordinators must cross check and evaluate the PROGRESS ACHIEVED + it's DOCUMENTATION by student against the work done by student and note their own comments about student's performance. 3. The lab coordinator must evaluate student's work for every lab from a score of 10 points. 4. The lab coordinator must compute average of these points at the end of semester to draw an estimate of the PERCENT MARKS to be awarded to the student for his/her performance. 5. The lab coordinator must IMMEDIATELY CONTACT MENTOR FACULTY of student in case of POOR PERFORMANCE or 2 CONTINUOUS ABSENCE from lab. 6. In case of absence, 00/10 MARKS will be awarded if the mentioned work is not presented in next lab by student.#   c r m 
 
 #   c r m 1 1 
 
 
