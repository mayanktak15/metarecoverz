MetaRecoverX
============

Recovery of Deleted Data and Associated Metadata from XFS and Btrfs Filesystems

MetaRecoverX is a Linux digital forensics and data recovery tool designed to recover deleted files and reconstruct their associated metadata from XFS and Btrfs filesystems.

The system scans low-level filesystem structures including:

- inodes
- allocation groups
- metadata logs
- filesystem trees

and reconstructs deleted data along with important metadata such as:

- filename
- directory path
- file size
- UID / GID
- permissions (mode)
- file extents

The tool is designed to run fully offline in Ubuntu / Linux TTY environments, making it suitable for digital forensics, incident response, and system administration workflows.

Project Information
-------------------

- **Institute:** Swami Keshvanand Institute of Technology, Management & Gramothan (SKIT), Jaipur
- **Department:** Information Technology
- **Academic Session:** 2025–2026
- **Team Name:** Tech-Aizen

### Team Members

- Mayank Tak (22ESKIT093) — Lead Developer (XFS module)
- Lucky Panchal (22ESKIT088) — Btrfs module and testing
- Milan Kumar (22ESKIT094) — Metadata pipeline and QA

### Faculty Mentors

- Mr. Jagendra Singh Chaudhary — Assistant Professor
- Ms. Richa Rawal — Assistant Professor

Key Features
------------

MetaRecoverX provides a complete offline forensic recovery pipeline.

Main capabilities:

- Recovery of deleted files from XFS
- Recovery of deleted files from Btrfs
- Metadata reconstruction
- Directory path reconstruction
- Extent-based file recovery
- Disk image analysis
- USB / pendrive forensic acquisition
- SHA-256 hashing for integrity
- Support for large disk images (>100GB)
- Fully offline execution

Supported Filesystems
---------------------

### XFS

The XFS scanner detects and parses:

- XFS superblock
- Allocation groups
- inode structures
- directory entries
- extent records

Recovered metadata:

- inode
- filename
- size
- uid
- gid
- mode
- extents

### Btrfs

The Btrfs scanner parses:

- superblock
- FS tree
- INODE_ITEM
- INODE_REF
- DIR_ITEM

Recovered metadata:

- inode
- filename
- size
- uid
- gid
- mode

Technology Stack
----------------

MetaRecoverX is designed for offline Linux environments.

Technologies used:

- C++17
- Bash
- POSIX APIs
- Linux coreutils

No external libraries or internet downloads are required.

Repository Structure
--------------------

```text
MetaRecoverX/
  core/
     src/
        xfs_scan.cpp
        btrfs_scan.cpp
        reconstruct_csv.cpp
        recover_csv.cpp

  scripts/
     setup.sh
     build.sh
     metarecoverx.sh
     detect_usb.sh
     run_demo.sh
     menu.sh

  artifacts/
  recovered/
  docs/
  tests/
```

Clone the Repository
--------------------

To copy the project from GitHub:

```bash
git clone https://github.com/YOUR_USERNAME/MetaRecoverX.git
cd MetaRecoverX
```

Build the Project
-----------------

Compile all tools:

```bash
chmod +x scripts/*.sh
./scripts/setup.sh
```

Compiled binaries will appear in `build/`:

- `build/xfs_scan`
- `build/btrfs_scan`
- `build/reconstruct_csv`
- `build/recover_csv`

How to Run
----------

### 1. Full offline pipeline on a disk image

```bash
./scripts/metarecoverx.sh pipeline --image /path/to/disk.img
```

This will:

1. Build tools
2. Acquire image information and hash
3. Scan filesystem structures (XFS and Btrfs)
4. Reconstruct metadata
5. Recover files
6. Generate a report

### 2. Interactive TTY menu

```bash
./scripts/menu.sh
```

Use this for a simple TTY interface (USB detection and demo pipeline).

### 3. Quick demo (dummy image)

```bash
./scripts/metarecoverx.sh demo
# or
./scripts/run_demo.sh
```

Demo Workflow
-------------

The demo performs:

1. Create test disk image (`dummy.img`)
2. Run filesystem scanners:
    - `xfs_scan`
    - `btrfs_scan`
3. Generate metadata:
    - `artifacts/metadata.csv`
    - `artifacts/metadata.json`
4. Run recovery engine
5. Generate report (`report.csv`)
6. Store recovered files in `recovered/`

This confirms that the entire pipeline works correctly.

Disk Image Recovery
-------------------

If you already have a raw disk image, run the full pipeline:

```bash
./scripts/metarecoverx.sh pipeline --image disk.img
```

Outputs:

- `artifacts/xfs_scan.csv`
- `artifacts/btrfs_scan.csv`
- `artifacts/metadata.csv`
- `artifacts/metadata.json`
- `recovered/`
- `report.csv`

USB / Pendrive Recovery
-----------------------

MetaRecoverX can recover deleted data from USB drives and removable storage.

### Detect connected USB devices

List available USB drives:

```bash
./scripts/detect_usb.sh
# or
./scripts/metarecoverx.sh detect-usb
```

Example output:

```text
NAME  TRAN  SIZE
sdb   usb   32G
sdc   usb   64G
```

### Run USB forensic recovery

```bash
sudo ./scripts/metarecoverx.sh pipeline-usb --device /dev/sdb
```

USB Recovery Workflow
---------------------

The system performs the following steps automatically:

1. Detect USB device (`lsblk`)
2. Acquire forensic image:
    - `dd if=/dev/sdb of=artifacts/usb_image.img bs=4M status=progress`
3. Generate SHA-256 hash:
    - `sha256sum artifacts/usb_image.img > artifacts/hash.txt`
4. Detect filesystem (`blkid`), e.g.:
    - `xfs`
    - `btrfs`
5. Scan filesystem metadata:
    - `xfs_scan`
    - `btrfs_scan`
6. Reconstruct metadata (`reconstruct_csv`):
    - `artifacts/metadata.csv`
    - `artifacts/metadata.json`
7. Recover files (`recover_csv`)

Recovered files are stored in `recovered/`.

Example recovered files:

```text
recovered/
  file.txt
  photo.jpg
  document.pdf
```

If the filename cannot be recovered:

```text
file_0001.bin
file_0002.bin
```

CSV Metadata Format
-------------------

Scanner output format:

```text
fs,inode,path,size,uid,gid,mode,extents
```

Example:

```text
xfs,10231,/home/user/file.txt,20480,1000,1000,644,8192:20480
```

Output Structure
----------------

After a successful run:

```text
artifacts/
  usb_image.img
  hash.txt
  xfs_scan.csv
  btrfs_scan.csv
  metadata.csv
  metadata.json

recovered/
  file.txt
  photo.jpg
  document.pdf

report.csv
```

Safety and Forensic Notes
-------------------------

When performing USB recovery:

- Always confirm the correct device before imaging.
- Never write data to the source device.

Prefer analyzing disk images instead of live devices.

Verify image integrity using SHA256 hashes.

Future Improvements

Planned enhancements include:

Snapshot recovery (Btrfs)

Advanced carving heuristics

HTML forensic reports

Performance optimizations

Support for additional filesystems

License

License will be added after academic evaluation.

Potential options:

MIT License

Apache 2.0

Acknowledgements

Special thanks to the Information Technology Department at SKIT Jaipur for academic guidance and project support.