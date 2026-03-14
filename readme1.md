You are an expert Linux filesystem developer and digital forensics engineer.

I have an academic project called **MetaRecoverX**.

The purpose of this project is to recover deleted data and metadata from:

• XFS filesystems
• Btrfs filesystems

The system must run **offline in Ubuntu TTY environments**.

Allowed technologies:

• C++17
• Bash
• coreutils
• POSIX APIs

Not allowed:

• Python
• Internet downloads
• external libraries

---

CURRENT PROJECT STRUCTURE

core/
xfs_scan.cpp
btrfs_scan.cpp
reconstruct_csv.cpp
recover_csv.cpp

scripts/
setup.sh
metarecoverx.sh
run_demo.sh

docs/
tests/

---

CURRENT WORKFLOW

1 Acquire disk image
2 Scan filesystem structures
3 Generate CSV metadata
4 Reconstruct metadata
5 Recover file contents

Example command:

./scripts/metarecoverx.sh pipeline --image disk.img

Outputs:

artifacts/
recovered/
report.csv

---

NEW FEATURE 1

Add **USB / Removable Drive Recovery Support**

Example command:

sudo ./scripts/metarecoverx.sh pipeline-usb --device /dev/sdb

Implementation requirements:

1 Detect USB devices

Use:

lsblk -o NAME,TRAN,SIZE,MOUNTPOINT

Create script:

scripts/detect_usb.sh

Output example:

sdb usb 32G
sdc usb 64G

---

2 Acquire forensic image

When pipeline-usb is used:

Create disk image:

dd if=/dev/sdb of=artifacts/usb_image.img bs=4M status=progress conv=sync

---

3 Generate hash

sha256sum artifacts/usb_image.img > artifacts/hash.txt

---

4 Pass image to existing pipeline

Reuse existing scanning workflow.

---

UPDATE scripts/metarecoverx.sh

Add new command:

pipeline-usb

Workflow:

1 Detect USB device
2 Acquire forensic image
3 Generate SHA256 hash
4 Detect filesystem
5 Run filesystem scanners
6 Reconstruct metadata
7 Recover files
8 Generate report.csv

---

OPTIONAL FEATURE

Filesystem detection:

Use:

blkid -o value -s TYPE image.img

If result is:

xfs → run xfs_scan
btrfs → run btrfs_scan

If unknown → run both scanners.

---

NEW FEATURE 2

Recover **REAL FILE METADATA**

Instead of only carving binary blobs, the system must recover:

filename
path
file size
uid
gid
mode
extents

---

CSV OUTPUT FORMAT

Scanners must produce:

fs,inode,path,size,uid,gid,mode,extents

Example:

xfs,10231,/home/user/file.txt,20480,1000,1000,644,8192:20480

---

METADATA RECONSTRUCTION

reconstruct_csv.cpp should combine results from:

xfs_scan.csv
btrfs_scan.csv

and produce:

metadata.csv
metadata.json

Example metadata.csv:

global_id,fs,path,size,uid,gid,mode,extents,recovery_plan
xfs-10231,xfs,/home/user/file.txt,20480,1000,1000,644,8192:20480,carve

---

RECOVERY ENGINE

recover_csv.cpp should:

read metadata.csv

recover extents from disk image

save recovered files into:

recovered/

Filename should be preserved.

Example:

recovered/
file.txt
photo.jpg
document.pdf

If filename unknown:

file_0001.bin

---

UPDATE SCANNERS

xfs_scan.cpp must parse:

XFS inode structures
directory entries

Directory entry example:

struct xfs_dir2_data_entry {
uint64 inode;
uint8 namelen;
char name[];
};

Extract:

inode
filename

---

btrfs_scan.cpp must parse:

Btrfs trees:

ROOT TREE
FS TREE
DIR_ITEM
INODE_REF

Extract:

inode
filename
size

---

SCANNER REQUIREMENTS

Scanners must:

• read raw disk image
• support large images (>100GB)
• use uint64_t offsets
• scan blocks sequentially

Example read pattern:

uint64_t offset = 0;
const uint64_t block_size = 4096;

while(offset < image_size) {

```
image.seekg(offset);

char buffer[4096];
image.read(buffer,4096);

// scan metadata

offset += block_size;
```

}

---

RECOVERY OUTPUT STRUCTURE

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

---

README UPDATE

Add section:

USB / Pendrive Recovery Support

Example:

./scripts/detect_usb.sh

sudo ./scripts/metarecoverx.sh pipeline-usb --device /dev/sdb

Example workflow:

1 Detect USB device
2 Acquire forensic image
3 Generate SHA256 hash
4 Scan filesystem
5 Reconstruct metadata
6 Recover files

---

CODE REQUIREMENTS

All code must:

• compile with C++17
• run offline
• avoid external libraries
• use only standard library + POSIX
• support large disk images

---

FINAL GOAL

MetaRecoverX should behave like a **Linux digital forensics recovery tool** supporting:

• disk image recovery
• USB drive recovery
• XFS metadata recovery
• Btrfs metadata recovery
• filename reconstruction
• metadata reconstruction
• offline execution

---

OUTPUT REQUIRED

Generate:

1 scripts/detect_usb.sh
2 updated scripts/metarecoverx.sh
3 updated xfs_scan.cpp
4 updated btrfs_scan.cpp
5 updated recover_csv.cpp
6 README update

Do NOT remove existing functionality.

Only extend the system.
